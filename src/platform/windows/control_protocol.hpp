/**
 * @file src/platform/windows/control_protocol.hpp
 * @brief C++ helpers for the Windows UMDF control protocol.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

// driver includes
#include "generic_pid_protocol.hpp"
#include "lvh_windows_protocol.h"

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail::windows {

  inline constexpr std::string_view default_control_device_path {LVH_WINDOWS_CONTROL_DEVICE_PATH};
  inline constexpr std::string_view global_control_device_path {LVH_WINDOWS_GLOBAL_CONTROL_DEVICE_PATH};
  inline constexpr std::uint16_t xbox_series_bluetooth_product_id = 0x0B13;
  inline constexpr std::uint8_t xbox_series_bluetooth_input_report_id = 0x01;
  inline constexpr std::size_t xbox_series_bluetooth_input_report_size = 17;
  inline constexpr std::size_t xbox_series_bluetooth_output_report_size = 9;

  /**
   * @brief Return the native Xbox Wireless Controller BLE report descriptor used by Windows.
   *
   * The inbox XInputHID path does not surface the Series Share button. The
   * Bluetooth HID identity exposes Share as the Consumer Record usage in the
   * final byte of report ID 1. Report ID 3 is the eight-byte rumble output.
   *
   * @return Xbox Series Bluetooth HID report descriptor.
   */
  inline std::vector<std::uint8_t> xbox_series_bluetooth_report_descriptor() {
    std::vector<std::uint8_t> descriptor;
    generic_pid_detail::append_hex(
      descriptor,
      "05010905A10185010901A10009300931150027FFFF0000950275108102C00901A10009330934150027FFFF0000950275108102C005010932150026FF"
      "039501750A81021500250075069501810305010935150026FF039501750A81021500250075069501810305010939150125083500463B016614007504"
      "950181427504950115002500350045006500810305091901290C150025017501950C810215002500750195048103050C0A2402150025019501750181"
      "0215002500750795018103050C09018502A101050C0A23021500250195017501810215002500750795018103C0050F09218503A10209971500250175"
      "0495019102150025007504950191030970150025647508950491020950660110550E150026FF0075089501910209A7150026FF007508950191026500"
      "5500097C150026FF00750895019102C0050609208504150026FF00750895018102C0"
    );
    return descriptor;
  }

  /**
   * @brief Convert the platform-neutral Xbox Series report to Microsoft's BLE packet layout.
   *
   * @param report Packed 17-byte XboxGIP-shaped input report.
   * @return Share-capable Xbox Series Bluetooth input report.
   */
  inline std::vector<std::uint8_t> make_xbox_series_windows_input_report(
    const std::vector<std::uint8_t> &report
  ) {
    if (report.size() < xbox_series_bluetooth_input_report_size) {
      return report;
    }

    std::vector<std::uint8_t> windows_report(xbox_series_bluetooth_input_report_size, 0);
    windows_report[0] = xbox_series_bluetooth_input_report_id;
    std::ranges::copy_n(report.begin(), 12U, windows_report.begin() + 1);
    windows_report[13] = report[14];

    const auto buttons = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(report[12]) |
      static_cast<std::uint16_t>(static_cast<std::uint16_t>(report[13]) << 8U)
    );
    const auto copy_button = [buttons, &windows_report](std::uint16_t source_mask, std::size_t target_bit) {
      if ((buttons & source_mask) != 0U) {
        windows_report[14U + (target_bit / 8U)] |= static_cast<std::uint8_t>(1U << (target_bit % 8U));
      }
    };

    copy_button(0x0001U, 0U);  // A
    copy_button(0x0002U, 1U);  // B
    copy_button(0x0004U, 2U);  // X
    copy_button(0x0008U, 3U);  // Y
    copy_button(0x0010U, 4U);  // LB
    copy_button(0x0020U, 5U);  // RB
    copy_button(0x0040U, 6U);  // Back/View
    copy_button(0x0080U, 7U);  // Start/Menu
    copy_button(0x0100U, 8U);  // L3
    copy_button(0x0200U, 9U);  // R3
    if (report[15] != 0U) {
      windows_report[15] |= 0x04U;  // Guide
    }
    if ((buttons & 0x0800U) != 0U) {
      windows_report[16] |= 0x01U;  // Share / Consumer Record
    }
    return windows_report;
  }

  inline std::uint32_t gamepad_flags(const GamepadProfileCapabilities &capabilities) {
    std::uint32_t flags = 0;
    if (capabilities.supports_rumble) {
      flags |= LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE;
    }
    if (capabilities.supports_motion) {
      flags |= LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION;
    }
    if (capabilities.supports_touchpad) {
      flags |= LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD;
    }
    if (capabilities.supports_rgb_led) {
      flags |= LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED;
    }
    if (capabilities.supports_battery) {
      flags |= LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY;
    }
    if (capabilities.supports_adaptive_triggers) {
      flags |= LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS;
    }

    return flags;
  }

  inline std::uint32_t protocol_bus_type(BusType bus_type) {
    switch (bus_type) {
      using enum BusType;

      case usb:
        return LVH_WINDOWS_BUS_USB;
      case bluetooth:
        return LVH_WINDOWS_BUS_BLUETOOTH;
      case unknown:
        return LVH_WINDOWS_BUS_UNKNOWN;
    }

    return LVH_WINDOWS_BUS_UNKNOWN;
  }

  inline std::uint32_t protocol_gamepad_kind(GamepadProfileKind kind) {
    switch (kind) {
      using enum GamepadProfileKind;

      case generic:
        return LVH_WINDOWS_GAMEPAD_GENERIC;
      case xbox_360:
        return LVH_WINDOWS_GAMEPAD_XBOX_360;
      case xbox_one:
        return LVH_WINDOWS_GAMEPAD_XBOX_ONE;
      case xbox_series:
        return LVH_WINDOWS_GAMEPAD_XBOX_SERIES;
      case dualshock4:
        return LVH_WINDOWS_GAMEPAD_DUALSHOCK4;
      case dualsense:
        return LVH_WINDOWS_GAMEPAD_DUALSENSE;
      case switch_pro:
        return LVH_WINDOWS_GAMEPAD_SWITCH_PRO;
    }

    return LVH_WINDOWS_GAMEPAD_GENERIC;
  }

  template<std::size_t Size>
  std::uint32_t copy_string(char (&target)[Size], std::string_view source) {
    std::ranges::fill(target, '\0');

    const auto copied = std::min(source.size(), Size - 1U);
    if (copied > 0U) {
      std::memcpy(target, source.data(), copied);
    }

    return static_cast<std::uint32_t>(copied);
  }

  template<std::size_t Size>
  std::uint32_t copy_bytes(std::uint8_t (&target)[Size], const std::vector<std::uint8_t> &source) {
    std::ranges::fill(target, std::uint8_t {});

    const auto copied = std::min(source.size(), Size);
    if (copied > 0U) {
      std::memcpy(target, source.data(), copied);
    }

    return static_cast<std::uint32_t>(copied);
  }

  inline LvhWindowsCreateGamepadRequest make_create_gamepad_request(
    DeviceId device_id,
    const CreateGamepadOptions &options
  ) {
    auto report_descriptor = options.profile.report_descriptor;
    auto input_report_size = options.profile.input_report_size;
    auto output_report_size = options.profile.output_report_size;
    auto bus_type = options.profile.bus_type;
    auto product_id = options.profile.product_id;
    auto report_id = options.profile.report_id;
    if (
      options.profile.gamepad_kind == GamepadProfileKind::generic &&
      options.profile.capabilities.supports_rumble
    ) {
      auto pid_descriptor = make_generic_pid_report_descriptor(report_descriptor);
      if (!pid_descriptor.empty()) {
        report_descriptor = std::move(pid_descriptor);
        output_report_size = generic_pid_output_report_size;
      }
    }
    if (options.profile.gamepad_kind == GamepadProfileKind::xbox_series) {
      report_descriptor = xbox_series_bluetooth_report_descriptor();
      input_report_size = xbox_series_bluetooth_input_report_size;
      output_report_size = xbox_series_bluetooth_output_report_size;
      bus_type = BusType::bluetooth;
      product_id = xbox_series_bluetooth_product_id;
      report_id = xbox_series_bluetooth_input_report_id;
    }
    LvhWindowsCreateGamepadRequest request {};
    request.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.client_device_id = device_id;
    request.bus_type = protocol_bus_type(bus_type);
    request.gamepad_kind = protocol_gamepad_kind(options.profile.gamepad_kind);
    request.flags = gamepad_flags(options.profile.capabilities);
    request.hardware_ids.vendor_id = options.profile.vendor_id;
    request.hardware_ids.product_id = product_id;
    request.hardware_ids.device_version = options.profile.version;
    request.hardware_ids.report_id = report_id;
    request.report_sizes.input_report_size = static_cast<std::uint32_t>(
      std::min(input_report_size, static_cast<std::size_t>(LVH_WINDOWS_MAX_INPUT_REPORT_SIZE))
    );
    request.report_sizes.output_report_size = static_cast<std::uint32_t>(
      std::min(output_report_size, static_cast<std::size_t>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE))
    );
    request.report_sizes.report_descriptor_size =
      copy_bytes(request.report_descriptor, report_descriptor);
    request.report_sizes.name_size = copy_string(request.name, options.profile.name);
    request.report_sizes.manufacturer_size = copy_string(request.manufacturer, options.profile.manufacturer);
    request.report_sizes.stable_id_size = copy_string(request.stable_id, options.metadata.stable_id);

    return request;
  }

  inline LvhWindowsDestroyDeviceRequest make_destroy_device_request(std::uint64_t driver_device_id) {
    LvhWindowsDestroyDeviceRequest request {};
    request.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.driver_device_id = driver_device_id;

    return request;
  }

  inline LvhWindowsSubmitInputReportRequest make_submit_input_report_request(
    std::uint64_t driver_device_id,
    const std::vector<std::uint8_t> &report
  ) {
    LvhWindowsSubmitInputReportRequest request {};
    request.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.driver_device_id = driver_device_id;
    request.report_size = copy_bytes(request.report, report);

    return request;
  }

}  // namespace lvh::detail::windows
