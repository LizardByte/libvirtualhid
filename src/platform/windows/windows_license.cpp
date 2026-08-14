/**
 * @file src/platform/windows/windows_license.cpp
 * @brief Windows broker-backed public license API definitions.
 */

// local includes
#include "lvh_windows_broker_config.hpp"
#include "lvh_windows_broker_protocol.h"
#include "platform/windows/windows_broker_client.hpp"

#include <libvirtualhid/license.hpp>

// standard includes
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <string_view>
#include <utility>

namespace lvh {
  namespace {

    LicenseState license_state_from(std::uint32_t state) {
      switch (static_cast<LvhWindowsBrokerLicenseState>(state)) {
        case LvhWindowsBrokerLicenseState::free:
          return LicenseState::unlicensed;
        case LvhWindowsBrokerLicenseState::licensed:
          return LicenseState::licensed;
        case LvhWindowsBrokerLicenseState::expired:
          return LicenseState::expired;
        case LvhWindowsBrokerLicenseState::disabled:
          return LicenseState::disabled;
        case LvhWindowsBrokerLicenseState::invalid:
        default:
          return LicenseState::invalid;
      }
    }

    LicenseStatus license_status_from(const LvhWindowsBrokerLicenseStatus &status) {
      return {
        .service_available = true,
        .state = license_state_from(status.state),
        .active_devices = status.active_devices,
        .activation_limit = status.activation_limit,
        .activation_usage = status.activation_usage,
        .plan_name = status.plan_name.data(),
        .customer_email = status.customer_email.data(),
        .expires_at = status.expires_at.data(),
        .message = status.message.data(),
        .purchase_url = std::string {windows::broker_config::buy_url},
        .manage_account_url = std::string {windows::broker_config::manage_account_url},
      };
    }

    LicenseStatus unavailable_license_status(std::string_view message, bool service_available = false) {
      LicenseStatus status;
      status.service_available = service_available;
      status.message = message;
      status.purchase_url = windows::broker_config::buy_url;
      status.manage_account_url = windows::broker_config::manage_account_url;
      return status;
    }

    template<std::size_t Size>
    void copy_c_string(std::array<char, Size> &target, std::string_view value) {
      std::ranges::fill(target, '\0');
      std::memcpy(target.data(), value.data(), value.size());
    }

    template<typename Response>
    LicenseResult license_result_from(const OperationStatus &status, const Response &response) {
      if (response.version != LVH_WINDOWS_BROKER_PROTOCOL_VERSION || response.size != sizeof(response)) {
        return {status, unavailable_license_status(status.message())};
      }

      auto license = license_status_from(response.license);
      if (!std::string_view {response.message.data()}.empty()) {
        license.message = response.message.data();
      }
      return {status, std::move(license)};
    }

    template<LvhWindowsBrokerRequestType RequestType>
    LicenseResult submit_license_request(std::string_view license_key, std::string_view instance_name) {
      if (license_key.size() >= LVH_WINDOWS_BROKER_MAX_LICENSE_KEY_SIZE) {
        auto status = OperationStatus::failure(ErrorCode::invalid_argument, "License key exceeds the platform service limit");
        return {status, unavailable_license_status(status.message(), true)};
      }
      if (instance_name.size() >= LVH_WINDOWS_BROKER_MAX_INSTANCE_NAME_SIZE) {
        auto status = OperationStatus::failure(ErrorCode::invalid_argument, "License instance name exceeds the platform service limit");
        return {status, unavailable_license_status(status.message(), true)};
      }

      LvhWindowsBrokerLicenseRequest request {};
      request.header = detail::windows_broker::make_request_header(RequestType, sizeof(request));
      copy_c_string(request.license_key, license_key);
      copy_c_string(request.instance_name, instance_name);

      LvhWindowsBrokerLicenseResponse response {};
      const auto status = detail::windows_broker::call(request, response, "Call the Windows license service");
      return license_result_from(status, response);
    }

  }  // namespace

  LicenseResult get_license_status() {
    LvhWindowsBrokerStatusRequest request {};
    request.header = detail::windows_broker::make_request_header(LvhWindowsBrokerRequestType::status, sizeof(request));

    LvhWindowsBrokerStatusResponse response {};
    const auto status = detail::windows_broker::call(request, response, "Query the Windows license service");
    return license_result_from(status, response);
  }

  LicenseResult activate_license(std::string_view license_key, std::string_view instance_name) {
    if (license_key.empty()) {
      auto status = OperationStatus::failure(ErrorCode::invalid_argument, "License key is required");
      return {status, unavailable_license_status(status.message(), true)};
    }
    return submit_license_request<LvhWindowsBrokerRequestType::activate_license>(license_key, instance_name);
  }

  LicenseResult validate_license() {
    return submit_license_request<LvhWindowsBrokerRequestType::validate_license>({}, {});
  }

  LicenseResult deactivate_license() {
    return submit_license_request<LvhWindowsBrokerRequestType::deactivate_license>({}, {});
  }

}  // namespace lvh
