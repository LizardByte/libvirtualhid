/**
 * @file src/include/libvirtualhid/license.hpp
 * @brief Provider-neutral license management API.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string>
#include <string_view>

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh {

  /**
   * @brief Machine license states reported by the platform license service.
   */
  enum class LicenseState {
    unavailable,  ///< The platform license service is not available.
    unlicensed,  ///< No active machine license is stored.
    licensed,  ///< The stored machine license is active.
    expired,  ///< The stored machine license has expired.
    disabled,  ///< The stored machine license was disabled by the provider.
    invalid,  ///< The stored machine license is invalid.
  };

  /**
   * @brief Current machine license details.
   */
  struct LicenseStatus {
    bool service_available = false;  ///< Whether the platform license service answered the request.
    LicenseState state = LicenseState::unavailable;  ///< Current machine license state.
    std::uint32_t active_devices = 0;  ///< Virtual devices currently tracked by the license service.
    std::uint32_t activation_limit = 0;  ///< Maximum machine activations allowed by the license.
    std::uint32_t activation_usage = 0;  ///< Machine activations currently used by the license.
    std::string plan_name;  ///< Human-readable plan name, when available.
    std::string customer_email;  ///< Customer email associated with the license, when available.
    std::string message;  ///< Human-readable license service status.
    std::string purchase_url;  ///< Hosted page where a license can be purchased.
    std::string manage_account_url;  ///< Hosted page where the customer can manage activations.

    /**
     * @brief Check whether the machine has an active license.
     *
     * @return `true` when the current state is licensed.
     */
    bool licensed() const {
      return state == LicenseState::licensed;
    }
  };

  /**
   * @brief Result returned by license service operations.
   */
  struct LicenseResult {
    OperationStatus status;  ///< Operation status, including transport and provider failures.
    LicenseStatus license;  ///< Latest license details returned by the service.

    /**
     * @brief Check whether the license operation succeeded.
     *
     * @return `true` when the operation completed successfully.
     */
    explicit operator bool() const {
      return status.ok();
    }
  };

  /**
   * @brief Read the locally stored machine license status without forcing remote validation.
   *
   * @return Current license result.
   */
  LicenseResult get_license_status();

  /**
   * @brief Activate a license on this machine.
   *
   * The license key is sent directly to the platform license service. The library does not
   * persist a copy or expose it in the returned status.
   * On Windows, authenticated local clients can activate or replace a license without elevation.
   *
   * @param license_key License key supplied by the customer.
   * @param instance_name Optional customer-visible name for this machine activation.
   * @return Activation result and latest license details.
   */
  LicenseResult activate_license(std::string_view license_key, std::string_view instance_name = {});

  /**
   * @brief Revalidate the stored machine license with the configured provider.
   *
   * @return Validation result and latest license details.
   */
  LicenseResult validate_license();

  /**
   * @brief Deactivate the stored license from this machine.
   *
   * On Windows, authenticated local clients can deactivate a license without elevation.
   *
   * @return Deactivation result and latest license details.
   */
  LicenseResult deactivate_license();

}  // namespace lvh
