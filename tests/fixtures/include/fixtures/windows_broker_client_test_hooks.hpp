/**
 * @file tests/fixtures/include/fixtures/windows_broker_client_test_hooks.hpp
 * @brief Private Windows broker client test hooks.
 */
#pragma once

// standard includes
#include <cstdint>

// lib includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail::test {

  enum class BrokerServiceScenario {
    pipe_unavailable_once,
    pipe_never_available,
    pipe_access_denied,
    pipe_busy_once,
    pipe_busy_timeout_once,
    pipe_busy_disappears_once,
    pipe_busy_failure,
    pipe_process_failure,
    zero_pipe_process,
    service_manager_failure,
    service_failure,
    service_query_failure,
    service_stopped,
    service_process_mismatch,
    success,
  };

  struct BrokerServiceVerificationResult {
    OperationStatus status;
    std::uint32_t closed_pipe_handles = 0;
    std::uint32_t closed_service_handles = 0;
    std::uint32_t create_attempts = 0;
    std::uint32_t sleep_attempts = 0;
    std::uint32_t wait_attempts = 0;
    bool transacted = false;
  };

  BrokerServiceVerificationResult verify_broker_service_scenario(
    BrokerServiceScenario scenario
  );

}  // namespace lvh::detail::test
