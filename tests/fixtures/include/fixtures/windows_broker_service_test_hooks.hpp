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
    std::string body;
    std::string error;
  };

  BrokerPolarResult broker_polar_scenario(BrokerPolarScenario scenario);

}  // namespace lvh::detail::test
