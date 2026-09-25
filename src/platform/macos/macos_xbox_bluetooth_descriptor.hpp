/**
 * @file src/platform/macos/macos_xbox_bluetooth_descriptor.hpp
 * @brief Xbox Bluetooth HID descriptor used by the macOS broker transport.
 */
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace lvh::detail::macos {
  inline constexpr std::uint8_t xbox_bluetooth_input_report_id = 0x01;
  inline constexpr std::uint8_t xbox_bluetooth_rumble_report_id = 0x03;
  inline constexpr std::uint8_t xbox_bluetooth_battery_report_id = 0x04;

  inline std::vector<std::uint8_t> make_xbox_bluetooth_report_descriptor(bool include_battery) {
    // Match the Xbox Bluetooth input, rumble, and battery report layout.
    // Steam's HIDAPI driver reads these byte offsets for wireless Xbox pads.
    std::vector<std::uint8_t> descriptor {
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x05,  // Usage (Game Pad)
      0xA1,
      0x01,  // Collection (Application)
      0x85,
      xbox_bluetooth_input_report_id,  // Report ID (1)
      0x09,
      0x01,  // Usage (Pointer)
      0xA1,
      0x00,  // Collection (Physical)
      0x09,
      0x30,  // Usage (X)
      0x09,
      0x31,  // Usage (Y)
      0x15,
      0x00,  // Logical Minimum (0)
      0x27,
      0xFF,
      0xFF,
      0x00,
      0x00,  // Logical Maximum (65534)
      0x95,
      0x02,  // Report Count (2)
      0x75,
      0x10,  // Report Size (16)
      0x81,
      0x02,  // Input (Data, Variable, Absolute)
      0xC0,  // End Collection
      0x09,
      0x01,  // Usage (Pointer)
      0xA1,
      0x00,  // Collection (Physical)
      0x09,
      0x33,  // Usage (Rx)
      0x09,
      0x34,  // Usage (Ry)
      0x15,
      0x00,  // Logical Minimum (0)
      0x27,
      0xFF,
      0xFF,
      0x00,
      0x00,  // Logical Maximum (65534)
      0x95,
      0x02,  // Report Count (2)
      0x75,
      0x10,  // Report Size (16)
      0x81,
      0x02,  // Input (Data, Variable, Absolute)
      0xC0,  // End Collection
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x32,  // Usage (Z)
      0x15,
      0x00,  // Logical Minimum (0)
      0x26,
      0xFF,
      0x03,  // Logical Maximum (1023)
      0x95,
      0x01,  // Report Count (1)
      0x75,
      0x0A,  // Report Size (10)
      0x81,
      0x02,  // Input (Data, Variable, Absolute)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x00,  // Logical Maximum (0)
      0x75,
      0x06,  // Report Size (6)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x03,  // Input (Constant, Variable, Absolute)
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x35,  // Usage (Rz)
      0x15,
      0x00,  // Logical Minimum (0)
      0x26,
      0xFF,
      0x03,  // Logical Maximum (1023)
      0x95,
      0x01,  // Report Count (1)
      0x75,
      0x0A,  // Report Size (10)
      0x81,
      0x02,  // Input (Data, Variable, Absolute)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x00,  // Logical Maximum (0)
      0x75,
      0x06,  // Report Size (6)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x03,  // Input (Constant, Variable, Absolute)
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x39,  // Usage (Hat Switch)
      0x15,
      0x01,  // Logical Minimum (1)
      0x25,
      0x08,  // Logical Maximum (8)
      0x35,
      0x00,  // Physical Minimum (0)
      0x46,
      0x3B,
      0x01,  // Physical Maximum (315)
      0x66,
      0x14,
      0x00,  // Unit (Degrees)
      0x75,
      0x04,  // Report Size (4)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x42,  // Input (Data, Variable, Absolute, Null State)
      0x75,
      0x04,  // Report Size (4)
      0x95,
      0x01,  // Report Count (1)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x00,  // Logical Maximum (0)
      0x35,
      0x00,  // Physical Minimum (0)
      0x45,
      0x00,  // Physical Maximum (0)
      0x65,
      0x00,  // Unit (None)
      0x81,
      0x03,  // Input (Constant, Variable, Absolute)
      0x05,
      0x09,  // Usage Page (Button)
      0x19,
      0x01,  // Usage Minimum (Button 1)
      0x29,
      0x0F,  // Usage Maximum (Button 15)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x01,  // Logical Maximum (1)
      0x75,
      0x01,  // Report Size (1)
      0x95,
      0x0F,  // Report Count (15)
      0x81,
      0x02,  // Input (Data, Variable, Absolute)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x00,  // Logical Maximum (0)
      0x75,
      0x01,  // Report Size (1)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x03,  // Input (Constant, Variable, Absolute)
      0x05,
      0x0C,  // Usage Page (Consumer)
      0x0A,
      0xB2,
      0x00,  // Usage (Record)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x01,  // Logical Maximum (1)
      0x95,
      0x01,  // Report Count (1)
      0x75,
      0x01,  // Report Size (1)
      0x81,
      0x02,  // Input (Data, Variable, Absolute)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x00,  // Logical Maximum (0)
      0x75,
      0x07,  // Report Size (7)
      0x95,
      0x01,  // Report Count (1)
      0x81,
      0x03,  // Input (Constant, Variable, Absolute)
      0x05,
      0x0F,  // Usage Page (Physical Interface Device)
      0x09,
      0x21,  // Usage (Set Effect Report)
      0x85,
      xbox_bluetooth_rumble_report_id,  // Report ID (3)
      0xA1,
      0x02,  // Collection (Logical)
      0x09,
      0x97,  // Usage (DC Enable Actuators)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x01,  // Logical Maximum (1)
      0x75,
      0x04,  // Report Size (4)
      0x95,
      0x01,  // Report Count (1)
      0x91,
      0x02,  // Output (Data, Variable, Absolute)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x00,  // Logical Maximum (0)
      0x75,
      0x04,  // Report Size (4)
      0x95,
      0x01,  // Report Count (1)
      0x91,
      0x03,  // Output (Constant, Variable, Absolute)
      0x09,
      0x70,  // Usage (Magnitude)
      0x15,
      0x00,  // Logical Minimum (0)
      0x25,
      0x64,  // Logical Maximum (100)
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x04,  // Report Count (4)
      0x91,
      0x02,  // Output (Data, Variable, Absolute)
      0x09,
      0x50,  // Usage (Duration)
      0x66,
      0x01,
      0x10,  // Unit (Seconds)
      0x55,
      0x0E,  // Unit Exponent (-2)
      0x15,
      0x00,  // Logical Minimum (0)
      0x26,
      0xFF,
      0x00,  // Logical Maximum (255)
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x01,  // Report Count (1)
      0x91,
      0x02,  // Output (Data, Variable, Absolute)
      0x09,
      0xA7,  // Usage (Start Delay)
      0x15,
      0x00,  // Logical Minimum (0)
      0x26,
      0xFF,
      0x00,  // Logical Maximum (255)
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x01,  // Report Count (1)
      0x91,
      0x02,  // Output (Data, Variable, Absolute)
      0x65,
      0x00,  // Unit (None)
      0x55,
      0x00,  // Unit Exponent (0)
      0x09,
      0x7C,  // Usage (Loop Count)
      0x15,
      0x00,  // Logical Minimum (0)
      0x26,
      0xFF,
      0x00,  // Logical Maximum (255)
      0x75,
      0x08,  // Report Size (8)
      0x95,
      0x01,  // Report Count (1)
      0x91,
      0x02,  // Output (Data, Variable, Absolute)
      0xC0,  // End Collection
    };

    if (include_battery) {
      // Keep the native wireless/source flag in a byte and the logical
      // range within the four Xbox Bluetooth battery levels.
      constexpr std::array<std::uint8_t, 16> battery_descriptor {
        0x05,
        0x06,  // Usage Page (Generic Device Controls)
        0x09,
        0x20,  // Usage (Battery Strength)
        0x85,
        xbox_bluetooth_battery_report_id,  // Report ID (4)
        0x15,
        0x04,  // Logical Minimum (wireless, empty)
        0x25,
        0x07,  // Logical Maximum (wireless, full)
        0x75,
        0x08,  // Report Size (8)
        0x95,
        0x01,  // Report Count (1)
        0x81,
        0x02,  // Input (Data, Variable, Absolute)
      };
      descriptor.insert(descriptor.end(), battery_descriptor.begin(), battery_descriptor.end());
    }

    descriptor.push_back(0xC0);  // End Collection
    return descriptor;
  }

}  // namespace lvh::detail::macos
