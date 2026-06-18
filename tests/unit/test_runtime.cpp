/**
 * @file tests/unit/test_runtime.cpp
 * @brief Unit tests for runtime and virtual gamepad handles.
 */

// local includes
#include "fixtures/fixtures.hpp"
#include <libvirtualhid/libvirtualhid.hpp>

TEST(RuntimeTest, FakeBackendReportsCapabilities) {
  auto runtime = lvh::Runtime::create();

  EXPECT_EQ(runtime->backend_kind(), lvh::BackendKind::fake);
  EXPECT_EQ(runtime->capabilities().backend_name, "fake");
  EXPECT_TRUE(runtime->capabilities().supports_gamepad);
  EXPECT_TRUE(runtime->capabilities().supports_output_reports);
  EXPECT_FALSE(runtime->capabilities().requires_installed_driver);
}

TEST(RuntimeTest, PlatformDefaultIsUnavailableInPhaseOne) {
  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);

  EXPECT_EQ(runtime->backend_kind(), lvh::BackendKind::platform_default);
  EXPECT_EQ(runtime->capabilities().backend_name, "platform-default-unimplemented");
  EXPECT_FALSE(runtime->capabilities().supports_gamepad);

  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
  EXPECT_FALSE(created);
  EXPECT_EQ(created.status.code(), lvh::ErrorCode::backend_unavailable);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesGamepad) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());

  ASSERT_TRUE(created);
  ASSERT_NE(created.gamepad, nullptr);
  EXPECT_TRUE(created.gamepad->is_open());
  EXPECT_EQ(runtime->active_device_count(), 1U);

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::b);
  state.left_stick.x = 2.0F;
  state.left_trigger = 2.0F;

  EXPECT_TRUE(created.gamepad->submit(state).ok());
  EXPECT_EQ(created.gamepad->submit_count(), 1U);
  EXPECT_EQ(created.gamepad->last_submitted_state().left_stick.x, 1.0F);
  EXPECT_EQ(created.gamepad->last_submitted_state().left_trigger, 1.0F);
  EXPECT_FALSE(created.gamepad->last_input_report().empty());

  EXPECT_TRUE(created.gamepad->close().ok());
  EXPECT_FALSE(created.gamepad->is_open());
  EXPECT_EQ(runtime->active_device_count(), 0U);
  EXPECT_EQ(created.gamepad->submit(state).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, DispatchesOutputCallback) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_gamepad(lvh::profiles::dualsense());
  ASSERT_TRUE(created);

  lvh::GamepadOutput received;
  bool was_called = false;
  created.gamepad->set_output_callback([&](const lvh::GamepadOutput &output) {
    received = output;
    was_called = true;
  });

  lvh::GamepadOutput output;
  output.kind = lvh::GamepadOutputKind::rumble;
  output.low_frequency_rumble = 123;
  output.high_frequency_rumble = 456;

  EXPECT_TRUE(created.gamepad->dispatch_output(output).ok());
  EXPECT_TRUE(was_called);
  EXPECT_EQ(received.kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(received.low_frequency_rumble, 123);
  EXPECT_EQ(received.high_frequency_rumble, 456);
}
