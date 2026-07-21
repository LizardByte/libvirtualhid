/**
 * @file tests/unit/test_gamepad_adapter.cpp
 * @brief Unit tests for platform-neutral gamepad adapter helpers.
 */

// standard includes
#include <optional>
#include <utility>

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/libvirtualhid.hpp>

TEST(GamepadAdapterTest, ReportsProfileSupport) {
  const auto generic = lvh::profiles::generic_gamepad();
  const auto xbox_360 = lvh::profiles::xbox_360();
  const auto xbox_one = lvh::profiles::xbox_one();
  const auto dualshock4 = lvh::profiles::dualshock4();
  const auto dualsense = lvh::profiles::dualsense();
  const auto switch_pro = lvh::profiles::switch_pro();
  const auto keyboard = lvh::profiles::keyboard();

  const auto generic_support = lvh::gamepad_profile_support(generic);
  EXPECT_TRUE(generic_support.supports_rumble);
  EXPECT_FALSE(generic_support.supports_touchpad);
  EXPECT_TRUE(generic_support.supports_misc1_button);
  EXPECT_FALSE(generic_support.supports_trigger_rumble);

  EXPECT_FALSE(lvh::gamepad_profile_support(xbox_360).supports_misc1_button);
  EXPECT_FALSE(lvh::gamepad_profile_support(xbox_one).supports_misc1_button);

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

  const auto switch_pro_support = lvh::gamepad_profile_support(switch_pro);
  EXPECT_TRUE(switch_pro_support.supports_rumble);
  EXPECT_TRUE(switch_pro_support.supports_motion);
  EXPECT_TRUE(switch_pro_support.supports_battery);
  EXPECT_TRUE(switch_pro_support.supports_misc1_button);

  const auto keyboard_support = lvh::gamepad_profile_support(keyboard);
  EXPECT_FALSE(keyboard_support.supports_rumble);
  EXPECT_FALSE(keyboard_support.supports_motion);
  EXPECT_FALSE(keyboard_support.supports_touchpad);
  EXPECT_FALSE(keyboard_support.supports_battery);
  EXPECT_FALSE(keyboard_support.supports_misc1_button);

  auto invalid_kind = generic;
  invalid_kind.gamepad_kind = static_cast<lvh::GamepadProfileKind>(255);
  EXPECT_FALSE(lvh::supports_gamepad_button(invalid_kind, lvh::GamepadButton::misc1));
}

