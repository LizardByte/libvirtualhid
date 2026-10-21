// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/shared/vhf_report_response.hpp
 * @brief Copy HID responses into VHF-owned report buffers.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace lvh::detail::windows {

  /**
   * @brief Copy only the returned report bytes into a VHF callback buffer.
   *
   * Windows 10 VHF can advertise a reportBufferLen greater than its own
   * allocation. Do not clear or pad to that advertised length.
   * The response must fit the report descriptor used to create the device.
   *
   * @param buffer VHF-owned buffer with room for the response and optional ID byte.
   * @param reported_length Length advertised by VHF, used to reject short requests.
   * @param report Complete report bytes, including the ID for a numbered report.
   * @param prepend_zero_id Whether to prepend the zero ID of an unnumbered report.
   * @return True if the response fits the advertised length; false without writing otherwise.
   */
  [[nodiscard]] inline bool copy_vhf_report_response(
    std::uint8_t *buffer,
    std::size_t reported_length,
    std::span<const std::uint8_t> report,
    bool prepend_zero_id = false
  ) {
    const auto leading_report_id_size = prepend_zero_id ? 1U : 0U;
    if (report.size() > reported_length || leading_report_id_size > reported_length - report.size()) {
      return false;
    }

    if (prepend_zero_id) {
      buffer[0] = 0;
    }
    std::ranges::copy(report, buffer + leading_report_id_size);
    return true;
  }

}  // namespace lvh::detail::windows
