// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/shared/lvh_windows_github_actions_evaluation.hpp
 * @brief Time-window helpers for the GitHub Actions gamepad evaluation exception.
 */
#pragma once

// standard includes
#include <chrono>

namespace lvh::windows::github_actions_evaluation {

  using Clock = std::chrono::system_clock;

  /**
   * @brief Maximum unlicensed gamepad evaluation window on GitHub-hosted CI.
   */
  inline constexpr auto duration = std::chrono::minutes {5};

  /**
   * @brief Check whether an evaluation window is active.
   *
   * A clock earlier than the persisted start is treated as expired so rolling
   * the system clock backward cannot extend the window.
   *
   * @param started_at Persisted start of the evaluation window.
   * @param now Current wall-clock time.
   * @return `true` from the start instant until, but not including, its deadline.
   */
  constexpr bool active(Clock::time_point started_at, Clock::time_point now) noexcept {
    return now >= started_at && now < started_at + duration;
  }

  /**
   * @brief Calculate display seconds remaining in an evaluation window.
   *
   * @param started_at Persisted start of the evaluation window.
   * @param now Current wall-clock time.
   * @return Remaining seconds rounded up, or zero when the window is inactive.
   */
  constexpr std::chrono::seconds remaining(
    Clock::time_point started_at,
    Clock::time_point now
  ) noexcept {
    if (!active(started_at, now)) {
      return std::chrono::seconds::zero();
    }
    return std::chrono::ceil<std::chrono::seconds>(started_at + duration - now);
  }

}  // namespace lvh::windows::github_actions_evaluation
