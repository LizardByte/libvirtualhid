// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/broker/broker_request_validation.hpp
 * @brief Validation helpers for untrusted Windows broker protocol messages.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string_view>
#include <utility>

// local includes
#include "lvh_windows_broker_protocol.h"
#include "mouse_protocol.hpp"

namespace lvh::windows::broker_validation {

  inline constexpr std::uint32_t known_gamepad_flags =
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS;

  template<typename Value, std::size_t Size>
  bool all_zero(const std::array<Value, Size> &values) {
    return std::ranges::all_of(values, [](const auto value) {
      return value == Value {};
    });
  }

  template<std::size_t Size>
  bool valid_c_string(const std::array<char, Size> &value, bool allow_empty = true) {
    const auto terminator = std::ranges::find(value, '\0');
    if (terminator == std::end(value) || (!allow_empty && terminator == std::begin(value))) {
      return false;
    }

    return std::ranges::all_of(std::next(terminator), std::end(value), [](const auto character) {
      return character == '\0';
    });
  }

  template<std::size_t Size>
  bool valid_sized_c_string(const std::array<char, Size> &value, std::uint32_t size) {
    if (size >= Size || value[size] != '\0') {
      return false;
    }

    const auto content_end = std::begin(value) + size;
    if (std::ranges::find(std::begin(value), content_end, '\0') != content_end) {
      return false;
    }

    return std::ranges::all_of(content_end, std::end(value), [](const auto character) {
      return character == '\0';
    });
  }

  inline bool valid_header(
    const LvhWindowsBrokerRequestHeader &header,
    LvhWindowsBrokerRequestType expected_type,
    std::uint32_t expected_size
  ) {
    return header.version == LVH_WINDOWS_BROKER_PROTOCOL_VERSION &&
           header.size == expected_size &&
           header.type == std::to_underlying(expected_type) &&
           header.reserved0 == 0U;
  }

  inline bool valid_device_request(const LvhWindowsCreateDeviceRequest &request) {
    const auto &sizes = request.report_sizes;
    const auto known_device = request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD ||
                              request.device_type == LVH_WINDOWS_DEVICE_MOUSE;
    const auto known_bus = request.bus_type == LVH_WINDOWS_BUS_UNKNOWN ||
                           request.bus_type == LVH_WINDOWS_BUS_USB ||
                           request.bus_type == LVH_WINDOWS_BUS_BLUETOOTH;
    const auto known_profile = request.gamepad_kind <= LVH_WINDOWS_GAMEPAD_DUALSHOCK4;
    const auto valid_mouse_descriptor =
      request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD ||
      (sizes.report_descriptor_size == lvh::detail::windows::mouse_report_descriptor.size() &&
       std::equal(
         lvh::detail::windows::mouse_report_descriptor.begin(),
         lvh::detail::windows::mouse_report_descriptor.end(),
         request.report_descriptor.begin()
       ));
    const auto valid_device_fields = request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD ||
                                     (request.gamepad_kind == LVH_WINDOWS_GAMEPAD_GENERIC &&
                                      request.flags == 0U &&
                                      request.hardware_ids.report_id == 0U &&
                                      sizes.input_report_size == LVH_WINDOWS_MOUSE_INPUT_REPORT_SIZE &&
                                      sizes.output_report_size == 0U);

    return request.version == LVH_WINDOWS_CONTROL_PROTOCOL_VERSION &&
           request.size == sizeof(request) &&
           request.client_device_id != 0U &&
           known_device &&
           known_bus &&
           known_profile &&
           valid_device_fields &&
           valid_mouse_descriptor &&
           (request.flags & ~known_gamepad_flags) == 0U &&
           all_zero(request.hardware_ids.reserved0) &&
           sizes.input_report_size > 0U &&
           sizes.input_report_size <= LVH_WINDOWS_MAX_INPUT_REPORT_SIZE &&
           sizes.output_report_size <= LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE &&
           sizes.report_descriptor_size > 0U &&
           sizes.report_descriptor_size <= LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE &&
           valid_sized_c_string(request.name, sizes.name_size) &&
           valid_sized_c_string(request.manufacturer, sizes.manufacturer_size) &&
           valid_sized_c_string(request.stable_id, sizes.stable_id_size) &&
           std::ranges::all_of(
             std::begin(request.report_descriptor) + sizes.report_descriptor_size,
             std::end(request.report_descriptor),
             [](const auto value) {
               return value == 0U;
             }
           );
  }

  inline bool valid_destroy_request(const LvhWindowsDestroyDeviceRequest &request) {
    return request.version == LVH_WINDOWS_CONTROL_PROTOCOL_VERSION &&
           request.size == sizeof(request) &&
           request.driver_device_id != 0U &&
           !all_zero(request.session_token.bytes);
  }

  inline bool valid_request(const LvhWindowsBrokerStatusRequest &request) {
    return valid_header(
      request.header,
      LvhWindowsBrokerRequestType::status,
      sizeof(request)
    );
  }

  inline bool valid_request(const LvhWindowsBrokerCreateDeviceRequest &request) {
    return valid_header(
             request.header,
             LvhWindowsBrokerRequestType::create_device,
             sizeof(request)
           ) &&
           request.client_control_handle != 0U &&
           valid_device_request(request.device);
  }

  inline bool valid_request(const LvhWindowsBrokerDestroyDeviceRequest &request) {
    return valid_header(
             request.header,
             LvhWindowsBrokerRequestType::destroy_device,
             sizeof(request)
           ) &&
           valid_destroy_request(request.device);
  }

  inline bool valid_request(
    const LvhWindowsBrokerLicenseRequest &request,
    LvhWindowsBrokerRequestType expected_type
  ) {
    using enum LvhWindowsBrokerRequestType;

    if (!valid_header(request.header, expected_type, sizeof(request))) {
      return false;
    }

    switch (expected_type) {
      case activate_license:
        return valid_c_string(request.license_key, false) &&
               valid_c_string(request.instance_name);
      case validate_license:
      case deactivate_license:
        return all_zero(request.license_key) && all_zero(request.instance_name);
      case status:
      case create_device:
      case destroy_device:
        return false;
    }

    return false;
  }

}  // namespace lvh::windows::broker_validation
