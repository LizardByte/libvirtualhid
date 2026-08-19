// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/shared/steam_deck_feature_reports.hpp
 * @brief Valve Steam Deck feature-report state shared by descriptor-driven backends.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace lvh::detail {

  inline constexpr std::size_t steam_deck_input_report_size = 64U;

  inline constexpr std::size_t steam_deck_feature_report_size = 64U;

  inline constexpr std::size_t steam_deck_packet_number_offset = 4U;

  /**
   * @brief Stamp a Valve-native Steam Deck input report with its sequence.
   * @param report Mutable native input-report bytes.
   * @param packet_number Packet sequence value.
   * @return Whether the report was large enough to stamp.
   */
  inline bool stamp_steam_deck_packet_number(
    std::span<std::uint8_t> report,
    std::uint32_t packet_number
  ) {
    if (report.size() < steam_deck_packet_number_offset + sizeof(packet_number)) {
      return false;
    }

    for (std::size_t index = 0; index < sizeof(packet_number); ++index) {
      report[steam_deck_packet_number_offset + index] =
        static_cast<std::uint8_t>((packet_number >> (index * 8U)) & 0xFFU);
    }
    return true;
  }

  /**
   * @brief Create a native neutral report for immediate consumer discovery.
   *
   * SDL probes a newly arrived Steam Deck endpoint by waiting only 16 ms for
   * its first state packet. Backends seed their periodic state with this
   * packet so an already-running consumer can complete that probe immediately.
   */
  inline std::vector<std::uint8_t> make_steam_deck_neutral_input_report() {
    auto report = std::vector<std::uint8_t>(steam_deck_input_report_size, 0U);
    report[0] = 0x01U;  // Valve input report version 1 (little endian).
    report[2] = 0x09U;  // ID_CONTROLLER_DECK_STATE.
    report[3] = static_cast<std::uint8_t>(steam_deck_input_report_size);
    return report;
  }

  /**
   * @brief Minimal native Steam Deck feature-report responder.
   *
   * Steam and SDL send Valve feature commands to disable the controller's
   * desktop mappings before consuming native state reports. A virtual device
   * has no lizard mode to change, but it must accept those commands and return
   * a feature payload. The Linux hid-steam driver additionally requests a
   * stable unit serial number during registration.
   */
  class SteamDeckFeatureReportState {
  public:
    explicit SteamDeckFeatureReportState(std::string_view serial = "libvirtualhid") {
      set_serial(serial);
    }

    void set_serial(std::string_view serial) {
      constexpr std::size_t maximum_serial_size = 20U;
      serial_size_ = std::min(serial.size(), maximum_serial_size);
      std::ranges::fill(serial_, '\0');
      std::copy_n(serial.begin(), serial_size_, serial_.begin());
    }

    bool handle_set_feature(std::span<const std::uint8_t> report) {
      constexpr std::uint8_t get_string_attribute = 0xAE;
      constexpr std::uint8_t unit_serial_attribute = 0x01;

      if (report.size() > 1U && report[0] == 0U && report[1] >= 0x80U) {
        report = report.subspan(1U);
      }
      if (report.empty()) {
        return false;
      }

      reply_.fill(0U);
      std::copy_n(report.begin(), std::min(report.size(), reply_.size()), reply_.begin());
      if (report[0] == get_string_attribute && report.size() >= 3U && report[2] == unit_serial_attribute) {
        reply_.fill(0U);
        reply_[0] = get_string_attribute;
        reply_[1] = static_cast<std::uint8_t>(serial_size_ + 1U);
        reply_[2] = unit_serial_attribute;
        std::copy_n(serial_.begin(), serial_size_, reply_.begin() + 3U);
      }

      return true;
    }

    [[nodiscard]] std::vector<std::uint8_t> get_feature_report() const {
      // HID feature APIs carry the unnumbered report ID as a leading zero,
      // followed by the controller's 64-byte Valve payload.
      auto report = std::vector<std::uint8_t>(steam_deck_feature_report_size + 1U, 0U);
      std::copy(reply_.begin(), reply_.end(), report.begin() + 1U);
      return report;
    }

  private:
    std::array<char, 21> serial_ {};
    std::size_t serial_size_ {};
    std::array<std::uint8_t, steam_deck_feature_report_size> reply_ {};
  };

}  // namespace lvh::detail
