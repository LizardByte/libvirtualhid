/**
 * @file src/platform/linux/uhid_backend.cpp
 * @brief Linux UHID backend definitions.
 */

// standard includes
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// platform includes
#include <fcntl.h>
#ifndef __user
  #define __user
#endif
#include <linux/uhid.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#if defined(LIBVIRTUALHID_HAVE_XTEST)
  #include <X11/extensions/XTest.h>
  #include <X11/keysym.h>
  #include <X11/Xlib.h>
  #include <X11/Xutil.h>
#endif

// local includes
#include "core/backend.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
  #include "platform/linux/uhid_backend_test_hooks.hpp"
#endif

namespace lvh::detail {
  namespace {

    constexpr auto uhid_path = "/dev/uhid";
    constexpr auto uinput_path = "/dev/uinput";
    constexpr auto absolute_axis_max = 65535;
    constexpr auto poll_timeout_ms = 100;

#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
    struct LinuxTestSyscalls {
      bool override_access = false;
      int access_result = 0;
      bool override_open = false;
      int open_result = 100000;
      bool override_write = false;
      std::atomic_int write_call_count = 0;
      int fail_write_call = -1;
      int short_write_call = -1;
      std::size_t short_write_size = 1;
      bool override_ioctl = false;
      std::atomic_int ioctl_call_count = 0;
      int fail_ioctl_call = -1;
      bool override_close = false;
      int close_result = 0;
      bool override_poll = false;
      std::atomic_int poll_call_count = 0;
      std::vector<int> poll_results;
      std::vector<short> poll_revents;
      std::vector<int> poll_errors;
      bool override_read = false;
      std::atomic_int read_call_count = 0;
      std::vector<std::ptrdiff_t> read_results;
      std::vector<int> read_errors;
      uhid_event read_event {};
      bool fake_xtest_keyboard = false;
      bool fake_xtest_mouse = false;
    };

    LinuxTestSyscalls *active_test_syscalls = nullptr;

    class ScopedLinuxTestSyscalls {
    public:
      explicit ScopedLinuxTestSyscalls(LinuxTestSyscalls &syscalls):
          previous_ {active_test_syscalls} {
        active_test_syscalls = &syscalls;
      }

      ScopedLinuxTestSyscalls(const ScopedLinuxTestSyscalls &) = delete;
      ScopedLinuxTestSyscalls &operator=(const ScopedLinuxTestSyscalls &) = delete;
      ScopedLinuxTestSyscalls(ScopedLinuxTestSyscalls &&) noexcept = delete;
      ScopedLinuxTestSyscalls &operator=(ScopedLinuxTestSyscalls &&) noexcept = delete;

      ~ScopedLinuxTestSyscalls() {
        active_test_syscalls = previous_;
      }

    private:
      LinuxTestSyscalls *previous_ = nullptr;
    };
#endif

    int system_access(const char *path, int mode) {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
      if (active_test_syscalls != nullptr && active_test_syscalls->override_access) {
        if (active_test_syscalls->access_result < 0) {
          errno = EACCES;
        }
        return active_test_syscalls->access_result;
      }
#endif
      return ::access(path, mode);
    }

    int system_open(const char *path, int flags) {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
      if (active_test_syscalls != nullptr && active_test_syscalls->override_open) {
        if (active_test_syscalls->open_result < 0) {
          errno = ENOENT;
        }
        return active_test_syscalls->open_result;
      }
#endif
      return ::open(path, flags);
    }

    int system_close(int fd) {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
      if (active_test_syscalls != nullptr && active_test_syscalls->override_close) {
        if (active_test_syscalls->close_result < 0) {
          errno = EIO;
        }
        return active_test_syscalls->close_result;
      }
#endif
      return ::close(fd);
    }

    std::ptrdiff_t system_write(int fd, const void *buffer, std::size_t size) {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
      if (active_test_syscalls != nullptr && active_test_syscalls->override_write) {
        const auto call_count = ++active_test_syscalls->write_call_count;
        if (active_test_syscalls->fail_write_call == call_count) {
          errno = EIO;
          return -1;
        }
        if (active_test_syscalls->short_write_call == call_count) {
          return static_cast<std::ptrdiff_t>(active_test_syscalls->short_write_size);
        }
        return static_cast<std::ptrdiff_t>(size);
      }
#endif
      return static_cast<std::ptrdiff_t>(::write(fd, buffer, size));
    }

    int system_ioctl(int fd, unsigned long request, unsigned long argument = 0) {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
      if (active_test_syscalls != nullptr && active_test_syscalls->override_ioctl) {
        const auto call_count = ++active_test_syscalls->ioctl_call_count;
        if (active_test_syscalls->fail_ioctl_call == call_count) {
          errno = EINVAL;
          return -1;
        }
        return 0;
      }
#endif
      return ::ioctl(fd, request, argument);
    }

    int system_poll(pollfd *descriptors, nfds_t descriptor_count, int timeout) {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
      if (active_test_syscalls != nullptr && active_test_syscalls->override_poll) {
        const auto call_index = static_cast<std::size_t>(active_test_syscalls->poll_call_count++);
        auto result = 0;
        if (call_index < active_test_syscalls->poll_results.size()) {
          result = active_test_syscalls->poll_results[call_index];
        }

        if (descriptor_count > 0) {
          descriptors[0].revents = 0;
          if (result > 0 && call_index < active_test_syscalls->poll_revents.size()) {
            descriptors[0].revents = active_test_syscalls->poll_revents[call_index];
          }
        }

        if (result < 0) {
          errno = call_index < active_test_syscalls->poll_errors.size() ? active_test_syscalls->poll_errors[call_index] : EIO;
        }
        return result;
      }
#endif
      return ::poll(descriptors, descriptor_count, timeout);
    }

    std::ptrdiff_t system_read(int fd, void *buffer, std::size_t size) {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
      if (active_test_syscalls != nullptr && active_test_syscalls->override_read) {
        const auto call_index = static_cast<std::size_t>(active_test_syscalls->read_call_count++);
        auto result = std::ptrdiff_t {0};
        if (call_index < active_test_syscalls->read_results.size()) {
          result = active_test_syscalls->read_results[call_index];
        }

        if (result < 0) {
          errno = call_index < active_test_syscalls->read_errors.size() ? active_test_syscalls->read_errors[call_index] : EIO;
          return result;
        }

        if (result > 0) {
          const auto bytes = std::min<std::size_t>(static_cast<std::size_t>(result), std::min(size, sizeof(uhid_event)));
          std::memcpy(buffer, &active_test_syscalls->read_event, bytes);
          return static_cast<std::ptrdiff_t>(bytes);
        }

        return result;
      }
#endif
      return static_cast<std::ptrdiff_t>(::read(fd, buffer, size));
    }

    std::string errno_message(int error) {
      return std::error_code(error, std::generic_category()).message();
    }

    Status system_error_status(ErrorCode code, const std::string &operation, int error) {
      return Status::failure(code, operation + ": " + errno_message(error));
    }

    bool can_access_uhid() {
      return system_access(uhid_path, R_OK | W_OK) == 0;
    }

    bool can_access_uinput() {
      return system_access(uinput_path, R_OK | W_OK) == 0;
    }

    std::uint16_t to_uhid_bus(BusType bus_type) {
      if (bus_type == BusType::bluetooth) {
        return BUS_BLUETOOTH;
      }
      return BUS_USB;
    }

    std::uint16_t to_uinput_bus(BusType bus_type) {
      return to_uhid_bus(bus_type);
    }

    template<std::size_t Size>
    void copy_string(__u8 (&destination)[Size], const std::string &source) {
      const auto length = std::min(source.size(), Size - 1);
      std::memcpy(destination, source.data(), length);
      destination[length] = 0;
    }

    template<std::size_t Size>
    void copy_string(char (&destination)[Size], const std::string &source) {
      const auto length = std::min(source.size(), Size - 1);
      std::memcpy(destination, source.data(), length);
      destination[length] = 0;
    }

    Status ioctl_status(const std::string &operation) {
      return system_error_status(ErrorCode::backend_failure, operation, errno);
    }

