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
#include "lvh_windows_protocol.h"

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail::windows {

  inline constexpr std::string_view default_control_device_path {LVH_WINDOWS_CONTROL_DEVICE_PATH};

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
    LvhWindowsCreateGamepadRequest request {};
    request.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.client_device_id = device_id;
    request.bus_type = protocol_bus_type(options.profile.bus_type);
    request.gamepad_kind = protocol_gamepad_kind(options.profile.gamepad_kind);
    request.flags = gamepad_flags(options.profile.capabilities);
    request.hardware_ids.vendor_id = options.profile.vendor_id;
    request.hardware_ids.product_id = options.profile.product_id;
    request.hardware_ids.device_version = options.profile.version;
    request.hardware_ids.report_id = options.profile.report_id;
    request.report_sizes.input_report_size = static_cast<std::uint32_t>(
      std::min(options.profile.input_report_size, static_cast<std::size_t>(LVH_WINDOWS_MAX_INPUT_REPORT_SIZE))
    );
    request.report_sizes.output_report_size = static_cast<std::uint32_t>(
      std::min(options.profile.output_report_size, static_cast<std::size_t>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE))
    );
    request.report_sizes.report_descriptor_size =
      copy_bytes(request.report_descriptor, options.profile.report_descriptor);
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
