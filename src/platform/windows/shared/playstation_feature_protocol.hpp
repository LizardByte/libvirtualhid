/**
 * @file src/platform/windows/shared/playstation_feature_protocol.hpp
 * @brief PlayStation feature-report responses used by the Windows VHF driver.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

// local includes
#include "lvh_windows_protocol.h"
#include "shared/playstation_feature_reports.hpp"

namespace lvh::detail::windows {

  namespace playstation_feature_protocol_detail {

    using namespace playstation_feature_reports;

    inline std::uint32_t crc32(std::span<const std::uint8_t> buffer, std::uint32_t seed = 0) {
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

    inline void write_u32_le(std::span<std::uint8_t> buffer, std::size_t offset, std::uint32_t value) {
      buffer[offset] = static_cast<std::uint8_t>(value & 0xFFU);
      buffer[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
      buffer[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
      buffer[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }

    inline std::optional<std::array<std::uint8_t, 6>> parse_mac_address(std::string_view text) {
      std::array<std::uint8_t, 6> mac {};
      for (std::size_t index = 0; index < mac.size(); ++index) {
        if (text.size() < 2U) {
          return std::nullopt;
        }

        unsigned int value = 0;
        const auto result = std::from_chars(text.data(), text.data() + 2, value, 16);
        if (result.ec != std::errc {} || result.ptr != text.data() + 2 || value > 0xFFU) {
          return std::nullopt;
        }
        mac[index] = static_cast<std::uint8_t>(value);
        text.remove_prefix(2U);
        if (index + 1U < mac.size()) {
          if (text.empty() || text.front() != ':') {
            return std::nullopt;
          }
          text.remove_prefix(1U);
        }
      }
      return mac;
    }

    inline std::array<std::uint8_t, 6> generated_mac_address(std::uint64_t client_device_id) {
      return {
        0x02,
        0x00,
        static_cast<std::uint8_t>((client_device_id >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((client_device_id >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((client_device_id >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(client_device_id & 0xFFU),
      };
    }

    inline std::array<std::uint8_t, 6> request_mac_address(const LvhWindowsCreateGamepadRequest &request) {
      const auto size = std::min<std::size_t>(request.report_sizes.stable_id_size, sizeof(request.stable_id));
      const auto stable_id = std::string_view {request.stable_id, size};
      return parse_mac_address(stable_id).value_or(generated_mac_address(request.client_device_id));
    }

    inline std::vector<std::uint8_t> copy_payload(std::span<const std::uint8_t> payload) {
      return {payload.begin(), payload.end()};
    }

    inline void set_pairing_mac(std::vector<std::uint8_t> &report, const LvhWindowsCreateGamepadRequest &request) {
      const auto mac = request_mac_address(request);
      std::copy(mac.rbegin(), mac.rend(), report.begin() + 1);
    }

    inline void set_bluetooth_crc(std::vector<std::uint8_t> &report) {
      if (report.size() < 4U) {
        return;
      }
      const auto crc_offset = report.size() - 4U;
      const auto seed = crc32(std::span {&playstation_feature_crc_seed, 1U});
      write_u32_le(report, crc_offset, crc32(std::span {report.data(), crc_offset}, seed));
    }

  }  // namespace playstation_feature_protocol_detail

  inline bool is_playstation_gamepad(std::uint32_t gamepad_kind) {
    return gamepad_kind == LVH_WINDOWS_GAMEPAD_DUALSHOCK4 || gamepad_kind == LVH_WINDOWS_GAMEPAD_DUALSENSE;
  }

  inline std::optional<std::vector<std::uint8_t>> make_playstation_feature_report(
    const LvhWindowsCreateGamepadRequest &request,
    std::uint8_t report_number
  ) {
    using namespace playstation_feature_protocol_detail;

    auto report = std::vector<std::uint8_t> {};
    if (request.gamepad_kind == LVH_WINDOWS_GAMEPAD_DUALSHOCK4) {
      switch (report_number) {
        case dualshock4_usb_calibration_report:
          report = copy_payload(dualshock4_usb_calibration_info);
          break;
        case dualshock4_bluetooth_calibration_report:
          if (request.bus_type != LVH_WINDOWS_BUS_BLUETOOTH) {
            return std::nullopt;
          }
          report = copy_payload(dualshock4_bluetooth_calibration_info);
          break;
        case dualshock4_pairing_report:
          report = copy_payload(dualshock4_pairing_info);
          set_pairing_mac(report, request);
          break;
        case dualshock4_firmware_report:
          report = copy_payload(dualshock4_firmware_info);
          break;
        default:
          return std::nullopt;
      }
    } else if (request.gamepad_kind == LVH_WINDOWS_GAMEPAD_DUALSENSE) {
      switch (report_number) {
        case dualsense_calibration_report:
          report = copy_payload(dualsense_calibration_info);
          break;
        case dualsense_pairing_report:
          report = copy_payload(dualsense_pairing_info);
          set_pairing_mac(report, request);
          break;
        case dualsense_firmware_report:
          report = copy_payload(dualsense_firmware_info);
          break;
        default:
          return std::nullopt;
      }
    } else {
      return std::nullopt;
    }

    if (request.bus_type == LVH_WINDOWS_BUS_BLUETOOTH) {
      set_bluetooth_crc(report);
    }
    return report;
  }

}  // namespace lvh::detail::windows
