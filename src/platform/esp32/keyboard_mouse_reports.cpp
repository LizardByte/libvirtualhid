/**
 * @file src/platform/esp32/keyboard_mouse_reports.cpp
 * @brief HID keyboard and mouse reports for the ESP32-S3 bridge keyboard/mouse slot.
 */

// standard includes
#include <algorithm>
#include <cmath>
#include <utility>

// local includes
#include "platform/esp32/bytes.hpp"
#include "platform/esp32/keyboard_mouse_reports.hpp"

namespace lvh::detail::esp32 {
  namespace {

    constexpr std::uint8_t usage_left_control = 0xE0;
    constexpr std::uint8_t usage_left_shift = 0xE1;
    constexpr std::uint8_t usage_right_gui = 0xE7;
    constexpr std::size_t rollover_keys = 6;

    /**
     * @brief Virtual-key codes with a fixed keyboard-page usage.
     */
    constexpr auto virtual_key_usages = std::to_array<std::pair<std::uint8_t, std::uint8_t>>({
      {0x08, 0x2A},  // Backspace
      {0x09, 0x2B},  // Tab
      {0x0C, 0x5D},  // Clear (keypad 5 without Num Lock)
      {0x13, 0x48},  // Pause
      {0x14, 0x39},  // Caps Lock
      {0x15, 0x90},  // Kana / Hangul -> LANG1
      {0x19, 0x91},  // Kanji / Hanja -> LANG2
      {0x1B, 0x29},  // Escape
      {0x1C, 0x8A},  // Convert -> International4
      {0x1D, 0x8B},  // Nonconvert -> International5
      {0x20, 0x2C},  // Space
      {0x21, 0x4B},  // Page Up
      {0x22, 0x4E},  // Page Down
      {0x23, 0x4D},  // End
      {0x24, 0x4A},  // Home
      {0x25, 0x50},  // Left
      {0x26, 0x52},  // Up
      {0x27, 0x4F},  // Right
      {0x28, 0x51},  // Down
      {0x2C, 0x46},  // Print Screen
      {0x2D, 0x49},  // Insert
      {0x2E, 0x4C},  // Delete
      {0x30, 0x27},  // 0
      {0x5B, 0xE3},  // Left Windows
      {0x5C, 0xE7},  // Right Windows
      {0x5D, 0x65},  // Applications
      {0x60, 0x62},  // Keypad 0
      {0x6A, 0x55},  // Keypad *
      {0x6B, 0x57},  // Keypad +
      {0x6C, 0x85},  // Keypad separator
      {0x6D, 0x56},  // Keypad -
      {0x6E, 0x63},  // Keypad .
      {0x6F, 0x54},  // Keypad /
      {0x90, 0x53},  // Num Lock
      {0x91, 0x47},  // Scroll Lock
      {0xA0, 0xE1},  // Left Shift
      {0xA1, 0xE5},  // Right Shift
      {0xA2, 0xE0},  // Left Control
      {0xA3, 0xE4},  // Right Control
      {0xA4, 0xE2},  // Left Alt
      {0xA5, 0xE6},  // Right Alt
      {0xAD, 0x7F},  // Volume Mute
      {0xAE, 0x81},  // Volume Down
      {0xAF, 0x80},  // Volume Up
      {0xBA, 0x33},  // ;:
      {0xBB, 0x2E},  // =+
      {0xBC, 0x36},  // ,<
      {0xBD, 0x2D},  // -_
      {0xBE, 0x37},  // .>
      {0xBF, 0x38},  // /?
      {0xC0, 0x35},  // `~
      {0xDB, 0x2F},  // [{
      {0xDC, 0x31},  // \|
      {0xDD, 0x30},  // ]}
      {0xDE, 0x34},  // '"
      {0xE2, 0x64},  // Non-US \|
      {0x2F, 0x75},  // Help
      {0x29, 0x77},  // Select
      {0x2B, 0x74},  // Execute
      {0x10, 0xE1},  // Shift
      {0x11, 0xE0},  // Control (extended: right)
      {0x12, 0xE2},  // Alt (extended: right)
      {0x0D, 0x28},  // Enter (extended: keypad)
      {0xFE, 0x9C},  // OEM Clear -> Clear
    });

