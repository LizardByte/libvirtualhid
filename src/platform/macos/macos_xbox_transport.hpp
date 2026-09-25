/**
 * @file src/platform/macos/macos_xbox_transport.hpp
 * @brief Steam-compatible macOS HID transport for Xbox gamepad profiles.
 */
#pragma once

#include "platform/macos/macos_xbox_bluetooth_descriptor.hpp"

#include <algorithm>
#include <cstdint>
#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>
#include <span>
#include <vector>

namespace lvh::detail::macos {

  inline bool uses_xbox_transport(const DeviceProfile &profile) {
    if (profile.vendor_id != 0x045E) {
      return false;
    }
    switch (profile.gamepad_kind) {
      case GamepadProfileKind::xbox_360:
        return profile.product_id == 0x028E;
      case GamepadProfileKind::xbox_one:
        return profile.product_id == 0x02EA;
      case GamepadProfileKind::xbox_series:
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
      transport.report_id = xbox_bluetooth_input_report_id;
      transport.input_report_size = 17;
      transport.output_report_size = 9;
      transport.report_descriptor = make_xbox_bluetooth_report_descriptor(true);
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
    return report;
  }

  inline std::vector<std::uint8_t> xbox_bluetooth_input_report(
    const GamepadState &state,
    std::span<const std::uint8_t> packed_report,
    bool include_share_button
  ) {
    if (packed_report.size() < 17U) {
      return {};
    }

    using enum GamepadButton;
    std::vector<std::uint8_t> report(17U, 0);
    report[0] = xbox_bluetooth_input_report_id;
    std::copy_n(packed_report.begin(), 8U, report.begin() + 1U);
    std::copy_n(packed_report.begin() + 8U, 4U, report.begin() + 9U);
    report[13] = packed_report[14];
    report[14] = static_cast<std::uint8_t>(
      (state.buttons.test(a) ? 0x01U : 0U) | (state.buttons.test(b) ? 0x02U : 0U) |
      (state.buttons.test(x) ? 0x08U : 0U) | (state.buttons.test(y) ? 0x10U : 0U) |
      (state.buttons.test(left_shoulder) ? 0x40U : 0U) | (state.buttons.test(right_shoulder) ? 0x80U : 0U)
    );
    report[15] = static_cast<std::uint8_t>(
      (state.buttons.test(back) ? 0x04U : 0U) | (state.buttons.test(start) ? 0x08U : 0U) |
      (state.buttons.test(guide) ? 0x10U : 0U) | (state.buttons.test(left_stick) ? 0x20U : 0U) |
      (state.buttons.test(right_stick) ? 0x40U : 0U)
    );
    if (include_share_button && state.buttons.test(misc1)) {
      report[16] = 0x01;
    }
    return report;
  }

}  // namespace lvh::detail::macos
