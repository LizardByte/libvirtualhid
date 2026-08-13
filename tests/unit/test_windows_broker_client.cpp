/**
 * @file tests/unit/test_windows_broker_client.cpp
 * @brief Tests for Windows broker service identity verification.
 */

// local includes
#include "fixtures/windows_broker_client_test_hooks.hpp"

// test includes
#include <gtest/gtest.h>

// standard includes
#include <string_view>

namespace {

  struct FailureCase {
    lvh::detail::test::BrokerServiceScenario scenario;
    std::string_view message;
    std::uint32_t closed_service_handles;
  };

  struct PipeCase {
    lvh::detail::test::BrokerServiceScenario scenario;
    std::string_view name;
    std::uint32_t create_attempts;
    std::uint32_t sleep_attempts;
    std::uint32_t wait_attempts;
  };

}  // namespace

TEST(WindowsBrokerClientTest, RejectsUnverifiedBrokerServiceEndpoints) {
  using enum lvh::detail::test::BrokerServiceScenario;

  for (const auto &[scenario, message, closed_service_handles] : {
         FailureCase {pipe_process_failure, "unable to identify named-pipe server", 0U},
         FailureCase {zero_pipe_process, "unable to identify named-pipe server", 0U},
         FailureCase {service_manager_failure, "unable to open Windows service manager", 0U},
         FailureCase {service_failure, "installed Windows broker service is unavailable", 1U},
         FailureCase {service_query_failure, "unable to verify Windows broker service", 2U},
         FailureCase {service_stopped, "is not the running installed Windows broker service", 2U},
         FailureCase {service_process_mismatch, "is not the running installed Windows broker service", 2U},
       }) {
    SCOPED_TRACE(message);
    const auto result = lvh::detail::test::verify_broker_service_scenario(scenario);

    EXPECT_FALSE(result.status.ok());
    EXPECT_EQ(result.status.code(), lvh::ErrorCode::backend_unavailable);
    EXPECT_NE(result.status.message().find(message), std::string::npos);
    EXPECT_EQ(result.closed_pipe_handles, 1U);
    EXPECT_EQ(result.closed_service_handles, closed_service_handles);
    EXPECT_FALSE(result.transacted);
  }
}

TEST(WindowsBrokerClientTest, TransactsOnlyWithRunningInstalledBrokerService) {
  const auto result = lvh::detail::test::verify_broker_service_scenario(
    lvh::detail::test::BrokerServiceScenario::success
  );

  EXPECT_TRUE(result.status.ok());
  EXPECT_EQ(result.closed_pipe_handles, 1U);
  EXPECT_EQ(result.closed_service_handles, 2U);
  EXPECT_TRUE(result.transacted);
}

TEST(WindowsBrokerClientTest, RetriesWhileTheBrokerPipeIsBeingRecreated) {
  using enum lvh::detail::test::BrokerServiceScenario;

  for (const auto &[scenario, name, create_attempts, sleep_attempts, wait_attempts] : {
         PipeCase {pipe_unavailable_once, "missing once", 2U, 1U, 0U},
         PipeCase {pipe_busy_once, "busy then available", 2U, 0U, 1U},
         PipeCase {pipe_busy_timeout_once, "busy wait timed out", 2U, 0U, 1U},
         PipeCase {pipe_busy_disappears_once, "busy pipe disappeared", 2U, 0U, 1U},
       }) {
    SCOPED_TRACE(name);
    const auto result =
      lvh::detail::test::verify_broker_service_scenario(scenario);

    EXPECT_TRUE(result.status.ok());
    EXPECT_EQ(result.create_attempts, create_attempts);
    EXPECT_EQ(result.sleep_attempts, sleep_attempts);
    EXPECT_EQ(result.wait_attempts, wait_attempts);
    EXPECT_EQ(result.closed_pipe_handles, 1U);
    EXPECT_EQ(result.closed_service_handles, 2U);
    EXPECT_TRUE(result.transacted);
  }
}

TEST(WindowsBrokerClientTest, ReportsBrokerPipeConnectionFailures) {
  using enum lvh::detail::test::BrokerServiceScenario;

  for (const auto &[scenario, name, create_attempts, sleep_attempts, wait_attempts] : {
         PipeCase {pipe_access_denied, "access denied", 1U, 0U, 0U},
         PipeCase {pipe_busy_failure, "busy wait failed", 1U, 0U, 1U},
         PipeCase {pipe_never_available, "retry deadline exhausted", 500U, 500U, 0U},
       }) {
    SCOPED_TRACE(name);
    const auto result =
      lvh::detail::test::verify_broker_service_scenario(scenario);

    EXPECT_FALSE(result.status.ok());
    EXPECT_EQ(result.status.code(), lvh::ErrorCode::backend_unavailable);
    EXPECT_FALSE(result.status.message().empty());
    EXPECT_EQ(result.create_attempts, create_attempts);
    EXPECT_EQ(result.sleep_attempts, sleep_attempts);
    EXPECT_EQ(result.wait_attempts, wait_attempts);
    EXPECT_EQ(result.closed_pipe_handles, 0U);
    EXPECT_EQ(result.closed_service_handles, 0U);
    EXPECT_FALSE(result.transacted);
  }
}
