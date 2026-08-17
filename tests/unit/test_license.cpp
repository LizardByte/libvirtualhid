/**
 * @file tests/unit/test_license.cpp
 * @brief Tests for the provider-neutral license API.
 */

// test includes
#include <gtest/gtest.h>

// local includes
#include "lvh_windows_github_actions_evaluation.hpp"

// lib includes
#include <libvirtualhid/license.hpp>

// standard includes
#include <chrono>

#if defined(_WIN32)
  // local includes
  #include "platform/windows/windows_broker_client.hpp"
#endif

TEST(LicenseStatusTest, LicensedReflectsCurrentState) {
  lvh::LicenseStatus status;
  EXPECT_FALSE(status.licensed());

  status.state = lvh::LicenseState::licensed;
  EXPECT_TRUE(status.licensed());

  status.state = lvh::LicenseState::expired;
  EXPECT_FALSE(status.licensed());
}

TEST(GitHubActionsEvaluationTest, IsActiveOnlyInsideFiveMinuteWindow) {
  using namespace std::chrono_literals;
  using lvh::windows::github_actions_evaluation::active;

  const auto started_at = lvh::windows::github_actions_evaluation::Clock::time_point {1000s};
  EXPECT_TRUE(active(started_at, started_at));
  EXPECT_TRUE(active(started_at, started_at + 5min - 1s));
  EXPECT_FALSE(active(started_at, started_at + 5min));
  EXPECT_FALSE(active(started_at, started_at - 1s));
}

TEST(GitHubActionsEvaluationTest, RemainingTimeClampsAtWindowBoundaries) {
  using namespace std::chrono_literals;
  using lvh::windows::github_actions_evaluation::remaining;

  const auto started_at = lvh::windows::github_actions_evaluation::Clock::time_point {1000s};
  EXPECT_EQ(remaining(started_at, started_at), 5min);
  EXPECT_EQ(remaining(started_at, started_at + 4min), 1min);
  EXPECT_EQ(remaining(started_at, started_at + 5min - 500ms), 1s);
  EXPECT_EQ(remaining(started_at, started_at + 5min), 0s);
  EXPECT_EQ(remaining(started_at, started_at - 1s), 0s);
}

#if !defined(_WIN32)
TEST(LicenseApiTest, UnsupportedPlatformReturnsExplicitFailure) {
  const auto queried = lvh::get_license_status();
  EXPECT_FALSE(queried);
  EXPECT_EQ(queried.status.code(), lvh::ErrorCode::backend_unavailable);
  EXPECT_EQ(queried.license.state, lvh::LicenseState::unavailable);
  EXPECT_FALSE(queried.license.service_available);

  EXPECT_FALSE(lvh::activate_license("test-key"));
  EXPECT_FALSE(lvh::validate_license());
  EXPECT_FALSE(lvh::deactivate_license());
}
#endif

#if defined(_WIN32)
TEST(WindowsBrokerClientTest, BuildsVersionedRequestHeader) {
  const auto header = lvh::detail::windows_broker::make_request_header(
    LvhWindowsBrokerRequestType::validate_license,
    sizeof(LvhWindowsBrokerLicenseRequest)
  );

  EXPECT_EQ(header.version, LVH_WINDOWS_BROKER_PROTOCOL_VERSION);
  EXPECT_EQ(header.size, sizeof(LvhWindowsBrokerLicenseRequest));
  EXPECT_EQ(header.type, static_cast<std::uint32_t>(LvhWindowsBrokerRequestType::validate_license));
  EXPECT_EQ(header.reserved0, 0U);
  EXPECT_EQ(LVH_WINDOWS_BROKER_PROTOCOL_VERSION, 3U);
}

TEST(WindowsBrokerClientTest, PreservesFixedWireLayout) {
  EXPECT_EQ(sizeof(LvhWindowsBrokerRequestHeader), 16U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerLicenseStatus), 796U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerStatusRequest), 16U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerStatusResponse), 1324U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerCreateGamepadRequest), 2528U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerCreateGamepadResponse), 1640U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerDestroyDeviceRequest), 64U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerDestroyDeviceResponse), 1324U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerLicenseRequest), 272U);
  EXPECT_EQ(sizeof(LvhWindowsBrokerLicenseResponse), 1324U);
}

TEST(WindowsBrokerClientTest, MapsLicenseAndTransportStatuses) {
  using lvh::detail::windows_broker::response_status;

  EXPECT_TRUE(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::success), "").ok());
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::invalid_argument), "bad").code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::unsupported_profile), "bad").code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::device_not_found), "bad").code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::backend_unavailable), "bad").code(), lvh::ErrorCode::backend_unavailable);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::license_required), "bad").code(), lvh::ErrorCode::license_required);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::license_invalid), "bad").code(), lvh::ErrorCode::license_invalid);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::activation_limit_reached), "bad").code(), lvh::ErrorCode::activation_limit_reached);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::network_unavailable), "bad").code(), lvh::ErrorCode::network_unavailable);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::backend_failure), "bad").code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(response_status(999U, "bad").code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(response_status(static_cast<std::uint32_t>(LvhWindowsBrokerStatusCode::backend_failure), "").message(), "Windows broker request failed");
}
#endif
