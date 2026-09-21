// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/macos/broker/protocol.hpp
 * @brief Private, versioned macOS broker wire protocol.
 */
#pragma once

#include "shared/steam_controller_protocol.hpp"

#include <array>
#include <cstdint>
#include <libvirtualhid/types.hpp>
#include <utility>

namespace lvh::detail::macos_broker {

  inline constexpr std::uint32_t protocol_version = 1;
  inline constexpr auto socket_path = "/var/run/libvirtualhid/broker.sock";
  inline constexpr std::size_t max_descriptor_size = 8192;
  inline constexpr std::size_t max_report_size = 1024;
  inline constexpr std::size_t max_text_size = 128;

  /**
   * @brief Whether the broker recognizes a serialized gamepad profile kind.
   */
  constexpr bool valid_gamepad_kind(std::uint32_t kind) {
    return kind <= static_cast<std::uint32_t>(std::to_underlying(GamepadProfileKind::steam_triton));
  }

  enum class MessageType : std::uint32_t {
    status = 1,
    activate,
    validate,
    deactivate,
    create,
    submit,
    close,
    output,
    response,
  };

  struct Message {  // NOSONAR(cpp:S1820): the fixed-size versioned wire message is deliberately flat.
    std::uint32_t version = protocol_version;
    MessageType type = MessageType::status;
    std::int32_t status = 0;
    std::uint32_t size = 0;
    std::uint32_t kind = 0;
    std::uint32_t bus = 0;
    std::uint32_t vendor_id = 0;
    std::uint32_t product_id = 0;
    std::uint32_t device_version = 0;
    std::uint32_t report_id = 0;
    std::uint32_t input_report_size = 0;
    std::uint32_t output_report_size = 0;
    std::uint32_t descriptor_size = 0;
    std::uint32_t active_devices = 0;
    std::uint32_t activation_limit = 0;
    std::uint32_t activation_usage = 0;
    std::uint32_t license_state = 0;
    std::array<char, max_text_size> name {};
    std::array<char, max_text_size> manufacturer {};
    std::array<char, max_text_size> stable_id {};
    std::array<char, max_text_size> license_key {};
    std::array<char, max_text_size> instance_name {};
    std::array<char, max_text_size> plan_name {};
    std::array<char, max_text_size> customer_email {};
    std::array<char, 256> message {};
    std::array<std::uint8_t, max_descriptor_size> data {};
  };

  /**
   * @brief Validate an input report against the selected broker profile.
   *
   * @param profile The accepted device-creation message.
   * @param request The input report message to validate.
   * @return Whether the report has an accepted type and size.
   */
  inline bool valid_submit_request(const Message &profile, const Message &request) {
    if (request.type != MessageType::submit || request.size > max_report_size) {
      return false;
    }
    if (request.size == profile.input_report_size) {
      return true;
    }
    return profile.kind == static_cast<std::uint32_t>(std::to_underlying(GamepadProfileKind::steam_triton)) &&
           request.size == steam_controller_protocol::battery_report_size &&
           request.data[0] == steam_controller_protocol::battery_report_id;
  }

}  // namespace lvh::detail::macos_broker
