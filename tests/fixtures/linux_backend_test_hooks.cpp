/**
 * @file tests/fixtures/linux_backend_test_hooks.cpp
 * @brief Linux backend test hook definitions.
 */

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// platform includes
#if defined(__linux__)
  #include <fcntl.h>
  #ifndef __user
    #define __user
  #endif
  #include <linux/input.h>
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
#endif

// lib includes
#if defined(__linux__)
  #include <libevdev/libevdev-uinput.h>
  #include <libevdev/libevdev.h>
#endif

// local includes
#include "fixtures/linux_backend_test_hooks.hpp"

#if defined(__linux__)
namespace lvh::detail::test {
  namespace {

    struct FakeLibevdevDevice {
      std::string name;
      std::uint16_t bustype = 0;
      std::uint16_t vendor = 0;
      std::uint16_t product = 0;
      std::uint16_t version = 0;
      std::vector<std::uint32_t> event_types;
      std::vector<LinuxLibevdevEventCode> event_codes;
      std::vector<std::uint32_t> properties;
    };

    struct FakeLibevdevUinput {
      FakeLibevdevDevice device;
    };

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
      bool override_xtest_query = false;
      bool xtest_query_result = true;
      bool override_x_keycode = false;
      std::atomic_int x_keycode_call_count = 0;
      int fail_x_keycode_call = -1;
      bool override_libevdev = false;
      bool libevdev_new_returns_null = false;
      bool fail_libevdev_event_type = false;
      bool fail_libevdev_event_code = false;
      bool fail_libevdev_property = false;
      bool fail_libevdev_create = false;
      std::vector<FakeLibevdevDevice> libevdev_devices;
      std::size_t libevdev_destroy_count = 0;
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

  }  // namespace
}  // namespace lvh::detail::test

int lvh_linux_test_access(const char *path, int mode) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_access) {
    if (lvh::detail::test::active_test_syscalls->access_result < 0) {
      errno = EACCES;
    }
    return lvh::detail::test::active_test_syscalls->access_result;
  }
  return ::access(path, mode);
}

int lvh_linux_test_open(const char *path, int flags) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_open) {
    if (lvh::detail::test::active_test_syscalls->open_result < 0) {
      errno = ENOENT;
      return lvh::detail::test::active_test_syscalls->open_result;
    }
    const auto fd = ::open("/dev/null", O_RDWR);
    if (fd < 0) {
      errno = EIO;
    }
    return fd;
  }
  return ::open(path, flags);
}

std::ptrdiff_t lvh_linux_test_write(int fd, const void *buffer, std::size_t size) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_write) {
    const auto call_count = ++lvh::detail::test::active_test_syscalls->write_call_count;
    if (lvh::detail::test::active_test_syscalls->fail_write_call == call_count) {
      errno = EIO;
      return -1;
    }
    if (lvh::detail::test::active_test_syscalls->short_write_call == call_count) {
      return static_cast<std::ptrdiff_t>(lvh::detail::test::active_test_syscalls->short_write_size);
    }
    return static_cast<std::ptrdiff_t>(size);
  }
  return static_cast<std::ptrdiff_t>(::write(fd, buffer, size));
}

int lvh_linux_test_ioctl(int fd, unsigned long request, unsigned long argument) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_ioctl) {
    const auto call_count = ++lvh::detail::test::active_test_syscalls->ioctl_call_count;
    if (lvh::detail::test::active_test_syscalls->fail_ioctl_call == call_count) {
      errno = EINVAL;
      return -1;
    }
    return 0;
  }
  return ::ioctl(fd, request, argument);
}

int lvh_linux_test_poll(pollfd *descriptors, nfds_t descriptor_count, int timeout) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_poll) {
    const auto call_index = static_cast<std::size_t>(lvh::detail::test::active_test_syscalls->poll_call_count++);
    auto result = 0;
    if (call_index < lvh::detail::test::active_test_syscalls->poll_results.size()) {
      result = lvh::detail::test::active_test_syscalls->poll_results[call_index];
    }

    if (descriptor_count > 0) {
      descriptors[0].revents = 0;
      if (result > 0 && call_index < lvh::detail::test::active_test_syscalls->poll_revents.size()) {
        descriptors[0].revents = lvh::detail::test::active_test_syscalls->poll_revents[call_index];
      }
    }

    if (result < 0) {
      errno = call_index < lvh::detail::test::active_test_syscalls->poll_errors.size() ?
                lvh::detail::test::active_test_syscalls->poll_errors[call_index] :
                EIO;
    }
    return result;
  }
  return ::poll(descriptors, descriptor_count, timeout);
}

std::ptrdiff_t lvh_linux_test_read(int fd, void *buffer, std::size_t size) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_read) {
    const auto call_index = static_cast<std::size_t>(lvh::detail::test::active_test_syscalls->read_call_count++);
    auto result = std::ptrdiff_t {0};
    if (call_index < lvh::detail::test::active_test_syscalls->read_results.size()) {
      result = lvh::detail::test::active_test_syscalls->read_results[call_index];
    }

    if (result < 0) {
      errno = call_index < lvh::detail::test::active_test_syscalls->read_errors.size() ?
                lvh::detail::test::active_test_syscalls->read_errors[call_index] :
                EIO;
      return result;
    }

    if (result > 0) {
      const auto bytes = std::min<std::size_t>(static_cast<std::size_t>(result), std::min(size, sizeof(uhid_event)));
      std::memcpy(buffer, &lvh::detail::test::active_test_syscalls->read_event, bytes);
      return static_cast<std::ptrdiff_t>(bytes);
    }

    return result;
  }
  return static_cast<std::ptrdiff_t>(::read(fd, buffer, size));
}

  #if defined(LIBVIRTUALHID_HAVE_XTEST)
Display *lvh_linux_test_x_open_display(const char *) {
  return reinterpret_cast<Display *>(0x1);
}

int lvh_linux_test_x_close_display(Display *) {
  return 0;
}

Bool lvh_linux_test_xtest_query_extension(Display *, int *, int *, int *, int *) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_xtest_query) {
    return lvh::detail::test::active_test_syscalls->xtest_query_result ? True : False;
  }
  return True;
}

KeyCode lvh_linux_test_x_keysym_to_keycode(Display *, KeySym keysym) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_x_keycode) {
    const auto call_count = ++lvh::detail::test::active_test_syscalls->x_keycode_call_count;
    if (lvh::detail::test::active_test_syscalls->fail_x_keycode_call == call_count) {
      return 0;
    }
  }
  return keysym == NoSymbol ? 0 : 1;
}

