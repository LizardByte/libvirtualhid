// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/shared/lvh_broker_license_policy.hpp
 * @brief Platform-neutral Polar benefits and broker license time limits.
 */
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace lvh::broker_license {

  struct PolarBenefit {
    std::string_view id;
    std::string_view plan_name;
    bool subscription_backed;
  };

  inline constexpr std::string_view polar_organization_id = "3db9f05a-44d7-42f1-ba7c-a0f198235fb7";
  inline constexpr auto allowed_benefits = std::array {
    PolarBenefit {.id = "eb316dac-bf6a-4359-95a2-86c299d48ecc", .plan_name = "Yearly", .subscription_backed = true},
    PolarBenefit {.id = "157374cb-f526-4154-81ba-9f2c92a053ca", .plan_name = "Lifetime", .subscription_backed = false},
  };

  inline constexpr std::string_view buy_url = "https://buy.polar.sh/polar_cl_zj6Io5NVukXfZSl97ULtFvImfI5L1jbL2cSnc0Y72Pt";
  inline constexpr std::string_view manage_account_url = "https://polar.sh/lizardbyte-llc/portal";

  inline constexpr auto validation_interval = std::chrono::hours {24};
  inline constexpr auto validation_retry_interval = std::chrono::seconds {60};
  inline constexpr auto outage_retention = std::chrono::hours {1};
  inline constexpr auto subscription_max_age = validation_interval + outage_retention;
  inline constexpr std::size_t unvalidated_active_device_limit = 1U;

  constexpr const PolarBenefit *benefit(std::string_view benefit_id) noexcept {
    for (const auto &candidate : allowed_benefits) {
      if (candidate.id == benefit_id) {
        return &candidate;
      }
    }
    return nullptr;
  }

  constexpr std::string_view plan_name(std::string_view benefit_id) noexcept {
    const auto *found = benefit(benefit_id);
    return found ? found->plan_name : std::string_view {};
  }

  constexpr bool subscription_current(std::uint64_t validated_at, std::uint64_t effective_timestamp) noexcept {
    constexpr auto maximum_age = std::chrono::duration_cast<std::chrono::seconds>(subscription_max_age).count();
    return validated_at != 0U && effective_timestamp >= validated_at &&
           effective_timestamp - validated_at < static_cast<std::uint64_t>(maximum_age);
  }

  template<class Duration>
  constexpr bool outage_retention_elapsed(Duration elapsed) noexcept {
    return elapsed >= outage_retention;
  }

  namespace github_actions_evaluation {

    using Clock = std::chrono::system_clock;
    inline constexpr auto duration = std::chrono::minutes {5};

    constexpr bool active(Clock::time_point started_at, Clock::time_point now) noexcept {
      return now >= started_at && now < started_at + duration;
    }

    constexpr std::chrono::seconds remaining(Clock::time_point started_at, Clock::time_point now) noexcept {
      if (!active(started_at, now)) {
        return std::chrono::seconds::zero();
      }
      return std::chrono::ceil<std::chrono::seconds>(started_at + duration - now);
    }

  }  // namespace github_actions_evaluation

}  // namespace lvh::broker_license