    int key_code_to_linux(KeyboardKeyCode key_code) {
      switch (key_code) {
        case 0x08:
          return KEY_BACKSPACE;
        case 0x09:
          return KEY_TAB;
        case 0x0D:
          return KEY_ENTER;
        case 0x10:
        case 0xA0:
          return KEY_LEFTSHIFT;
        case 0x11:
        case 0xA2:
          return KEY_LEFTCTRL;
        case 0x12:
        case 0xA4:
          return KEY_LEFTALT;
        case 0x14:
          return KEY_CAPSLOCK;
        case 0x1B:
          return KEY_ESC;
        case 0x20:
          return KEY_SPACE;
        case 0x21:
          return KEY_PAGEUP;
        case 0x22:
          return KEY_PAGEDOWN;
        case 0x23:
          return KEY_END;
        case 0x24:
          return KEY_HOME;
        case 0x25:
          return KEY_LEFT;
        case 0x26:
          return KEY_UP;
        case 0x27:
          return KEY_RIGHT;
        case 0x28:
          return KEY_DOWN;
        case 0x2C:
          return KEY_SYSRQ;
        case 0x2D:
          return KEY_INSERT;
        case 0x2E:
          return KEY_DELETE;
        case 0x5B:
          return KEY_LEFTMETA;
        case 0x5C:
          return KEY_RIGHTMETA;
        case 0x90:
          return KEY_NUMLOCK;
        case 0x91:
          return KEY_SCROLLLOCK;
        case 0xA1:
          return KEY_RIGHTSHIFT;
        case 0xA3:
          return KEY_RIGHTCTRL;
        case 0xA5:
          return KEY_RIGHTALT;
        case 0xBA:
          return KEY_SEMICOLON;
        case 0xBB:
          return KEY_EQUAL;
        case 0xBC:
          return KEY_COMMA;
        case 0xBD:
          return KEY_MINUS;
        case 0xBE:
          return KEY_DOT;
        case 0xBF:
          return KEY_SLASH;
        case 0xC0:
          return KEY_GRAVE;
        case 0xDB:
          return KEY_LEFTBRACE;
        case 0xDC:
          return KEY_BACKSLASH;
        case 0xDD:
          return KEY_RIGHTBRACE;
        case 0xDE:
          return KEY_APOSTROPHE;
        case 0xE2:
          return KEY_102ND;
        default:
          break;
      }

      if (key_code >= 0x30 && key_code <= 0x39) {
        static constexpr int digit_keys[] {
          KEY_0,
          KEY_1,
          KEY_2,
          KEY_3,
          KEY_4,
          KEY_5,
          KEY_6,
          KEY_7,
          KEY_8,
          KEY_9,
        };
        return digit_keys[key_code - 0x30];
      }
      if (key_code >= 0x41 && key_code <= 0x5A) {
        static constexpr int letter_keys[] {
          KEY_A,
          KEY_B,
          KEY_C,
          KEY_D,
          KEY_E,
          KEY_F,
          KEY_G,
          KEY_H,
          KEY_I,
          KEY_J,
          KEY_K,
          KEY_L,
          KEY_M,
          KEY_N,
          KEY_O,
          KEY_P,
          KEY_Q,
          KEY_R,
          KEY_S,
          KEY_T,
          KEY_U,
          KEY_V,
          KEY_W,
          KEY_X,
          KEY_Y,
          KEY_Z,
        };
        return letter_keys[key_code - 0x41];
      }
      if (key_code >= 0x60 && key_code <= 0x69) {
        static constexpr int keypad_digit_keys[] {
          KEY_KP0,
          KEY_KP1,
          KEY_KP2,
          KEY_KP3,
          KEY_KP4,
          KEY_KP5,
          KEY_KP6,
          KEY_KP7,
          KEY_KP8,
          KEY_KP9,
        };
        return keypad_digit_keys[key_code - 0x60];
      }
      if (key_code == 0x6A) {
        return KEY_KPASTERISK;
      }
      if (key_code == 0x6B) {
        return KEY_KPPLUS;
      }
      if (key_code == 0x6D) {
        return KEY_KPMINUS;
      }
      if (key_code == 0x6E) {
        return KEY_KPDOT;
      }
      if (key_code == 0x6F) {
        return KEY_KPSLASH;
      }
      if (key_code >= 0x70 && key_code <= 0x87) {
        static constexpr int function_keys[] {
          KEY_F1,
          KEY_F2,
          KEY_F3,
          KEY_F4,
          KEY_F5,
          KEY_F6,
          KEY_F7,
          KEY_F8,
          KEY_F9,
          KEY_F10,
          KEY_F11,
          KEY_F12,
          KEY_F13,
          KEY_F14,
          KEY_F15,
          KEY_F16,
          KEY_F17,
          KEY_F18,
          KEY_F19,
          KEY_F20,
          KEY_F21,
          KEY_F22,
          KEY_F23,
          KEY_F24,
        };
        return function_keys[key_code - 0x70];
      }

      return -1;
    }

    int mouse_button_to_linux(MouseButton button) {
      switch (button) {
        case MouseButton::left:
          return BTN_LEFT;
        case MouseButton::middle:
          return BTN_MIDDLE;
        case MouseButton::right:
          return BTN_RIGHT;
        case MouseButton::side:
          return BTN_SIDE;
        case MouseButton::extra:
          return BTN_EXTRA;
      }

      return BTN_LEFT;
    }

    int scale_absolute_axis(std::int32_t value, std::int32_t limit) {
      if (limit <= 0) {
        return 0;
      }

      const auto clamped = std::clamp(value, 0, limit);
      const auto numerator = static_cast<std::int64_t>(clamped) * absolute_axis_max;
      return static_cast<int>(numerator / limit);
    }

    std::vector<std::uint32_t> decode_utf8(const std::string &text) {
      std::vector<std::uint32_t> codepoints;
      for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i]);
        std::uint32_t codepoint = 0;
        std::size_t length = 0;

        if (first <= 0x7F) {
          codepoint = first;
          length = 1;
        } else if ((first & 0xE0U) == 0xC0U) {
          codepoint = first & 0x1FU;
          length = 2;
        } else if ((first & 0xF0U) == 0xE0U) {
          codepoint = first & 0x0FU;
          length = 3;
        } else if ((first & 0xF8U) == 0xF0U) {
          codepoint = first & 0x07U;
          length = 4;
        } else {
          ++i;
          continue;
        }

        if (i + length > text.size()) {
          break;
        }

        bool valid = true;
        for (std::size_t offset = 1; offset < length; ++offset) {
          const auto next = static_cast<unsigned char>(text[i + offset]);
          if ((next & 0xC0U) != 0x80U) {
            valid = false;
            break;
          }
          codepoint = (codepoint << 6U) | (next & 0x3FU);
        }

