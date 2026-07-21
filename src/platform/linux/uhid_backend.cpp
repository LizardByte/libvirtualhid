/**
 * @file src/platform/linux/uhid_backend.cpp
 * @brief Linux UHID backend definitions.
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
#include <format>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// platform includes
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

// lib includes
#include <libevdev/libevdev-uinput.h>
#include <libevdev/libevdev.h>

// local includes
#include "core/backend.hpp"
#include "shared/playstation_feature_reports.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

namespace lvh::detail {
  namespace {  // NOSONAR(cpp:S1000): Linux backend internals need internal linkage; tests include this file with syscall overrides.

    constexpr auto uhid_path = "/dev/uhid";
    constexpr auto uinput_path = "/dev/uinput";
    constexpr auto absolute_axis_max = 65535;
    constexpr auto touch_axis_max_x = 19200;
    constexpr auto touch_axis_max_y = 10800;
    constexpr auto touch_max_contacts = 16;
    constexpr auto touch_pressure_max = 253;
    constexpr auto tablet_pressure_max = 4096;
    constexpr auto tablet_distance_max = 1024;
    constexpr auto tablet_resolution = 28;
    constexpr auto poll_timeout_ms = 100;
    constexpr auto uinput_feedback_startup_delay = std::chrono::milliseconds {100};
    constexpr auto xbox_trigger_max = 255;
    // The Bluetooth bus selects the sparse evdev mapping that matches the
    // button capabilities exposed by these Xbox uinput devices.
    constexpr auto xbox_sparse_uinput_bus = BUS_BLUETOOTH;
    constexpr std::uint16_t xbox_wireless_uinput_product_id = 0x0B20;
    constexpr std::uint16_t xbox_series_uinput_product_id = 0x0B13;
    namespace ps = playstation_feature_reports;
    constexpr auto playstation_periodic_report_ms = 10;

    int system_access(const char *path, int mode) {
      return ::access(path, mode);
    }

    int system_open(const char *path, int flags) {
      return ::open(path, flags);
    }

    int system_close(int fd) {
      return ::close(fd);
    }

    std::ptrdiff_t system_write(int fd, std::span<const std::byte> buffer) {
      return static_cast<std::ptrdiff_t>(::write(fd, buffer.data(), buffer.size()));
    }

    int system_ioctl(int fd, unsigned long request, unsigned long argument = 0) {
      return ::ioctl(fd, request, argument);
    }

    int system_poll(pollfd *descriptors, nfds_t descriptor_count, int timeout) {
      return ::poll(descriptors, descriptor_count, timeout);
    }

    std::ptrdiff_t system_read(int fd, std::span<std::byte> buffer) {
      return static_cast<std::ptrdiff_t>(::read(fd, buffer.data(), buffer.size()));
    }

    std::string errno_message(int error) {
      return std::error_code(error, std::generic_category()).message();
    }

    OperationStatus system_error_status(ErrorCode code, const std::string &operation, int error) {
      return OperationStatus::failure(code, operation + ": " + errno_message(error));
    }

    bool can_access_uhid() {
      return system_access(uhid_path, R_OK | W_OK) == 0;
    }

    bool can_access_uinput() {
      return system_access(uinput_path, R_OK | W_OK) == 0;
    }

    std::array<std::uint8_t, 6> generated_mac_address(DeviceId id) {
      return {
        0x02,
        0x00,
        static_cast<std::uint8_t>((id >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((id >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((id >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(id & 0xFFU),
      };
    }

    std::optional<std::array<std::uint8_t, 6>> parse_mac_address(const std::string &text) {
      std::array<std::uint8_t, 6> mac {};
      std::istringstream stream {text};
      for (std::size_t index = 0; index < mac.size(); ++index) {
        unsigned int value = 0;
        stream >> std::hex >> value;
        if (!stream || value > 0xFFU) {
          return std::nullopt;
        }
        mac[index] = static_cast<std::uint8_t>(value);
        if (index + 1U < mac.size()) {
          char separator = 0;
          stream >> separator;
          if (separator != ':') {
            return std::nullopt;
          }
        }
      }
      return mac;
    }

    std::string format_mac_address(const std::array<std::uint8_t, 6> &mac) {
      std::ostringstream stream;
      stream << std::hex << std::setfill('0');
      for (std::size_t index = 0; index < mac.size(); ++index) {
        if (index != 0) {
          stream << ':';
        }
        stream << std::setw(2) << static_cast<unsigned int>(mac[index]);
      }
      return stream.str();
    }

    std::uint32_t crc32(std::span<const std::uint8_t> buffer, std::uint32_t seed = 0) {
      auto crc = seed ^ 0xFFFFFFFFU;
      for (const auto byte : buffer) {
        crc ^= byte;
        for (auto bit = 0; bit < 8; ++bit) {
          const auto mask = 0U - (crc & 1U);
          crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
      }
      return crc ^ 0xFFFFFFFFU;
    }

    std::uint32_t playstation_crc_seed(std::uint8_t seed) {
      return crc32(std::span {&seed, 1U});
    }

    void write_u32_le(std::uint8_t *buffer, std::uint32_t value) {
      buffer[0] = static_cast<std::uint8_t>(value & 0xFFU);
      buffer[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
      buffer[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
      buffer[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }

    bool is_playstation_profile(GamepadProfileKind kind) {
      return kind == GamepadProfileKind::dualshock4 || kind == GamepadProfileKind::dualsense;
    }

    bool uses_uinput_gamepad_profile(GamepadProfileKind kind) {
      switch (kind) {
        using enum GamepadProfileKind;

        case generic:
        case xbox_360:
        case xbox_one:
        case xbox_series:
        case switch_pro:
          return true;
        case dualshock4:
        case dualsense:
          return false;
      }

      return false;
    }

    std::optional<int> uinput_misc1_button(GamepadProfileKind kind) {
      switch (kind) {
        using enum GamepadProfileKind;

        case generic:
        case xbox_series:
          return KEY_RECORD;
        case switch_pro:
          return BTN_Z;
        case xbox_360:
        case xbox_one:
        case dualshock4:
        case dualsense:
          return std::nullopt;
      }

      return std::nullopt;
    }

    bool uses_sparse_uinput_button_slots(GamepadProfileKind kind) {
      using enum GamepadProfileKind;

      return kind == xbox_360 || kind == xbox_one || kind == xbox_series;
    }

    std::uint16_t to_uhid_bus(BusType bus_type) {
      if (bus_type == BusType::bluetooth) {
        return BUS_BLUETOOTH;
      }
      return BUS_USB;
    }

    std::uint16_t to_uhid_bus(const DeviceProfile &profile) {
      if (profile.gamepad_kind == GamepadProfileKind::switch_pro) {
        return BUS_VIRTUAL;
      }
      return to_uhid_bus(profile.bus_type);
    }

    std::uint16_t to_uinput_bus(BusType bus_type) {
      return to_uhid_bus(bus_type);
    }

    template<std::size_t Size>
    void copy_string(__u8 (&destination)[Size], std::string_view source) {
      const auto length = std::min(source.size(), Size - 1);
      std::memcpy(destination, source.data(), length);
      destination[length] = 0;
    }

    template<std::size_t Size>
    void copy_string(char (&destination)[Size], std::string_view source) {
      const auto length = std::min(source.size(), Size - 1);
      std::memcpy(destination, source.data(), length);
      destination[length] = 0;
    }

    template<std::size_t Size>
    void copy_string(std::array<char, Size> &destination, std::string_view source) {
      const auto length = std::min(source.size(), Size - 1);
      std::memcpy(destination.data(), source.data(), length);
      destination[length] = 0;
    }

    std::optional<std::string> read_first_line(const std::filesystem::path &path) {
      std::ifstream file {path};
      if (!file) {
        return std::nullopt;
      }

      std::string line;
      std::getline(file, line);
      return line;
    }

    void append_node(std::vector<DeviceNode> &nodes, DeviceNodeKind kind, const std::filesystem::path &path) {
      nodes.push_back({.kind = kind, .path = path.string()});
    }

    void append_node_if_missing(std::vector<DeviceNode> &nodes, DeviceNodeKind kind, const std::filesystem::path &path) {
      const auto path_string = path.string();
      const auto existing = std::ranges::find_if(nodes, [kind, &path_string](const DeviceNode &node) {
        return node.kind == kind && node.path == path_string;
      });
      if (existing == nodes.end()) {
        nodes.push_back({.kind = kind, .path = path_string});
      }
    }

    bool hidraw_name_matches(const std::filesystem::path &uevent_path, std::string_view name) {
      std::ifstream file {uevent_path};
      if (!file) {
        return false;
      }

      std::string line;
      while (std::getline(file, line)) {
        constexpr std::string_view key {"HID_NAME="};
        if (line.starts_with(key)) {
          return line.size() == key.size() + name.size() && line.ends_with(name);
        }
      }

      return false;
    }

    bool hidraw_metadata_matches(
      const std::filesystem::path &uevent_path,
      std::string_view name,
      std::string_view physical_id,
      std::string_view unique_id
    ) {
      std::ifstream file {uevent_path};
      if (!file) {
        return false;
      }

      auto actual_name = std::optional<std::string> {};
      auto actual_physical_id = std::optional<std::string> {};
      auto actual_unique_id = std::optional<std::string> {};
      std::string line;
      while (std::getline(file, line)) {
        if (constexpr std::string_view name_key {"HID_NAME="}; line.starts_with(name_key)) {
          actual_name = line.substr(name_key.size());
          continue;
        }
        if (constexpr std::string_view phys_key {"HID_PHYS="}; line.starts_with(phys_key)) {
          actual_physical_id = line.substr(phys_key.size());
          continue;
        }
        if (constexpr std::string_view uniq_key {"HID_UNIQ="}; line.starts_with(uniq_key)) {
          actual_unique_id = line.substr(uniq_key.size());
        }
      }

      auto matched_stable_metadata = false;
      if (!physical_id.empty() && actual_physical_id.has_value()) {
        matched_stable_metadata = true;
        if (*actual_physical_id != physical_id) {
          return false;
        }
      }
      if (!unique_id.empty() && actual_unique_id.has_value()) {
        matched_stable_metadata = true;
        if (*actual_unique_id != unique_id) {
          return false;
        }
      }
      if (matched_stable_metadata) {
        return true;
      }
      return !name.empty() && actual_name.has_value() && *actual_name == name;
    }

    std::vector<DeviceNode> discover_hidraw_nodes_by_metadata(
      std::string_view name,
      std::string_view physical_id,
      std::string_view unique_id,
      const std::filesystem::path &hidraw_root = "/sys/class/hidraw"
    ) {
      using enum DeviceNodeKind;

      std::vector<DeviceNode> nodes;
      std::error_code error;
      if (!std::filesystem::exists(hidraw_root, error)) {
        return nodes;
      }

      for (std::filesystem::directory_iterator it {hidraw_root, error}, end; !error && it != end; it.increment(error)) {
        if (!hidraw_metadata_matches(it->path() / "device" / "uevent", name, physical_id, unique_id)) {
          continue;
        }

        append_node(nodes, hidraw, std::filesystem::path {"/dev"} / it->path().filename());
        append_node(nodes, sysfs, it->path());
      }

      return nodes;
    }

    std::vector<DeviceNode> discover_input_nodes_by_name(
      const std::string &name,
      const std::filesystem::path &input_root,
      const std::filesystem::path &hidraw_root
    ) {
      using enum DeviceNodeKind;

      std::vector<DeviceNode> nodes;
      if (name.empty()) {
        return nodes;
      }

      std::error_code error;
      if (std::filesystem::exists(input_root, error)) {
        for (std::filesystem::directory_iterator it {input_root, error}, end; !error && it != end; it.increment(error)) {
          const auto filename = it->path().filename().string();
          const auto is_event_node = filename.starts_with("event");
          if (const auto is_joystick_node = filename.starts_with("js"); !is_event_node && !is_joystick_node) {
            continue;
          }

          if (const auto sysfs_name = read_first_line(it->path() / "device" / "name"); !sysfs_name || *sysfs_name != name) {
            continue;
          }

          append_node(
            nodes,
            is_event_node ? input_event : joystick,
            std::filesystem::path {"/dev/input"} / it->path().filename()
          );
          append_node(nodes, sysfs, it->path());
        }
      }

      if (std::filesystem::exists(hidraw_root, error)) {
        for (std::filesystem::directory_iterator it {hidraw_root, error}, end; !error && it != end; it.increment(error)) {
          if (!hidraw_name_matches(it->path() / "device" / "uevent", name)) {
            continue;
          }

          append_node(nodes, hidraw, std::filesystem::path {"/dev"} / it->path().filename());
          append_node(nodes, sysfs, it->path());
        }
      }

      return nodes;
    }

    std::vector<DeviceNode> discover_input_nodes_by_name(const std::string &name) {
      return discover_input_nodes_by_name(name, "/sys/class/input", "/sys/class/hidraw");
    }

    OperationStatus ioctl_status(const std::string &operation) {
      return system_error_status(ErrorCode::backend_failure, operation, errno);
    }

    template<typename Target, std::size_t Size>
    std::optional<Target> mapped_keyboard_code(
      KeyboardKeyCode key_code,
      const std::array<std::pair<KeyboardKeyCode, Target>, Size> &mappings
    ) {
      const auto it = std::ranges::find_if(mappings, [key_code](const auto &mapping) {
        return mapping.first == key_code;
      });
      if (it == mappings.end()) {
        return std::nullopt;
      }
      return it->second;
    }

    int key_code_to_linux(KeyboardKeyCode key_code) {
      static constexpr std::array<std::pair<KeyboardKeyCode, int>, 47> special_keys {{
        {0x08, KEY_BACKSPACE},
        {0x09, KEY_TAB},
        {0x0D, KEY_ENTER},
        {0x10, KEY_LEFTSHIFT},
        {0xA0, KEY_LEFTSHIFT},
        {0x11, KEY_LEFTCTRL},
        {0xA2, KEY_LEFTCTRL},
        {0x12, KEY_LEFTALT},
        {0xA4, KEY_LEFTALT},
        {0x14, KEY_CAPSLOCK},
        {0x1B, KEY_ESC},
        {0x20, KEY_SPACE},
        {0x21, KEY_PAGEUP},
        {0x22, KEY_PAGEDOWN},
        {0x23, KEY_END},
        {0x24, KEY_HOME},
        {0x25, KEY_LEFT},
        {0x26, KEY_UP},
        {0x27, KEY_RIGHT},
        {0x28, KEY_DOWN},
        {0x2C, KEY_SYSRQ},
        {0x2D, KEY_INSERT},
        {0x2E, KEY_DELETE},
        {0x5B, KEY_LEFTMETA},
        {0x5C, KEY_RIGHTMETA},
        {0x6A, KEY_KPASTERISK},
        {0x6B, KEY_KPPLUS},
        {0x6D, KEY_KPMINUS},
        {0x6E, KEY_KPDOT},
        {0x6F, KEY_KPSLASH},
        {0x90, KEY_NUMLOCK},
        {0x91, KEY_SCROLLLOCK},
        {0xA1, KEY_RIGHTSHIFT},
        {0xA3, KEY_RIGHTCTRL},
        {0xA5, KEY_RIGHTALT},
        {0xBA, KEY_SEMICOLON},
        {0xBB, KEY_EQUAL},
        {0xBC, KEY_COMMA},
        {0xBD, KEY_MINUS},
        {0xBE, KEY_DOT},
        {0xBF, KEY_SLASH},
        {0xC0, KEY_GRAVE},
        {0xDB, KEY_LEFTBRACE},
        {0xDC, KEY_BACKSLASH},
        {0xDD, KEY_RIGHTBRACE},
        {0xDE, KEY_APOSTROPHE},
        {0xE2, KEY_102ND},
      }};

      if (const auto linux_key = mapped_keyboard_code(key_code, special_keys); linux_key.has_value()) {
        return linux_key.value();
      }

      if (key_code >= 0x30 && key_code <= 0x39) {
        static constexpr std::array digit_keys {
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
        static constexpr std::array letter_keys {
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
        static constexpr std::array keypad_digit_keys {
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
      if (key_code >= 0x70 && key_code <= 0x87) {
        static constexpr std::array function_keys {
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
        using enum MouseButton;

        case left:
          return BTN_LEFT;
        case middle:
          return BTN_MIDDLE;
        case right:
          return BTN_RIGHT;
        case side:
          return BTN_SIDE;
        case extra:
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

    int scale_normalized_axis(float value, int maximum) {
      return static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * static_cast<float>(maximum)));
    }

    int clamp_degrees(std::int32_t value) {
      return std::clamp(value, -90, 90);
    }

    int tablet_tilt_units(float degrees) {
      const auto radians = std::clamp(degrees, -90.0F, 90.0F) * static_cast<float>(std::numbers::pi) / 180.0F;
      return static_cast<int>(std::lround(radians * tablet_resolution));
    }

    std::vector<std::uint32_t> decode_utf8(std::string_view text) {
      std::vector<std::uint32_t> codepoints;
      const auto bytes = std::as_bytes(std::span {text.data(), text.size()});
      for (std::size_t i = 0; i < text.size();) {
        const auto first = bytes[i];
        std::uint32_t codepoint = 0;
        std::size_t length = 0;

        if (const auto first_value = std::to_integer<std::uint32_t>(first); first_value <= 0x7FU) {
          codepoint = first_value;
          length = 1;
        } else if ((first & std::byte {0xE0}) == std::byte {0xC0}) {
          codepoint = std::to_integer<std::uint32_t>(first & std::byte {0x1F});
          length = 2;
        } else if ((first & std::byte {0xF0}) == std::byte {0xE0}) {
          codepoint = std::to_integer<std::uint32_t>(first & std::byte {0x0F});
          length = 3;
        } else if ((first & std::byte {0xF8}) == std::byte {0xF0}) {
          codepoint = std::to_integer<std::uint32_t>(first & std::byte {0x07});
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
          const auto next = bytes[i + offset];
          if ((next & std::byte {0xC0}) != std::byte {0x80}) {
            valid = false;
            break;
          }
          codepoint = (codepoint << 6U) | std::to_integer<std::uint32_t>(next & std::byte {0x3F});
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
      return std::format("{:X}", codepoint);
    }

    KeyboardKeyCode hex_digit_key_code(char digit) {
      if (digit >= '0' && digit <= '9') {
        return static_cast<KeyboardKeyCode>(0x30 + (digit - '0'));
      }
      return static_cast<KeyboardKeyCode>(0x41 + (digit - 'A'));
    }

    template<std::size_t Count, class SubmitKeyEvent>
    OperationStatus submit_keyboard_events(const std::array<KeyboardEvent, Count> &events, SubmitKeyEvent &submit_key_event) {
      for (const auto &event : events) {
        if (const auto status = submit_key_event(event); !status.ok()) {
          return status;
        }
      }
      return OperationStatus::success();
    }

    template<class SubmitKeyEvent>
    OperationStatus type_text_with_unicode_hex(std::string_view text, SubmitKeyEvent submit_key_event) {
      static constexpr std::array<KeyboardEvent, 6> unicode_hex_prefix {{
        {.key_code = 0xA2, .pressed = true},
        {.key_code = 0xA0, .pressed = true},
        {.key_code = 0x55, .pressed = true},
        {.key_code = 0x55, .pressed = false},
        {.key_code = 0xA0, .pressed = false},
        {.key_code = 0xA2, .pressed = false},
      }};
      static constexpr std::array<KeyboardEvent, 2> unicode_hex_suffix {{
        {.key_code = 0x0D, .pressed = true},
        {.key_code = 0x0D, .pressed = false},
      }};

      for (const auto codepoint : decode_utf8(text)) {
        const auto hex = uppercase_hex(codepoint);

        if (const auto status = submit_keyboard_events(unicode_hex_prefix, submit_key_event); !status.ok()) {
          return status;
        }

        for (const auto digit : hex) {
          const auto key_code = hex_digit_key_code(digit);
          const std::array<KeyboardEvent, 2> digit_events {{
            {.key_code = key_code, .pressed = true},
            {.key_code = key_code, .pressed = false},
          }};
          if (const auto status = submit_keyboard_events(digit_events, submit_key_event); !status.ok()) {
            return status;
          }
        }

        if (const auto status = submit_keyboard_events(unicode_hex_suffix, submit_key_event); !status.ok()) {
          return status;
        }
      }

      return OperationStatus::success();
    }

    [[maybe_unused]] int legacy_scroll_steps(std::int32_t distance) {
      if (distance == 0) {
        return 0;
      }

      if (const auto steps = distance / 120; steps != 0) {
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
      OperationStatus create_uinput_device(const DeviceProfile &profile, DeviceId id);

      OperationStatus emit_event(std::uint16_t type, std::uint16_t code, std::int32_t value) {
        std::lock_guard lock {write_mutex_};
        return emit_event_locked(type, code, value);
      }

      OperationStatus sync() {
        return emit_event(EV_SYN, SYN_REPORT, 0);
      }

      OperationStatus close_uinput(const std::string &description) {
        if (!open_.exchange(false)) {
          return OperationStatus::success();
        }

        auto status = OperationStatus::success();
        if (fd_ >= 0) {
          if (uinput_device_ != nullptr) {
            libevdev_uinput_destroy(uinput_device_);
            uinput_device_ = nullptr;
          } else if (system_ioctl(fd_, UI_DEV_DESTROY) < 0) {
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
      OperationStatus emit_event_locked(std::uint16_t type, std::uint16_t code, std::int32_t value) {
        if (fd_ < 0) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput device is closed");
        }

        input_event event {};
        event.type = type;
        event.code = code;
        event.value = value;

        const auto event_buffer = std::as_bytes(std::span {&event, 1U});
        const auto result = system_write(fd_, event_buffer);
        if (result < 0) {
          return system_error_status(ErrorCode::backend_failure, "failed to write uinput event", errno);
        }
        if (static_cast<std::size_t>(result) != sizeof(event)) {
          return OperationStatus::failure(ErrorCode::backend_failure, "short write while sending uinput event");
        }

        return OperationStatus::success();
      }

      int fd_ = -1;
      libevdev_uinput *uinput_device_ = nullptr;
      std::atomic_bool open_ = true;
      std::mutex write_mutex_;
    };

    struct LibevdevDeviceDeleter {
      void operator()(libevdev *device) const {
        libevdev_free(device);
      }
    };

    using LibevdevDevice = std::unique_ptr<libevdev, LibevdevDeviceDeleter>;

    struct UinputCreationResult {
      OperationStatus status;
      libevdev_uinput *device = nullptr;
    };

    input_absinfo make_absinfo(
      int minimum,
      int maximum,
      int fuzz = 0,
      int flat = 0,
      int resolution = 0
    ) {
      input_absinfo info {};
      info.minimum = minimum;
      info.maximum = maximum;
      info.fuzz = fuzz;
      info.flat = flat;
      info.resolution = resolution;
      return info;
    }

    OperationStatus libevdev_status(int result, const std::string &operation) {
      if (result >= 0) {
        return OperationStatus::success();
      }
      return OperationStatus::failure(ErrorCode::backend_failure, operation + ": " + errno_message(-result));
    }

    OperationStatus enable_evdev_type(libevdev *device, unsigned int type, const std::string &description) {
      return libevdev_status(libevdev_enable_event_type(device, type), "failed to enable " + description);
    }

    OperationStatus enable_evdev_code(
      libevdev *device,
      unsigned int type,
      unsigned int code,
      const std::string &description,
      const input_absinfo *absinfo = nullptr
    ) {
      return libevdev_status(
        libevdev_enable_event_code(device, type, code, absinfo),
        std::format("failed to enable {} {}", description, code)
      );
    }

    OperationStatus enable_evdev_property(libevdev *device, unsigned int property, const std::string &description) {
      return libevdev_status(
        libevdev_enable_property(device, property),
        "failed to enable " + description + " property"
      );
    }

    OperationStatus configure_evdev_keyboard(libevdev *device) {
      if (const auto status = enable_evdev_type(device, EV_KEY, "keyboard key events"); !status.ok()) {
        return status;
      }

      for (auto code = 1; code < KEY_MAX; ++code) {
        if (const auto status = enable_evdev_code(device, EV_KEY, code, "keyboard key"); !status.ok()) {
          return status;
        }
      }

      return OperationStatus::success();
    }

    OperationStatus configure_evdev_mouse(libevdev *device) {
      if (const auto status = enable_evdev_type(device, EV_KEY, "mouse button events"); !status.ok()) {
        return status;
      }
      if (const auto status = enable_evdev_type(device, EV_REL, "relative mouse events"); !status.ok()) {
        return status;
      }
      if (const auto status = enable_evdev_type(device, EV_ABS, "absolute mouse events"); !status.ok()) {
        return status;
      }

      for (const auto button : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_SIDE, BTN_EXTRA}) {
        if (const auto status = enable_evdev_code(device, EV_KEY, button, "mouse button"); !status.ok()) {
          return status;
        }
      }

      for (const auto code : {REL_X, REL_Y}) {
        if (const auto status = enable_evdev_code(device, EV_REL, code, "relative mouse axis"); !status.ok()) {
          return status;
        }
      }

#if defined(REL_WHEEL_HI_RES)
      if (const auto status = enable_evdev_code(device, EV_REL, REL_WHEEL_HI_RES, "high-resolution vertical scroll"); !status.ok()) {
        return status;
      }
#else
      if (const auto status = enable_evdev_code(device, EV_REL, REL_WHEEL, "vertical scroll"); !status.ok()) {
        return status;
      }
#endif

#if defined(REL_HWHEEL_HI_RES)
      if (const auto status = enable_evdev_code(device, EV_REL, REL_HWHEEL_HI_RES, "high-resolution horizontal scroll"); !status.ok()) {
        return status;
      }
#else
      if (const auto status = enable_evdev_code(device, EV_REL, REL_HWHEEL, "horizontal scroll"); !status.ok()) {
        return status;
      }
#endif

      auto x = make_absinfo(0, absolute_axis_max);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_X, "absolute mouse axis", &x); !status.ok()) {
        return status;
      }
      auto y = make_absinfo(0, absolute_axis_max);
      return enable_evdev_code(device, EV_ABS, ABS_Y, "absolute mouse axis", &y);
    }

    OperationStatus configure_evdev_touch_axes(libevdev *device) {
      if (const auto status = enable_evdev_type(device, EV_KEY, "touch key events"); !status.ok()) {
        return status;
      }
      if (const auto status = enable_evdev_type(device, EV_ABS, "touch absolute events"); !status.ok()) {
        return status;
      }

      auto slot = make_absinfo(0, touch_max_contacts - 1);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_MT_SLOT, "touch absolute axis", &slot); !status.ok()) {
        return status;
      }
      auto x = make_absinfo(0, touch_axis_max_x);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_X, "touch absolute axis", &x); !status.ok()) {
        return status;
      }
      auto y = make_absinfo(0, touch_axis_max_y);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_Y, "touch absolute axis", &y); !status.ok()) {
        return status;
      }
      auto mt_x = make_absinfo(0, touch_axis_max_x);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_MT_POSITION_X, "touch absolute axis", &mt_x); !status.ok()) {
        return status;
      }
      auto mt_y = make_absinfo(0, touch_axis_max_y);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_MT_POSITION_Y, "touch absolute axis", &mt_y); !status.ok()) {
        return status;
      }
      auto tracking = make_absinfo(0, 65535);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_MT_TRACKING_ID, "touch absolute axis", &tracking); !status.ok()) {
        return status;
      }
      auto pressure = make_absinfo(0, touch_pressure_max);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_PRESSURE, "touch absolute axis", &pressure); !status.ok()) {
        return status;
      }
      auto mt_pressure = make_absinfo(0, touch_pressure_max);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_MT_PRESSURE, "touch absolute axis", &mt_pressure); !status.ok()) {
        return status;
      }
      auto orientation = make_absinfo(-90, 90);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_MT_ORIENTATION, "touch absolute axis", &orientation); !status.ok()) {
        return status;
      }

      return enable_evdev_code(device, EV_KEY, BTN_TOUCH, "touch button");
    }

    OperationStatus configure_evdev_touchscreen(libevdev *device) {
      if (const auto status = configure_evdev_touch_axes(device); !status.ok()) {
        return status;
      }
      return enable_evdev_property(device, INPUT_PROP_DIRECT, "direct touch");
    }

    OperationStatus configure_evdev_trackpad(libevdev *device) {
      if (const auto status = configure_evdev_touch_axes(device); !status.ok()) {
        return status;
      }

      for (const auto button : {BTN_LEFT, BTN_TOOL_FINGER, BTN_TOOL_DOUBLETAP, BTN_TOOL_TRIPLETAP, BTN_TOOL_QUADTAP, BTN_TOOL_QUINTTAP}) {
        if (const auto status = enable_evdev_code(device, EV_KEY, button, "trackpad button"); !status.ok()) {
          return status;
        }
      }

      if (const auto status = enable_evdev_property(device, INPUT_PROP_POINTER, "pointer"); !status.ok()) {
        return status;
      }
      return enable_evdev_property(device, INPUT_PROP_BUTTONPAD, "buttonpad");
    }

    OperationStatus configure_evdev_pen_tablet(libevdev *device) {
      if (const auto status = enable_evdev_type(device, EV_KEY, "pen tablet key events"); !status.ok()) {
        return status;
      }
      if (const auto status = enable_evdev_type(device, EV_ABS, "pen tablet absolute events"); !status.ok()) {
        return status;
      }

      std::vector<int> buttons {BTN_TOUCH, BTN_STYLUS, BTN_STYLUS2, BTN_TOOL_PEN, BTN_TOOL_RUBBER, BTN_TOOL_BRUSH, BTN_TOOL_PENCIL, BTN_TOOL_AIRBRUSH};
#if defined(BTN_STYLUS3)
      buttons.push_back(BTN_STYLUS3);
#endif
      for (const auto button : buttons) {
        if (const auto status = enable_evdev_code(device, EV_KEY, button, "pen tablet button"); !status.ok()) {
          return status;
        }
      }

      // libinput requires tablet coordinate and tilt axes to advertise resolution.
      auto x = make_absinfo(0, touch_axis_max_x, 1, 0, tablet_resolution);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_X, "pen tablet absolute axis", &x); !status.ok()) {
        return status;
      }
      auto y = make_absinfo(0, touch_axis_max_y, 1, 0, tablet_resolution);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_Y, "pen tablet absolute axis", &y); !status.ok()) {
        return status;
      }
      auto pressure = make_absinfo(0, tablet_pressure_max);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_PRESSURE, "pen tablet absolute axis", &pressure); !status.ok()) {
        return status;
      }
      auto distance = make_absinfo(0, tablet_distance_max);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_DISTANCE, "pen tablet absolute axis", &distance); !status.ok()) {
        return status;
      }
      auto tilt_x = make_absinfo(-90, 90, 0, 0, tablet_resolution);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_TILT_X, "pen tablet absolute axis", &tilt_x); !status.ok()) {
        return status;
      }
      auto tilt_y = make_absinfo(-90, 90, 0, 0, tablet_resolution);
      if (const auto status = enable_evdev_code(device, EV_ABS, ABS_TILT_Y, "pen tablet absolute axis", &tilt_y); !status.ok()) {
        return status;
      }

      if (const auto status = enable_evdev_property(device, INPUT_PROP_POINTER, "tablet pointer"); !status.ok()) {
        return status;
      }
      return enable_evdev_property(device, INPUT_PROP_DIRECT, "tablet direct");
    }

    OperationStatus configure_evdev_gamepad_event_types(libevdev *device, bool supports_rumble) {
      if (const auto status = enable_evdev_type(device, EV_KEY, "gamepad button events"); !status.ok()) {
        return status;
      }
      if (const auto status = enable_evdev_type(device, EV_ABS, "gamepad absolute events"); !status.ok()) {
        return status;
      }
      if (!supports_rumble) {
        return OperationStatus::success();
      }
      return enable_evdev_type(device, EV_FF, "gamepad force-feedback events");
    }

    OperationStatus configure_evdev_gamepad_buttons(libevdev *device, GamepadProfileKind profile_kind) {
      constexpr std::array active_buttons {
        BTN_SOUTH,
        BTN_EAST,
        BTN_NORTH,
        BTN_WEST,
        BTN_TL,
        BTN_TR,
        BTN_SELECT,
        BTN_START,
        BTN_MODE,
        BTN_THUMBL,
        BTN_THUMBR,
      };
      for (const auto button : active_buttons) {
        if (const auto status = enable_evdev_code(device, EV_KEY, button, "gamepad button"); !status.ok()) {
          return status;
        }
      }

      if (uses_sparse_uinput_button_slots(profile_kind)) {
        // Linux gamepad consumers use the sparse button sequence. These unused
        // capabilities preserve the active button indices.
        constexpr std::array reserved_buttons {BTN_C, BTN_Z, BTN_TL2, BTN_TR2};
        for (const auto button : reserved_buttons) {
          if (const auto status = enable_evdev_code(device, EV_KEY, button, "reserved gamepad button slot"); !status.ok()) {
            return status;
          }
        }
      }
      if (profile_kind == GamepadProfileKind::switch_pro) {
        for (const auto button : {BTN_TL2, BTN_TR2}) {
          if (const auto status = enable_evdev_code(device, EV_KEY, button, "gamepad trigger button"); !status.ok()) {
            return status;
          }
        }
      }
      if (const auto misc1_button = uinput_misc1_button(profile_kind); misc1_button.has_value()) {
        if (const auto status = enable_evdev_code(device, EV_KEY, *misc1_button, "gamepad auxiliary button"); !status.ok()) {
          return status;
        }
      }
      return OperationStatus::success();
    }

    OperationStatus configure_evdev_gamepad_axes(libevdev *device, GamepadProfileKind profile_kind) {
      auto dpad = make_absinfo(-1, 1);
      for (const auto code : {ABS_HAT0X, ABS_HAT0Y}) {
        if (const auto status = enable_evdev_code(device, EV_ABS, code, "gamepad directional axis", &dpad); !status.ok()) {
          return status;
        }
      }

      auto stick = make_absinfo(-32768, 32767, 16, 128);
      for (const auto code : {ABS_X, ABS_Y, ABS_RX, ABS_RY}) {
        if (const auto status = enable_evdev_code(device, EV_ABS, code, "gamepad stick axis", &stick); !status.ok()) {
          return status;
        }
      }

      if (profile_kind == GamepadProfileKind::switch_pro) {
        return OperationStatus::success();
      }

      auto trigger = make_absinfo(0, xbox_trigger_max);
      for (const auto code : {ABS_Z, ABS_RZ}) {
        if (const auto status = enable_evdev_code(device, EV_ABS, code, "gamepad trigger axis", &trigger); !status.ok()) {
          return status;
        }
      }
      return OperationStatus::success();
    }

    OperationStatus configure_evdev_gamepad_force_feedback(libevdev *device, bool supports_rumble) {
      if (!supports_rumble) {
        return OperationStatus::success();
      }
      constexpr std::array effect_codes {FF_RUMBLE, FF_CONSTANT, FF_PERIODIC, FF_SINE, FF_RAMP};
      for (const auto code : effect_codes) {
        if (const auto status = enable_evdev_code(device, EV_FF, code, "gamepad force-feedback effect"); !status.ok()) {
          return status;
        }
      }
      return enable_evdev_code(device, EV_FF, FF_GAIN, "gamepad force-feedback gain");
    }

    OperationStatus configure_evdev_gamepad(libevdev *device, const DeviceProfile &profile) {
      const auto supports_rumble = profile.capabilities.supports_rumble;
      if (const auto status = configure_evdev_gamepad_event_types(device, supports_rumble); !status.ok()) {
        return status;
      }
      if (const auto status = configure_evdev_gamepad_buttons(device, profile.gamepad_kind); !status.ok()) {
        return status;
      }
      if (const auto status = configure_evdev_gamepad_axes(device, profile.gamepad_kind); !status.ok()) {
        return status;
      }
      return configure_evdev_gamepad_force_feedback(device, supports_rumble);
    }

    OperationStatus configure_evdev_device(libevdev *device, const DeviceProfile &profile) {
      switch (profile.device_type) {
        using enum DeviceType;

        case keyboard:
          return configure_evdev_keyboard(device);
        case mouse:
          return configure_evdev_mouse(device);
        case touchscreen:
          return configure_evdev_touchscreen(device);
        case trackpad:
          return configure_evdev_trackpad(device);
        case pen_tablet:
          return configure_evdev_pen_tablet(device);
        case gamepad:
          if (uses_uinput_gamepad_profile(profile.gamepad_kind)) {
            return configure_evdev_gamepad(device, profile);
          }
          return OperationStatus::failure(ErrorCode::unsupported_profile, "gamepad profile is not supported through uinput");
      }

      return OperationStatus::failure(ErrorCode::unsupported_profile, "unsupported uinput device type");
    }

    UinputCreationResult create_libevdev_uinput_device(int fd, const DeviceProfile &profile, DeviceId id) {
      if (fd < 0) {
        return {OperationStatus::failure(ErrorCode::backend_failure, "uinput file descriptor is closed"), nullptr};
      }

      LibevdevDevice device {libevdev_new()};
      if (!device) {
        return {OperationStatus::failure(ErrorCode::backend_failure, "failed to allocate libevdev device"), nullptr};
      }

      libevdev_set_name(device.get(), profile.name.c_str());
      const auto xbox_360 = profile.gamepad_kind == GamepadProfileKind::xbox_360;
      const auto xbox_one = profile.gamepad_kind == GamepadProfileKind::xbox_one;
      const auto xbox_series = profile.gamepad_kind == GamepadProfileKind::xbox_series;
      const auto uses_sparse_xbox_mapping = xbox_360 || xbox_one || xbox_series;
      auto vendor_id = profile.vendor_id;
      auto product_id = profile.product_id;
      if (xbox_one) {
        product_id = xbox_wireless_uinput_product_id;
      } else if (xbox_series) {
        product_id = xbox_series_uinput_product_id;
      }
      libevdev_set_id_bustype(
        device.get(),
        uses_sparse_xbox_mapping ? xbox_sparse_uinput_bus : to_uinput_bus(profile.bus_type)
      );
      libevdev_set_id_vendor(device.get(), vendor_id);
      libevdev_set_id_product(device.get(), product_id);
      libevdev_set_id_version(device.get(), profile.version);

      if (const auto status = configure_evdev_device(device.get(), profile); !status.ok()) {
        return {status, nullptr};
      }

      libevdev_uinput *uinput_device = nullptr;
      if (const auto result = libevdev_uinput_create_from_device(device.get(), fd, &uinput_device); result < 0) {
        return {
          OperationStatus::failure(
            ErrorCode::backend_failure,
            std::format("failed to create uinput device {}: {}", id, errno_message(-result))
          ),
          nullptr,
        };
      }

      return {OperationStatus::success(), uinput_device};
    }

    OperationStatus UinputDevice::create_uinput_device(const DeviceProfile &profile, DeviceId id) {
      auto result = create_libevdev_uinput_device(fd_, profile, id);
      if (!result.status.ok()) {
        return result.status;
      }

      uinput_device_ = result.device;
      return OperationStatus::success();
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

      OperationStatus create(DeviceId id, const CreateKeyboardOptions &options) {
        device_name_ = options.profile.name;
        auto status = create_uinput_device(options.profile, id);
        if (status.ok() && options.auto_repeat_interval_ms > 0) {
          start_repeat_thread(options.auto_repeat_interval_ms);
        }
        return status;
      }

      OperationStatus submit(const KeyboardEvent &event) override {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput keyboard is closed");
        }

        auto status = emit_keyboard_event(event);
        if (status.ok()) {
          update_pressed_keys(event);
        }
        return status;
      }

      OperationStatus type_text(const KeyboardTextEvent &event) override {
        return type_text_with_unicode_hex(event.text, [this](const KeyboardEvent &key_event) {
          return submit(key_event);
        });
      }

      OperationStatus close() override {
        stop_repeat_thread();
        return close_uinput("uinput keyboard");
      }

      std::vector<DeviceNode> device_nodes() const override {
        return discover_input_nodes_by_name(device_name_);
      }

    private:
      OperationStatus emit_keyboard_event(const KeyboardEvent &event) {
        const auto linux_key = key_code_to_linux(event.key_code);
        if (linux_key < 0) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "keyboard key code is not supported by the Linux backend");
        }

        if (const auto status = emit_event(EV_KEY, static_cast<std::uint16_t>(linux_key), event.pressed ? 1 : 0); !status.ok()) {
          return status;
        }
        return sync();
      }

      void update_pressed_keys(const KeyboardEvent &event) {
        std::lock_guard lock {pressed_keys_mutex_};
        if (event.pressed) {
          pressed_keys_.insert(event.key_code);
        } else {
          pressed_keys_.erase(event.key_code);
        }
      }

      std::vector<KeyboardKeyCode> pressed_keys_snapshot() const {
        std::lock_guard lock {pressed_keys_mutex_};
        return {pressed_keys_.begin(), pressed_keys_.end()};
      }

      void start_repeat_thread(std::uint32_t interval_ms) {
        repeat_running_ = true;
        repeat_thread_ = std::jthread {[this, interval_ms](std::stop_token stop_token) {
          while (!stop_token.stop_requested() && repeat_running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds {interval_ms});
            if (stop_token.stop_requested() || !repeat_running_ || !is_open()) {
              break;
            }

            for (const auto key_code : pressed_keys_snapshot()) {
              if (stop_token.stop_requested() || !repeat_running_ || !is_open()) {
                break;
              }
              static_cast<void>(emit_keyboard_event({.key_code = key_code, .pressed = true}));
            }
          }
        }};
      }

      void stop_repeat_thread() {
        repeat_running_ = false;
        if (repeat_thread_.joinable()) {
          repeat_thread_.request_stop();
          repeat_thread_.join();
        }
      }

      std::string device_name_;
      std::atomic_bool repeat_running_ = false;
      std::jthread repeat_thread_;
      mutable std::mutex pressed_keys_mutex_;
      std::set<KeyboardKeyCode> pressed_keys_;
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

      OperationStatus create(DeviceId id, const CreateMouseOptions &options) {
        device_name_ = options.profile.name;
        return create_uinput_device(options.profile, id);
      }

      OperationStatus submit(const MouseEvent &event) override {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput mouse is closed");
        }

        switch (event.kind) {
          using enum MouseEventKind;

          case relative_motion:
            return submit_relative_motion(event);
          case absolute_motion:
            return submit_absolute_motion(event);
          case button:
            return submit_button(event);
          case vertical_scroll:
            return submit_vertical_scroll(event.high_resolution_scroll);
          case horizontal_scroll:
            return submit_horizontal_scroll(event.high_resolution_scroll);
        }

        return OperationStatus::failure(ErrorCode::invalid_argument, "unsupported mouse event kind");
      }

      OperationStatus close() override {
        return close_uinput("uinput mouse");
      }

      std::vector<DeviceNode> device_nodes() const override {
        return discover_input_nodes_by_name(device_name_);
      }

    private:
      std::string device_name_;

      OperationStatus submit_relative_motion(const MouseEvent &event) {
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

      OperationStatus submit_absolute_motion(const MouseEvent &event) {
        if (const auto status = emit_event(EV_ABS, ABS_X, scale_absolute_axis(event.x, event.width)); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_Y, scale_absolute_axis(event.y, event.height)); !status.ok()) {
          return status;
        }
        return sync();
      }

      OperationStatus submit_button(const MouseEvent &event) {
        if (const auto status = emit_event(EV_KEY, static_cast<std::uint16_t>(mouse_button_to_linux(event.button)), event.pressed ? 1 : 0); !status.ok()) {
          return status;
        }
        return sync();
      }

      OperationStatus submit_vertical_scroll(std::int32_t distance) {
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

      OperationStatus submit_horizontal_scroll(std::int32_t distance) {
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

    /**
     * @brief Shared stateful multitouch uinput device.
     */
    class UinputTouchDevice: private UinputDevice {
    public:
      explicit UinputTouchDevice(int file_descriptor):
          UinputDevice {file_descriptor} {}

      UinputTouchDevice(const UinputTouchDevice &) = delete;
      UinputTouchDevice &operator=(const UinputTouchDevice &) = delete;
      UinputTouchDevice(UinputTouchDevice &&) noexcept = delete;
      UinputTouchDevice &operator=(UinputTouchDevice &&) noexcept = delete;

      virtual ~UinputTouchDevice() = default;

      OperationStatus close_touch_device(const std::string &description) {
        return close_uinput(description);
      }

      std::vector<DeviceNode> touch_device_nodes() const {
        return discover_input_nodes_by_name(device_name_);
      }

    protected:
      OperationStatus create_touch_device(DeviceId id, const DeviceProfile &profile) {
        device_name_ = profile.name;
        return create_uinput_device(profile, id);
      }

      OperationStatus place_touch_contact(const TouchContact &contact, bool update_trackpad_buttons) {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput touch device is closed");
        }
        if (contact.id < 0) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "touch contact id must not be negative");
        }

        const auto slot = slot_for_contact(contact.id);
        if (!slot.has_value()) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "too many active touch contacts");
        }

        if (const auto status = select_slot(*slot); !status.ok()) {
          return status;
        }
        if (new_slot_) {
          if (const auto status = emit_event(EV_ABS, ABS_MT_TRACKING_ID, *slot); !status.ok()) {
            return status;
          }
          new_slot_ = false;
          if (update_trackpad_buttons) {
            if (const auto status = emit_trackpad_tool_buttons(); !status.ok()) {
              return status;
            }
          } else {
            if (const auto status = emit_event(EV_KEY, BTN_TOUCH, 1); !status.ok()) {
              return status;
            }
          }
        }

        const auto x = scale_normalized_axis(contact.x, touch_axis_max_x);
        const auto y = scale_normalized_axis(contact.y, touch_axis_max_y);
        const auto pressure = scale_normalized_axis(contact.pressure, touch_pressure_max);
        if (const auto status = emit_event(EV_ABS, ABS_X, x); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_MT_POSITION_X, x); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_Y, y); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_MT_POSITION_Y, y); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_PRESSURE, pressure); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_MT_PRESSURE, pressure); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_MT_ORIENTATION, clamp_degrees(contact.orientation)); !status.ok()) {
          return status;
        }
        return sync();
      }

      OperationStatus release_touch_contact(std::int32_t contact_id, bool update_trackpad_buttons) {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput touch device is closed");
        }
        const auto slot_it = contacts_.find(contact_id);
        if (slot_it == contacts_.end()) {
          return OperationStatus::success();
        }

        if (const auto status = select_slot(slot_it->second); !status.ok()) {
          return status;
        }
        contacts_.erase(slot_it);
        if (const auto status = emit_event(EV_ABS, ABS_MT_TRACKING_ID, -1); !status.ok()) {
          return status;
        }

        if (update_trackpad_buttons) {
          if (const auto status = emit_trackpad_tool_buttons(); !status.ok()) {
            return status;
          }
        } else if (contacts_.empty()) {
          if (const auto status = emit_event(EV_KEY, BTN_TOUCH, 0); !status.ok()) {
            return status;
          }
        }

        return sync();
      }

      OperationStatus emit_touch_button(bool pressed) {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput touch device is closed");
        }
        if (const auto status = emit_event(EV_KEY, BTN_LEFT, pressed ? 1 : 0); !status.ok()) {
          return status;
        }
        return sync();
      }

    private:
      std::optional<int> slot_for_contact(std::int32_t contact_id) {
        if (const auto it = contacts_.find(contact_id); it != contacts_.end()) {
          new_slot_ = false;
          return it->second;
        }

        for (auto slot = 0; slot < touch_max_contacts; ++slot) {
          const auto used = std::ranges::any_of(contacts_, [slot](const auto &entry) {
            return entry.second == slot;
          });
          if (!used) {
            contacts_.emplace(contact_id, slot);
            new_slot_ = true;
            return slot;
          }
        }

        return std::nullopt;
      }

      OperationStatus select_slot(int slot) {
        if (current_slot_ == slot) {
          return OperationStatus::success();
        }
        current_slot_ = slot;
        return emit_event(EV_ABS, ABS_MT_SLOT, slot);
      }

      OperationStatus emit_trackpad_tool_buttons() {
        const auto count = contacts_.size();
        if (const auto status = emit_event(EV_KEY, BTN_TOUCH, count == 0 ? 0 : 1); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_KEY, BTN_TOOL_FINGER, count == 1 ? 1 : 0); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_KEY, BTN_TOOL_DOUBLETAP, count == 2 ? 1 : 0); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_KEY, BTN_TOOL_TRIPLETAP, count == 3 ? 1 : 0); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_KEY, BTN_TOOL_QUADTAP, count == 4 ? 1 : 0); !status.ok()) {
          return status;
        }
        return emit_event(EV_KEY, BTN_TOOL_QUINTTAP, count >= 5 ? 1 : 0);
      }

      std::string device_name_;
      std::map<std::int32_t, int> contacts_;
      int current_slot_ = -1;
      bool new_slot_ = false;
    };

    /**
     * @brief Backend touchscreen backed by one Linux uinput file descriptor.
     */
    class UinputTouchscreen final: public BackendTouchscreen, private UinputTouchDevice {
    public:
      explicit UinputTouchscreen(int file_descriptor):
          UinputTouchDevice {file_descriptor} {}

      ~UinputTouchscreen() override {
        static_cast<void>(close());
      }

      OperationStatus create(DeviceId id, const CreateTouchscreenOptions &options) {
        return create_touch_device(id, options.profile);
      }

      OperationStatus place_contact(const TouchContact &contact) override {
        return place_touch_contact(contact, false);
      }

      OperationStatus release_contact(std::int32_t contact_id, PointerTransition /*transition*/) override {
        return release_touch_contact(contact_id, false);
      }

      OperationStatus close() override {
        return close_touch_device("uinput touchscreen");
      }

      std::vector<DeviceNode> device_nodes() const override {
        return touch_device_nodes();
      }
    };

    /**
     * @brief Backend trackpad backed by one Linux uinput file descriptor.
     */
    class UinputTrackpad final: public BackendTrackpad, private UinputTouchDevice {
    public:
      explicit UinputTrackpad(int file_descriptor):
          UinputTouchDevice {file_descriptor} {}

      ~UinputTrackpad() override {
        static_cast<void>(close());
      }

      OperationStatus create(DeviceId id, const CreateTrackpadOptions &options) {
        return create_touch_device(id, options.profile);
      }

      OperationStatus place_contact(const TouchContact &contact) override {
        return place_touch_contact(contact, true);
      }

      OperationStatus release_contact(std::int32_t contact_id, PointerTransition /*transition*/) override {
        return release_touch_contact(contact_id, true);
      }

      OperationStatus button(bool pressed) override {
        return emit_touch_button(pressed);
      }

      OperationStatus close() override {
        return close_touch_device("uinput trackpad");
      }

      std::vector<DeviceNode> device_nodes() const override {
        return touch_device_nodes();
      }
    };

    int pen_tool_to_linux(PenToolType tool) {
      switch (tool) {
        using enum PenToolType;

        case pen:
          return BTN_TOOL_PEN;
        case eraser:
          return BTN_TOOL_RUBBER;
        case brush:
          return BTN_TOOL_BRUSH;
        case pencil:
          return BTN_TOOL_PENCIL;
        case airbrush:
          return BTN_TOOL_AIRBRUSH;
        case touch:
          return BTN_TOUCH;
        case unchanged:
          return -1;
      }

      return -1;
    }

    int pen_button_to_linux(PenButton button) {
      switch (button) {
        using enum PenButton;

        case primary:
          return BTN_STYLUS;
        case secondary:
          return BTN_STYLUS2;
        case tertiary:
#if defined(BTN_STYLUS3)
          return BTN_STYLUS3;
#else
          return BTN_STYLUS2;
#endif
      }

      return BTN_STYLUS;
    }

    /**
     * @brief Backend pen tablet backed by one Linux uinput file descriptor.
     */
    class UinputPenTablet final: public BackendPenTablet, private UinputDevice {
    public:
      explicit UinputPenTablet(int file_descriptor):
          UinputDevice {file_descriptor} {}

      ~UinputPenTablet() override {
        static_cast<void>(close());
      }

      OperationStatus create(DeviceId id, const CreatePenTabletOptions &options) {
        device_name_ = options.profile.name;
        return create_uinput_device(options.profile, id);
      }

      OperationStatus place_tool(const PenToolState &state) override {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput pen tablet is closed");
        }

        if (const auto status = update_pen_tool(state.tool); !status.ok()) {
          return status;
        }
        if (const auto status = emit_pen_position(state); !status.ok()) {
          return status;
        }
        if (const auto status = emit_pen_pressure(state.pressure); !status.ok()) {
          return status;
        }
        if (const auto status = emit_pen_distance(state.distance); !status.ok()) {
          return status;
        }
        if (const auto status = emit_pen_tilt(state); !status.ok()) {
          return status;
        }
        return sync();
      }

      OperationStatus button(PenButton button, bool pressed) override {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput pen tablet is closed");
        }
        if (const auto status = emit_event(EV_KEY, static_cast<std::uint16_t>(pen_button_to_linux(button)), pressed ? 1 : 0); !status.ok()) {
          return status;
        }
        return sync();
      }

      OperationStatus close() override {
        return close_uinput("uinput pen tablet");
      }

      std::vector<DeviceNode> device_nodes() const override {
        return discover_input_nodes_by_name(device_name_);
      }

    private:
      OperationStatus emit_pen_tool(PenToolType tool, std::int32_t value) {
        const auto tool_code = pen_tool_to_linux(tool);
        if (tool_code < 0) {
          return OperationStatus::success();
        }
        return emit_event(EV_KEY, static_cast<std::uint16_t>(tool_code), value);
      }

      OperationStatus update_pen_tool(PenToolType tool) {
        if (tool == PenToolType::unchanged || tool == last_tool_) {
          return OperationStatus::success();
        }
        if (const auto status = emit_pen_tool(tool, 1); !status.ok()) {
          return status;
        }
        if (const auto status = emit_pen_tool(last_tool_, 0); !status.ok()) {
          return status;
        }
        last_tool_ = tool;
        return OperationStatus::success();
      }

      OperationStatus emit_pen_position(const PenToolState &state) {
        if (const auto status = emit_event(EV_ABS, ABS_X, scale_normalized_axis(state.x, touch_axis_max_x)); !status.ok()) {
          return status;
        }
        return emit_event(EV_ABS, ABS_Y, scale_normalized_axis(state.y, touch_axis_max_y));
      }

      OperationStatus emit_pen_pressure(float pressure) {
        if (pressure < 0.0F) {
          return OperationStatus::success();
        }
        if (const auto status = emit_event(EV_ABS, ABS_PRESSURE, scale_normalized_axis(pressure, tablet_pressure_max)); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_DISTANCE, 0); !status.ok()) {
          return status;
        }
        return emit_event(EV_KEY, BTN_TOUCH, pressure > 0.0F ? 1 : 0);
      }

      OperationStatus emit_pen_distance(float distance) {
        if (distance < 0.0F) {
          return OperationStatus::success();
        }
        if (const auto status = emit_event(EV_ABS, ABS_DISTANCE, scale_normalized_axis(distance, tablet_distance_max)); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_PRESSURE, 0); !status.ok()) {
          return status;
        }
        return emit_event(EV_KEY, BTN_TOUCH, 0);
      }

      OperationStatus emit_pen_tilt(const PenToolState &state) {
        if (const auto status = emit_event(EV_ABS, ABS_TILT_X, tablet_tilt_units(state.tilt_x)); !status.ok()) {
          return status;
        }
        return emit_event(EV_ABS, ABS_TILT_Y, tablet_tilt_units(state.tilt_y));
      }

      std::string device_name_;
      PenToolType last_tool_ = PenToolType::unchanged;
    };

