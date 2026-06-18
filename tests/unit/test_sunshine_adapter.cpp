/**
 * @file tests/unit/test_sunshine_adapter.cpp
 * @brief Unit tests for the Sunshine-oriented gamepad lifecycle.
 */

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/libvirtualhid.hpp>

TEST(SunshineAdapterTest, ExercisesArrivalUpdateFeedbackAndRemoval) {
  auto runtime = lvh::Runtime::create();

  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::dualsense();
  options.metadata.global_index = 2;
  options.metadata.client_relative_index = 0;
  options.metadata.client_type = lvh::ClientControllerType::playstation;
  options.metadata.has_motion_sensors = true;
  options.metadata.has_touchpad = true;
  options.metadata.has_rgb_led = true;
  options.metadata.has_battery = true;
  options.metadata.stable_id = "moonlight-client-0";

  auto created = runtime->create_gamepad(options);
  ASSERT_TRUE(created);

  EXPECT_EQ(created.gamepad->metadata().global_index, 2);
  EXPECT_EQ(created.gamepad->metadata().client_relative_index, 0);
  EXPECT_EQ(created.gamepad->metadata().client_type, lvh::ClientControllerType::playstation);
  EXPECT_TRUE(created.gamepad->profile().capabilities.supports_motion);
  EXPECT_TRUE(created.gamepad->profile().capabilities.supports_touchpad);

  bool feedback_received = false;
  created.gamepad->set_output_callback([&](const lvh::GamepadOutput &output) {
    feedback_received = output.kind == lvh::GamepadOutputKind::rumble &&
                        output.low_frequency_rumble == 0x4000 &&
                        output.high_frequency_rumble == 0x2000;
  });

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.buttons.set(lvh::GamepadButton::dpad_up);
  state.left_stick = {0.25F, -0.5F};
  state.right_trigger = 1.0F;

  EXPECT_TRUE(created.gamepad->submit(state).ok());
  EXPECT_EQ(created.gamepad->submit_count(), 1U);

  lvh::GamepadOutput rumble;
  rumble.kind = lvh::GamepadOutputKind::rumble;
  rumble.low_frequency_rumble = 0x4000;
  rumble.high_frequency_rumble = 0x2000;

  EXPECT_TRUE(created.gamepad->dispatch_output(rumble).ok());
  EXPECT_TRUE(feedback_received);

  EXPECT_TRUE(created.gamepad->close().ok());
  EXPECT_EQ(runtime->active_device_count(), 0U);
}