        if (valid) {
          codepoints.push_back(codepoint);
          i += length;
        } else {
          ++i;
        }
      }

      return codepoints;
    }

    std::string uppercase_hex(std::uint32_t codepoint) {
      std::ostringstream stream;
      stream << std::uppercase << std::hex << codepoint;
      return stream.str();
    }

    KeyboardKeyCode hex_digit_key_code(char digit) {
      if (digit >= '0' && digit <= '9') {
        return static_cast<KeyboardKeyCode>(0x30 + (digit - '0'));
      }
      return static_cast<KeyboardKeyCode>(0x41 + (digit - 'A'));
    }

    int legacy_scroll_steps(std::int32_t distance) {
      if (distance == 0) {
        return 0;
      }

      const auto steps = distance / 120;
      if (steps != 0) {
        return steps;
      }
      return distance > 0 ? 1 : -1;
    }

    /**
     * @brief Shared Linux uinput device wrapper.
     */
    class UinputDevice {
    public:
      explicit UinputDevice(int file_descriptor):
          fd_ {file_descriptor} {}

      UinputDevice(const UinputDevice &) = delete;
      UinputDevice &operator=(const UinputDevice &) = delete;
      UinputDevice(UinputDevice &&) noexcept = delete;
      UinputDevice &operator=(UinputDevice &&) noexcept = delete;

      virtual ~UinputDevice() {
        static_cast<void>(close_uinput("uinput device"));
      }

    protected:
      Status emit_event(std::uint16_t type, std::uint16_t code, std::int32_t value) {
        std::lock_guard lock {write_mutex_};
        return emit_event_locked(type, code, value);
      }

      Status sync() {
        return emit_event(EV_SYN, SYN_REPORT, 0);
      }

      Status close_uinput(const std::string &description) {
        if (!open_.exchange(false)) {
          return Status::success();
        }

        auto status = Status::success();
        if (fd_ >= 0) {
          if (system_ioctl(fd_, UI_DEV_DESTROY) < 0) {
            status = ioctl_status("failed to destroy " + description);
          }
          if (system_close(fd_) != 0 && status.ok()) {
            status = system_error_status(ErrorCode::backend_failure, "failed to close /dev/uinput", errno);
          }
          fd_ = -1;
        }

        return status;
      }

      bool is_open() const {
        return open_;
      }

      int file_descriptor() const {
        return fd_;
      }

    private:
      Status emit_event_locked(std::uint16_t type, std::uint16_t code, std::int32_t value) {
        if (fd_ < 0) {
          return Status::failure(ErrorCode::device_closed, "uinput device is closed");
        }

        input_event event {};
        event.type = type;
        event.code = code;
        event.value = value;

        const auto result = system_write(fd_, &event, sizeof(event));
        if (result < 0) {
          return system_error_status(ErrorCode::backend_failure, "failed to write uinput event", errno);
        }
        if (static_cast<std::size_t>(result) != sizeof(event)) {
          return Status::failure(ErrorCode::backend_failure, "short write while sending uinput event");
        }

        return Status::success();
      }

      int fd_ = -1;
      std::atomic_bool open_ = true;
      std::mutex write_mutex_;
    };

    Status write_uinput_user_device(int fd, const DeviceProfile &profile, DeviceId id) {
      uinput_user_dev device {};
      copy_string(device.name, profile.name);
      device.id.bustype = to_uinput_bus(profile.bus_type);
      device.id.vendor = profile.vendor_id;
      device.id.product = profile.product_id;
      device.id.version = profile.version;
      device.absmin[ABS_X] = 0;
      device.absmax[ABS_X] = absolute_axis_max;
      device.absmin[ABS_Y] = 0;
      device.absmax[ABS_Y] = absolute_axis_max;
      device.absfuzz[ABS_X] = 0;
      device.absfuzz[ABS_Y] = 0;
      device.absflat[ABS_X] = 0;
      device.absflat[ABS_Y] = 0;

      const auto result = system_write(fd, &device, sizeof(device));
      if (result < 0) {
        return system_error_status(ErrorCode::backend_failure, "failed to write uinput device definition", errno);
      }
      if (static_cast<std::size_t>(result) != sizeof(device)) {
        return Status::failure(ErrorCode::backend_failure, "short write while creating uinput device");
      }

      if (system_ioctl(fd, UI_DEV_CREATE) < 0) {
        return ioctl_status("failed to create uinput device " + std::to_string(id));
      }

      return Status::success();
    }

    Status enable_uinput_keyboard(int fd) {
      if (system_ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        return ioctl_status("failed to enable uinput keyboard key events");
      }

      for (auto code = 1; code < KEY_MAX; ++code) {
        if (system_ioctl(fd, UI_SET_KEYBIT, code) < 0) {
          return ioctl_status("failed to enable uinput keyboard key " + std::to_string(code));
        }
      }

      return Status::success();
    }

    Status enable_uinput_mouse(int fd) {
      if (system_ioctl(fd, UI_SET_EVBIT, EV_KEY) < 0) {
        return ioctl_status("failed to enable uinput mouse button events");
      }
      if (system_ioctl(fd, UI_SET_EVBIT, EV_REL) < 0) {
        return ioctl_status("failed to enable uinput relative mouse events");
      }
      if (system_ioctl(fd, UI_SET_EVBIT, EV_ABS) < 0) {
        return ioctl_status("failed to enable uinput absolute mouse events");
      }

      for (const auto button : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA}) {
        if (system_ioctl(fd, UI_SET_KEYBIT, button) < 0) {
          return ioctl_status("failed to enable uinput mouse button " + std::to_string(button));
        }
      }

      for (const auto code : {REL_X, REL_Y}) {
        if (system_ioctl(fd, UI_SET_RELBIT, code) < 0) {
          return ioctl_status("failed to enable uinput relative axis " + std::to_string(code));
        }
      }

#if defined(REL_WHEEL_HI_RES)
      if (system_ioctl(fd, UI_SET_RELBIT, REL_WHEEL_HI_RES) < 0) {
        return ioctl_status("failed to enable uinput high-resolution vertical scroll");
      }
#else
      if (system_ioctl(fd, UI_SET_RELBIT, REL_WHEEL) < 0) {
        return ioctl_status("failed to enable uinput vertical scroll");
      }
#endif

#if defined(REL_HWHEEL_HI_RES)
      if (system_ioctl(fd, UI_SET_RELBIT, REL_HWHEEL_HI_RES) < 0) {
        return ioctl_status("failed to enable uinput high-resolution horizontal scroll");
      }
#else
      if (system_ioctl(fd, UI_SET_RELBIT, REL_HWHEEL) < 0) {
        return ioctl_status("failed to enable uinput horizontal scroll");
      }
#endif

      if (system_ioctl(fd, UI_SET_ABSBIT, ABS_X) < 0) {
        return ioctl_status("failed to enable uinput absolute X axis");
      }
      if (system_ioctl(fd, UI_SET_ABSBIT, ABS_Y) < 0) {
        return ioctl_status("failed to enable uinput absolute Y axis");
      }

      return Status::success();
    }

    /**
     * @brief Backend keyboard backed by one Linux uinput file descriptor.
     */
    class UinputKeyboard final: public BackendKeyboard, private UinputDevice {
    public:
      explicit UinputKeyboard(int file_descriptor):
          UinputDevice {file_descriptor} {}

      ~UinputKeyboard() override {
        static_cast<void>(close());
      }

      Status create(DeviceId id, const CreateKeyboardOptions &options) {
        if (const auto status = enable_uinput_keyboard(file_descriptor()); !status.ok()) {
          return status;
        }
        return write_uinput_user_device(file_descriptor(), options.profile, id);
      }

      Status submit(const KeyboardEvent &event) override {
        if (!is_open()) {
          return Status::failure(ErrorCode::device_closed, "uinput keyboard is closed");
        }

        const auto linux_key = key_code_to_linux(event.key_code);
        if (linux_key < 0) {
          return Status::failure(ErrorCode::invalid_argument, "keyboard key code is not supported by the Linux backend");
        }

        if (const auto status = emit_event(EV_KEY, static_cast<std::uint16_t>(linux_key), event.pressed ? 1 : 0); !status.ok()) {
          return status;
        }
        return sync();
      }

      Status type_text(const KeyboardTextEvent &event) override {
        for (const auto codepoint : decode_utf8(event.text)) {
          const auto hex = uppercase_hex(codepoint);

          if (const auto status = submit({.key_code = 0xA2, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0xA0, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0x55, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0x55, .pressed = false}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0xA0, .pressed = false}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0xA2, .pressed = false}); !status.ok()) {
            return status;
          }

          for (const auto digit : hex) {
            const auto key_code = hex_digit_key_code(digit);
            if (const auto status = submit({.key_code = key_code, .pressed = true}); !status.ok()) {
              return status;
            }
            if (const auto status = submit({.key_code = key_code, .pressed = false}); !status.ok()) {
              return status;
            }
          }

          if (const auto status = submit({.key_code = 0x0D, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0x0D, .pressed = false}); !status.ok()) {
            return status;
          }
        }

        return Status::success();
      }

      Status close() override {
        return close_uinput("uinput keyboard");
      }
    };

    /**
     * @brief Backend mouse backed by one Linux uinput file descriptor.
     */
    class UinputMouse final: public BackendMouse, private UinputDevice {
    public:
      explicit UinputMouse(int file_descriptor):
          UinputDevice {file_descriptor} {}

      ~UinputMouse() override {
        static_cast<void>(close());
      }

      Status create(DeviceId id, const CreateMouseOptions &options) {
        if (const auto status = enable_uinput_mouse(file_descriptor()); !status.ok()) {
          return status;
        }
        return write_uinput_user_device(file_descriptor(), options.profile, id);
      }

      Status submit(const MouseEvent &event) override {
        if (!is_open()) {
          return Status::failure(ErrorCode::device_closed, "uinput mouse is closed");
        }

        switch (event.kind) {
          case MouseEventKind::relative_motion:
            return submit_relative_motion(event);
          case MouseEventKind::absolute_motion:
            return submit_absolute_motion(event);
          case MouseEventKind::button:
            return submit_button(event);
          case MouseEventKind::vertical_scroll:
            return submit_vertical_scroll(event.high_resolution_scroll);
          case MouseEventKind::horizontal_scroll:
            return submit_horizontal_scroll(event.high_resolution_scroll);
        }

        return Status::failure(ErrorCode::invalid_argument, "unsupported mouse event kind");
      }

      Status close() override {
        return close_uinput("uinput mouse");
      }

    private:
      Status submit_relative_motion(const MouseEvent &event) {
        if (event.x != 0) {
          if (const auto status = emit_event(EV_REL, REL_X, event.x); !status.ok()) {
            return status;
          }
        }
        if (event.y != 0) {
          if (const auto status = emit_event(EV_REL, REL_Y, event.y); !status.ok()) {
            return status;
          }
        }
        return sync();
      }

      Status submit_absolute_motion(const MouseEvent &event) {
        if (const auto status = emit_event(EV_ABS, ABS_X, scale_absolute_axis(event.x, event.width)); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_Y, scale_absolute_axis(event.y, event.height)); !status.ok()) {
          return status;
        }
        return sync();
      }

      Status submit_button(const MouseEvent &event) {
        if (const auto status = emit_event(EV_KEY, static_cast<std::uint16_t>(mouse_button_to_linux(event.button)), event.pressed ? 1 : 0); !status.ok()) {
          return status;
        }
        return sync();
      }

      Status submit_vertical_scroll(std::int32_t distance) {
#if defined(REL_WHEEL_HI_RES)
        if (const auto status = emit_event(EV_REL, REL_WHEEL_HI_RES, distance); !status.ok()) {
          return status;
        }
#else
        if (const auto status = emit_event(EV_REL, REL_WHEEL, legacy_scroll_steps(distance)); !status.ok()) {
          return status;
        }
#endif
        return sync();
      }

      Status submit_horizontal_scroll(std::int32_t distance) {
#if defined(REL_HWHEEL_HI_RES)
        if (const auto status = emit_event(EV_REL, REL_HWHEEL_HI_RES, distance); !status.ok()) {
          return status;
        }
#else
        if (const auto status = emit_event(EV_REL, REL_HWHEEL, legacy_scroll_steps(distance)); !status.ok()) {
          return status;
        }
#endif
        return sync();
      }
    };