TEST(GamepadAdapterTest, ChecksButtonsAndOutputsByProfile) {
  const auto xbox = lvh::profiles::xbox_series();
  const auto generic = lvh::profiles::generic_gamepad();
  const auto dualshock4 = lvh::profiles::dualshock4();
  const auto dualsense = lvh::profiles::dualsense();
  const auto switch_pro = lvh::profiles::switch_pro();
  const auto keyboard = lvh::profiles::keyboard();

  EXPECT_TRUE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::guide));
  EXPECT_TRUE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::misc1));
  EXPECT_FALSE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::touchpad));
  EXPECT_FALSE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::paddle1));
  EXPECT_FALSE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::paddle2));
  EXPECT_FALSE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::paddle3));
  EXPECT_FALSE(lvh::supports_gamepad_button(xbox, lvh::GamepadButton::paddle4));

  EXPECT_TRUE(lvh::supports_gamepad_button(dualshock4, lvh::GamepadButton::touchpad));
  EXPECT_FALSE(lvh::supports_gamepad_button(dualshock4, lvh::GamepadButton::misc1));
  EXPECT_TRUE(lvh::supports_gamepad_button(dualsense, lvh::GamepadButton::misc1));
  EXPECT_FALSE(lvh::supports_gamepad_button(keyboard, lvh::GamepadButton::a));
  EXPECT_FALSE(lvh::supports_gamepad_button(generic, static_cast<lvh::GamepadButton>(255)));

  EXPECT_TRUE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::rumble));
  EXPECT_TRUE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::rgb_led));
  EXPECT_FALSE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::adaptive_triggers));
  EXPECT_FALSE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::trigger_rumble));
  EXPECT_TRUE(lvh::supports_gamepad_output(dualshock4, lvh::GamepadOutputKind::raw_report));
  EXPECT_TRUE(lvh::supports_gamepad_output(dualsense, lvh::GamepadOutputKind::adaptive_triggers));
  EXPECT_TRUE(lvh::supports_gamepad_output(switch_pro, lvh::GamepadOutputKind::rumble));
  EXPECT_TRUE(lvh::supports_gamepad_output(switch_pro, lvh::GamepadOutputKind::raw_report));
  EXPECT_TRUE(lvh::supports_gamepad_output(generic, lvh::GamepadOutputKind::raw_report));
  EXPECT_FALSE(lvh::supports_gamepad_output(keyboard, lvh::GamepadOutputKind::rumble));
  EXPECT_FALSE(lvh::supports_gamepad_output(generic, static_cast<lvh::GamepadOutputKind>(255)));
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
  EXPECT_TRUE(adapter.set_right_stick({-0.25F, 0.5F}).ok());
  EXPECT_TRUE(adapter.set_left_trigger(0.5F).ok());
  EXPECT_TRUE(adapter.set_right_trigger(1.0F).ok());
  EXPECT_TRUE(adapter.set_touchpad_contact(0, {.id = 3, .active = true, .x = 0.5F, .y = 0.25F}).ok());
  EXPECT_TRUE(adapter.set_acceleration(lvh::Vector3 {.x = 1.0F, .y = 2.0F, .z = 3.0F}).ok());
  EXPECT_TRUE(adapter.set_gyroscope(lvh::Vector3 {.x = 4.0F, .y = 5.0F, .z = 6.0F}).ok());
  EXPECT_TRUE(adapter.set_battery({.state = lvh::GamepadBatteryState::charging, .percentage = 80}).ok());

  const auto *gamepad = adapter.gamepad();
  ASSERT_NE(gamepad, nullptr);
  EXPECT_EQ(gamepad->submit_count(), 10U);

  const auto submitted = gamepad->last_submitted_state();
  EXPECT_TRUE(submitted.buttons.test(lvh::GamepadButton::a));
  EXPECT_FLOAT_EQ(submitted.left_stick.x, 0.25F);
  EXPECT_FLOAT_EQ(submitted.left_stick.y, -0.5F);
  EXPECT_FLOAT_EQ(submitted.right_stick.x, -0.25F);
  EXPECT_FLOAT_EQ(submitted.right_stick.y, 0.5F);
  EXPECT_FLOAT_EQ(submitted.left_trigger, 0.5F);
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
  EXPECT_EQ(adapter.set_gyroscope(lvh::Vector3 {.x = 0.0F, .y = 1.0F, .z = 0.0F}).code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(
    adapter.set_motion(lvh::Vector3 {.x = 1.0F, .y = 0.0F, .z = 0.0F}, lvh::Vector3 {.x = 0.0F, .y = 1.0F, .z = 0.0F}).code(),
    lvh::ErrorCode::unsupported_profile
  );
  EXPECT_EQ(adapter.clear_motion().code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(
    adapter.set_battery({.state = lvh::GamepadBatteryState::discharging, .percentage = 50}).code(),
    lvh::ErrorCode::unsupported_profile
  );
  EXPECT_EQ(adapter.clear_battery().code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(adapter.clear_touchpad_contact(0).code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(adapter.set_button(lvh::GamepadButton::touchpad, true).code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(adapter.set_button(lvh::GamepadButton::paddle1, true).code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(adapter.gamepad()->submit_count(), 1U);
}

TEST(GamepadAdapterTest, RejectsInvalidCreationAndClosedAdapterUpdates) {
  auto runtime = lvh::Runtime::create();
  ASSERT_NE(runtime, nullptr);

  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::keyboard();
  options.metadata.stable_id = "adapter-keyboard";
  const auto failed = lvh::GamepadStateAdapter::create(*runtime, options);
  EXPECT_FALSE(failed);
  EXPECT_EQ(failed.status.code(), lvh::ErrorCode::unsupported_profile);

  lvh::GamepadStateAdapter adapter(nullptr);
  const auto &const_adapter = adapter;
  EXPECT_EQ(const_adapter.gamepad(), nullptr);
  EXPECT_FALSE(const_adapter.is_open());

  lvh::GamepadState state;
  EXPECT_EQ(adapter.submit().code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_state(state).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_button(lvh::GamepadButton::a, true).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_left_stick({0.25F, -0.25F}).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_right_stick({-0.25F, 0.25F}).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_left_trigger(0.5F).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_right_trigger(0.5F).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_acceleration(std::nullopt).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_gyroscope(std::nullopt).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(
    adapter.set_motion(lvh::Vector3 {.x = 1.0F, .y = 0.0F, .z = 0.0F}, lvh::Vector3 {.x = 0.0F, .y = 1.0F, .z = 0.0F}).code(),
    lvh::ErrorCode::device_closed
  );
  EXPECT_EQ(adapter.clear_motion().code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.set_battery({.state = lvh::GamepadBatteryState::discharging, .percentage = 25}).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(adapter.clear_battery().code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(
    adapter.set_touchpad_contact(0, {.id = 1, .active = true, .x = 0.5F, .y = 0.25F}).code(),
    lvh::ErrorCode::device_closed
  );
  EXPECT_EQ(adapter.clear_touchpad_contact(0).code(), lvh::ErrorCode::device_closed);

  bool callback_called = false;
  adapter.set_output_callback([&callback_called](const lvh::GamepadOutput &) {
    callback_called = true;
  });
  EXPECT_EQ(adapter.dispatch_output({}).code(), lvh::ErrorCode::device_closed);
  EXPECT_FALSE(callback_called);
  EXPECT_EQ(adapter.close().code(), lvh::ErrorCode::device_closed);
}

TEST(GamepadAdapterTest, ReplacesStateAndClearsOptionalInputs) {
  auto runtime = lvh::Runtime::create();
  ASSERT_NE(runtime, nullptr);

  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::dualsense();
  options.metadata.stable_id = "adapter-dualsense-state";

  auto created = lvh::GamepadStateAdapter::create(*runtime, options);
  ASSERT_TRUE(created) << created.status.message();
  ASSERT_NE(created.adapter, nullptr);

  auto &adapter = *created.adapter;
  lvh::GamepadState replacement;
  replacement.buttons.set(lvh::GamepadButton::a);
  replacement.left_stick = {.x = 0.1F, .y = -0.2F};
  replacement.right_stick = {.x = 0.3F, .y = -0.4F};
  replacement.left_trigger = 0.5F;
  replacement.right_trigger = 0.6F;

  EXPECT_TRUE(adapter.set_state(replacement).ok());
  const auto &const_adapter = adapter;
  ASSERT_NE(const_adapter.gamepad(), nullptr);
  EXPECT_TRUE(const_adapter.is_open());
  EXPECT_TRUE(const_adapter.state().buttons.test(lvh::GamepadButton::a));

  EXPECT_TRUE(adapter.set_motion(lvh::Vector3 {.x = 1.0F, .y = 2.0F, .z = 3.0F}, lvh::Vector3 {.x = 4.0F, .y = 5.0F, .z = 6.0F}).ok());
  ASSERT_TRUE(adapter.gamepad()->last_submitted_state().acceleration.has_value());
  ASSERT_TRUE(adapter.gamepad()->last_submitted_state().gyroscope.has_value());
  EXPECT_TRUE(adapter.clear_motion().ok());
  EXPECT_FALSE(adapter.gamepad()->last_submitted_state().acceleration.has_value());
  EXPECT_FALSE(adapter.gamepad()->last_submitted_state().gyroscope.has_value());

  EXPECT_TRUE(adapter.set_battery({.state = lvh::GamepadBatteryState::charging, .percentage = 75}).ok());
  ASSERT_TRUE(adapter.gamepad()->last_submitted_state().battery.has_value());
  EXPECT_TRUE(adapter.clear_battery().ok());
  EXPECT_FALSE(adapter.gamepad()->last_submitted_state().battery.has_value());

  const lvh::GamepadTouchContact contact {.id = 7, .active = true, .x = 0.2F, .y = 0.8F};
  EXPECT_EQ(adapter.set_touchpad_contact(2, contact).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_TRUE(adapter.set_touchpad_contact(1, contact).ok());
  EXPECT_TRUE(adapter.gamepad()->last_submitted_state().touchpad_contacts[1].active);
  EXPECT_EQ(adapter.clear_touchpad_contact(2).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_TRUE(adapter.clear_touchpad_contact(1).ok());
  EXPECT_FALSE(adapter.gamepad()->last_submitted_state().touchpad_contacts[1].active);
}

TEST(GamepadAdapterTest, MovesAdaptersAndClosesOwnedGamepadOnDestruction) {
  auto runtime = lvh::Runtime::create();
  ASSERT_NE(runtime, nullptr);

  lvh::CreateGamepadOptions first_options;
  first_options.profile = lvh::profiles::generic_gamepad();
  first_options.metadata.stable_id = "adapter-move-first";

  lvh::CreateGamepadOptions second_options;
  second_options.profile = lvh::profiles::generic_gamepad();
  second_options.metadata.stable_id = "adapter-move-second";

  {
    auto scoped = lvh::GamepadStateAdapter::create(*runtime, first_options);
    ASSERT_TRUE(scoped) << scoped.status.message();
    ASSERT_NE(scoped.adapter, nullptr);
    EXPECT_EQ(runtime->active_device_count(), 1U);
  }
  EXPECT_EQ(runtime->active_device_count(), 0U);

  auto first = lvh::GamepadStateAdapter::create(*runtime, first_options);
  ASSERT_TRUE(first) << first.status.message();
  ASSERT_NE(first.adapter, nullptr);
  lvh::GamepadStateAdapter moved {std::move(*first.adapter)};
  EXPECT_EQ(first.adapter->gamepad(), nullptr);
  ASSERT_NE(moved.gamepad(), nullptr);
  const auto first_device_id = moved.gamepad()->device_id();

  auto second = lvh::GamepadStateAdapter::create(*runtime, second_options);
  ASSERT_TRUE(second) << second.status.message();
  ASSERT_NE(second.adapter, nullptr);
  moved = std::move(*second.adapter);
  EXPECT_EQ(second.adapter->gamepad(), nullptr);
  ASSERT_NE(moved.gamepad(), nullptr);
  EXPECT_NE(moved.gamepad()->device_id(), first_device_id);
  EXPECT_TRUE(moved.close().ok());
}