int lvh_linux_test_x_flush(Display *) {
  return 0;
}

int lvh_linux_test_default_screen(Display *) {
  return 0;
}

int lvh_linux_test_display_width(Display *, int) {
  return 1920;
}

int lvh_linux_test_display_height(Display *, int) {
  return 1080;
}

int lvh_linux_test_xtest_fake_key_event(Display *, unsigned int, Bool, unsigned long) {
  return 1;
}

int lvh_linux_test_xtest_fake_button_event(Display *, unsigned int, Bool, unsigned long) {
  return 1;
}

int lvh_linux_test_xtest_fake_motion_event(Display *, int, int, int, unsigned long) {
  return 1;
}

int lvh_linux_test_xtest_fake_relative_motion_event(Display *, int, int, unsigned long) {
  return 1;
}
  #endif

extern "C" libevdev *lvh_linux_test_libevdev_new() {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    if (lvh::detail::test::active_test_syscalls->libevdev_new_returns_null) {
      return nullptr;
    }
    return reinterpret_cast<libevdev *>(new lvh::detail::test::FakeLibevdevDevice);
  }
  return ::libevdev_new();
}

extern "C" void lvh_linux_test_libevdev_free(libevdev *device) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    delete reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device);
    return;
  }
  ::libevdev_free(device);
}

extern "C" void lvh_linux_test_libevdev_set_name(libevdev *device, const char *name) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->name = name == nullptr ? "" : name;
    return;
  }
  ::libevdev_set_name(device, name);
}

extern "C" void lvh_linux_test_libevdev_set_id_bustype(libevdev *device, int bustype) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->bustype = static_cast<std::uint16_t>(bustype);
    return;
  }
  ::libevdev_set_id_bustype(device, bustype);
}

extern "C" void lvh_linux_test_libevdev_set_id_vendor(libevdev *device, int vendor) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->vendor = static_cast<std::uint16_t>(vendor);
    return;
  }
  ::libevdev_set_id_vendor(device, vendor);
}

extern "C" void lvh_linux_test_libevdev_set_id_product(libevdev *device, int product) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->product = static_cast<std::uint16_t>(product);
    return;
  }
  ::libevdev_set_id_product(device, product);
}

extern "C" void lvh_linux_test_libevdev_set_id_version(libevdev *device, int version) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->version = static_cast<std::uint16_t>(version);
    return;
  }
  ::libevdev_set_id_version(device, version);
}

extern "C" int lvh_linux_test_libevdev_enable_event_type(libevdev *device, unsigned int type) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    if (lvh::detail::test::active_test_syscalls->fail_libevdev_event_type) {
      return -EIO;
    }
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->event_types.push_back(type);
    return 0;
  }
  return ::libevdev_enable_event_type(device, type);
}

extern "C" int lvh_linux_test_libevdev_enable_event_code(
  libevdev *device,
  unsigned int type,
  unsigned int code,
  const void *data
) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    if (lvh::detail::test::active_test_syscalls->fail_libevdev_event_code) {
      return -EIO;
    }

    lvh::detail::test::LinuxLibevdevEventCode event_code {
      .type = type,
      .code = code,
    };
    if (type == EV_ABS && data != nullptr) {
      const auto *absinfo = static_cast<const input_absinfo *>(data);
      event_code.has_absinfo = true;
      event_code.minimum = absinfo->minimum;
      event_code.maximum = absinfo->maximum;
      event_code.fuzz = absinfo->fuzz;
      event_code.flat = absinfo->flat;
      event_code.resolution = absinfo->resolution;
    }
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->event_codes.push_back(event_code);
    return 0;
  }
  return ::libevdev_enable_event_code(device, type, code, data);
}

extern "C" int lvh_linux_test_libevdev_enable_property(libevdev *device, unsigned int property) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    if (lvh::detail::test::active_test_syscalls->fail_libevdev_property) {
      return -EIO;
    }
    reinterpret_cast<lvh::detail::test::FakeLibevdevDevice *>(device)->properties.push_back(property);
    return 0;
  }
  return ::libevdev_enable_property(device, property);
}

extern "C" int lvh_linux_test_libevdev_uinput_create_from_device(
  const libevdev *device,
  int uinput_fd,
  libevdev_uinput **uinput_device
) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    if (lvh::detail::test::active_test_syscalls->fail_libevdev_create || uinput_fd < 0) {
      return -EIO;
    }

    const auto &fake_device = *reinterpret_cast<const lvh::detail::test::FakeLibevdevDevice *>(device);
    lvh::detail::test::active_test_syscalls->libevdev_devices.push_back(fake_device);
    *uinput_device = reinterpret_cast<libevdev_uinput *>(new lvh::detail::test::FakeLibevdevUinput {fake_device});
    return 0;
  }
  return ::libevdev_uinput_create_from_device(device, uinput_fd, uinput_device);
}

