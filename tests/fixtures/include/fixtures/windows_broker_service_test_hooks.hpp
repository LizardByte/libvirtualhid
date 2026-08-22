/**
 * @file tests/fixtures/include/fixtures/windows_broker_service_test_hooks.hpp
 * @brief Private Windows broker persistence and Polar transport test hooks.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string>

namespace lvh::detail::test {

  enum class BrokerPersistenceFailure {
    dpapi,
    create_file,
    partial_write,
    flush,
    close,
  };

  struct BrokerPersistenceFailureResult {
    bool saved = false;
    std::string message;
  };

  BrokerPersistenceFailureResult broker_persistence_failure(
    BrokerPersistenceFailure failure
  );

  enum class BrokerPolarScenario {
    open_failure,
    timeout_configuration_failure,
    connect_failure,
    request_failure,
    send_failure,
    receive_failure,
    structured_error,
    malformed_error,
    success,
  };

  struct BrokerPolarResult {
    bool transport_ok = false;
    std::uint32_t http_status = 0;
    bool server_time_available = false;
    bool timeouts_configured = false;
    int resolve_timeout = 0;
    int connect_timeout = 0;
    int send_timeout = 0;
    int receive_timeout = 0;
    std::string body;
    std::string error;
  };

  BrokerPolarResult broker_polar_scenario(BrokerPolarScenario scenario);

  struct BrokerSubscriptionValidationResult {
    bool yearly_benefit_is_subscription_backed = false;
    bool subscription_validation_before_deadline_is_current = false;
    bool subscription_validation_at_deadline_is_stale = false;
  };

  BrokerSubscriptionValidationResult broker_subscription_validation_policy();

  struct BrokerLicenseFallbackResult {
    bool same_boot_anchor_is_accepted = false;
    bool changed_boot_anchor_is_rejected = false;
    bool uptime_rollback_is_rejected = false;
    bool first_unvalidated_device_is_allowed = false;
    bool second_unvalidated_device_is_rejected = false;
    bool existing_gamepads_are_retained_before_one_hour = false;
    bool outage_limit_applies_at_one_hour = false;
    bool first_gamepad_is_retained_after_one_hour = false;
    bool excess_gamepad_is_revoked_after_one_hour = false;
    bool evaluation_gamepad_is_not_outage_limited = false;
    bool licensed_device_is_revoked = false;
    bool evaluation_device_is_preserved = false;
    bool monotonic_clock = false;
    bool persisted_boot_anchor_round_trips = false;
    std::uint64_t retry_interval_seconds = 0;
    std::uint64_t monotonic_timestamp = 0;
  };

  BrokerLicenseFallbackResult broker_license_fallback_policy();

  struct BrokerCleanupRetryResult {
    bool failed_destruction_is_retained = false;
    bool failed_destruction_is_retried = false;
    bool successful_destruction_is_forgotten = false;
    std::uint32_t destruction_attempts = 0;
  };

  BrokerCleanupRetryResult broker_cleanup_retry_policy();

}  // namespace lvh::detail::test
