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

namespace lvh::detail {
  namespace {

    constexpr auto uhid_path = "/dev/uhid";
    constexpr auto uinput_path = "/dev/uinput";
    constexpr auto absolute_axis_max = 65535;
    constexpr auto poll_timeout_ms = 100;

    int system_access(const char *path, int mode) {
      return ::access(path, mode);
    }

    int system_open(const char *path, int flags) {
      return ::open(path, flags);
    }

    int system_close(int fd) {
      return ::close(fd);
    }

    std::ptrdiff_t system_write(int fd, const void *buffer, std::size_t size) {
      return static_cast<std::ptrdiff_t>(::write(fd, buffer, size));
    }

    int system_ioctl(int fd, unsigned long request, unsigned long argument = 0) {
      return ::ioctl(fd, request, argument);
    }

    int system_poll(pollfd *descriptors, nfds_t descriptor_count, int timeout) {
      return ::poll(descriptors, descriptor_count, timeout);
    }

    std::ptrdiff_t system_read(int fd, void *buffer, std::size_t size) {
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

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<LinuxUhidBackend>();
  }

}  // namespace lvh::detail