extern "C" void lvh_linux_test_libevdev_uinput_destroy(libevdev_uinput *uinput_device) {
  if (lvh::detail::test::active_test_syscalls != nullptr && lvh::detail::test::active_test_syscalls->override_libevdev) {
    ++lvh::detail::test::active_test_syscalls->libevdev_destroy_count;
    delete reinterpret_cast<lvh::detail::test::FakeLibevdevUinput *>(uinput_device);
    return;
  }
  ::libevdev_uinput_destroy(uinput_device);
}

  #define access lvh_linux_test_access
  #define ioctl lvh_linux_test_ioctl
  #define open lvh_linux_test_open
  #define poll lvh_linux_test_poll
  #define read lvh_linux_test_read
  #define write lvh_linux_test_write
  #define libevdev_enable_event_code lvh_linux_test_libevdev_enable_event_code
  #define libevdev_enable_event_type lvh_linux_test_libevdev_enable_event_type
  #define libevdev_enable_property lvh_linux_test_libevdev_enable_property
  #define libevdev_free lvh_linux_test_libevdev_free
  #define libevdev_new lvh_linux_test_libevdev_new
  #define libevdev_set_id_bustype lvh_linux_test_libevdev_set_id_bustype
  #define libevdev_set_id_product lvh_linux_test_libevdev_set_id_product
  #define libevdev_set_id_vendor lvh_linux_test_libevdev_set_id_vendor
  #define libevdev_set_id_version lvh_linux_test_libevdev_set_id_version
  #define libevdev_set_name lvh_linux_test_libevdev_set_name
  #define libevdev_uinput_create_from_device lvh_linux_test_libevdev_uinput_create_from_device
  #define libevdev_uinput_destroy lvh_linux_test_libevdev_uinput_destroy

  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    #if defined(DefaultScreen)
      #undef DefaultScreen
    #endif
    #if defined(DisplayHeight)
      #undef DisplayHeight
    #endif
    #if defined(DisplayWidth)
      #undef DisplayWidth
    #endif

    #define DefaultScreen lvh_linux_test_default_screen
    #define DisplayHeight lvh_linux_test_display_height
    #define DisplayWidth lvh_linux_test_display_width
    #define XCloseDisplay lvh_linux_test_x_close_display
    #define XFlush lvh_linux_test_x_flush
    #define XKeysymToKeycode lvh_linux_test_x_keysym_to_keycode
    #define XOpenDisplay lvh_linux_test_x_open_display
    #define XTestFakeButtonEvent lvh_linux_test_xtest_fake_button_event
    #define XTestFakeKeyEvent lvh_linux_test_xtest_fake_key_event
    #define XTestFakeMotionEvent lvh_linux_test_xtest_fake_motion_event
    #define XTestFakeRelativeMotionEvent lvh_linux_test_xtest_fake_relative_motion_event
    #define XTestQueryExtension lvh_linux_test_xtest_query_extension
  #endif

  #define create_platform_backend create_platform_backend_for_linux_backend_test_hooks
  #include "../../src/platform/linux/uhid_backend.cpp"
  #undef create_platform_backend

  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    #undef XTestQueryExtension
    #undef XTestFakeRelativeMotionEvent
    #undef XTestFakeMotionEvent
    #undef XTestFakeKeyEvent
    #undef XTestFakeButtonEvent
    #undef XOpenDisplay
    #undef XKeysymToKeycode
    #undef XFlush
    #undef XCloseDisplay
    #undef DisplayWidth
    #undef DisplayHeight
    #undef DefaultScreen
  #endif

  #undef write
  #undef read
  #undef poll
  #undef open
  #undef ioctl
  #undef access
  #undef libevdev_uinput_destroy
  #undef libevdev_uinput_create_from_device
  #undef libevdev_set_name
  #undef libevdev_set_id_version
  #undef libevdev_set_id_vendor
  #undef libevdev_set_id_product
  #undef libevdev_set_id_bustype
  #undef libevdev_new
  #undef libevdev_free
  #undef libevdev_enable_property
  #undef libevdev_enable_event_type
  #undef libevdev_enable_event_code

namespace lvh::detail::test {
  namespace {

    constexpr auto fake_fd = 100000;

    int open_test_fd() {
      return ::open("/dev/null", O_RDWR);
    }

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

    bool read_uhid_event_type(int fd, unsigned int event_type, uhid_event &event) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {1};
      while (std::chrono::steady_clock::now() < deadline) {
        if (!read_uhid_event(fd, event)) {
          return false;
        }
        if (event.type == event_type) {
          return true;
        }
      }
      return false;
    }

    std::uint32_t read_u32_le(const std::uint8_t *buffer) {
      return static_cast<std::uint32_t>(buffer[0]) |
             (static_cast<std::uint32_t>(buffer[1]) << 8U) |
             (static_cast<std::uint32_t>(buffer[2]) << 16U) |
             (static_cast<std::uint32_t>(buffer[3]) << 24U);
    }

    void enable_fake_device_syscalls(LinuxTestSyscalls &syscalls) {
      syscalls.override_access = true;
      syscalls.override_open = true;
      syscalls.override_write = true;
      syscalls.override_ioctl = true;
      syscalls.override_libevdev = true;
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

    OperationStatus run_fake_uhid_read_loop(LinuxTestSyscalls &syscalls, int expected_poll_calls) {
      syscalls.override_write = true;

      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      CreateGamepadOptions options;
      options.profile = profiles::generic_gamepad();

      const auto fd = open_test_fd();
      if (fd < 0) {
        return system_error_status(ErrorCode::backend_failure, "failed to open test file descriptor", errno);
      }

      UhidGamepad gamepad {fd};
      if (const auto status = gamepad.create(1, options); !status.ok()) {
        return status;
      }

      const auto saw_expected_polls = wait_for_poll_calls(syscalls, expected_poll_calls);
      const auto close_status = gamepad.close();
      if (!saw_expected_polls) {
        return OperationStatus::failure(ErrorCode::backend_failure, "fake UHID read loop did not consume the scripted poll calls");
      }
      return close_status;
    }

    DeviceProfile profile_for_uinput_device_type(DeviceType device_type) {
      switch (device_type) {
        case DeviceType::keyboard:
          return profiles::keyboard();
        case DeviceType::mouse:
          return profiles::mouse();
        case DeviceType::touchscreen:
          return profiles::touchscreen();
        case DeviceType::trackpad:
          return profiles::trackpad();
        case DeviceType::pen_tablet:
          return profiles::pen_tablet();
        case DeviceType::gamepad:
          return profiles::generic_gamepad();
      }

      return profiles::mouse();
    }

    OperationStatus create_uinput_device_by_type(int fd, DeviceType device_type) {
      switch (device_type) {
        case DeviceType::keyboard:
          {
            CreateKeyboardOptions options;
            options.profile = profile_for_uinput_device_type(device_type);
            UinputKeyboard keyboard {fd};
            return keyboard.create(1, options);
          }
        case DeviceType::mouse:
          {
            CreateMouseOptions options;
            options.profile = profile_for_uinput_device_type(device_type);
            UinputMouse mouse {fd};
            return mouse.create(1, options);
          }
        case DeviceType::touchscreen:
          {
            CreateTouchscreenOptions options;
            options.profile = profile_for_uinput_device_type(device_type);
            UinputTouchscreen touchscreen {fd};
            return touchscreen.create(1, options);
          }
        case DeviceType::trackpad:
          {
            CreateTrackpadOptions options;
            options.profile = profile_for_uinput_device_type(device_type);
            UinputTrackpad trackpad {fd};
            return trackpad.create(1, options);
          }
        case DeviceType::pen_tablet:
          {
            CreatePenTabletOptions options;
            options.profile = profile_for_uinput_device_type(device_type);
            UinputPenTablet pen_tablet {fd};
            return pen_tablet.create(1, options);
          }
        case DeviceType::gamepad:
          {
            auto status = create_libevdev_uinput_device(fd, profile_for_uinput_device_type(device_type), 1).status;
            static_cast<void>(system_close(fd));
            return status;
          }
      }

      return OperationStatus::failure(ErrorCode::unsupported_profile, "unsupported fake uinput device type");
    }

    LinuxLibevdevCreationResult create_fake_libevdev_device(
      DeviceType device_type,
      void (*configure_failure)(LinuxTestSyscalls &) = nullptr
    ) {
      LinuxTestSyscalls syscalls;
      syscalls.override_libevdev = true;
      if (configure_failure != nullptr) {
        configure_failure(syscalls);
      }
      ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

      LinuxLibevdevCreationResult result;
      const auto fd = open_test_fd();
      if (fd < 0) {
        result.status = system_error_status(ErrorCode::backend_failure, "failed to open test file descriptor", errno);
        return result;
      }

      result.status = create_uinput_device_by_type(fd, device_type);
      if (!syscalls.libevdev_devices.empty()) {
        const auto &device = syscalls.libevdev_devices.back();
        result.name = device.name;
        result.bustype = device.bustype;
        result.vendor = device.vendor;
        result.product = device.product;
        result.version = device.version;
        result.event_types = device.event_types;
        result.event_codes = device.event_codes;
        result.properties = device.properties;
      }
      result.destroy_count = syscalls.libevdev_destroy_count;
      if (result.status.ok()) {
        result.close_status = OperationStatus::success();
      }
      return result;
    }

  }  // namespace

