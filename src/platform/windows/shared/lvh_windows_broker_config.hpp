// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/shared/lvh_windows_broker_config.hpp
 * @brief Compiled Windows broker licensing constants.
 *
 * Update this file when Polar organization, benefit, or purchase URL changes.
 */
#pragma once

#include <array>
#include <string_view>

namespace lvh::windows::broker_config {

  struct PolarBenefit {
    std::string_view id;
    std::string_view plan_name;
  };

  // Polar's public license API identifies the organization and license-key benefit,
  // not the product. Restrict production licenses to this organization's yearly and
  // lifetime license-key benefits.
  inline constexpr auto polar_organization_id =
    std::string_view {"3db9f05a-44d7-42f1-ba7c-a0f198235fb7"};
  inline constexpr auto allowed_benefits = std::array {
    PolarBenefit {
      .id = "eb316dac-bf6a-4359-95a2-86c299d48ecc",
      .plan_name = "Yearly",
    },
    PolarBenefit {
      .id = "157374cb-f526-4154-81ba-9f2c92a053ca",
      .plan_name = "Lifetime",
    },
  };

  // Use persistent Polar Checkout Links and the organization's hosted customer portal.
  inline constexpr auto buy_url =
    std::string_view {"https://buy.polar.sh/polar_cl_zj6Io5NVukXfZSl97ULtFvImfI5L1jbL2cSnc0Y72Pt"};
  inline constexpr auto manage_account_url =
    std::string_view {"https://polar.sh/lizardbyte-llc/portal"};

}  // namespace lvh::windows::broker_config