    constexpr auto keyboard_descriptor = std::to_array<std::uint8_t>({
      // Keyboard, report id 1.
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x06,  // Usage (Keyboard)
      0xA1,
      0x01,  // Collection (Application)
      0x85,
      LVHB_KM_KEYBOARD_REPORT_ID,  // Report ID
      0x05,
      0x07,  // Usage Page (Keyboard/Keypad)
      0x19,
      0xE0,  // Usage Minimum (Left Control)
      0x29,
      0xE7,  // Usage Maximum (Right GUI)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x01,  // Logical Maximum (1)
      0x75,
      0x01,  // Report Size (1)
      0x95,
      0x08,  // Report Count (8)
      0x81,
      0x02,  // Input (Data,Var,Abs): modifiers
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x01,  // Input (Const): reserved
      0x05,
      0x08,  // Usage Page (LEDs)
      0x19,
      0x01,  // Usage Minimum (Num Lock)
      0x29,
      0x05,  // Usage Maximum (Kana)
      0x75,
      0x01,  // Report Size (1)
      0x95,
      0x05,  // Report Count (5)
      0x91,
      0x02,  // Output (Data,Var,Abs): LEDs
      0x75,
      0x03,  // Report Size (3)
      0x95,
      0x01,  // Report Count (1)
      0x91,
      0x01,  // Output (Const): padding
      0x05,
      0x07,  // Usage Page (Keyboard/Keypad)
      0x19,
      0x00,  // Usage Minimum (0)
      0x29,
      0xE7,  // Usage Maximum (Right GUI)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0xE7,  // Logical Maximum (231)
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x06,  // Report Count (6)
      0x81,
      0x00,  // Input (Data,Array,Abs): keys
      0xC0,  // End Collection
    });

    constexpr auto relative_mouse_descriptor = std::to_array<std::uint8_t>({
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x02,  // Usage (Mouse)
      0xA1,
      0x01,  // Collection (Application)
      0x85,
      LVHB_KM_RELATIVE_REPORT_ID,  // Report ID
      0x09,
      0x01,  // Usage (Pointer)
      0xA1,
      0x00,  // Collection (Physical)
      0x05,
      0x09,  // Usage Page (Button)
      0x19,
      0x01,  // Usage Minimum (1)
      0x29,
      0x05,  // Usage Maximum (5)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x01,  // Logical Maximum (1)
      0x75,
      0x01,  // Report Size (1)
      0x95,
      0x05,  // Report Count (5)
      0x81,
      0x02,  // Input (Data,Var,Abs): buttons
      0x75,
      0x03,  // Report Size (3)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x01,  // Input (Const): padding
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x30,  // Usage (X)
      0x09,
      0x31,  // Usage (Y)
      0x16,
      0x01,
      0x80,  // Logical Minimum (-32767)
      0x26,
      0xFF,
      0x7F,  // Logical Maximum (32767)
      0x75,
      0x10,  // Report Size (16)
      0x95,
      0x02,  // Report Count (2)
      0x81,
      0x06,  // Input (Data,Var,Rel)
      0x09,
      0x38,  // Usage (Wheel)
      0x15,
      0x81,  // Logical Minimum (-127)
      0x25,
      0x7F,  // Logical Maximum (127)
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x06,  // Input (Data,Var,Rel): wheel
      0x05,
      0x0C,  // Usage Page (Consumer)
      0x0A,
      0x38,
      0x02,  // Usage (AC Pan)
      0x15,
      0x81,  // Logical Minimum (-127)
      0x25,
      0x7F,  // Logical Maximum (127)
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x06,  // Input (Data,Var,Rel): pan
      0xC0,  // End Collection
      0xC0,  // End Collection
    });

    constexpr auto absolute_pointer_descriptor = std::to_array<std::uint8_t>({
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x02,  // Usage (Mouse)
      0xA1,
      0x01,  // Collection (Application)
      0x85,
      LVHB_KM_ABSOLUTE_REPORT_ID,  // Report ID
      0x09,
      0x01,  // Usage (Pointer)
      0xA1,
      0x00,  // Collection (Physical)
      0x05,
      0x09,  // Usage Page (Button)
      0x19,
      0x01,  // Usage Minimum (1)
      0x29,
      0x05,  // Usage Maximum (5)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x01,  // Logical Maximum (1)
      0x75,
      0x01,  // Report Size (1)
      0x95,
      0x05,  // Report Count (5)
      0x81,
      0x02,  // Input (Data,Var,Abs): buttons
      0x75,
      0x03,  // Report Size (3)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x01,  // Input (Const): padding
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x30,  // Usage (X)
      0x09,
      0x31,  // Usage (Y)
      0x15,
      0x00,  // Logical Minimum (0)
      0x26,
      0xFF,
      0x7F,  // Logical Maximum (32767)
      0x75,
      0x10,  // Report Size (16)
      0x95,
      0x02,  // Report Count (2)
      0x81,
      0x02,  // Input (Data,Var,Abs)
      0xC0,  // End Collection
      0xC0,  // End Collection
    });