  std::string linux_copy_string_char_buffer(const std::string &source) {
    std::array<char, 5> destination {};
    copy_string(destination, source);
    return destination.data();
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

  int linux_pen_tool(PenToolType tool) {
    return pen_tool_to_linux(tool);
  }

  int linux_pen_button(PenButton button) {
    return pen_button_to_linux(button);
  }

  std::string linux_dualsense_mac_address(const std::string &stable_id, DeviceId id) {
    return format_mac_address(parse_mac_address(stable_id).value_or(generated_mac_address(id)));
  }

  bool linux_first_line_matches(const std::string &path, const std::string &expected) {
    const auto line = read_first_line(path);
    return line && *line == expected;
  }

  bool linux_first_line_missing(const std::string &path) {
    return !read_first_line(path);
  }

  bool linux_hidraw_name_matches(const std::string &path, const std::string &name) {
    return hidraw_name_matches(path, name);
  }

  std::vector<DeviceNode> linux_discover_nodes_by_name(const std::string &name) {
    return discover_input_nodes_by_name(name);
  }

  std::vector<DeviceNode> linux_discover_nodes_by_name(
    const std::string &name,
    const std::string &input_root,
    const std::string &hidraw_root
  ) {
    return discover_input_nodes_by_name(name, input_root, hidraw_root);
  }

  std::size_t linux_empty_device_nodes_count() {
    UhidGamepad gamepad {-1};
    UinputKeyboard keyboard {-1};
    UinputMouse mouse {-1};
    UinputTouchscreen touchscreen {-1};
    UinputTrackpad trackpad {-1};
    UinputPenTablet pen_tablet {-1};

    return gamepad.device_nodes().size() + keyboard.device_nodes().size() + mouse.device_nodes().size() +
           touchscreen.device_nodes().size() + trackpad.device_nodes().size() + pen_tablet.device_nodes().size();
  }

  std::size_t linux_uhid_descriptor_limit() {
    uhid_event event {};
    return sizeof(event.u.create2.rd_data);
  }

  std::size_t linux_uhid_input_limit() {
    uhid_event event {};
    return sizeof(event.u.input2.data);
  }

  OperationStatus linux_uhid_create_with_descriptor_size(std::size_t descriptor_size) {
    auto profile = profiles::generic_gamepad();
    profile.report_descriptor.assign(descriptor_size, 0);

    CreateGamepadOptions options;
    options.profile = std::move(profile);

    UhidGamepad gamepad {-1};
    return gamepad.create(1, options);
  }

  OperationStatus linux_uhid_submit_report_size(std::size_t report_size) {
    UhidGamepad gamepad {-1};
    return gamepad.submit(std::vector<std::uint8_t>(report_size, 0));
  }

  OperationStatus linux_uhid_submit_after_close() {
    UhidGamepad gamepad {-1};
    static_cast<void>(gamepad.close());
    return gamepad.submit({0});
  }

  OperationStatus linux_uinput_keyboard_create_invalid_fd() {
    CreateKeyboardOptions options;
    options.profile = profiles::keyboard();

    UinputKeyboard keyboard {-1};
    return keyboard.create(1, options);
  }

  OperationStatus linux_uinput_keyboard_submit_invalid_fd(const KeyboardEvent &event) {
    UinputKeyboard keyboard {-1};
    return keyboard.submit(event);
  }

  OperationStatus linux_uinput_keyboard_type_text_invalid_fd(const std::string &text) {
    UinputKeyboard keyboard {-1};
    return keyboard.type_text({.text = text});
  }

  OperationStatus linux_uinput_keyboard_submit_after_close() {
    UinputKeyboard keyboard {-1};
    static_cast<void>(keyboard.close());
    return keyboard.submit({.key_code = 0x41, .pressed = true});
  }

  LinuxInputSubmissionResult linux_uinput_keyboard_submit_pipe(const KeyboardEvent &event) {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
    }

    UinputKeyboard keyboard {descriptors[1]};
    auto status = keyboard.submit(event);
    static_cast<void>(keyboard.close());
    auto records = read_input_events_until_eof(descriptors[0]);
    static_cast<void>(::close(descriptors[0]));
    return {std::move(status), std::move(records)};
  }

  OperationStatus linux_uinput_user_device_invalid_fd() {
    return create_libevdev_uinput_device(-1, profiles::mouse(), 1).status;
  }