#if defined(LIBVIRTUALHID_HAVE_XTEST)
    KeySym key_code_to_keysym(KeyboardKeyCode key_code) {
      switch (key_code) {
        case 0x08:
          return XK_BackSpace;
        case 0x09:
          return XK_Tab;
        case 0x0D:
          return XK_Return;
        case 0x10:
        case 0xA0:
          return XK_Shift_L;
        case 0x11:
        case 0xA2:
          return XK_Control_L;
        case 0x12:
        case 0xA4:
          return XK_Alt_L;
        case 0x14:
          return XK_Caps_Lock;
        case 0x1B:
          return XK_Escape;
        case 0x20:
          return XK_space;
        case 0x21:
          return XK_Page_Up;
        case 0x22:
          return XK_Page_Down;
        case 0x23:
          return XK_End;
        case 0x24:
          return XK_Home;
        case 0x25:
          return XK_Left;
        case 0x26:
          return XK_Up;
        case 0x27:
          return XK_Right;
        case 0x28:
          return XK_Down;
        case 0x2D:
          return XK_Insert;
        case 0x2E:
          return XK_Delete;
        case 0x5B:
          return XK_Super_L;
        case 0x5C:
          return XK_Super_R;
        case 0x90:
          return XK_Num_Lock;
        case 0x91:
          return XK_Scroll_Lock;
        case 0xA1:
          return XK_Shift_R;
        case 0xA3:
          return XK_Control_R;
        case 0xA5:
          return XK_Alt_R;
        case 0xBA:
          return XK_semicolon;
        case 0xBB:
          return XK_equal;
        case 0xBC:
          return XK_comma;
        case 0xBD:
          return XK_minus;
        case 0xBE:
          return XK_period;
        case 0xBF:
          return XK_slash;
        case 0xC0:
          return XK_grave;
        case 0xDB:
          return XK_bracketleft;
        case 0xDC:
          return XK_backslash;
        case 0xDD:
          return XK_bracketright;
        case 0xDE:
          return XK_apostrophe;
        default:
          break;
      }

      if (key_code >= 0x30 && key_code <= 0x39) {
        return XK_0 + static_cast<KeySym>(key_code - 0x30);
      }
      if (key_code >= 0x41 && key_code <= 0x5A) {
        return XK_a + static_cast<KeySym>(key_code - 0x41);
      }
      if (key_code >= 0x60 && key_code <= 0x69) {
        return XK_KP_0 + static_cast<KeySym>(key_code - 0x60);
      }
      if (key_code == 0x6A) {
        return XK_KP_Multiply;
      }
      if (key_code == 0x6B) {
        return XK_KP_Add;
      }
      if (key_code == 0x6D) {
        return XK_KP_Subtract;
      }
      if (key_code == 0x6E) {
        return XK_KP_Decimal;
      }
      if (key_code == 0x6F) {
        return XK_KP_Divide;
      }
      if (key_code >= 0x70 && key_code <= 0x87) {
        return XK_F1 + static_cast<KeySym>(key_code - 0x70);
      }

      return NoSymbol;
    }

    int mouse_button_to_xtest(MouseButton button) {
      switch (button) {
        case MouseButton::left:
          return 1;
        case MouseButton::middle:
          return 2;
        case MouseButton::right:
          return 3;
        case MouseButton::side:
          return 8;
        case MouseButton::extra:
          return 9;
      }

      return 1;
    }

    bool query_xtest(Display *display) {
      int event_base = 0;
      int error_base = 0;
      int major = 0;
      int minor = 0;
      return XTestQueryExtension(display, &event_base, &error_base, &major, &minor) == True;
    }

    bool can_use_xtest() {
      Display *display = XOpenDisplay(nullptr);
      if (display == nullptr) {
        return false;
      }

      const auto available = query_xtest(display);
      XCloseDisplay(display);
      return available;
    }

    /**
     * @brief Backend keyboard backed by X11 XTest fallback events.
     */
    class XTestKeyboard final: public BackendKeyboard {
    public:
      XTestKeyboard() = default;

      ~XTestKeyboard() override {
        static_cast<void>(close());
      }

      Status create() {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
          return Status::failure(ErrorCode::backend_unavailable, "failed to open X display for XTest keyboard fallback");
        }
        if (!query_xtest(display_)) {
          return Status::failure(ErrorCode::backend_unavailable, "XTest extension is not available");
        }

        return Status::success();
      }

      Status submit(const KeyboardEvent &event) override {
        if (display_ == nullptr) {
          return Status::failure(ErrorCode::device_closed, "XTest keyboard is closed");
        }

        const auto keysym = key_code_to_keysym(event.key_code);
        if (keysym == NoSymbol) {
          return Status::failure(ErrorCode::invalid_argument, "keyboard key code is not supported by XTest fallback");
        }

        const auto keycode = XKeysymToKeycode(display_, keysym);
        if (keycode == 0) {
          return Status::failure(ErrorCode::invalid_argument, "keyboard key code has no X11 keycode");
        }

        XTestFakeKeyEvent(display_, keycode, event.pressed ? True : False, CurrentTime);
        XFlush(display_);
        return Status::success();
      }

      Status type_text(const KeyboardTextEvent &event) override {
        for (const auto codepoint : decode_utf8(event.text)) {
          const auto hex = uppercase_hex(codepoint);

          if (const auto status = submit({.key_code = 0xA2, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0xA0, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0x55, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0x55, .pressed = false}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0xA0, .pressed = false}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0xA2, .pressed = false}); !status.ok()) {
            return status;
          }

          for (const auto digit : hex) {
            const auto key_code = hex_digit_key_code(digit);
            if (const auto status = submit({.key_code = key_code, .pressed = true}); !status.ok()) {
              return status;
            }
            if (const auto status = submit({.key_code = key_code, .pressed = false}); !status.ok()) {
              return status;
            }
          }

          if (const auto status = submit({.key_code = 0x0D, .pressed = true}); !status.ok()) {
            return status;
          }
          if (const auto status = submit({.key_code = 0x0D, .pressed = false}); !status.ok()) {
            return status;
          }
        }

        return Status::success();
      }

      Status close() override {
        if (display_ != nullptr) {
          XCloseDisplay(display_);
          display_ = nullptr;
        }
        return Status::success();
      }

    private:
      Display *display_ = nullptr;
    };

    /**
     * @brief Backend mouse backed by X11 XTest fallback events.
     */
    class XTestMouse final: public BackendMouse {
    public:
      XTestMouse() = default;

      ~XTestMouse() override {
        static_cast<void>(close());
      }

      Status create() {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
          return Status::failure(ErrorCode::backend_unavailable, "failed to open X display for XTest mouse fallback");
        }
        if (!query_xtest(display_)) {
          return Status::failure(ErrorCode::backend_unavailable, "XTest extension is not available");
        }

        return Status::success();
      }

      Status submit(const MouseEvent &event) override {
        if (display_ == nullptr) {
          return Status::failure(ErrorCode::device_closed, "XTest mouse is closed");
        }

        switch (event.kind) {
          case MouseEventKind::relative_motion:
            XTestFakeRelativeMotionEvent(display_, event.x, event.y, CurrentTime);
            break;
          case MouseEventKind::absolute_motion:
            submit_absolute_motion(event);
            break;
          case MouseEventKind::button:
            XTestFakeButtonEvent(display_, mouse_button_to_xtest(event.button), event.pressed ? True : False, CurrentTime);
            break;
          case MouseEventKind::vertical_scroll:
            submit_scroll(event.high_resolution_scroll, 4, 5);
            break;
          case MouseEventKind::horizontal_scroll:
            submit_scroll(event.high_resolution_scroll, 6, 7);
            break;
        }

        XFlush(display_);
        return Status::success();
      }

      Status close() override {
        if (display_ != nullptr) {
          XCloseDisplay(display_);
          display_ = nullptr;
        }
        return Status::success();
      }

    private:
      void submit_absolute_motion(const MouseEvent &event) {
        const auto screen = DefaultScreen(display_);
        const auto screen_width = DisplayWidth(display_, screen);
        const auto screen_height = DisplayHeight(display_, screen);
        const auto x = scale_absolute_axis(event.x, event.width) * std::max(screen_width - 1, 0) / absolute_axis_max;
        const auto y = scale_absolute_axis(event.y, event.height) * std::max(screen_height - 1, 0) / absolute_axis_max;
        XTestFakeMotionEvent(display_, screen, x, y, CurrentTime);
      }

      void submit_scroll(std::int32_t distance, int positive_button, int negative_button) {
        const auto steps = std::abs(legacy_scroll_steps(distance));
        const auto button = distance >= 0 ? positive_button : negative_button;
        for (auto i = 0; i < steps; ++i) {
          XTestFakeButtonEvent(display_, button, True, CurrentTime);
          XTestFakeButtonEvent(display_, button, False, CurrentTime);
        }
      }

      Display *display_ = nullptr;
    };
