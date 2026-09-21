/**
 * @file src/shared/steam_controller_protocol.hpp
 * @brief Shared Valve Steam Controller (2026) HID protocol helpers.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace lvh::detail::steam_controller_protocol {

  inline constexpr std::uint8_t state_report_id = 0x42;
  inline constexpr std::uint8_t battery_report_id = 0x43;
  inline constexpr std::size_t state_report_size = 54;
  inline constexpr std::size_t battery_report_size = 15;
  inline constexpr std::size_t feature_report_size = 64;
  inline constexpr auto input_interval_us = 4032;

  /**
   * @brief Track the last feature command and synthesize native-shaped replies.
   */
  class FeatureState {
  public:
    bool handle_set_feature(std::uint8_t report_id, std::span<const std::uint8_t> payload) {
      const auto index = feature_index(report_id);
      if (!index.has_value()) {
        return false;
      }

      auto normalized = std::vector<std::uint8_t> {};
      normalized.reserve(std::min(feature_report_size, payload.size() + 1U));
      if (payload.empty() || payload.front() != report_id) {
        normalized.push_back(report_id);
      }
      normalized.insert(
        normalized.end(),
        payload.begin(),
        payload.begin() + std::min(payload.size(), feature_report_size - normalized.size())
      );
      normalized.resize(feature_report_size, 0U);
      requests_[*index] = std::move(normalized);
      return true;
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get_feature_report(std::uint8_t report_id) const {
      const auto index = feature_index(report_id);
      if (!index.has_value()) {
        return std::nullopt;
      }

      std::vector<std::uint8_t> response(feature_report_size, 0U);
      response[0] = report_id;
      if (requests_[*index].size() < 2U) {
        return response;
      }

      const auto command = requests_[*index][1];
      response[1] = command;
      if (command == 0x83U) {
        // GET_ATTRIBUTES_VALUES: product ID, capabilities, and board revision.
        constexpr std::array<std::uint8_t, 15> attributes {
          0x01,
          0x02,
          0x13,
          0x00,
          0x00,
          0x02,
          0x00,
          0x00,
          0x00,
          0x00,
          0x09,
          0x48,
          0x00,
          0x00,
          0x00,
        };
        response[2] = static_cast<std::uint8_t>(attributes.size());
        std::ranges::copy(attributes, response.begin() + 3U);
      } else if (command == 0xAEU) {
        // GET_STRING_ATTRIBUTE: echo the requested tag followed by a stable
        // controller identifier in the protocol's 20-byte value field.
        constexpr auto identifier = std::string_view {"LVH000000000001"};
        response[2] = 21U;
        response[3] = requests_[*index].size() > 3U ? requests_[*index][3] : 1U;
        std::ranges::copy(identifier, response.begin() + 4U);
      }
      return response;
    }

  private:
    static std::optional<std::size_t> feature_index(std::uint8_t report_id) {
      if (report_id == 1U || report_id == 2U) {
        return static_cast<std::size_t>(report_id - 1U);
      }
      return std::nullopt;
    }

    std::array<std::vector<std::uint8_t>, 2> requests_;
  };

}  // namespace lvh::detail::steam_controller_protocol
