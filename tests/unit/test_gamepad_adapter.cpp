/**
 * @file tests/unit/test_gamepad_adapter.cpp
 * @brief Unit tests for platform-neutral gamepad adapter helpers.
 */

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/libvirtualhid.hpp>

TEST(GamepadAdapterTest, ReportsProfileSupport) {
  const auto generic = lvh::profiles::generic_gamepad();
  const auto dualshock4 = lvh::profiles::dualshock4();
  const auto dualsense = lvh::profiles::dualsense();

  const auto generic_support = lvh::gamepad_profile_support(generic);
  EXPECT_FALSE(generic_support.supports_rumble);
  EXPECT_FALSE(generic_support.supports_touchpad);
  EXPECT_TRUE(generic_support.supports_misc1_button);
  EXPECT_FALSE(generic_support.supports_trigger_rumble);

  const auto dualshock4_support = lvh::gamepad_profile_support(dualshock4);
  EXPECT_TRUE(dualshock4_support.supports_rumble);
  EXPECT_TRUE(dualshock4_support.supports_rgb_led);
  EXPECT_TRUE(dualshock4_support.supports_motion);
  EXPECT_TRUE(dualshock4_support.supports_touchpad);
  EXPECT_TRUE(dualshock4_support.supports_battery);
  EXPECT_TRUE(dualshock4_support.supports_touchpad_button);
  EXPECT_FALSE(dualshock4_support.supports_adaptive_triggers);
  EXPECT_FALSE(dualshock4_support.supports_misc1_button);

  const auto dualsense_support = lvh::gamepad_profile_support(dualsense);
  EXPECT_TRUE(dualsense_support.supports_rumble);
  EXPECT_TRUE(dualsense_support.supports_rgb_led);
  EXPECT_TRUE(dualsense_support.supports_adaptive_triggers);
  EXPECT_TRUE(dualsense_support.supports_motion);
  EXPECT_TRUE(dualsense_support.supports_touchpad);
  EXPECT_TRUE(dualsense_support.supports_battery);
  EXPECT_TRUE(dualsense_support.supports_misc1_button);
  EXPECT_EQ(dualsense_support.supported_rear_paddle_count, 0U);
}

TEST(GamepadAdapterTest, ChecksButtonsAndOutputsByProfile) {
  const auto xbox = lvh::profiles::xbox_series();
  const auto dualshock4 = lvh::profiles::dualshock4();
  const auto dualsense = lvh::profiles::dualsense();

  EXPECT_TRUE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::guide));
  EXPECT_TRUE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::misc1));
  EXPECT_FALSE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::touchpad));
  EXPECT_FALSE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::paddle1));

  EXPECT_TRUE(lvh::supports_gamepad_button(dualshock4, lvh::GamepadButton::touchpad));
  EXPECT_FALSE(lvh::supports_gamepad_button(dualshock4, lvh::GamepadButton::misc1));
  EXPECT_TRUE(lvh::supports_gamepad_button(dualsense, lvh::GamepadButton::misc1));

  EXPECT_TRUE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::rumble));
  EXPECT_TRUE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::rgb_led));
  EXPECT_FALSE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::adaptive_triggers));
  EXPECT_FALSE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::trigger_rumble));
  EXPECT_TRUE(lvh::supports_gamepad_output(dualsense, lvh::GamepadOutputKind::adaptive_triggers));
}