#if defined(LIBVIRTUALHID_HAVE_XTEST)
    KeySym key_code_to_keysym(KeyboardKeyCode key_code) {
      static constexpr std::array<std::pair<KeyboardKeyCode, KeySym>, 45> special_keysyms {{
        {0x08, XK_BackSpace},
        {0x09, XK_Tab},
        {0x0D, XK_Return},
        {0x10, XK_Shift_L},
        {0xA0, XK_Shift_L},
        {0x11, XK_Control_L},
        {0xA2, XK_Control_L},
        {0x12, XK_Alt_L},
        {0xA4, XK_Alt_L},
        {0x14, XK_Caps_Lock},
        {0x1B, XK_Escape},
        {0x20, XK_space},
        {0x21, XK_Page_Up},
        {0x22, XK_Page_Down},
        {0x23, XK_End},
        {0x24, XK_Home},
        {0x25, XK_Left},
        {0x26, XK_Up},
        {0x27, XK_Right},
        {0x28, XK_Down},
        {0x2D, XK_Insert},
        {0x2E, XK_Delete},
        {0x5B, XK_Super_L},
        {0x5C, XK_Super_R},
        {0x6A, XK_KP_Multiply},
        {0x6B, XK_KP_Add},
        {0x6D, XK_KP_Subtract},
        {0x6E, XK_KP_Decimal},
        {0x6F, XK_KP_Divide},
        {0x90, XK_Num_Lock},
        {0x91, XK_Scroll_Lock},
        {0xA1, XK_Shift_R},
        {0xA3, XK_Control_R},
        {0xA5, XK_Alt_R},
        {0xBA, XK_semicolon},
        {0xBB, XK_equal},
        {0xBC, XK_comma},
        {0xBD, XK_minus},
        {0xBE, XK_period},
        {0xBF, XK_slash},
        {0xC0, XK_grave},
        {0xDB, XK_bracketleft},
        {0xDC, XK_backslash},
        {0xDD, XK_bracketright},
        {0xDE, XK_apostrophe},
      }};

      if (const auto keysym = mapped_keyboard_code(key_code, special_keysyms); keysym.has_value()) {
        return keysym.value();
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
      if (key_code >= 0x70 && key_code <= 0x87) {
        return XK_F1 + static_cast<KeySym>(key_code - 0x70);
      }

      return NoSymbol;
    }

    int mouse_button_to_xtest(MouseButton button) {
      switch (button) {
        using enum MouseButton;

        case left:
          return 1;
        case middle:
          return 2;
        case right:
          return 3;
        case side:
          return 8;
        case extra:
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

      OperationStatus create() {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
          return OperationStatus::failure(ErrorCode::backend_unavailable, "failed to open X display for XTest keyboard fallback");
        }
        if (!query_xtest(display_)) {
          return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest extension is not available");
        }

        return OperationStatus::success();
      }

      OperationStatus submit(const KeyboardEvent &event) override {
        if (display_ == nullptr) {
          return OperationStatus::failure(ErrorCode::device_closed, "XTest keyboard is closed");
        }

        const auto keysym = key_code_to_keysym(event.key_code);
        if (keysym == NoSymbol) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "keyboard key code is not supported by XTest fallback");
        }

        const auto keycode = XKeysymToKeycode(display_, keysym);
        if (keycode == 0) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "keyboard key code has no X11 keycode");
        }

        XTestFakeKeyEvent(display_, keycode, event.pressed ? True : False, CurrentTime);
        XFlush(display_);
        return OperationStatus::success();
      }

      OperationStatus type_text(const KeyboardTextEvent &event) override {
        return type_text_with_unicode_hex(event.text, [this](const KeyboardEvent &key_event) {
          return submit(key_event);
        });
      }

      OperationStatus close() override {
        if (display_ != nullptr) {
          XCloseDisplay(display_);
          display_ = nullptr;
        }
        return OperationStatus::success();
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

      OperationStatus create() {
        display_ = XOpenDisplay(nullptr);
        if (display_ == nullptr) {
          return OperationStatus::failure(ErrorCode::backend_unavailable, "failed to open X display for XTest mouse fallback");
        }
        if (!query_xtest(display_)) {
          return OperationStatus::failure(ErrorCode::backend_unavailable, "XTest extension is not available");
        }

        return OperationStatus::success();
      }

      OperationStatus submit(const MouseEvent &event) override {
        if (display_ == nullptr) {
          return OperationStatus::failure(ErrorCode::device_closed, "XTest mouse is closed");
        }

        switch (event.kind) {
          using enum MouseEventKind;

          case relative_motion:
            XTestFakeRelativeMotionEvent(display_, event.x, event.y, CurrentTime);
            break;
          case absolute_motion:
            submit_absolute_motion(event);
            break;
          case button:
            XTestFakeButtonEvent(display_, mouse_button_to_xtest(event.button), event.pressed ? True : False, CurrentTime);
            break;
          case vertical_scroll:
            submit_scroll(event.high_resolution_scroll, 4, 5);
            break;
          case horizontal_scroll:
            submit_scroll(event.high_resolution_scroll, 6, 7);
            break;
        }

        XFlush(display_);
        return OperationStatus::success();
      }

      OperationStatus close() override {
        if (display_ != nullptr) {
          XCloseDisplay(display_);
          display_ = nullptr;
        }
        return OperationStatus::success();
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

    struct UinputRumbleEffect {
      std::uint16_t start_weak_magnitude = 0;
      std::uint16_t start_strong_magnitude = 0;
      std::uint16_t end_weak_magnitude = 0;
      std::uint16_t end_strong_magnitude = 0;
      ff_envelope envelope {};
      std::chrono::milliseconds length {0};
      std::chrono::milliseconds delay {0};
      std::chrono::steady_clock::time_point start {};
      std::chrono::steady_clock::time_point end {};
      int playback_count = 1;
      bool infinite = false;
      bool active = false;
    };

    std::pair<std::uint64_t, std::uint64_t> rumble_effect_cycle_timing(
      std::uint64_t elapsed,
      std::uint64_t length
    ) {
      if (length == 0U) {
        return {elapsed, std::numeric_limits<std::uint64_t>::max()};
      }

      const auto cycle_elapsed = elapsed % length;
      return {cycle_elapsed, length - cycle_elapsed};
    }

    /**
     * @brief Standard gamepad exposed through canonical Linux input events.
     */
    class UinputGamepad final: public BackendGamepad, private UinputDevice {
    public:
      UinputGamepad(int file_descriptor, GamepadProfileKind profile_kind):
          UinputDevice {file_descriptor},
          profile_kind_ {profile_kind} {}

      ~UinputGamepad() override = default;

      OperationStatus create(DeviceId id, const CreateGamepadOptions &options) {
        if (options.profile.gamepad_kind != profile_kind_ || !uses_uinput_gamepad_profile(profile_kind_)) {
          return OperationStatus::failure(ErrorCode::unsupported_profile, "gamepad profile is not supported through uinput");
        }

        device_name_ = options.profile.name;
        if (const auto status = create_uinput_device(options.profile, id); !status.ok()) {
          return status;
        }

        if (options.profile.capabilities.supports_rumble) {
          running_ = true;
          reader_ = std::jthread {[this](std::stop_token stop_token) {
            read_output_loop(stop_token);
          }};
        }
        return OperationStatus::success();
      }

      OperationStatus submit(
        const GamepadState &state,
        const std::vector<std::uint8_t> & /*report*/
      ) override {
        if (!is_open()) {
          return OperationStatus::failure(ErrorCode::device_closed, "uinput Xbox gamepad is closed");
        }

        const auto normalized = reports::normalize_state(state);
        std::lock_guard lock {submit_mutex_};
        if (const auto status = emit_button_state(normalized.buttons); !status.ok()) {
          return status;
        }
        if (const auto status = emit_axis_state(normalized); !status.ok()) {
          return status;
        }
        return sync();
      }

      void set_output_callback(OutputCallback callback) override {
        std::lock_guard lock {callback_mutex_};
        output_callback_ = std::move(callback);
      }

      std::vector<DeviceNode> device_nodes() const override {
        return discover_input_nodes_by_name(device_name_);
      }

      OperationStatus close() override {
        running_ = false;
        if (reader_.joinable()) {
          reader_.request_stop();
          reader_.join();
        }
        dispatch_rumble(0, 0);
        return close_uinput("uinput gamepad");
      }

    private:
      template<std::size_t Size>
      OperationStatus emit_button_map(
        const ButtonSet &buttons,
        const std::array<std::pair<GamepadButton, int>, Size> &button_map
      ) {
        for (const auto &[button, code] : button_map) {
          if (const auto status = emit_event(EV_KEY, code, buttons.test(button) ? 1 : 0); !status.ok()) {
            return status;
          }
        }

        return OperationStatus::success();
      }

      OperationStatus emit_button_state(const ButtonSet &buttons) {
        using enum GamepadButton;

        constexpr std::array standard_face_button_map {
          std::pair {a, BTN_SOUTH},
          std::pair {b, BTN_EAST},
          std::pair {x, BTN_NORTH},
          std::pair {y, BTN_WEST},
        };
        constexpr std::array switch_face_button_map {
          std::pair {a, BTN_EAST},
          std::pair {b, BTN_SOUTH},
          std::pair {x, BTN_NORTH},
          std::pair {y, BTN_WEST},
        };
        constexpr std::array common_button_map {
          std::pair {left_shoulder, BTN_TL},
          std::pair {right_shoulder, BTN_TR},
          std::pair {back, BTN_SELECT},
          std::pair {start, BTN_START},
          std::pair {guide, BTN_MODE},
          std::pair {left_stick, BTN_THUMBL},
          std::pair {right_stick, BTN_THUMBR},
        };

        const auto &face_button_map =
          profile_kind_ == GamepadProfileKind::switch_pro ? switch_face_button_map : standard_face_button_map;
        if (const auto status = emit_button_map(buttons, face_button_map); !status.ok()) {
          return status;
        }
        if (const auto status = emit_button_map(buttons, common_button_map); !status.ok()) {
          return status;
        }
        if (const auto misc1_button = uinput_misc1_button(profile_kind_); misc1_button.has_value()) {
          if (const auto status = emit_event(EV_KEY, *misc1_button, buttons.test(misc1) ? 1 : 0); !status.ok()) {
            return status;
          }
        }

        return OperationStatus::success();
      }

      OperationStatus emit_axis_state(const GamepadState &state) {
        using enum GamepadButton;

        if (const auto status = emit_event(EV_ABS, ABS_HAT0X, dpad_axis(state.buttons, dpad_left, dpad_right)); !status.ok()) {
          return status;
        }
        if (const auto status = emit_event(EV_ABS, ABS_HAT0Y, dpad_axis(state.buttons, dpad_up, dpad_down)); !status.ok()) {
          return status;
        }

        const std::array stick_axes {
          std::pair {ABS_X, static_cast<int>(reports::normalize_axis(state.left_stick.x))},
          std::pair {ABS_Y, static_cast<int>(reports::normalize_axis(-state.left_stick.y))},
          std::pair {ABS_RX, static_cast<int>(reports::normalize_axis(state.right_stick.x))},
          std::pair {ABS_RY, static_cast<int>(reports::normalize_axis(-state.right_stick.y))},
        };
        for (const auto &[code, value] : stick_axes) {
          if (const auto status = emit_event(EV_ABS, code, value); !status.ok()) {
            return status;
          }
        }

        if (profile_kind_ == GamepadProfileKind::switch_pro) {
          if (const auto status = emit_event(EV_KEY, BTN_TL2, state.left_trigger > 0.0F ? 1 : 0); !status.ok()) {
            return status;
          }
          return emit_event(EV_KEY, BTN_TR2, state.right_trigger > 0.0F ? 1 : 0);
        }

        const std::array trigger_axes {
          std::pair {ABS_Z, static_cast<int>(reports::normalize_trigger(state.left_trigger))},
          std::pair {ABS_RZ, static_cast<int>(reports::normalize_trigger(state.right_trigger))},
        };
        for (const auto &[code, value] : trigger_axes) {
          if (const auto status = emit_event(EV_ABS, code, value); !status.ok()) {
            return status;
          }
        }
        return OperationStatus::success();
      }

      static int dpad_axis(const ButtonSet &buttons, GamepadButton negative, GamepadButton positive) {
        const auto negative_pressed = buttons.test(negative);
        if (const auto positive_pressed = buttons.test(positive); negative_pressed == positive_pressed) {
          return 0;
        }
        return negative_pressed ? -1 : 1;
      }

      void read_output_loop(std::stop_token stop_token) {
        std::this_thread::sleep_for(uinput_feedback_startup_delay);
        while (!stop_token.stop_requested() && running_) {
          pollfd descriptor {};
          descriptor.fd = file_descriptor();
          descriptor.events = POLLIN;

          const auto poll_result = system_poll(&descriptor, 1, poll_timeout_ms);
          if (poll_result < 0) {
            if (errno == EINTR) {
              continue;
            }
            return;
          }
          if (poll_result > 0) {
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
              return;
            }
            if ((descriptor.revents & POLLIN) != 0 && !read_output_events()) {
              return;
            }
          }
          update_rumble();
        }
      }

      bool read_output_events() {
        std::array<input_event, 16> events {};
        const auto result = system_read(file_descriptor(), std::as_writable_bytes(std::span {events}));
        if (result < 0) {
          return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
        }
        if (result == 0) {
          return false;
        }

        const auto event_count = static_cast<std::size_t>(result) / sizeof(input_event);
        for (std::size_t index = 0; index < event_count; ++index) {
          handle_output_event(events[index]);
        }
        return true;
      }

      void handle_output_event(const input_event &event) {
        if (event.type == EV_UINPUT && event.code == UI_FF_UPLOAD) {
          upload_rumble_effect(event.value);
          return;
        }
        if (event.type == EV_UINPUT && event.code == UI_FF_ERASE) {
          erase_rumble_effect(event.value);
          return;
        }
        if (event.type != EV_FF) {
          return;
        }
        if (event.code == FF_GAIN) {
          gain_ = static_cast<std::uint16_t>(std::clamp(event.value, 0, static_cast<int>(std::numeric_limits<std::uint16_t>::max())));
          return;
        }

        const auto effect = rumble_effects_.find(event.code);
        if (effect == rumble_effects_.end()) {
          return;
        }
        if (event.value <= 0) {
          effect->second.active = false;
          return;
        }

        const auto now = std::chrono::steady_clock::now();
        effect->second.active = true;
        effect->second.start = now + effect->second.delay;
        effect->second.playback_count = std::max(event.value, 1);
        effect->second.end = rumble_effect_end(effect->second);
      }

      void upload_rumble_effect(int request_id) {
        uinput_ff_upload upload {};
        upload.request_id = request_id;
        if (system_ioctl(file_descriptor(), UI_BEGIN_FF_UPLOAD, reinterpret_cast<unsigned long>(&upload)) < 0) {
          return;
        }

        if (auto effect = normalize_rumble_effect(upload.effect); effect.has_value()) {
          if (const auto existing = rumble_effects_.find(upload.effect.id); existing != rumble_effects_.end()) {
            effect->active = existing->second.active;
            if (effect->active) {
              effect->start = existing->second.start;
              effect->playback_count = existing->second.playback_count;
              effect->end = rumble_effect_end(*effect);
            }
          }
          rumble_effects_.insert_or_assign(upload.effect.id, *effect);
        }

        upload.retval = 0;
        static_cast<void>(system_ioctl(file_descriptor(), UI_END_FF_UPLOAD, reinterpret_cast<unsigned long>(&upload)));
      }

      void erase_rumble_effect(int request_id) {
        uinput_ff_erase erase {};
        erase.request_id = request_id;
        if (system_ioctl(file_descriptor(), UI_BEGIN_FF_ERASE, reinterpret_cast<unsigned long>(&erase)) < 0) {
          return;
        }

        rumble_effects_.erase(erase.effect_id);
        erase.retval = 0;
        static_cast<void>(system_ioctl(file_descriptor(), UI_END_FF_ERASE, reinterpret_cast<unsigned long>(&erase)));
      }

      void update_rumble() {
        const auto now = std::chrono::steady_clock::now();
        auto weak = std::uint64_t {0};
        auto strong = std::uint64_t {0};
        for (auto &[id, effect] : rumble_effects_) {
          static_cast<void>(id);
          if (!effect.active || now < effect.start) {
            continue;
          }
          if (!effect.infinite && now >= effect.end) {
            effect.active = false;
            continue;
          }
          const auto [effect_weak, effect_strong] = rumble_magnitudes(effect, now);
          weak += effect_weak;
          strong += effect_strong;
        }

        constexpr auto maximum = std::numeric_limits<std::uint16_t>::max();
        const auto scaled_weak = static_cast<std::uint16_t>(std::min<std::uint64_t>(weak, maximum) * gain_ / maximum);
        const auto scaled_strong = static_cast<std::uint16_t>(std::min<std::uint64_t>(strong, maximum) * gain_ / maximum);
        dispatch_rumble(scaled_strong, scaled_weak);
      }

      static std::uint16_t scale_signed_magnitude(std::int16_t value) {
        const auto magnitude = static_cast<std::uint32_t>(std::abs(static_cast<int>(value)));
        return static_cast<std::uint16_t>(std::min<std::uint32_t>(magnitude * 2U, std::numeric_limits<std::uint16_t>::max()));
      }

      static std::optional<UinputRumbleEffect> normalize_rumble_effect(const ff_effect &source) {
        auto effect = UinputRumbleEffect {
          .length = std::chrono::milliseconds {source.replay.length},
          .delay = std::chrono::milliseconds {source.replay.delay},
          .infinite = source.replay.length == 0U,
        };

        switch (source.type) {
          case FF_RUMBLE:
            effect.start_weak_magnitude = source.u.rumble.weak_magnitude;
            effect.start_strong_magnitude = source.u.rumble.strong_magnitude;
            effect.end_weak_magnitude = effect.start_weak_magnitude;
            effect.end_strong_magnitude = effect.start_strong_magnitude;
            break;
          case FF_CONSTANT:
            {
              const auto magnitude = scale_signed_magnitude(source.u.constant.level);
              effect.start_weak_magnitude = magnitude;
              effect.start_strong_magnitude = magnitude;
              effect.end_weak_magnitude = magnitude;
              effect.end_strong_magnitude = magnitude;
              effect.envelope = source.u.constant.envelope;
              break;
            }
          case FF_PERIODIC:
            {
              const auto magnitude = scale_signed_magnitude(source.u.periodic.magnitude);
              effect.start_weak_magnitude = magnitude;
              effect.start_strong_magnitude = magnitude;
              effect.end_weak_magnitude = magnitude;
              effect.end_strong_magnitude = magnitude;
              effect.envelope = source.u.periodic.envelope;
              break;
            }
          case FF_RAMP:
            effect.start_weak_magnitude = scale_signed_magnitude(source.u.ramp.start_level);
            effect.start_strong_magnitude = effect.start_weak_magnitude;
            effect.end_weak_magnitude = scale_signed_magnitude(source.u.ramp.end_level);
            effect.end_strong_magnitude = effect.end_weak_magnitude;
            effect.envelope = source.u.ramp.envelope;
            break;
          default:
            return std::nullopt;
        }

        return effect;
      }

      static std::chrono::steady_clock::time_point rumble_effect_end(const UinputRumbleEffect &effect) {
        if (effect.infinite) {
          return std::chrono::steady_clock::time_point::max();
        }
        return effect.start + effect.length * effect.playback_count;
      }

      static std::uint16_t interpolate_magnitude(std::uint16_t start, std::uint16_t end, std::uint64_t elapsed, std::uint64_t length) {
        if (length == 0U || elapsed >= length) {
          return end;
        }
        const auto weighted = static_cast<std::uint64_t>(start) * (length - elapsed) + static_cast<std::uint64_t>(end) * elapsed;
        return static_cast<std::uint16_t>(weighted / length);
      }

      static std::uint16_t apply_envelope(
        std::uint16_t magnitude,
        const ff_envelope &envelope,
        std::uint64_t elapsed,
        std::uint64_t remaining
      ) {
        if (envelope.attack_length > 0U && elapsed < envelope.attack_length) {
          return interpolate_magnitude(envelope.attack_level, magnitude, elapsed, envelope.attack_length);
        }
        if (envelope.fade_length > 0U && remaining < envelope.fade_length) {
          return interpolate_magnitude(envelope.fade_level, magnitude, remaining, envelope.fade_length);
        }
        return magnitude;
      }

      static std::pair<std::uint16_t, std::uint16_t> rumble_magnitudes(
        const UinputRumbleEffect &effect,
        std::chrono::steady_clock::time_point now
      ) {
        const auto elapsed = static_cast<std::uint64_t>(std::max<std::int64_t>(
          std::chrono::duration_cast<std::chrono::milliseconds>(now - effect.start).count(),
          0
        ));
        if (effect.infinite) {
          constexpr auto no_finite_end = std::numeric_limits<std::uint64_t>::max();
          return {
            apply_envelope(effect.start_weak_magnitude, effect.envelope, elapsed, no_finite_end),
            apply_envelope(effect.start_strong_magnitude, effect.envelope, elapsed, no_finite_end),
          };
        }
        const auto length = static_cast<std::uint64_t>(std::max<std::int64_t>(effect.length.count(), 0));
        const auto [cycle_elapsed, cycle_remaining] = rumble_effect_cycle_timing(elapsed, length);
        const auto weak =
          interpolate_magnitude(effect.start_weak_magnitude, effect.end_weak_magnitude, cycle_elapsed, length);
        const auto strong =
          interpolate_magnitude(effect.start_strong_magnitude, effect.end_strong_magnitude, cycle_elapsed, length);
        return {
          apply_envelope(weak, effect.envelope, cycle_elapsed, cycle_remaining),
          apply_envelope(strong, effect.envelope, cycle_elapsed, cycle_remaining),
        };
      }

      void dispatch_rumble(std::uint16_t low_frequency, std::uint16_t high_frequency) {
        if (last_low_frequency_ == low_frequency && last_high_frequency_ == high_frequency) {
          return;
        }
        last_low_frequency_ = low_frequency;
        last_high_frequency_ = high_frequency;

        OutputCallback callback;
        {
          std::lock_guard lock {callback_mutex_};
          callback = output_callback_;
        }
        if (callback) {
          GamepadOutput output;
          output.kind = GamepadOutputKind::rumble;
          output.low_frequency_rumble = low_frequency;
          output.high_frequency_rumble = high_frequency;
          callback(output);
        }
      }

      std::string device_name_;
      GamepadProfileKind profile_kind_;
      std::atomic_bool running_ = false;
      std::mutex submit_mutex_;
      std::mutex callback_mutex_;
      OutputCallback output_callback_;
      std::map<int, UinputRumbleEffect> rumble_effects_;
      std::uint16_t gain_ = std::numeric_limits<std::uint16_t>::max();
      std::uint16_t last_low_frequency_ = 0;
      std::uint16_t last_high_frequency_ = 0;
      std::jthread reader_;
    };

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

      OperationStatus create(DeviceId id, const CreateGamepadOptions &options) {
        uhid_event event {};
        auto &request = event.u.create2;

        if (options.profile.report_descriptor.size() > sizeof(request.rd_data)) {
          return OperationStatus::failure(ErrorCode::unsupported_profile, "HID report descriptor is too large for UHID");
        }

        event.type = UHID_CREATE2;
        unique_id_ = options.metadata.stable_id.empty() ? std::to_string(id) : options.metadata.stable_id;
        if (is_playstation_profile(options.profile.gamepad_kind)) {
          playstation_mac_address_ = parse_mac_address(options.metadata.stable_id).value_or(generated_mac_address(id));
          unique_id_ = format_mac_address(playstation_mac_address_);
        }
        physical_id_ = std::format("libvirtualhid/uhid/{}", id);

        copy_string(request.name, options.profile.name);
        copy_string(request.phys, physical_id_);
        copy_string(request.uniq, unique_id_);
        request.rd_size = static_cast<std::uint16_t>(options.profile.report_descriptor.size());
        request.bus = to_uhid_bus(options.profile);
        request.vendor = options.profile.vendor_id;
        request.product = options.profile.product_id;
        request.version = options.profile.version;
        std::memcpy(request.rd_data, options.profile.report_descriptor.data(), options.profile.report_descriptor.size());
        profile_ = options.profile;
        device_name_ = options.profile.name;
        {
          std::lock_guard lock {report_mutex_};
          last_report_ = reports::pack_input_report(profile_, {});
        }

        if (const auto status = write_event(event); !status.ok()) {
          return status;
        }

        running_ = true;
        reader_ = std::jthread {[this](std::stop_token stop_token) {
          read_loop(stop_token);
        }};
        if (is_playstation_profile(profile_.gamepad_kind)) {
          periodic_reporter_ = std::jthread {[this](std::stop_token stop_token) {
            periodic_report_loop(stop_token);
          }};
        }
        return OperationStatus::success();
      }

      OperationStatus submit(
        const GamepadState & /*state*/,
        const std::vector<std::uint8_t> &report
      ) override {
        if (!open_) {
          return OperationStatus::failure(ErrorCode::device_closed, "UHID gamepad is closed");
        }

        uhid_event event {};
        if (report.size() > sizeof(event.u.input2.data)) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "HID input report is too large for UHID");
        }

        event.type = UHID_INPUT2;
        event.u.input2.size = static_cast<std::uint16_t>(report.size());
        std::memcpy(event.u.input2.data, report.data(), report.size());
        auto status = write_event(event);
        if (status.ok()) {
          std::lock_guard lock {report_mutex_};
          last_report_ = report;
        }
        return status;
      }

      void set_output_callback(OutputCallback callback) override {
        std::lock_guard lock {callback_mutex_};
        output_callback_ = std::move(callback);
      }

      std::vector<DeviceNode> device_nodes() const override {
        auto nodes = discover_input_nodes_by_name(device_name_);
        for (const auto &node : discover_hidraw_nodes_by_metadata(device_name_, physical_id_, unique_id_)) {
          append_node_if_missing(nodes, node.kind, node.path);
        }
        return nodes;
      }

      OperationStatus close() override {
        if (!open_.exchange(false)) {
          return OperationStatus::success();
        }

        running_ = false;
        if (periodic_reporter_.joinable()) {
          periodic_reporter_.request_stop();
        }
        if (reader_.joinable()) {
          reader_.request_stop();
        }

        auto status = OperationStatus::success();
        if (fd_ >= 0) {
          uhid_event event {};
          event.type = UHID_DESTROY;
          status = write_event(event);
        }

        if (periodic_reporter_.joinable()) {
          periodic_reporter_.join();
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
      OperationStatus write_event(const uhid_event &event) {
        using enum ErrorCode;

        std::lock_guard lock {write_mutex_};
        if (fd_ < 0) {
          return OperationStatus::failure(device_closed, "UHID file descriptor is closed");
        }

        const auto event_buffer = std::as_bytes(std::span {&event, 1U});
        const auto result = system_write(fd_, event_buffer);
        if (result < 0) {
          return system_error_status(backend_failure, "failed to write UHID event", errno);
        }
        if (static_cast<std::size_t>(result) != sizeof(event)) {
          return OperationStatus::failure(backend_failure, "short write while sending UHID event");
        }

        return OperationStatus::success();
      }

      void read_loop(std::stop_token stop_token) {
        while (!stop_token.stop_requested() && running_) {
          pollfd descriptor {};
          descriptor.fd = fd_;
          descriptor.events = POLLIN;

          const auto result = system_poll(&descriptor, 1, poll_timeout_ms);
          if (result < 0) {
            if (errno == EINTR) {
              continue;
            }
            return;
          }
          if (result == 0) {
            continue;
          }
          if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            return;
          }
          if ((descriptor.revents & POLLIN) == 0) {
            continue;
          }

          uhid_event event {};
          const auto read_result = system_read(fd_, std::as_writable_bytes(std::span {&event, 1U}));
          if (read_result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
              continue;
            }
            return;
          }
          if (read_result == 0) {
            return;
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
            send_get_report_reply(event.u.get_report.id, event.u.get_report.rnum);
            break;
          case UHID_SET_REPORT:
            dispatch_set_report(event.u.set_report.rnum, event.u.set_report.data, event.u.set_report.size);
            send_set_report_reply(event.u.set_report.id, 0);
            break;
          default:
            break;
        }
      }

      void periodic_report_loop(std::stop_token stop_token) {
        while (!stop_token.stop_requested() && running_) {
          std::this_thread::sleep_for(std::chrono::milliseconds {playstation_periodic_report_ms});
          if (stop_token.stop_requested() || !running_ || !open_) {
            break;
          }

          std::vector<std::uint8_t> report;
          {
            std::lock_guard lock {report_mutex_};
            report = last_report_;
          }
          if (!report.empty()) {
            static_cast<void>(submit({}, report));
          }
        }
      }

      void dispatch_output_report(const __u8 *data, std::size_t report_size) {
        const auto size = std::min<std::size_t>(report_size, UHID_DATA_MAX);
        std::vector<std::uint8_t> report(data, data + size);
        dispatch_output_report(report);
      }

      void dispatch_set_report(std::uint8_t report_number, const __u8 *data, std::size_t report_size) {
        const auto size = std::min<std::size_t>(report_size, UHID_DATA_MAX);
        std::vector<std::uint8_t> report(data, data + size);
        if (report_number != 0 && (report.empty() || report.front() != report_number)) {
          report.insert(report.begin(), report_number);
        }
        dispatch_output_report(report);
      }

      void dispatch_output_report(const std::vector<std::uint8_t> &report) {
        OutputCallback callback;
        {
          std::lock_guard lock {callback_mutex_};
          callback = output_callback_;
        }

        if (!callback) {
          return;
        }

        for (const auto &output : reports::parse_output_reports(profile_, report)) {
          callback(output);
        }
      }

      void send_get_report_reply(std::uint32_t id, std::uint8_t report_number) {
        uhid_event event {};
        event.type = UHID_GET_REPORT_REPLY;
        event.u.get_report_reply.id = id;
        event.u.get_report_reply.err = EIO;

        if (profile_.gamepad_kind == GamepadProfileKind::dualshock4) {
          event.u.get_report_reply.err = 0;
          switch (report_number) {
            case ps::dualshock4_usb_calibration_report:
              copy_get_report_payload(event, ps::dualshock4_usb_calibration_info);
              break;
            case ps::dualshock4_bluetooth_calibration_report:
              if (profile_.bus_type == BusType::bluetooth) {
                copy_get_report_payload(event, ps::dualshock4_bluetooth_calibration_info);
                break;
              }
              event.u.get_report_reply.err = EINVAL;
              break;
            case ps::dualshock4_pairing_report:
              copy_get_report_payload(event, ps::dualshock4_pairing_info);
              for (std::size_t index = 0; index < playstation_mac_address_.size(); ++index) {
                event.u.get_report_reply.data[1U + index] =
                  playstation_mac_address_[playstation_mac_address_.size() - 1U - index];
              }
              break;
            case ps::dualshock4_firmware_report:
              copy_get_report_payload(event, ps::dualshock4_firmware_info);
              break;
            default:
              event.u.get_report_reply.err = EINVAL;
              break;
          }
        } else if (profile_.gamepad_kind == GamepadProfileKind::dualsense) {
          event.u.get_report_reply.err = 0;
          switch (report_number) {
            case ps::dualsense_calibration_report:
              copy_get_report_payload(event, ps::dualsense_calibration_info);
              break;
            case ps::dualsense_pairing_report:
              copy_get_report_payload(event, ps::dualsense_pairing_info);
              for (std::size_t index = 0; index < playstation_mac_address_.size(); ++index) {
                event.u.get_report_reply.data[1U + index] =
                  playstation_mac_address_[playstation_mac_address_.size() - 1U - index];
              }
              break;
            case ps::dualsense_firmware_report:
              copy_get_report_payload(event, ps::dualsense_firmware_info);
              break;
            default:
              event.u.get_report_reply.err = EINVAL;
              break;
          }
        }

        if (
          profile_.bus_type == BusType::bluetooth && is_playstation_profile(profile_.gamepad_kind) &&
          event.u.get_report_reply.err == 0 && event.u.get_report_reply.size >= 4U
        ) {
          const auto crc_offset = static_cast<std::size_t>(event.u.get_report_reply.size) - 4U;
          const auto crc = crc32(
            std::span<const std::uint8_t> {event.u.get_report_reply.data, crc_offset},
            playstation_crc_seed(ps::playstation_feature_crc_seed)
          );
          write_u32_le(event.u.get_report_reply.data + crc_offset, crc);
        }

        static_cast<void>(write_event(event));
      }

      static void copy_get_report_payload(uhid_event &event, std::span<const std::uint8_t> payload) {
        event.u.get_report_reply.size = static_cast<std::uint16_t>(std::min<std::size_t>(payload.size(), UHID_DATA_MAX));
        std::memcpy(event.u.get_report_reply.data, payload.data(), event.u.get_report_reply.size);
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
      std::string device_name_;
      std::string physical_id_;
      std::string unique_id_;
      std::array<std::uint8_t, 6> playstation_mac_address_ {};
      std::vector<std::uint8_t> last_report_;
      std::atomic_bool open_ = true;
      std::atomic_bool running_ = false;
      std::jthread reader_;
      std::jthread periodic_reporter_;
      std::mutex write_mutex_;
      std::mutex report_mutex_;
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
        capabilities_.supports_gamepad = uhid_accessible || uinput_accessible;
        capabilities_.supports_keyboard = uinput_accessible || xtest_accessible;
        capabilities_.supports_mouse = uinput_accessible || xtest_accessible;
        capabilities_.supports_touchscreen = uinput_accessible;
        capabilities_.supports_trackpad = uinput_accessible;
        capabilities_.supports_pen_tablet = uinput_accessible;
        capabilities_.supports_output_reports = uhid_accessible || uinput_accessible;
        capabilities_.supports_xtest_fallback = xtest_accessible;
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) override {
        if (uses_uinput_gamepad_profile(options.profile.gamepad_kind)) {
          const auto fd = system_open(uinput_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
          if (fd < 0) {
            return {system_error_status(ErrorCode::backend_unavailable, "failed to open /dev/uinput", errno), nullptr};
          }

          auto gamepad = std::make_unique<UinputGamepad>(fd, options.profile.gamepad_kind);
          if (const auto status = gamepad->create(id, options); !status.ok()) {
            static_cast<void>(gamepad->close());
            return {status, nullptr};
          }
          return {OperationStatus::success(), std::move(gamepad)};
        }

        const auto fd = system_open(uhid_path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
          return {system_error_status(ErrorCode::backend_unavailable, "failed to open /dev/uhid", errno), nullptr};
        }

        auto gamepad = std::make_unique<UhidGamepad>(fd);
        if (const auto status = gamepad->create(id, options); !status.ok()) {
          static_cast<void>(gamepad->close());
          return {status, nullptr};
        }

        return {OperationStatus::success(), std::move(gamepad)};
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

        return {OperationStatus::success(), std::move(keyboard)};
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

        return {OperationStatus::success(), std::move(mouse)};
      }

      BackendTouchscreenCreationResult create_touchscreen(
        DeviceId id,
        const CreateTouchscreenOptions &options
      ) override {
        const auto fd = system_open(uinput_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
          return {system_error_status(ErrorCode::backend_unavailable, "failed to open /dev/uinput", errno), nullptr};
        }

        auto touchscreen = std::make_unique<UinputTouchscreen>(fd);
        if (const auto status = touchscreen->create(id, options); !status.ok()) {
          static_cast<void>(touchscreen->close());
          return {status, nullptr};
        }

        return {OperationStatus::success(), std::move(touchscreen)};
      }

      BackendTrackpadCreationResult create_trackpad(DeviceId id, const CreateTrackpadOptions &options) override {
        const auto fd = system_open(uinput_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
          return {system_error_status(ErrorCode::backend_unavailable, "failed to open /dev/uinput", errno), nullptr};
        }

        auto trackpad = std::make_unique<UinputTrackpad>(fd);
        if (const auto status = trackpad->create(id, options); !status.ok()) {
          static_cast<void>(trackpad->close());
          return {status, nullptr};
        }

        return {OperationStatus::success(), std::move(trackpad)};
      }

      BackendPenTabletCreationResult create_pen_tablet(DeviceId id, const CreatePenTabletOptions &options) override {
        const auto fd = system_open(uinput_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
          return {system_error_status(ErrorCode::backend_unavailable, "failed to open /dev/uinput", errno), nullptr};
        }

        auto pen_tablet = std::make_unique<UinputPenTablet>(fd);
        if (const auto status = pen_tablet->create(id, options); !status.ok()) {
          static_cast<void>(pen_tablet->close());
          return {status, nullptr};
        }

        return {OperationStatus::success(), std::move(pen_tablet)};
      }

    private:
      BackendKeyboardCreationResult create_xtest_keyboard() {
#if defined(LIBVIRTUALHID_HAVE_XTEST)
        auto keyboard = std::make_unique<XTestKeyboard>();
        if (const auto status = keyboard->create(); !status.ok()) {
          return {status, nullptr};
        }
        return {OperationStatus::success(), std::move(keyboard)};
#else
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "failed to open /dev/uinput"), nullptr};
#endif
      }

      BackendMouseCreationResult create_xtest_mouse() {
#if defined(LIBVIRTUALHID_HAVE_XTEST)
        auto mouse = std::make_unique<XTestMouse>();
        if (const auto status = mouse->create(); !status.ok()) {
          return {status, nullptr};
        }
        return {OperationStatus::success(), std::move(mouse)};
#else
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "failed to open /dev/uinput"), nullptr};
#endif
      }

      BackendCapabilities capabilities_;
    };

  }  // namespace

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<LinuxUhidBackend>();
  }

}  // namespace lvh::detail
