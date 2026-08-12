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