TEST(GamepadAdapterTest, CachesAndSubmitsPartialUpdates) {
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
  options.metadata.stable_id = "remote-client-0";

  auto created = lvh::GamepadStateAdapter::create(*runtime, options);
  ASSERT_TRUE(created);
  auto &adapter = *created.adapter;
  ASSERT_NE(adapter.gamepad(), nullptr);

  EXPECT_EQ(adapter.gamepad()->metadata().global_index, 2);
  EXPECT_TRUE(adapter.support().supports_motion);
  EXPECT_TRUE(adapter.support().supports_touchpad);

  bool feedback_received = false;
  adapter.set_output_callback([&feedback_received](const lvh::GamepadOutput &output) {
    feedback_received = output.kind == lvh::GamepadOutputKind::rumble &&
                        output.low_frequency_rumble == 0x4000 &&
                        output.high_frequency_rumble == 0x2000;
  });

  EXPECT_TRUE(adapter.set_button(lvh::GamepadButton::a, true).ok());
  EXPECT_TRUE(adapter.set_left_stick({0.25F, -0.5F}).ok());
  EXPECT_TRUE(adapter.set_right_trigger(1.0F).ok());
  EXPECT_TRUE(adapter.set_touchpad_contact(0, {.id = 3, .active = true, .x = 0.5F, .y = 0.25F}).ok());
  EXPECT_TRUE(adapter.set_acceleration(lvh::Vector3 {.x = 1.0F, .y = 2.0F, .z = 3.0F}).ok());
  EXPECT_TRUE(adapter.set_gyroscope(lvh::Vector3 {.x = 4.0F, .y = 5.0F, .z = 6.0F}).ok());
  EXPECT_TRUE(adapter.set_battery({.state = lvh::GamepadBatteryState::charging, .percentage = 80}).ok());

  const auto *gamepad = adapter.gamepad();
  ASSERT_NE(gamepad, nullptr);
  EXPECT_EQ(gamepad->submit_count(), 7U);

  const auto submitted = gamepad->last_submitted_state();
  EXPECT_TRUE(submitted.buttons.test(lvh::GamepadButton::a));
  EXPECT_FLOAT_EQ(submitted.left_stick.x, 0.25F);
  EXPECT_FLOAT_EQ(submitted.left_stick.y, -0.5F);
  EXPECT_FLOAT_EQ(submitted.right_trigger, 1.0F);
  ASSERT_TRUE(submitted.acceleration.has_value());
  EXPECT_FLOAT_EQ(submitted.acceleration->z, 3.0F);
  ASSERT_TRUE(submitted.gyroscope.has_value());
  EXPECT_FLOAT_EQ(submitted.gyroscope->z, 6.0F);
  ASSERT_TRUE(submitted.battery.has_value());
  EXPECT_EQ(submitted.battery->state, lvh::GamepadBatteryState::charging);
  EXPECT_EQ(submitted.battery->percentage, 80U);
  EXPECT_TRUE(submitted.touchpad_contacts[0].active);

  lvh::GamepadOutput rumble;
  rumble.kind = lvh::GamepadOutputKind::rumble;
  rumble.low_frequency_rumble = 0x4000;
  rumble.high_frequency_rumble = 0x2000;

  EXPECT_TRUE(adapter.dispatch_output(rumble).ok());
  EXPECT_TRUE(feedback_received);
  EXPECT_TRUE(adapter.close().ok());
  EXPECT_EQ(runtime->active_device_count(), 0U);
}

TEST(GamepadAdapterTest, RejectsUnsupportedPartialUpdates) {
  auto runtime = lvh::Runtime::create();

  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::generic_gamepad();
  options.metadata.stable_id = "generic-client-0";

  auto created = lvh::GamepadStateAdapter::create(*runtime, options);
  ASSERT_TRUE(created);
  auto &adapter = *created.adapter;

  EXPECT_EQ(
    adapter.set_touchpad_contact(0, {.id = 1, .active = true, .x = 0.5F, .y = 0.25F}).code(),
    lvh::ErrorCode::unsupported_profile
  );
  EXPECT_EQ(adapter.set_acceleration(lvh::Vector3 {.x = 1.0F, .y = 0.0F, .z = 0.0F}).code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(
    adapter.set_battery({.state = lvh::GamepadBatteryState::discharging, .percentage = 50}).code(),
    lvh::ErrorCode::unsupported_profile
  );
  EXPECT_EQ(adapter.set_button(lvh::GamepadButton::touchpad, true).code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(adapter.set_button(lvh::GamepadButton::paddle1, true).code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(adapter.gamepad()->submit_count(), 0U);
}