#else
    bool can_use_xtest() {
      return false;
    }
#endif

    /**
     * @brief Backend gamepad backed by one Linux UHID file descriptor.
     */
    class UhidGamepad final: public BackendGamepad {
    public:
      explicit UhidGamepad(int file_descriptor):
          fd_ {file_descriptor} {}

      UhidGamepad(const UhidGamepad &) = delete;
      UhidGamepad &operator=(const UhidGamepad &) = delete;
      UhidGamepad(UhidGamepad &&) noexcept = delete;
      UhidGamepad &operator=(UhidGamepad &&) noexcept = delete;

      ~UhidGamepad() override {
        static_cast<void>(close());
      }

      Status create(DeviceId id, const CreateGamepadOptions &options) {
        uhid_event event {};
        auto &request = event.u.create2;

        if (options.profile.report_descriptor.size() > sizeof(request.rd_data)) {
          return Status::failure(ErrorCode::unsupported_profile, "HID report descriptor is too large for UHID");
        }

        event.type = UHID_CREATE2;
        copy_string(request.name, options.profile.name);
        copy_string(request.phys, "libvirtualhid/uhid/" + std::to_string(id));
        copy_string(request.uniq, options.metadata.stable_id.empty() ? std::to_string(id) : options.metadata.stable_id);
        request.rd_size = static_cast<std::uint16_t>(options.profile.report_descriptor.size());
        request.bus = to_uhid_bus(options.profile.bus_type);
        request.vendor = options.profile.vendor_id;
        request.product = options.profile.product_id;
        request.version = options.profile.version;
        std::memcpy(request.rd_data, options.profile.report_descriptor.data(), options.profile.report_descriptor.size());
        profile_ = options.profile;

        if (const auto status = write_event(event); !status.ok()) {
          return status;
        }

        running_ = true;
        reader_ = std::thread {[this]() {
          read_loop();
        }};
        return Status::success();
      }

      Status submit(const std::vector<std::uint8_t> &report) override {
        if (!open_) {
          return Status::failure(ErrorCode::device_closed, "UHID gamepad is closed");
        }

        uhid_event event {};
        if (report.size() > sizeof(event.u.input2.data)) {
          return Status::failure(ErrorCode::invalid_argument, "HID input report is too large for UHID");
        }

        event.type = UHID_INPUT2;
        event.u.input2.size = static_cast<std::uint16_t>(report.size());
        std::memcpy(event.u.input2.data, report.data(), report.size());
        return write_event(event);
      }

      void set_output_callback(OutputCallback callback) override {
        std::lock_guard lock {callback_mutex_};
        output_callback_ = std::move(callback);
      }

      Status close() override {
        if (!open_.exchange(false)) {
          return Status::success();
        }

        running_ = false;

        auto status = Status::success();
        if (fd_ >= 0) {
          uhid_event event {};
          event.type = UHID_DESTROY;
          status = write_event(event);
        }

        if (reader_.joinable()) {
          reader_.join();
        }

        if (fd_ >= 0) {
          if (system_close(fd_) != 0 && status.ok()) {
            status = system_error_status(ErrorCode::backend_failure, "failed to close /dev/uhid", errno);
          }
          fd_ = -1;
        }

        return status;
      }

    private:
      Status write_event(const uhid_event &event) {
        std::lock_guard lock {write_mutex_};
        if (fd_ < 0) {
          return Status::failure(ErrorCode::device_closed, "UHID file descriptor is closed");
        }

        const auto result = system_write(fd_, &event, sizeof(event));
        if (result < 0) {
          return system_error_status(ErrorCode::backend_failure, "failed to write UHID event", errno);
        }
        if (static_cast<std::size_t>(result) != sizeof(event)) {
          return Status::failure(ErrorCode::backend_failure, "short write while sending UHID event");
        }

        return Status::success();
      }

      void read_loop() {
        while (running_) {
          pollfd descriptor {};
          descriptor.fd = fd_;
          descriptor.events = POLLIN;

          const auto result = system_poll(&descriptor, 1, poll_timeout_ms);
          if (result < 0) {
            if (errno == EINTR) {
              continue;
            }
            break;
          }
          if (result == 0) {
            continue;
          }
          if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            break;
          }
          if ((descriptor.revents & POLLIN) == 0) {
            continue;
          }

          uhid_event event {};
          const auto read_result = system_read(fd_, &event, sizeof(event));
          if (read_result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
              continue;
            }
            break;
          }
          if (read_result == 0) {
            break;
          }

          handle_event(event);
        }
      }

      void handle_event(const uhid_event &event) {
        switch (event.type) {
          case UHID_OUTPUT:
            dispatch_output_report(event.u.output.data, event.u.output.size);
            break;
          case UHID_GET_REPORT:
            send_get_report_reply(event.u.get_report.id);
            break;
          case UHID_SET_REPORT:
            dispatch_output_report(event.u.set_report.data, event.u.set_report.size);
            send_set_report_reply(event.u.set_report.id, 0);
            break;
          default:
            break;
        }
      }

      void dispatch_output_report(const __u8 *data, std::size_t report_size) {
        OutputCallback callback;
        {
          std::lock_guard lock {callback_mutex_};
          callback = output_callback_;
        }

        if (!callback) {
          return;
        }

        const auto size = std::min<std::size_t>(report_size, UHID_DATA_MAX);
        std::vector<std::uint8_t> report(data, data + size);
        auto output = reports::parse_output_report(profile_, report);
        callback(output);
      }

      void send_get_report_reply(std::uint32_t id) {
        uhid_event event {};
        event.type = UHID_GET_REPORT_REPLY;
        event.u.get_report_reply.id = id;
        event.u.get_report_reply.err = EIO;
        static_cast<void>(write_event(event));
      }

      void send_set_report_reply(std::uint32_t id, std::uint16_t error) {
        uhid_event event {};
        event.type = UHID_SET_REPORT_REPLY;
        event.u.set_report_reply.id = id;
        event.u.set_report_reply.err = error;
        static_cast<void>(write_event(event));
      }

      int fd_ = -1;
      DeviceProfile profile_;
      std::atomic_bool open_ = true;
      std::atomic_bool running_ = false;
      std::thread reader_;
      std::mutex write_mutex_;
      std::mutex callback_mutex_;
      OutputCallback output_callback_;
    };

    /**
     * @brief Linux platform backend backed by UHID.
     */
    class LinuxUhidBackend final: public Backend {
    public:
      LinuxUhidBackend() {
        const auto uhid_accessible = can_access_uhid();
        const auto uinput_accessible = can_access_uinput();
        const auto xtest_accessible = can_use_xtest();
        capabilities_.backend_name = "linux-uhid-uinput";
        capabilities_.supports_virtual_hid = uhid_accessible || uinput_accessible;
        capabilities_.supports_gamepad = uhid_accessible;
        capabilities_.supports_keyboard = uinput_accessible || xtest_accessible;
        capabilities_.supports_mouse = uinput_accessible || xtest_accessible;
        capabilities_.supports_output_reports = uhid_accessible;
        capabilities_.supports_xtest_fallback = xtest_accessible;
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) override {
        const auto fd = system_open(uhid_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
          return {system_error_status(ErrorCode::backend_unavailable, "failed to open /dev/uhid", errno), nullptr};
        }

        auto gamepad = std::make_unique<UhidGamepad>(fd);
        if (const auto status = gamepad->create(id, options); !status.ok()) {
          static_cast<void>(gamepad->close());
          return {status, nullptr};
        }

        return {Status::success(), std::move(gamepad)};
      }

      BackendKeyboardCreationResult create_keyboard(DeviceId id, const CreateKeyboardOptions &options) override {
        const auto fd = system_open(uinput_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
          return create_xtest_keyboard();
        }

        auto keyboard = std::make_unique<UinputKeyboard>(fd);
        if (const auto status = keyboard->create(id, options); !status.ok()) {
          static_cast<void>(keyboard->close());
          auto fallback = create_xtest_keyboard();
          if (fallback) {
            return fallback;
          }
          return {status, nullptr};
        }

        return {Status::success(), std::move(keyboard)};
      }

      BackendMouseCreationResult create_mouse(DeviceId id, const CreateMouseOptions &options) override {
        const auto fd = system_open(uinput_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
          return create_xtest_mouse();
        }

        auto mouse = std::make_unique<UinputMouse>(fd);
        if (const auto status = mouse->create(id, options); !status.ok()) {
          static_cast<void>(mouse->close());
          auto fallback = create_xtest_mouse();
          if (fallback) {
            return fallback;
          }
          return {status, nullptr};
        }

        return {Status::success(), std::move(mouse)};
      }

    private:
      BackendKeyboardCreationResult create_xtest_keyboard() {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
        if (active_test_syscalls != nullptr && active_test_syscalls->fake_xtest_keyboard) {
          return {Status::success(), std::make_unique<UinputKeyboard>(active_test_syscalls->open_result)};
        }
#endif
#if defined(LIBVIRTUALHID_HAVE_XTEST)
        auto keyboard = std::make_unique<XTestKeyboard>();
        if (const auto status = keyboard->create(); !status.ok()) {
          return {status, nullptr};
        }
        return {Status::success(), std::move(keyboard)};
#else
        return {Status::failure(ErrorCode::backend_unavailable, "failed to open /dev/uinput"), nullptr};
#endif
      }

      BackendMouseCreationResult create_xtest_mouse() {
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
        if (active_test_syscalls != nullptr && active_test_syscalls->fake_xtest_mouse) {
          return {Status::success(), std::make_unique<UinputMouse>(active_test_syscalls->open_result)};
        }
#endif
#if defined(LIBVIRTUALHID_HAVE_XTEST)
        auto mouse = std::make_unique<XTestMouse>();
        if (const auto status = mouse->create(); !status.ok()) {
          return {status, nullptr};
        }
        return {Status::success(), std::move(mouse)};
#else
        return {Status::failure(ErrorCode::backend_unavailable, "failed to open /dev/uinput"), nullptr};
#endif
      }

      BackendCapabilities capabilities_;
    };

  }  // namespace

