// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/shared/vhf_input_report_queue.hpp
 * @brief Bounded Windows VHF input-report coalescing.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <optional>
#include <utility>
#include <vector>

// local includes
#include "lvh_windows_protocol.h"

namespace lvh::detail::windows {

  /**
   * @brief Maximum number of discrete input-state transitions retained while VHF is not ready.
   */
  inline constexpr std::size_t vhf_max_pending_input_reports = 32U;

  /**
   * @brief A bounded queue that replaces superseded continuous gamepad states.
   *
   * Axis, trigger, motion, battery, and touch-position changes can safely replace
   * the newest pending report when the report's discrete state is unchanged.
   * Button, D-pad, trigger-threshold, report-ID, and touch-contact lifecycle
   * transitions remain ordered so short presses and contacts are not silently
   * coalesced away.
   */
  class VhfInputReportQueue {
  public:
    VhfInputReportQueue(std::uint32_t gamepad_kind, std::uint32_t bus_type, std::uint8_t input_report_id):
        gamepad_kind_ {gamepad_kind},
        bus_type_ {bus_type},
        input_report_id_ {input_report_id} {
    }

    void push(std::vector<std::uint8_t> report) {
      if (is_protocol_report(report)) {
        make_room();
        protocol_reports_.push_back(std::move(report));
        return;
      }

      if (!reports_.empty() && same_discrete_state(reports_.back(), report)) {
        reports_.back() = std::move(report);
        return;
      }

      make_room();
      reports_.push_back(std::move(report));
    }

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> pop() {
      if (!protocol_reports_.empty()) {
        auto report = std::move(protocol_reports_.front());
        protocol_reports_.pop_front();
        return report;
      }
      if (reports_.empty()) {
        return std::nullopt;
      }

      auto report = std::move(reports_.front());
      reports_.pop_front();
      return report;
    }

    void clear() {
      protocol_reports_.clear();
      reports_.clear();
    }

    [[nodiscard]] bool empty() const noexcept {
      return protocol_reports_.empty() && reports_.empty();
    }

    [[nodiscard]] std::size_t size() const noexcept {
      return protocol_reports_.size() + reports_.size();
    }

  private:
    [[nodiscard]] bool is_protocol_report(const std::vector<std::uint8_t> &report) const noexcept {
      return input_report_id_ != 0U && !report.empty() && report.front() != input_report_id_;
    }

    void make_room() {
      if (size() < vhf_max_pending_input_reports) {
        return;
      }
      if (!reports_.empty()) {
        reports_.pop_front();
      } else {
        protocol_reports_.pop_front();
      }
    }

    static bool equal_at(
      const std::vector<std::uint8_t> &left,
      const std::vector<std::uint8_t> &right,
      std::initializer_list<std::size_t> offsets
    ) {
      return std::ranges::all_of(offsets, [&](const auto offset) {
        return offset < left.size() && offset < right.size() && left[offset] == right[offset];
      });
    }

    [[nodiscard]] bool same_discrete_state(
      const std::vector<std::uint8_t> &left,
      const std::vector<std::uint8_t> &right
    ) const {
      if (left.size() != right.size()) {
        return false;
      }
      if (
        input_report_id_ != 0U &&
        (left.empty() || left.front() != input_report_id_ || right.front() != input_report_id_)
      ) {
        return false;
      }

      if (gamepad_kind_ == LVH_WINDOWS_GAMEPAD_GENERIC) {
        return equal_at(left, right, {0U, 1U, 2U});
      }
      if (gamepad_kind_ == LVH_WINDOWS_GAMEPAD_XBOX_ONE || gamepad_kind_ == LVH_WINDOWS_GAMEPAD_XBOX_SERIES) {
        return equal_at(left, right, {12U, 13U, 14U, 15U});
      }
      if (gamepad_kind_ == LVH_WINDOWS_GAMEPAD_SWITCH_PRO) {
        return equal_at(left, right, {0U, 3U, 4U, 5U});
      }

      const auto is_bluetooth = bus_type_ == LVH_WINDOWS_BUS_BLUETOOTH;
      if (gamepad_kind_ == LVH_WINDOWS_GAMEPAD_DUALSHOCK4) {
        const auto payload_offset = is_bluetooth ? 3U : 1U;
        return equal_at(
          left,
          right,
          {
            0U,
            payload_offset + 4U,
            payload_offset + 5U,
            payload_offset + 6U,
            payload_offset + 34U,
            payload_offset + 38U,
          }
        );
      }
      if (gamepad_kind_ == LVH_WINDOWS_GAMEPAD_DUALSENSE) {
        const auto payload_offset = is_bluetooth ? 2U : 1U;
        return equal_at(
          left,
          right,
          {
            0U,
            payload_offset + 7U,
            payload_offset + 8U,
            payload_offset + 9U,
            payload_offset + 32U,
            payload_offset + 36U,
          }
        );
      }

      return false;
    }

    std::uint32_t gamepad_kind_;
    std::uint32_t bus_type_;
    std::uint8_t input_report_id_;
    std::deque<std::vector<std::uint8_t>> protocol_reports_;
    std::deque<std::vector<std::uint8_t>> reports_;
  };

}  // namespace lvh::detail::windows