  OperationStatus linux_uinput_user_device_pipe() {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno);
    }

    auto status = create_libevdev_uinput_device(descriptors[1], profiles::mouse(), 1).status;
    static_cast<void>(::close(descriptors[0]));
    static_cast<void>(::close(descriptors[1]));
    return status;
  }

  OperationStatus linux_uinput_mouse_create_invalid_fd() {
    CreateMouseOptions options;
    options.profile = profiles::mouse();

    UinputMouse mouse {-1};
    return mouse.create(1, options);
  }

  OperationStatus linux_uinput_mouse_submit_invalid_fd(const MouseEvent &event) {
    UinputMouse mouse {-1};
    return mouse.submit(event);
  }

  OperationStatus linux_uinput_mouse_submit_after_close() {
    UinputMouse mouse {-1};
    static_cast<void>(mouse.close());
    return mouse.submit({.kind = MouseEventKind::relative_motion, .x = 1, .y = 1});
  }

  LinuxInputSubmissionResult linux_uinput_mouse_submit_pipe(const MouseEvent &event) {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
    }

    UinputMouse mouse {descriptors[1]};
    auto status = mouse.submit(event);
    static_cast<void>(mouse.close());
    auto records = read_input_events_until_eof(descriptors[0]);
    static_cast<void>(::close(descriptors[0]));
    return {std::move(status), std::move(records)};
  }

  LinuxInputSubmissionResult linux_uinput_touchscreen_contact_pipe(const TouchContact &contact) {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
    }

    UinputTouchscreen touchscreen {descriptors[1]};
    auto status = touchscreen.place_contact(contact);
    if (status.ok()) {
      status = touchscreen.release_contact(contact.id);
    }
    static_cast<void>(touchscreen.close());
    auto records = read_input_events_until_eof(descriptors[0]);
    static_cast<void>(::close(descriptors[0]));
    return {std::move(status), std::move(records)};
  }

  LinuxInputSubmissionResult linux_uinput_trackpad_contact_pipe(const TouchContact &contact) {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
    }

    UinputTrackpad trackpad {descriptors[1]};
    auto status = trackpad.place_contact(contact);
    if (status.ok()) {
      status = trackpad.button(true);
    }
    if (status.ok()) {
      status = trackpad.button(false);
    }
    if (status.ok()) {
      status = trackpad.release_contact(contact.id);
    }
    static_cast<void>(trackpad.close());
    auto records = read_input_events_until_eof(descriptors[0]);
    static_cast<void>(::close(descriptors[0]));
    return {std::move(status), std::move(records)};
  }

  LinuxInputSubmissionResult linux_uinput_pen_tablet_tool_pipe(const PenToolState &state) {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
    }

    UinputPenTablet pen_tablet {descriptors[1]};
    auto status = pen_tablet.place_tool(state);
    if (status.ok()) {
      status = pen_tablet.button(PenButton::primary, true);
    }
    if (status.ok()) {
      status = pen_tablet.button(PenButton::primary, false);
    }
    static_cast<void>(pen_tablet.close());
    auto records = read_input_events_until_eof(descriptors[0]);
    static_cast<void>(::close(descriptors[0]));
    return {std::move(status), std::move(records)};
  }

  LinuxInputSubmissionResult linux_uinput_trackpad_multi_contact_pipe() {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
    }

    UinputTrackpad trackpad {descriptors[1]};
    auto status = OperationStatus::success();
    for (auto id = 0; id < 5 && status.ok(); ++id) {
      status = trackpad.place_contact({
        .id = id,
        .x = static_cast<float>(id) / 5.0F,
        .y = 0.5F,
        .pressure = 0.5F,
        .orientation = id * 10,
      });
    }
    for (auto id = 4; id >= 0 && status.ok(); --id) {
      status = trackpad.release_contact(id);
    }
    static_cast<void>(trackpad.close());
    auto records = read_input_events_until_eof(descriptors[0]);
    static_cast<void>(::close(descriptors[0]));
    return {std::move(status), std::move(records)};
  }

  OperationStatus linux_uinput_touchscreen_invalid_contacts() {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno);
    }

    UinputTouchscreen touchscreen {descriptors[1]};
    static_cast<void>(touchscreen.place_contact({.id = -1}));

    auto status = OperationStatus::success();
    for (auto id = 0; id <= touch_max_contacts && status.ok(); ++id) {
      status = touchscreen.place_contact({
        .id = id,
        .x = 0.5F,
        .y = 0.5F,
        .pressure = 0.5F,
        .orientation = 0,
      });
    }

    static_cast<void>(touchscreen.close());
    static_cast<void>(read_input_events_until_eof(descriptors[0]));
    static_cast<void>(::close(descriptors[0]));
    return status;
  }

  LinuxInputSubmissionResult linux_uinput_pen_tablet_transition_pipe() {
    std::array<int, 2> descriptors {-1, -1};
    if (::pipe(descriptors.data()) != 0) {
      return {system_error_status(ErrorCode::backend_failure, "failed to create pipe", errno), {}};
    }

    UinputPenTablet pen_tablet {descriptors[1]};
    auto status = OperationStatus::success();
    using enum PenToolType;
    constexpr std::array tools {
      pen,
      eraser,
      brush,
      pencil,
      airbrush,
      touch,
      unchanged,
    };
    for (const auto tool : tools) {
      if (!status.ok()) {
        break;
      }
      status = pen_tablet.place_tool({
        .tool = tool,
        .x = 0.25F,
        .y = 0.75F,
        .pressure = tool == eraser ? -1.0F : 0.25F,
        .distance = tool == eraser ? 0.5F : -1.0F,
        .tilt_x = 120.0F,
        .tilt_y = -120.0F,
      });
    }
    if (status.ok()) {
      status = pen_tablet.button(PenButton::secondary, true);
    }
    if (status.ok()) {
      status = pen_tablet.button(PenButton::secondary, false);
    }
    if (status.ok()) {
      status = pen_tablet.button(PenButton::tertiary, true);
    }
    if (status.ok()) {
      status = pen_tablet.button(PenButton::tertiary, false);
    }
    static_cast<void>(pen_tablet.close());
    auto records = read_input_events_until_eof(descriptors[0]);
    static_cast<void>(::close(descriptors[0]));
    return {std::move(status), std::move(records)};
  }

  OperationStatus linux_uinput_pen_tablet_closed_status() {
    UinputPenTablet pen_tablet {-1};
    static_cast<void>(pen_tablet.close());
    static_cast<void>(pen_tablet.place_tool({}));
    return pen_tablet.button(PenButton::primary, true);
  }

  LinuxUhidRoundTripResult linux_uhid_socketpair_roundtrip() {
    LinuxUhidRoundTripResult result;
    std::array<int, 2> descriptors {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors.data()) != 0) {
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
      result.saw_create =
        event.type == UHID_CREATE2 && event.u.create2.vendor == profile.vendor_id && event.u.create2.product == profile.product_id;
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

  LinuxUhidRoundTripResult linux_dualsense_uhid_socketpair_reports() {
    LinuxUhidRoundTripResult result;
    std::array<int, 2> descriptors {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors.data()) != 0) {
      result.create_status = system_error_status(ErrorCode::backend_failure, "failed to create socketpair", errno);
      result.submit_status = result.create_status;
      result.close_status = result.create_status;
      return result;
    }

    CreateGamepadOptions options;
    options.profile = profiles::dualsense_usb();
    options.metadata.stable_id = "02:03:04:05:06:07";

    UhidGamepad gamepad {descriptors[0]};
    result.create_status = gamepad.create(8, options);

    uhid_event event {};
    if (read_uhid_event_type(descriptors[1], UHID_CREATE2, event)) {
      result.saw_create = event.u.create2.vendor == options.profile.vendor_id &&
                          event.u.create2.product == options.profile.product_id;
    }

    event = {};
    event.type = UHID_GET_REPORT;
    event.u.get_report.id = 11;
    event.u.get_report.rnum = 0x05;
    static_cast<void>(write_uhid_event(descriptors[1], event));
    if (read_uhid_event_type(descriptors[1], UHID_GET_REPORT_REPLY, event)) {
      result.saw_dualsense_calibration = event.u.get_report_reply.err == 0 && event.u.get_report_reply.size > 0 &&
                                         event.u.get_report_reply.data[0] == 0x05;
    }

    event = {};
    event.type = UHID_GET_REPORT;
    event.u.get_report.id = 12;
    event.u.get_report.rnum = 0x09;
    static_cast<void>(write_uhid_event(descriptors[1], event));
    if (read_uhid_event_type(descriptors[1], UHID_GET_REPORT_REPLY, event)) {
      result.saw_dualsense_pairing = event.u.get_report_reply.err == 0 && event.u.get_report_reply.size > 7 &&
                                     event.u.get_report_reply.data[0] == 0x09 &&
                                     event.u.get_report_reply.data[1] == 0x07 &&
                                     event.u.get_report_reply.data[6] == 0x02;
    }

    event = {};
    event.type = UHID_GET_REPORT;
    event.u.get_report.id = 13;
    event.u.get_report.rnum = 0x20;
    static_cast<void>(write_uhid_event(descriptors[1], event));
    if (read_uhid_event_type(descriptors[1], UHID_GET_REPORT_REPLY, event)) {
      result.saw_dualsense_firmware = event.u.get_report_reply.err == 0 && event.u.get_report_reply.size > 0 &&
                                      event.u.get_report_reply.data[0] == 0x20;
    }

    event = {};
    event.type = UHID_GET_REPORT;
    event.u.get_report.id = 15;
    event.u.get_report.rnum = 0x7F;
    static_cast<void>(write_uhid_event(descriptors[1], event));
    static_cast<void>(read_uhid_event_type(descriptors[1], UHID_GET_REPORT_REPLY, event));

    result.close_status = gamepad.close();
    static_cast<void>(::close(descriptors[1]));
    result.submit_status = OperationStatus::success();
    return result;
  }

  LinuxUhidRoundTripResult linux_dualsense_bluetooth_uhid_socketpair_reports() {
    LinuxUhidRoundTripResult result;
    std::array<int, 2> descriptors {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors.data()) != 0) {
      result.create_status = system_error_status(ErrorCode::backend_failure, "failed to create socketpair", errno);
      result.submit_status = result.create_status;
      result.close_status = result.create_status;
      return result;
    }

    CreateGamepadOptions options;
    options.profile = profiles::dualsense_bluetooth();
    options.metadata.stable_id = "02:03:04:05:06:07";

    UhidGamepad gamepad {descriptors[0]};
    result.create_status = gamepad.create(9, options);

    uhid_event event {};
    if (read_uhid_event_type(descriptors[1], UHID_CREATE2, event)) {
      result.saw_create = event.u.create2.vendor == options.profile.vendor_id &&
                          event.u.create2.product == options.profile.product_id &&
                          event.u.create2.bus == BUS_BLUETOOTH;
    }

    if (read_uhid_event_type(descriptors[1], UHID_INPUT2, event)) {
      const auto report_size = static_cast<std::size_t>(event.u.input2.size);
      if (report_size == options.profile.input_report_size && event.u.input2.data[0] == 0x31) {
        const auto crc_offset = report_size - 4U;
        const auto expected_crc = crc32(event.u.input2.data, crc_offset, dualsense_crc_seed(0xA1));
        const auto actual_crc = read_u32_le(event.u.input2.data + crc_offset);
        result.saw_dualsense_bluetooth_input = expected_crc == actual_crc;
      }
    }

    event = {};
    event.type = UHID_GET_REPORT;
    event.u.get_report.id = 14;
    event.u.get_report.rnum = 0x09;
    static_cast<void>(write_uhid_event(descriptors[1], event));
    if (read_uhid_event_type(descriptors[1], UHID_GET_REPORT_REPLY, event)) {
      const auto report_size = static_cast<std::size_t>(event.u.get_report_reply.size);
      result.saw_dualsense_pairing = event.u.get_report_reply.err == 0 && report_size > 7U &&
                                     event.u.get_report_reply.data[0] == 0x09 &&
                                     event.u.get_report_reply.data[1] == 0x07 &&
                                     event.u.get_report_reply.data[6] == 0x02;
      if (report_size >= 4U) {
        const auto crc_offset = report_size - 4U;
        const auto expected_crc = crc32(
          event.u.get_report_reply.data,
          crc_offset,
          dualsense_crc_seed(dualsense_feature_crc_seed)
        );
        const auto actual_crc = read_u32_le(event.u.get_report_reply.data + crc_offset);
        result.saw_dualsense_feature_crc = expected_crc == actual_crc;
      }
    }

    result.close_status = gamepad.close();
    static_cast<void>(::close(descriptors[1]));
    result.submit_status = OperationStatus::success();
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

    CreateTouchscreenOptions touchscreen_options;
    touchscreen_options.profile = profiles::touchscreen();
    touchscreen_options.stable_id = "fake-linux-touchscreen";
    auto touchscreen = backend.create_touchscreen(4, touchscreen_options);
    result.touchscreen_status = touchscreen.status;
    if (touchscreen) {
      result.touchscreen_close_status = touchscreen.touchscreen->close();
    }

    CreateTrackpadOptions trackpad_options;
    trackpad_options.profile = profiles::trackpad();
    trackpad_options.stable_id = "fake-linux-trackpad";
    auto trackpad = backend.create_trackpad(5, trackpad_options);
    result.trackpad_status = trackpad.status;
    if (trackpad) {
      result.trackpad_close_status = trackpad.trackpad->close();
    }

    CreatePenTabletOptions pen_tablet_options;
    pen_tablet_options.profile = profiles::pen_tablet();
    pen_tablet_options.stable_id = "fake-linux-pen-tablet";
    auto pen_tablet = backend.create_pen_tablet(6, pen_tablet_options);
    result.pen_tablet_status = pen_tablet.status;
    if (pen_tablet) {
      result.pen_tablet_close_status = pen_tablet.pen_tablet->close();
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

  OperationStatus linux_backend_gamepad_fake_open_failure() {
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

  OperationStatus linux_backend_gamepad_fake_create_failure() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_write_call = 1;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateGamepadOptions options;
    options.profile = profiles::xbox_360();
    return backend.create_gamepad(1, options).status;
  }

  OperationStatus linux_backend_keyboard_fake_open_failure() {
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

  OperationStatus linux_backend_keyboard_fake_create_failure() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateKeyboardOptions options;
    options.profile = profiles::keyboard();
    return backend.create_keyboard(1, options).status;
  }

  OperationStatus linux_backend_keyboard_fake_fallback_success() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateKeyboardOptions options;
    options.profile = profiles::keyboard();
    auto keyboard = backend.create_keyboard(1, options);
    if (!keyboard) {
      return keyboard.status;
    }
    return keyboard.keyboard->close();
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_backend_mouse_fake_open_failure() {
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

  OperationStatus linux_backend_mouse_fake_create_failure() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateMouseOptions options;
    options.profile = profiles::mouse();
    return backend.create_mouse(1, options).status;
  }

  OperationStatus linux_backend_mouse_fake_fallback_success() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateMouseOptions options;
    options.profile = profiles::mouse();
    auto mouse = backend.create_mouse(1, options);
    if (!mouse) {
      return mouse.status;
    }
    return mouse.mouse->close();
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_backend_keyboard_fake_create_failure_without_fallback() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    syscalls.override_xtest_query = true;
    syscalls.xtest_query_result = false;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateKeyboardOptions options;
    options.profile = profiles::keyboard();
    return backend.create_keyboard(1, options).status;
  }

  OperationStatus linux_backend_mouse_fake_create_failure_without_fallback() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    syscalls.override_xtest_query = true;
    syscalls.xtest_query_result = false;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateMouseOptions options;
    options.profile = profiles::mouse();
    return backend.create_mouse(1, options).status;
  }

  OperationStatus linux_backend_touchscreen_fake_open_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_access = true;
    syscalls.override_open = true;
    syscalls.open_result = -1;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateTouchscreenOptions options;
    options.profile = profiles::touchscreen();
    return backend.create_touchscreen(1, options).status;
  }

  OperationStatus linux_backend_touchscreen_fake_create_failure() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateTouchscreenOptions options;
    options.profile = profiles::touchscreen();
    return backend.create_touchscreen(1, options).status;
  }

  OperationStatus linux_backend_trackpad_fake_open_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_access = true;
    syscalls.override_open = true;
    syscalls.open_result = -1;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateTrackpadOptions options;
    options.profile = profiles::trackpad();
    return backend.create_trackpad(1, options).status;
  }

  OperationStatus linux_backend_trackpad_fake_create_failure() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreateTrackpadOptions options;
    options.profile = profiles::trackpad();
    return backend.create_trackpad(1, options).status;
  }

  OperationStatus linux_backend_pen_tablet_fake_open_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_access = true;
    syscalls.override_open = true;
    syscalls.open_result = -1;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreatePenTabletOptions options;
    options.profile = profiles::pen_tablet();
    return backend.create_pen_tablet(1, options).status;
  }

  OperationStatus linux_backend_pen_tablet_fake_create_failure() {
    LinuxTestSyscalls syscalls;
    enable_fake_device_syscalls(syscalls);
    syscalls.fail_libevdev_create = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    LinuxUhidBackend backend;

    CreatePenTabletOptions options;
    options.profile = profiles::pen_tablet();
    return backend.create_pen_tablet(1, options).status;
  }

  OperationStatus linux_uhid_submit_fake_write_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.fail_write_call = 1;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UhidGamepad gamepad {fake_fd};
    return gamepad.submit({0});
  }

  OperationStatus linux_uhid_submit_fake_short_write() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.short_write_call = 1;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UhidGamepad gamepad {fake_fd};
    return gamepad.submit({0});
  }

  OperationStatus linux_uhid_close_fake_write_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.fail_write_call = 1;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UhidGamepad gamepad {fake_fd};
    return gamepad.close();
  }

  OperationStatus linux_uhid_close_fake_close_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UhidGamepad gamepad {fake_fd};
    return gamepad.close();
  }

  OperationStatus linux_uhid_read_loop_fake_retry_branches() {
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

  OperationStatus linux_uhid_read_loop_fake_poll_errors() {
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

  OperationStatus linux_uhid_read_loop_fake_read_error() {
    LinuxTestSyscalls syscalls;
    syscalls.override_poll = true;
    syscalls.poll_results = {1};
    syscalls.poll_revents = {POLLIN};
    syscalls.override_read = true;
    syscalls.read_results = {-1};
    syscalls.read_errors = {EIO};
    return run_fake_uhid_read_loop(syscalls, 1);
  }

  OperationStatus linux_uhid_read_loop_fake_output_without_callback() {
    LinuxTestSyscalls syscalls;
    syscalls.override_poll = true;
    syscalls.poll_results = {1, 1};
    syscalls.poll_revents = {POLLIN, POLLIN};
    syscalls.override_read = true;
    syscalls.read_results = {static_cast<std::ptrdiff_t>(sizeof(uhid_event)), 0};
    syscalls.read_event.type = UHID_OUTPUT;
    return run_fake_uhid_read_loop(syscalls, 2);
  }

  LinuxLibevdevCreationResult linux_uinput_create_fake_libevdev_device(DeviceType device_type) {
    return create_fake_libevdev_device(device_type);
  }

  OperationStatus linux_uinput_create_fake_libevdev_allocation_failure(DeviceType device_type) {
    return create_fake_libevdev_device(device_type, [](LinuxTestSyscalls &syscalls) {
             syscalls.libevdev_new_returns_null = true;
           })
      .status;
  }

  OperationStatus linux_uinput_create_fake_libevdev_event_type_failure(DeviceType device_type) {
    return create_fake_libevdev_device(device_type, [](LinuxTestSyscalls &syscalls) {
             syscalls.fail_libevdev_event_type = true;
           })
      .status;
  }

  OperationStatus linux_uinput_create_fake_libevdev_event_code_failure(DeviceType device_type) {
    return create_fake_libevdev_device(device_type, [](LinuxTestSyscalls &syscalls) {
             syscalls.fail_libevdev_event_code = true;
           })
      .status;
  }

  OperationStatus linux_uinput_create_fake_libevdev_property_failure(DeviceType device_type) {
    return create_fake_libevdev_device(device_type, [](LinuxTestSyscalls &syscalls) {
             syscalls.fail_libevdev_property = true;
           })
      .status;
  }

  OperationStatus linux_uinput_create_fake_libevdev_create_failure(DeviceType device_type) {
    return create_fake_libevdev_device(device_type, [](LinuxTestSyscalls &syscalls) {
             syscalls.fail_libevdev_create = true;
           })
      .status;
  }

  OperationStatus linux_uinput_keyboard_submit_fake_write_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.fail_write_call = 1;
    syscalls.override_ioctl = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UinputKeyboard keyboard {fake_fd};
    return keyboard.submit({.key_code = 0x41, .pressed = true});
  }

  OperationStatus linux_uinput_keyboard_submit_fake_short_write() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.short_write_call = 1;
    syscalls.override_ioctl = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UinputKeyboard keyboard {fake_fd};
    return keyboard.submit({.key_code = 0x41, .pressed = true});
  }

  OperationStatus linux_uinput_keyboard_type_text_fake_success() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.override_ioctl = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UinputKeyboard keyboard {fake_fd};
    return keyboard.type_text({.text = "A"});
  }

  OperationStatus linux_uinput_keyboard_type_text_fake_write_failure(int fail_write_call) {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.fail_write_call = fail_write_call;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UinputKeyboard keyboard {fake_fd};
    return keyboard.type_text({.text = "A"});
  }

  OperationStatus linux_uinput_keyboard_close_fake_close_failure() {
    LinuxTestSyscalls syscalls;
    syscalls.override_ioctl = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UinputKeyboard keyboard {fake_fd};
    return keyboard.close();
  }

  OperationStatus linux_uinput_keyboard_auto_repeat_fake_success() {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.override_ioctl = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    CreateKeyboardOptions options;
    options.profile = profiles::keyboard();
    options.auto_repeat_interval_ms = 1;

    const auto fd = open_test_fd();
    if (fd < 0) {
      return system_error_status(ErrorCode::backend_failure, "failed to open test file descriptor", errno);
    }

    UinputKeyboard keyboard {fd};
    auto status = keyboard.create(1, options);
    if (status.ok()) {
      status = keyboard.submit({.key_code = 0x41, .pressed = true});
    }
    std::this_thread::sleep_for(std::chrono::milliseconds {10});
    return keyboard.close();
  }

  OperationStatus linux_uinput_mouse_submit_fake_write_failure(const MouseEvent &event) {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.fail_write_call = 1;
    syscalls.override_ioctl = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UinputMouse mouse {fake_fd};
    return mouse.submit(event);
  }

  OperationStatus linux_uinput_mouse_submit_fake_short_write(const MouseEvent &event) {
    LinuxTestSyscalls syscalls;
    syscalls.override_write = true;
    syscalls.short_write_call = 1;
    syscalls.override_ioctl = true;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    UinputMouse mouse {fake_fd};
    return mouse.submit(event);
  }

  OperationStatus linux_xtest_keyboard_submit_success() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    XTestKeyboard keyboard;
    if (const auto status = keyboard.create(); !status.ok()) {
      return status;
    }
    return keyboard.submit({.key_code = 0x41, .pressed = true});
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_keyboard_submit_invalid() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    XTestKeyboard keyboard;
    if (const auto status = keyboard.create(); !status.ok()) {
      return status;
    }
    return keyboard.submit({.key_code = 0x2C, .pressed = true});
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_keyboard_submit_closed() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    XTestKeyboard keyboard;
    if (const auto status = keyboard.create(); !status.ok()) {
      return status;
    }
    static_cast<void>(keyboard.close());
    return keyboard.submit({.key_code = 0x41, .pressed = true});
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_keyboard_type_text_success() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    XTestKeyboard keyboard;
    if (const auto status = keyboard.create(); !status.ok()) {
      return status;
    }
    return keyboard.type_text({.text = "A"});
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_keyboard_type_text_fake_keycode_failure(int fail_keycode_call) {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    LinuxTestSyscalls syscalls;
    syscalls.override_x_keycode = true;
    syscalls.fail_x_keycode_call = fail_keycode_call;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    XTestKeyboard keyboard;
    if (const auto status = keyboard.create(); !status.ok()) {
      return status;
    }
    return keyboard.type_text({.text = "A"});
  #else
    static_cast<void>(fail_keycode_call);
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_keyboard_create_query_failure() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    LinuxTestSyscalls syscalls;
    syscalls.override_xtest_query = true;
    syscalls.xtest_query_result = false;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    XTestKeyboard keyboard;
    return keyboard.create();
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_mouse_submit_success() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    XTestMouse mouse;
    if (const auto status = mouse.create(); !status.ok()) {
      return status;
    }

    using enum MouseEventKind;
    if (const auto status = mouse.submit({.kind = relative_motion, .x = 1, .y = -1}); !status.ok()) {
      return status;
    }
    if (const auto status = mouse.submit({.kind = absolute_motion, .x = 1, .y = 1, .width = 2, .height = 2}); !status.ok()) {
      return status;
    }
    if (const auto status = mouse.submit({.kind = button, .button = MouseButton::extra, .pressed = true}); !status.ok()) {
      return status;
    }
    if (const auto status = mouse.submit({.kind = vertical_scroll, .high_resolution_scroll = 120}); !status.ok()) {
      return status;
    }
    return mouse.submit({.kind = horizontal_scroll, .high_resolution_scroll = -120});
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_mouse_submit_closed() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    XTestMouse mouse;
    if (const auto status = mouse.create(); !status.ok()) {
      return status;
    }
    static_cast<void>(mouse.close());
    return mouse.submit({.kind = MouseEventKind::relative_motion, .x = 1, .y = 1});
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  OperationStatus linux_xtest_mouse_create_query_failure() {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    LinuxTestSyscalls syscalls;
    syscalls.override_xtest_query = true;
    syscalls.xtest_query_result = false;
    ScopedLinuxTestSyscalls scoped_syscalls {syscalls};

    XTestMouse mouse;
    return mouse.create();
  #else
    return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest fallback is not enabled");
  #endif
  }

  unsigned long linux_xtest_keysym(KeyboardKeyCode key_code) {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    return key_code_to_keysym(key_code);
  #else
    static_cast<void>(key_code);
    return 0;
  #endif
  }

  int linux_xtest_mouse_button(MouseButton button) {
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    return mouse_button_to_xtest(button);
  #else
    static_cast<void>(button);
    return 1;
  #endif
  }

}  // namespace lvh::detail::test
#endif