#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
  namespace test {
    namespace {
      constexpr auto fake_fd = 100000;

      std::vector<LinuxInputEventRecord> read_input_events_until_eof(int fd) {
        std::vector<LinuxInputEventRecord> records;
        input_event event {};
        while (::read(fd, &event, sizeof(event)) == sizeof(event)) {
          records.push_back({
            .type = event.type,
            .code = event.code,
            .value = event.value,
          });
        }
        return records;
      }

      bool write_uhid_event(int fd, const uhid_event &event) {
        auto *data = reinterpret_cast<const char *>(&event);
        std::size_t written = 0;
        while (written < sizeof(event)) {
          const auto result = ::write(fd, data + written, sizeof(event) - written);
          if (result <= 0) {
            return false;
          }
          written += static_cast<std::size_t>(result);
        }
        return true;
      }

      bool read_uhid_event(int fd, uhid_event &event) {
        pollfd descriptor {};
        descriptor.fd = fd;
        descriptor.events = POLLIN;
        const auto poll_result = ::poll(&descriptor, 1, 1000);
        if (poll_result <= 0 || (descriptor.revents & POLLIN) == 0) {
          return false;
        }

        auto *data = reinterpret_cast<char *>(&event);
        std::size_t read_size = 0;
        while (read_size < sizeof(event)) {
          const auto result = ::read(fd, data + read_size, sizeof(event) - read_size);
          if (result <= 0) {
            return false;
          }
          read_size += static_cast<std::size_t>(result);
        }
        return true;
      }

      void enable_fake_device_syscalls(LinuxTestSyscalls &syscalls) {
        syscalls.override_access = true;
        syscalls.override_open = true;
        syscalls.override_write = true;
        syscalls.override_ioctl = true;
        syscalls.override_close = true;
      }

      bool wait_for_poll_calls(const LinuxTestSyscalls &syscalls, int expected_calls) {
        using namespace std::chrono_literals;

        for (auto attempt = 0; attempt < 100; ++attempt) {
          if (syscalls.poll_call_count.load() >= expected_calls) {
            return true;
          }
          std::this_thread::sleep_for(1ms);
        }
        return false;
      }

      Status run_fake_uhid_read_loop(LinuxTestSyscalls &syscalls, int expected_poll_calls) {
        syscalls.override_write = true;
        syscalls.override_close = true;

        ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

        CreateGamepadOptions options;
        options.profile = profiles::generic_gamepad();

        UhidGamepad gamepad {fake_fd};
        if (const auto status = gamepad.create(1, options); !status.ok()) {
          return status;
        }

        const auto saw_expected_polls = wait_for_poll_calls(syscalls, expected_poll_calls);
        const auto close_status = gamepad.close();
        if (!saw_expected_polls) {
          return Status::failure(ErrorCode::backend_failure, "fake UHID read loop did not consume the scripted poll calls");
        }
        return close_status;
      }

    }  // namespace

    std::string linux_copy_string_char_buffer(const std::string &source) {
      char destination[5] {};
      copy_string(destination, source);
      return destination;
    }

    int linux_key_code(KeyboardKeyCode key_code) {
      return key_code_to_linux(key_code);
    }

    int linux_mouse_button(MouseButton button) {
      return mouse_button_to_linux(button);
    }

    std::uint16_t linux_uhid_bus(BusType bus_type) {
      return to_uhid_bus(bus_type);
    }

    std::uint16_t linux_uinput_bus(BusType bus_type) {
      return to_uinput_bus(bus_type);
    }

    int linux_absolute_axis(std::int32_t value, std::int32_t limit) {
      return scale_absolute_axis(value, limit);
    }

    std::vector<std::uint32_t> linux_decode_utf8(const std::string &text) {
      return decode_utf8(text);
    }

    std::string linux_uppercase_hex(std::uint32_t codepoint) {
      return uppercase_hex(codepoint);
    }

    KeyboardKeyCode linux_hex_digit_key_code(char digit) {
      return hex_digit_key_code(digit);
    }

    int linux_legacy_scroll_steps(std::int32_t distance) {
      return legacy_scroll_steps(distance);
    }

    std::size_t linux_uhid_descriptor_limit() {
      uhid_event event {};
      return sizeof(event.u.create2.rd_data);
    }

    std::size_t linux_uhid_input_limit() {
      uhid_event event {};
      return sizeof(event.u.input2.data);
    }

    Status linux_uhid_create_with_descriptor_size(std::size_t descriptor_size) {
      auto profile = profiles::generic_gamepad();
      profile.report_descriptor.assign(descriptor_size, 0);

      CreateGamepadOptions options;
      options.profile = std::move(profile);

      UhidGamepad gamepad {-1};
      return gamepad.create(1, options);
    }

    Status linux_uhid_submit_report_size(std::size_t report_size) {
      UhidGamepad gamepad {-1};
      return gamepad.submit(std::vector<std::uint8_t>(report_size, 0));
    }

    Status linux_uhid_submit_after_close() {
      UhidGamepad gamepad {-1};
      static_cast<void>(gamepad.close());
      return gamepad.submit({0});
    }

    Status linux_uinput_keyboard_create_invalid_fd() {
      CreateKeyboardOptions options;
      options.profile = profiles::keyboard();

      UinputKeyboard keyboard {-1};
      return keyboard.create(1, options);
    }

    Status linux_uinput_keyboard_submit_invalid_fd(const KeyboardEvent &event) {
      UinputKeyboard keyboard {-1};
      return keyboard.submit(event);
    }

    Status linux_uinput_keyboard_type_text_invalid_fd(const std::string &text) {
      UinputKeyboard keyboard {-1};
      return keyboard.type_text({.text = text});
    }

    Status linux_uinput_keyboard_submit_after_close() {
      UinputKeyboard keyboard {-1};
      static_cast<void>(keyboard.close());
      return keyboard.submit({.key_code = 0x41, .pressed = true});
    }

    LinuxInputSubmissionResult linux_uinput_keyboard_submit_pipe(const KeyboardEvent &event) {
      int descriptors[2] {-1, -1};
      if (::pipe(descriptors) != 0) {
        return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
      }

      UinputKeyboard keyboard {descriptors[1]};
      auto status = keyboard.submit(event);
      static_cast<void>(keyboard.close());
      auto records = read_input_events_until_eof(descriptors[0]);
      static_cast<void>(::close(descriptors[0]));
      return {std::move(status), std::move(records)};
    }

    Status linux_uinput_user_device_invalid_fd() {
      return write_uinput_user_device(-1, profiles::mouse(), 1);
    }

    Status linux_uinput_user_device_pipe() {
      int descriptors[2] {-1, -1};
      if (::pipe(descriptors) != 0) {
        return system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno);
      }

      auto status = write_uinput_user_device(descriptors[1], profiles::mouse(), 1);
      static_cast<void>(::close(descriptors[0]));
      static_cast<void>(::close(descriptors[1]));
      return status;
    }

    Status linux_uinput_mouse_create_invalid_fd() {
      CreateMouseOptions options;
      options.profile = profiles::mouse();

      UinputMouse mouse {-1};
      return mouse.create(1, options);
    }

    Status linux_uinput_mouse_submit_invalid_fd(const MouseEvent &event) {
      UinputMouse mouse {-1};
      return mouse.submit(event);
    }

    Status linux_uinput_mouse_submit_after_close() {
      UinputMouse mouse {-1};
      static_cast<void>(mouse.close());
      return mouse.submit({.kind = MouseEventKind::relative_motion, .x = 1, .y = 1});
    }

    LinuxInputSubmissionResult linux_uinput_mouse_submit_pipe(const MouseEvent &event) {
      int descriptors[2] {-1, -1};
      if (::pipe(descriptors) != 0) {
        return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
      }

      UinputMouse mouse {descriptors[1]};
      auto status = mouse.submit(event);
      static_cast<void>(mouse.close());
      auto records = read_input_events_until_eof(descriptors[0]);
      static_cast<void>(::close(descriptors[0]));
      return {std::move(status), std::move(records)};
    }

    LinuxUhidRoundTripResult linux_uhid_socketpair_roundtrip() {
      LinuxUhidRoundTripResult result;
      int descriptors[2] {-1, -1};
      if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        result.create_status = system_error_status(ErrorCode::backend_failure, "failed to create socketpair", errno);
        result.submit_status = result.create_status;
        result.close_status = result.create_status;
        return result;
      }

      auto profile = profiles::xbox_360();
      CreateGamepadOptions options;
      options.profile = profile;
      options.metadata.stable_id = "linux-uhid-roundtrip";

      UhidGamepad gamepad {descriptors[0]};
      result.create_status = gamepad.create(7, options);

      uhid_event event {};
      if (read_uhid_event(descriptors[1], event)) {
        result.saw_create = event.type == UHID_CREATE2 && event.u.create2.vendor == profile.vendor_id &&
                            event.u.create2.product == profile.product_id;
      }

      gamepad.set_output_callback([&result](const GamepadOutput &output) {
        ++result.output_callback_count;
        result.last_output = output;
      });

      event = {};
      event.type = UHID_OUTPUT;
      event.u.output.size = static_cast<__u16>(profile.output_report_size);
      event.u.output.data[0] = profile.report_id;
      event.u.output.data[1] = 0x34;
      event.u.output.data[2] = 0x12;
      event.u.output.data[3] = 0x78;
      event.u.output.data[4] = 0x56;
      static_cast<void>(write_uhid_event(descriptors[1], event));

      event = {};
      event.type = UHID_GET_REPORT;
      event.u.get_report.id = 9;
      static_cast<void>(write_uhid_event(descriptors[1], event));
      if (read_uhid_event(descriptors[1], event)) {
        result.saw_get_report_reply = event.type == UHID_GET_REPORT_REPLY && event.u.get_report_reply.id == 9;
      }

      event = {};
      event.type = UHID_SET_REPORT;
      event.u.set_report.id = 10;
      event.u.set_report.size = static_cast<__u16>(profile.output_report_size);
      event.u.set_report.data[0] = profile.report_id;
      event.u.set_report.data[1] = 0x78;
      event.u.set_report.data[2] = 0x56;
      event.u.set_report.data[3] = 0x34;
      event.u.set_report.data[4] = 0x12;
      static_cast<void>(write_uhid_event(descriptors[1], event));
      if (read_uhid_event(descriptors[1], event)) {
        result.saw_set_report_reply = event.type == UHID_SET_REPORT_REPLY && event.u.set_report_reply.id == 10;
      }

      event = {};
      event.type = UHID_OPEN;
      static_cast<void>(write_uhid_event(descriptors[1], event));

      lvh::GamepadState state;
      state.buttons.set(GamepadButton::a);
      const auto report = reports::pack_input_report(profile, state);
      result.submit_status = gamepad.submit(report);
      if (read_uhid_event(descriptors[1], event)) {
        result.saw_input = event.type == UHID_INPUT2 && event.u.input2.size == report.size();
      }

      result.close_status = gamepad.close();
      if (read_uhid_event(descriptors[1], event)) {
        result.saw_destroy = event.type == UHID_DESTROY;
      }

      static_cast<void>(::close(descriptors[1]));
      return result;
    }

    LinuxBackendFakeCreationResult linux_backend_create_all_fake_success() {
      LinuxTestSyscalls syscalls;
      enable_fake_device_syscalls(syscalls);
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxBackendFakeCreationResult result;
      LinuxUhidBackend backend;
      result.capabilities = backend.capabilities();

      CreateGamepadOptions gamepad_options;
      gamepad_options.profile = profiles::xbox_360();
      gamepad_options.metadata.stable_id = "fake-linux-gamepad";
      auto gamepad = backend.create_gamepad(1, gamepad_options);
      result.gamepad_status = gamepad.status;
      if (gamepad) {
        result.gamepad_close_status = gamepad.gamepad->close();
      }

      CreateKeyboardOptions keyboard_options;
      keyboard_options.profile = profiles::keyboard();
      keyboard_options.stable_id = "fake-linux-keyboard";
      auto keyboard = backend.create_keyboard(2, keyboard_options);
      result.keyboard_status = keyboard.status;
      if (keyboard) {
        result.keyboard_close_status = keyboard.keyboard->close();
      }

      CreateMouseOptions mouse_options;
      mouse_options.profile = profiles::mouse();
      mouse_options.stable_id = "fake-linux-mouse";
      auto mouse = backend.create_mouse(3, mouse_options);
      result.mouse_status = mouse.status;
      if (mouse) {
        result.mouse_close_status = mouse.mouse->close();
      }

      return result;
    }

    BackendCapabilities linux_backend_fake_unavailable_capabilities() {
      LinuxTestSyscalls syscalls;
      syscalls.override_access = true;
      syscalls.access_result = -1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;
      return backend.capabilities();
    }

    Status linux_backend_gamepad_fake_open_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_access = true;
      syscalls.override_open = true;
      syscalls.open_result = -1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateGamepadOptions options;
      options.profile = profiles::xbox_360();
      return backend.create_gamepad(1, options).status;
    }

    Status linux_backend_gamepad_fake_create_failure() {
      LinuxTestSyscalls syscalls;
      enable_fake_device_syscalls(syscalls);
      syscalls.fail_write_call = 1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateGamepadOptions options;
      options.profile = profiles::xbox_360();
      return backend.create_gamepad(1, options).status;
    }

    Status linux_backend_keyboard_fake_open_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_access = true;
      syscalls.override_open = true;
      syscalls.open_result = -1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateKeyboardOptions options;
      options.profile = profiles::keyboard();
      return backend.create_keyboard(1, options).status;
    }

    Status linux_backend_keyboard_fake_create_failure() {
      LinuxTestSyscalls syscalls;
      enable_fake_device_syscalls(syscalls);
      syscalls.fail_ioctl_call = 1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateKeyboardOptions options;
      options.profile = profiles::keyboard();
      return backend.create_keyboard(1, options).status;
    }

    Status linux_backend_keyboard_fake_fallback_success() {
      LinuxTestSyscalls syscalls;
      enable_fake_device_syscalls(syscalls);
      syscalls.fail_ioctl_call = 1;
      syscalls.fake_xtest_keyboard = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateKeyboardOptions options;
      options.profile = profiles::keyboard();
      auto keyboard = backend.create_keyboard(1, options);
      if (!keyboard) {
        return keyboard.status;
      }
      return keyboard.keyboard->close();
    }

    Status linux_backend_mouse_fake_open_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_access = true;
      syscalls.override_open = true;
      syscalls.open_result = -1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateMouseOptions options;
      options.profile = profiles::mouse();
      return backend.create_mouse(1, options).status;
    }

    Status linux_backend_mouse_fake_create_failure() {
      LinuxTestSyscalls syscalls;
      enable_fake_device_syscalls(syscalls);
      syscalls.fail_ioctl_call = 1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateMouseOptions options;
      options.profile = profiles::mouse();
      return backend.create_mouse(1, options).status;
    }

    Status linux_backend_mouse_fake_fallback_success() {
      LinuxTestSyscalls syscalls;
      enable_fake_device_syscalls(syscalls);
      syscalls.fail_ioctl_call = 1;
      syscalls.fake_xtest_mouse = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxUhidBackend backend;

      CreateMouseOptions options;
      options.profile = profiles::mouse();
      auto mouse = backend.create_mouse(1, options);
      if (!mouse) {
        return mouse.status;
      }
      return mouse.mouse->close();
    }

    Status linux_uhid_submit_fake_write_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.fail_write_call = 1;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UhidGamepad gamepad {fake_fd};
      return gamepad.submit({0});
    }

    Status linux_uhid_submit_fake_short_write() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.short_write_call = 1;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UhidGamepad gamepad {fake_fd};
      return gamepad.submit({0});
    }

    Status linux_uhid_close_fake_write_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.fail_write_call = 1;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UhidGamepad gamepad {fake_fd};
      return gamepad.close();
    }

    Status linux_uhid_close_fake_close_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.override_close = true;
      syscalls.close_result = -1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UhidGamepad gamepad {fake_fd};
      return gamepad.close();
    }

    Status linux_uhid_read_loop_fake_retry_branches() {
      LinuxTestSyscalls syscalls;
      syscalls.override_poll = true;
      syscalls.poll_results = {-1, 0, 1, 1, 1};
      syscalls.poll_revents = {0, 0, 0, POLLIN, POLLIN};
      syscalls.poll_errors = {EINTR};
      syscalls.override_read = true;
      syscalls.read_results = {-1, 0};
      syscalls.read_errors = {EAGAIN};
      return run_fake_uhid_read_loop(syscalls, 5);
    }

    Status linux_uhid_read_loop_fake_poll_errors() {
      LinuxTestSyscalls syscall_failure;
      syscall_failure.override_poll = true;
      syscall_failure.poll_results = {-1};
      syscall_failure.poll_errors = {EIO};
      if (const auto status = run_fake_uhid_read_loop(syscall_failure, 1); !status.ok()) {
        return status;
      }

      LinuxTestSyscalls event_failure;
      event_failure.override_poll = true;
      event_failure.poll_results = {1};
      event_failure.poll_revents = {POLLERR};
      return run_fake_uhid_read_loop(event_failure, 1);
    }

    Status linux_uhid_read_loop_fake_read_error() {
      LinuxTestSyscalls syscalls;
      syscalls.override_poll = true;
      syscalls.poll_results = {1};
      syscalls.poll_revents = {POLLIN};
      syscalls.override_read = true;
      syscalls.read_results = {-1};
      syscalls.read_errors = {EIO};
      return run_fake_uhid_read_loop(syscalls, 1);
    }

    Status linux_uhid_read_loop_fake_output_without_callback() {
      LinuxTestSyscalls syscalls;
      syscalls.override_poll = true;
      syscalls.poll_results = {1, 1};
      syscalls.poll_revents = {POLLIN, POLLIN};
      syscalls.override_read = true;
      syscalls.read_results = {static_cast<std::ptrdiff_t>(sizeof(uhid_event)), 0};
      syscalls.read_event.type = UHID_OUTPUT;
      return run_fake_uhid_read_loop(syscalls, 2);
    }

    Status linux_uinput_keyboard_create_fake_ioctl_failure(int fail_ioctl_call) {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.override_ioctl = true;
      syscalls.fail_ioctl_call = fail_ioctl_call;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      CreateKeyboardOptions options;
      options.profile = profiles::keyboard();

      UinputKeyboard keyboard {fake_fd};
      return keyboard.create(1, options);
    }

    Status linux_uinput_user_device_fake_short_write() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.short_write_call = 1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      return write_uinput_user_device(fake_fd, profiles::mouse(), 1);
    }

    Status linux_uinput_user_device_fake_create_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.override_ioctl = true;
      syscalls.fail_ioctl_call = 1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      return write_uinput_user_device(fake_fd, profiles::mouse(), 1);
    }

    Status linux_uinput_keyboard_submit_fake_write_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.fail_write_call = 1;
      syscalls.override_ioctl = true;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UinputKeyboard keyboard {fake_fd};
      return keyboard.submit({.key_code = 0x41, .pressed = true});
    }

    Status linux_uinput_keyboard_submit_fake_short_write() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.short_write_call = 1;
      syscalls.override_ioctl = true;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UinputKeyboard keyboard {fake_fd};
      return keyboard.submit({.key_code = 0x41, .pressed = true});
    }

    Status linux_uinput_keyboard_type_text_fake_success() {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.override_ioctl = true;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UinputKeyboard keyboard {fake_fd};
      return keyboard.type_text({.text = "A"});
    }

    Status linux_uinput_keyboard_close_fake_close_failure() {
      LinuxTestSyscalls syscalls;
      syscalls.override_ioctl = true;
      syscalls.override_close = true;
      syscalls.close_result = -1;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UinputKeyboard keyboard {fake_fd};
      return keyboard.close();
    }

    Status linux_uinput_mouse_create_fake_ioctl_failure(int fail_ioctl_call) {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.override_ioctl = true;
      syscalls.fail_ioctl_call = fail_ioctl_call;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      CreateMouseOptions options;
      options.profile = profiles::mouse();

      UinputMouse mouse {fake_fd};
      return mouse.create(1, options);
    }

    Status linux_uinput_mouse_submit_fake_write_failure(const MouseEvent &event) {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.fail_write_call = 1;
      syscalls.override_ioctl = true;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UinputMouse mouse {fake_fd};
      return mouse.submit(event);
    }

    Status linux_uinput_mouse_submit_fake_short_write(const MouseEvent &event) {
      LinuxTestSyscalls syscalls;
      syscalls.override_write = true;
      syscalls.short_write_call = 1;
      syscalls.override_ioctl = true;
      syscalls.override_close = true;
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      UinputMouse mouse {fake_fd};
      return mouse.submit(event);
    }

  }  // namespace test
#endif

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<LinuxUhidBackend>();
  }

}  // namespace lvh::detail
