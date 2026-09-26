/**
 * @file src/platform/macos/macos_xbox_transport.hpp
 * @brief Steam-compatible macOS HID transport for Xbox gamepad profiles.
 */
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>
#include <span>
#include <vector>

namespace lvh::detail::macos {

  inline constexpr std::uint8_t xbox_gip_input_command = 0x20;

  inline std::vector<std::uint8_t> xbox_gip_report_descriptor() {
    // IOHIDUserDevice exposes its transport as Virtual even when Bluetooth is
    // requested. Steam therefore selects its wired GIP decoder for these IDs.
    return {
      0x05,
      0x01,  // Usage Page (Generic Desktop)
      0x09,
      0x05,  // Usage (Game Pad)
      0xA1,
      0x01,  // Collection (Application)
      0x06,
      0x00,
      0xFF,  // Usage Page (Vendor Defined)
      0x09,
      0x01,  // Usage (Vendor Defined 1)
      0x15,
      0x00,  // Logical Minimum (0)
      0x26,
      0xFF,
      0x00,  // Logical Maximum (255)
      0x75,
      0x08,  // Report Size (8)
      0x85,
      xbox_gip_input_command,  // Report ID (GIP input command)
      0x95,
      0x18,  // 24 bytes after the report ID
      0x81,
      0x02,  // Input (Data, Variable, Absolute)
      0x85,
      0x03,  // Bluetooth-compatible rumble output
      0x95,
      0x08,
      0x91,
      0x02,  // Output (Data, Variable, Absolute)
      0x85,
      0x09,  // Wired GIP rumble output
      0x95,
      0x0C,
      0x91,
      0x02,
      0xC0,
    };
  }

  inline std::vector<std::uint8_t> xbox_gip_transport_input_report(
    const GamepadState &state,
    std::span<const std::uint8_t> packed,
    bool series
  ) {
    if (packed.size() < 12U) {
      return {};
    }

    using enum GamepadButton;
    const auto pressed = [&state](GamepadButton button, unsigned int bit) {
      return state.buttons.test(button) ? (1U << bit) : 0U;
    };
    std::vector<std::uint8_t> report(25U);
    report[0] = xbox_gip_input_command;
    report[3] = 0x10;  // Sixteen-byte GIP state payload.
    report[4] = static_cast<std::uint8_t>(pressed(start, 2) | pressed(back, 3) | pressed(a, 4) | pressed(b, 5) | pressed(x, 6) | pressed(y, 7));
    report[5] = static_cast<std::uint8_t>(pressed(dpad_up, 0) | pressed(dpad_down, 1) | pressed(dpad_left, 2) | pressed(dpad_right, 3) | pressed(left_shoulder, 4) | pressed(right_shoulder, 5) | pressed(left_stick, 6) | pressed(right_stick, 7));
    std::copy_n(packed.begin() + 8, 4, report.begin() + 6);  // Triggers.
    std::copy_n(packed.begin(), 8, report.begin() + 10);  // Sticks.
    for (const auto index : {12U, 13U, 16U, 17U}) {
      report[index] = static_cast<std::uint8_t>(0xFFU - report[index]);
    }
    for (const auto index : {11U, 13U, 15U, 17U}) {
      report[index] = std::to_integer<std::uint8_t>(std::byte {report[index]} ^ std::byte {0x80});
    }
    report[18] = series && state.buttons.test(misc1) ? 0x01 : 0x00;
    report[20] = 0x07;  // GIP virtual-key command for Guide.
    report[21] = 0x20;  // Internal command.
    report[23] = 0x01;
    report[24] = state.buttons.test(guide) ? 0x01 : 0x00;
    return report;
  }

  inline std::vector<GamepadOutput> xbox_transport_output_reports(
    const DeviceProfile &profile,
    const std::vector<std::uint8_t> &report
  ) {
    if (
      (profile.gamepad_kind == GamepadProfileKind::xbox_one || profile.gamepad_kind == GamepadProfileKind::xbox_series) &&
      report.size() >= 13U && report[0] == 0x09U && report[3] == 0x09U
    ) {
      // The wired GIP rumble payload has a leading reserved byte. Reuse the
      // shared four-motor decoder with its equivalent report-ID-3 layout.
      std::vector<std::uint8_t> normalized {0x03};
      normalized.insert(normalized.end(), report.begin() + 5, report.begin() + 13);
      auto outputs = reports::parse_output_reports(profile, normalized);
      for (auto &output : outputs) {
        output.raw_report = report;
      }
      return outputs;
    }
    return reports::parse_output_reports(profile, report);
  }