    bool is_modifier(std::uint8_t usage) {
      return usage >= usage_left_control && usage <= usage_right_gui;
    }

  }  // namespace

  std::vector<std::uint8_t> keyboard_mouse_report_descriptor() {
    std::vector<std::uint8_t> descriptor;
    descriptor.insert(descriptor.end(), keyboard_descriptor.begin(), keyboard_descriptor.end());
    descriptor.insert(descriptor.end(), relative_mouse_descriptor.begin(), relative_mouse_descriptor.end());
    descriptor.insert(descriptor.end(), absolute_pointer_descriptor.begin(), absolute_pointer_descriptor.end());
    return descriptor;
  }

  std::vector<std::uint8_t> keyboard_mouse_neutral_reports() {
    const auto keyboard = KeyboardReportState {}.report();
    const auto mouse = make_relative_mouse_report(0, 0, 0, 0, 0);
    std::vector<std::uint8_t> reports;
    reports.push_back(static_cast<std::uint8_t>(keyboard.size()));
    reports.insert(reports.end(), keyboard.begin(), keyboard.end());
    reports.push_back(static_cast<std::uint8_t>(mouse.size()));
    reports.insert(reports.end(), mouse.begin(), mouse.end());
    return reports;
  }

  std::optional<std::uint8_t> hid_usage_from_virtual_key(KeyboardKeyCode virtual_key, bool extended) {
    if (virtual_key >= 0x31 && virtual_key <= 0x39) {
      return static_cast<std::uint8_t>(0x1E + (virtual_key - 0x31));  // 1-9
    }
    if (virtual_key >= 0x41 && virtual_key <= 0x5A) {
      return static_cast<std::uint8_t>(0x04 + (virtual_key - 0x41));  // A-Z
    }
    if (virtual_key >= 0x61 && virtual_key <= 0x69) {
      return static_cast<std::uint8_t>(0x59 + (virtual_key - 0x61));  // Keypad 1-9
    }
    if (virtual_key >= 0x70 && virtual_key <= 0x7B) {
      return static_cast<std::uint8_t>(0x3A + (virtual_key - 0x70));  // F1-F12
    }
    if (virtual_key >= 0x7C && virtual_key <= 0x87) {
      return static_cast<std::uint8_t>(0x68 + (virtual_key - 0x7C));  // F13-F24
    }
    if (extended) {
      switch (virtual_key) {
        case 0x0D:
          return std::uint8_t {0x58};  // Keypad Enter
        case 0x11:
          return std::uint8_t {0xE4};  // Right Control
        case 0x12:
          return std::uint8_t {0xE6};  // Right Alt
        default:
          break;
      }
    }
    const auto it = std::ranges::find_if(virtual_key_usages, [virtual_key](const auto &mapping) {
      return mapping.first == virtual_key;
    });
    if (it == virtual_key_usages.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::optional<std::pair<std::uint8_t, bool>> hid_usage_from_ascii(char character) {
    if (character >= 'a' && character <= 'z') {
      return std::pair {static_cast<std::uint8_t>(0x04 + (character - 'a')), false};
    }
    if (character >= 'A' && character <= 'Z') {
      return std::pair {static_cast<std::uint8_t>(0x04 + (character - 'A')), true};
    }
    if (character >= '1' && character <= '9') {
      return std::pair {static_cast<std::uint8_t>(0x1E + (character - '1')), false};
    }
    static constexpr auto symbols = std::to_array<std::pair<char, std::pair<std::uint8_t, bool>>>({
      {'0', {0x27, false}},
      {' ', {0x2C, false}},
      {'\n', {0x28, false}},
      {'\t', {0x2B, false}},
      {'-', {0x2D, false}},
      {'_', {0x2D, true}},
      {'=', {0x2E, false}},
      {'+', {0x2E, true}},
      {'[', {0x2F, false}},
      {'{', {0x2F, true}},
      {']', {0x30, false}},
      {'}', {0x30, true}},
      {'\\', {0x31, false}},
      {'|', {0x31, true}},
      {';', {0x33, false}},
      {':', {0x33, true}},
      {'\'', {0x34, false}},
      {'"', {0x34, true}},
      {'`', {0x35, false}},
      {'~', {0x35, true}},
      {',', {0x36, false}},
      {'<', {0x36, true}},
      {'.', {0x37, false}},
      {'>', {0x37, true}},
      {'/', {0x38, false}},
      {'?', {0x38, true}},
      {'!', {0x1E, true}},
      {'@', {0x1F, true}},
      {'#', {0x20, true}},
      {'$', {0x21, true}},
      {'%', {0x22, true}},
      {'^', {0x23, true}},
      {'&', {0x24, true}},
      {'*', {0x25, true}},
      {'(', {0x26, true}},
      {')', {0x27, true}},
    });
    const auto it = std::ranges::find_if(symbols, [character](const auto &mapping) {
      return mapping.first == character;
    });
    if (it == symbols.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  bool KeyboardReportState::apply(std::uint8_t usage, bool pressed) {
    if (is_modifier(usage)) {
      const auto bit = static_cast<std::uint8_t>(1U << (usage - usage_left_control));
      const auto updated = static_cast<std::uint8_t>(pressed ? (modifiers_ | bit) : (modifiers_ & ~bit));
      const auto changed = updated != modifiers_;
      modifiers_ = updated;
      return changed;
    }
    const auto it = std::ranges::find(keys_, usage);
    if (pressed) {
      if (it != keys_.end()) {
        return false;
      }
      // Beyond six keys the oldest is dropped rather than reporting rollover errors.
      if (keys_.size() == rollover_keys) {
        keys_.erase(keys_.begin());
      }
      keys_.push_back(usage);
      return true;
    }
    if (it == keys_.end()) {
      return false;
    }
    keys_.erase(it);
    return true;
  }

  KeyboardReport KeyboardReportState::report() const {
    return report_with(0, false);
  }

  KeyboardReport KeyboardReportState::report_with(std::uint8_t usage, bool shift) const {
    KeyboardReport report {};
    report[0] = LVHB_KM_KEYBOARD_REPORT_ID;
    report[1] = static_cast<std::uint8_t>(modifiers_ | (shift ? (1U << (usage_left_shift - usage_left_control)) : 0U));
    std::size_t index = 3;
    for (const auto key : keys_) {
      report[index++] = key;
    }
    if (usage != 0U && std::ranges::find(keys_, usage) == keys_.end()) {
      // With six keys held, the typed key takes the last position so the character still arrives.
      report[std::min(index, report.size() - 1U)] = usage;
    }
    return report;
  }

  RelativeMouseReport make_relative_mouse_report(std::uint8_t buttons, std::int16_t x, std::int16_t y, std::int8_t wheel, std::int8_t pan) {
    RelativeMouseReport report {LVHB_KM_RELATIVE_REPORT_ID, buttons};
    write_le(std::span<std::uint8_t> {report}, 2, static_cast<std::uint16_t>(x));
    write_le(std::span<std::uint8_t> {report}, 4, static_cast<std::uint16_t>(y));
    report[6] = static_cast<std::uint8_t>(wheel);
    report[7] = static_cast<std::uint8_t>(pan);
    return report;
  }

  AbsoluteMouseReport make_absolute_mouse_report(std::uint8_t buttons, std::uint16_t x, std::uint16_t y) {
    AbsoluteMouseReport report {LVHB_KM_ABSOLUTE_REPORT_ID, buttons};
    write_le(std::span<std::uint8_t> {report}, 2, x);
    write_le(std::span<std::uint8_t> {report}, 4, y);
    return report;
  }

  std::uint16_t scale_absolute_pointer_axis(
    float value,
    std::int32_t source_dimension,
    std::int32_t viewport_offset,
    std::int32_t viewport_dimension,
    std::int32_t desktop_offset,
    std::int32_t desktop_dimension
  ) {
    if (source_dimension <= 0) {
      return 0;
    }
    const auto clamped = std::clamp(value, 0.0F, static_cast<float>(source_dimension));
    auto fraction = clamped / static_cast<float>(source_dimension);
    if (viewport_dimension > 0 && desktop_dimension > 0) {
      const auto viewport_span = static_cast<float>(std::max(viewport_dimension - 1, 0));
      const auto target = static_cast<float>(viewport_offset - desktop_offset) + fraction * viewport_span;
      fraction = target / static_cast<float>(desktop_dimension);
    }
    fraction = std::clamp(fraction, 0.0F, 1.0F);
    return static_cast<std::uint16_t>(std::lround(fraction * static_cast<float>(absolute_pointer_max)));
  }

}  // namespace lvh::detail::esp32
