/**
 * @file tests/unit/test_windows_backend.cpp
 * @brief Unit tests for the Windows backend control-channel integration.
 */

// local includes
#include "fixtures/fixtures.hpp"
#include "fixtures/windows_backend_test_hooks.hpp"

namespace {

  class WindowsBackendTest: public WindowsTest {};

}  // namespace

TEST_F(WindowsBackendTest, FakeChannelExercisesLifecycleSubmitCloseAndOutput) {
  const auto result = lvh::detail::test::windows_backend_fake_channel_lifecycle();

  EXPECT_EQ(result.capabilities.backend_name, "windows-umdf");
  EXPECT_TRUE(result.capabilities.requires_installed_driver);
  EXPECT_TRUE(result.capabilities.supports_virtual_hid);
  EXPECT_TRUE(result.capabilities.supports_gamepad);
  EXPECT_TRUE(result.capabilities.supports_keyboard);
  EXPECT_TRUE(result.capabilities.supports_mouse);
  EXPECT_TRUE(result.capabilities.supports_output_reports);

  ASSERT_TRUE(result.create_status.ok()) << result.create_status.message();
  ASSERT_TRUE(result.submit_status.ok()) << result.submit_status.message();
  ASSERT_TRUE(result.close_status.ok()) << result.close_status.message();
  EXPECT_TRUE(result.second_close_status.ok());
  EXPECT_EQ(result.submit_after_close_status.code(), lvh::ErrorCode::device_closed);

  ASSERT_EQ(result.device_nodes.size(), 1U);
  EXPECT_EQ(result.device_nodes.front().kind, lvh::DeviceNodeKind::other);
  EXPECT_EQ(result.device_nodes.front().path, R"(\\.\LibVirtualHid#100)");

  EXPECT_EQ(result.create_requests, 1U);
  EXPECT_EQ(result.submit_requests, 1U);
  EXPECT_EQ(result.destroy_requests, 1U);

  ASSERT_TRUE(result.saw_output);
  EXPECT_EQ(result.last_output.kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(result.last_output.low_frequency_rumble, 0x5678U);
  EXPECT_EQ(result.last_output.high_frequency_rumble, 0x1234U);
  ASSERT_GE(result.last_output.raw_report.size(), 5U);
  EXPECT_EQ(result.last_output.raw_report[0], 1U);
}

TEST_F(WindowsBackendTest, FakeChannelCoversCreateFailureBranches) {
  const auto result = lvh::detail::test::windows_backend_fake_channel_failures();

  EXPECT_EQ(result.invalid_argument_status.code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(result.unsupported_profile_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.device_closed_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(result.backend_failure_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(result.transport_failure_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(result.unavailable_status.code(), lvh::ErrorCode::backend_unavailable);

  ASSERT_TRUE(result.empty_nodes_create_status.ok()) << result.empty_nodes_create_status.message();
  EXPECT_TRUE(result.empty_device_nodes.empty());
  EXPECT_EQ(result.oversized_submit_status.code(), lvh::ErrorCode::invalid_argument);
}

TEST_F(WindowsBackendTest, UtilityHookCoversEnvironmentErrorAndThreadBranches) {
  const auto result = lvh::detail::test::windows_backend_fake_channel_utilities();

  EXPECT_EQ(result.default_device_path, R"(\\.\LibVirtualHid)");
  EXPECT_EQ(result.custom_device_path, R"(\\.\LibVirtualHid-Test)");
  EXPECT_EQ(result.formatted_error_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(result.fallback_error_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_NE(result.formatted_error_status.message().find("format known Windows error:"), std::string::npos);
  EXPECT_NE(
    result.fallback_error_status.message().find("format unknown Windows error: Windows error 3758096385"),
    std::string::npos
  );
  EXPECT_TRUE(result.keyboard_create_status.ok()) << result.keyboard_create_status.message();
  EXPECT_TRUE(result.keyboard_close_status.ok()) << result.keyboard_close_status.message();
  EXPECT_EQ(result.keyboard_submit_after_close_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_TRUE(result.mouse_create_status.ok()) << result.mouse_create_status.message();
  EXPECT_TRUE(result.mouse_close_status.ok()) << result.mouse_close_status.message();
  EXPECT_EQ(result.mouse_submit_after_close_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_FALSE(result.timeout_result);
}