  inline bool uses_xbox_transport(const DeviceProfile &profile) {
    if (profile.vendor_id != 0x045E) {
      return false;
    }
    using enum GamepadProfileKind;
    switch (profile.gamepad_kind) {
      case xbox_360:
        return profile.product_id == 0x028E;
      case xbox_one:
        return profile.product_id == 0x02EA;
      case xbox_series:
        return profile.product_id == 0x0B12;
      default:
        return false;
    }
  }

  inline DeviceProfile xbox_transport_profile(const DeviceProfile &requested) {
    if (!uses_xbox_transport(requested)) {
      return requested;
    }

    if (requested.gamepad_kind != GamepadProfileKind::xbox_360) {
      auto transport = requested;
      transport.bus_type = BusType::bluetooth;
      transport.product_id = requested.gamepad_kind == GamepadProfileKind::xbox_series ? 0x0B13 : 0x0B20;
      transport.version = 0x0513;
      transport.report_id = xbox_gip_input_command;
      transport.input_report_size = 25;
      transport.output_report_size = 9;
      transport.report_descriptor = xbox_gip_report_descriptor();
      return transport;
    }

    // macOS Steam maps 045e:028e version 0114 as fifteen numbered buttons,
    // six axes, and a USB device.
    auto transport = profiles::xbox_360();
    transport.name = requested.name;
    transport.report_descriptor = {
      0x05,
      0x01,  // Generic Desktop
      0x09,
      0x05,  // Game Pad
      0xA1,
      0x01,  // Application collection
      0x85,
      0x01,  // Report ID 1
      0x05,
      0x09,  // Button page
      0x19,
      0x01,  // Button 1
      0x29,
      0x10,  // Button 16 (optional miscellaneous input)
      0x15,
      0x00,
      0x25,
      0x01,
      0x75,
      0x01,
      0x95,
      0x10,
      0x81,
      0x02,  // Fifteen mapped buttons and optional Share
      0x05,
      0x01,  // Generic Desktop
      0x15,
      0x00,
      0x26,
      0xFF,
      0x00,
      0x75,
      0x08,
      0x95,
      0x06,
      0x09,
      0x30,  // X
      0x09,
      0x31,  // Y
      0x09,
      0x32,  // Z (left trigger)
      0x09,
      0x33,  // Rx
      0x09,
      0x34,  // Ry
      0x09,
      0x35,  // Rz (right trigger)
      0x81,
      0x02,
      0x06,
      0x00,
      0xFF,  // Vendor-defined rumble output
      0x09,
      0x01,
      0x15,
      0x00,
      0x26,
      0xFF,
      0x00,
      0x75,
      0x08,
      0x95,
      0x04,
      0x91,
      0x02,
      0xC0,
    };
    return transport;
  }

  inline std::vector<std::uint8_t> xbox_transport_input_report(const GamepadState &state) {
    auto report = reports::pack_input_report(profiles::xbox_360(), state);
    if (report.size() != 9U) {
      return {};
    }

    using enum GamepadButton;
    // SDL's macOS mapping for this identity assigns stick clicks to 6/7,
    // Start/Back/Guide to 8/9/10, and the D-pad to buttons 11 through 14.
    const auto button = [&state](GamepadButton value, unsigned int bit) {
      return state.buttons.test(value) ? (1U << bit) : 0U;
    };
    const auto bits = button(a, 0) | button(b, 1) | button(x, 2) | button(y, 3) |
                      button(left_shoulder, 4) | button(right_shoulder, 5) |
                      button(left_stick, 6) | button(right_stick, 7) |
                      button(start, 8) | button(back, 9) | button(guide, 10) |
                      button(dpad_up, 11) | button(dpad_down, 12) |
                      button(dpad_left, 13) | button(dpad_right, 14) |
                      button(misc1, 15);
    report[1] = static_cast<std::uint8_t>(bits & 0xFFU);
    report[2] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
    report[4] = static_cast<std::uint8_t>(0xFFU - report[4]);
    report[7] = static_cast<std::uint8_t>(0xFFU - report[7]);
    return report;
  }

}  // namespace lvh::detail::macos
