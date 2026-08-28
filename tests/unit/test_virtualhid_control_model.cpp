/**
 * @file tests/unit/test_virtualhid_control_model.cpp
 * @brief Unit tests for virtualhid_control model helpers.
 */

// standard includes
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// third-party includes
#include <gtest/gtest.h>

// local includes
#include <virtualhid_control_model.hpp>

namespace {
  namespace control = lvh::tools::virtualhid_control;

  bool visible_button(
    const std::array<bool, control::button_choices.size()> &visible_buttons,
    lvh::GamepadButton button
  ) {
    for (std::size_t index = 0; index < control::button_choices.size(); ++index) {
      if (control::button_choices[index].button == button) {
        return visible_buttons[index];
      }
    }
    return false;
  }

  lvh::GamepadOutput output(lvh::GamepadOutputKind kind) {
    lvh::GamepadOutput value;
    value.kind = kind;
    return value;
  }
}  // namespace

TEST(VirtualHidControlModelTest, NamesKnownAndFallbackEnumValues) {
  EXPECT_EQ(control::device_type_name(lvh::DeviceType::gamepad), L"gamepad");
  EXPECT_EQ(control::device_type_name(lvh::DeviceType::keyboard), L"keyboard");
  EXPECT_EQ(control::device_type_name(lvh::DeviceType::mouse), L"mouse");
  EXPECT_EQ(control::device_type_name(lvh::DeviceType::touchscreen), L"touchscreen");
  EXPECT_EQ(control::device_type_name(lvh::DeviceType::trackpad), L"trackpad");
  EXPECT_EQ(control::device_type_name(lvh::DeviceType::pen_tablet), L"pen tablet");
  EXPECT_EQ(control::device_type_name(static_cast<lvh::DeviceType>(255)), L"unknown");

  EXPECT_EQ(control::node_kind_name(lvh::DeviceNodeKind::input_event), L"input");
  EXPECT_EQ(control::node_kind_name(lvh::DeviceNodeKind::joystick), L"joystick");
  EXPECT_EQ(control::node_kind_name(lvh::DeviceNodeKind::hidraw), L"hidraw");
  EXPECT_EQ(control::node_kind_name(lvh::DeviceNodeKind::sysfs), L"sysfs");
  EXPECT_EQ(control::node_kind_name(lvh::DeviceNodeKind::other), L"other");
  EXPECT_EQ(control::node_kind_name(static_cast<lvh::DeviceNodeKind>(255)), L"other");

  EXPECT_EQ(control::output_kind_name(lvh::GamepadOutputKind::rumble), L"rumble");
  EXPECT_EQ(control::output_kind_name(lvh::GamepadOutputKind::rgb_led), L"rgb led");
  EXPECT_EQ(control::output_kind_name(lvh::GamepadOutputKind::adaptive_triggers), L"adaptive triggers");
  EXPECT_EQ(control::output_kind_name(lvh::GamepadOutputKind::raw_report), L"raw report");
  EXPECT_EQ(control::output_kind_name(lvh::GamepadOutputKind::trigger_rumble), L"trigger rumble");
  EXPECT_EQ(control::output_kind_name(lvh::GamepadOutputKind::player_leds), L"player leds");
  EXPECT_EQ(control::output_kind_name(static_cast<lvh::GamepadOutputKind>(255)), L"raw report");

  EXPECT_EQ(control::battery_state_name(lvh::GamepadBatteryState::unknown), L"unknown");
  EXPECT_EQ(control::battery_state_name(lvh::GamepadBatteryState::discharging), L"discharging");
  EXPECT_EQ(control::battery_state_name(lvh::GamepadBatteryState::charging), L"charging");
  EXPECT_EQ(control::battery_state_name(lvh::GamepadBatteryState::full), L"full");
  EXPECT_EQ(
    control::battery_state_name(lvh::GamepadBatteryState::voltage_or_temperature_error),
    L"voltage/temperature error"
  );
  EXPECT_EQ(control::battery_state_name(lvh::GamepadBatteryState::temperature_error), L"temperature error");
  EXPECT_EQ(control::battery_state_name(lvh::GamepadBatteryState::charging_error), L"charging error");
  EXPECT_EQ(control::battery_state_name(static_cast<lvh::GamepadBatteryState>(255)), L"unknown");

  EXPECT_EQ(control::yes_no(true), L"yes");
  EXPECT_EQ(control::yes_no(false), L"no");
}

TEST(VirtualHidControlModelTest, NormalizesPastedLicenseKeys) {
  EXPECT_EQ(control::normalized_license_key("test-license-key"), "test-license-key");
  EXPECT_EQ(control::normalized_license_key("\r\n test-license-key \t"), "test-license-key");
  EXPECT_TRUE(control::normalized_license_key(" \r\n\t").empty());
}

TEST(VirtualHidControlModelTest, MapsProfileChoicesToProfiles) {
  for (const auto &choice : control::profile_choices) {
    const auto profile = control::profile_for_choice(choice);
    ASSERT_TRUE(profile.has_value()) << std::string(choice.id.begin(), choice.id.end());
    EXPECT_EQ(profile->device_type, lvh::DeviceType::gamepad);
    EXPECT_EQ(profile->gamepad_kind, choice.kind);
    EXPECT_FALSE(profile->name.empty());
  }

  const control::ProfileChoice invalid {
    L"invalid",
    L"Invalid",
    static_cast<lvh::GamepadProfileKind>(255),
    lvh::ClientControllerType::unknown,
  };
  EXPECT_FALSE(control::profile_for_choice(invalid).has_value());
}

TEST(VirtualHidControlModelTest, ExposesGamepadAndMouseDeviceChoices) {
  ASSERT_EQ(control::device_type_choices.size(), 2U);
  EXPECT_EQ(control::device_type_choices[0].type, lvh::DeviceType::gamepad);
  EXPECT_EQ(control::device_type_choices[1].type, lvh::DeviceType::mouse);

  ASSERT_EQ(control::mouse_button_choices.size(), 5U);
  EXPECT_EQ(control::mouse_button_choices[0].button, lvh::MouseButton::left);
  EXPECT_EQ(control::mouse_button_choices[1].button, lvh::MouseButton::middle);
  EXPECT_EQ(control::mouse_button_choices[2].button, lvh::MouseButton::right);
  EXPECT_EQ(control::mouse_button_choices[3].button, lvh::MouseButton::side);
  EXPECT_EQ(control::mouse_button_choices[4].button, lvh::MouseButton::extra);
}

TEST(VirtualHidControlModelTest, MapsKeyboardMouseActionsToEvents) {
  using enum control::MouseControlAction;

  const auto left = control::mouse_event_for_action(move_left, 25, 120);
  EXPECT_EQ(left.kind, lvh::MouseEventKind::relative_motion);
  EXPECT_EQ(left.x, -25);
  EXPECT_EQ(left.y, 0);

  const auto right = control::mouse_event_for_action(move_right, 25, 120);
  EXPECT_EQ(right.x, 25);
  const auto up = control::mouse_event_for_action(move_up, 25, 120);
  EXPECT_EQ(up.y, -25);
  const auto down = control::mouse_event_for_action(move_down, 25, 120);
  EXPECT_EQ(down.y, 25);

  const auto wheel_up_event = control::mouse_event_for_action(wheel_up, 25, 120);
  EXPECT_EQ(wheel_up_event.kind, lvh::MouseEventKind::vertical_scroll);
  EXPECT_EQ(wheel_up_event.high_resolution_scroll, 120);
  const auto wheel_down_event = control::mouse_event_for_action(wheel_down, 25, 120);
  EXPECT_EQ(wheel_down_event.high_resolution_scroll, -120);
  const auto pan_left_event = control::mouse_event_for_action(pan_left, 25, 120);
  EXPECT_EQ(pan_left_event.kind, lvh::MouseEventKind::horizontal_scroll);
  EXPECT_EQ(pan_left_event.high_resolution_scroll, -120);
  const auto pan_right_event = control::mouse_event_for_action(pan_right, 25, 120);
  EXPECT_EQ(pan_right_event.high_resolution_scroll, 120);
}

TEST(VirtualHidControlModelTest, BuildsMomentaryMouseButtonEvents) {
  const auto press = control::mouse_button_event(lvh::MouseButton::side, true);
  EXPECT_EQ(press.kind, lvh::MouseEventKind::button);
  EXPECT_EQ(press.button, lvh::MouseButton::side);
  EXPECT_TRUE(press.pressed);

  const auto release = control::mouse_button_event(lvh::MouseButton::side, false);
  EXPECT_EQ(release.kind, lvh::MouseEventKind::button);
  EXPECT_EQ(release.button, lvh::MouseButton::side);
  EXPECT_FALSE(release.pressed);
}

TEST(VirtualHidControlModelTest, ConvertsSliderValues) {
  EXPECT_EQ(control::axis_to_slider(-2.0F), -control::slider_scale);
  EXPECT_EQ(control::axis_to_slider(-0.5F), -50);
  EXPECT_EQ(control::axis_to_slider(0.0F), 0);
  EXPECT_EQ(control::axis_to_slider(0.5F), 50);
  EXPECT_EQ(control::axis_to_slider(2.0F), control::slider_scale);

  EXPECT_EQ(control::trigger_to_slider(-1.0F), 0);
  EXPECT_EQ(control::trigger_to_slider(0.25F), 25);
  EXPECT_EQ(control::trigger_to_slider(1.0F), control::slider_scale);
  EXPECT_EQ(control::trigger_to_slider(2.0F), control::slider_scale);

  EXPECT_FLOAT_EQ(control::slider_to_float(-25), -0.25F);
  EXPECT_FLOAT_EQ(control::slider_to_float(0), 0.0F);
  EXPECT_FLOAT_EQ(control::slider_to_float(75), 0.75F);
}

TEST(VirtualHidControlModelTest, MapsBatteryComboChoices) {
  for (std::size_t index = 0; index < control::battery_choices.size(); ++index) {
    EXPECT_EQ(control::battery_choice_index(control::battery_choices[index].state), static_cast<int>(index));
  }
  EXPECT_EQ(control::battery_choice_index(static_cast<lvh::GamepadBatteryState>(255)), 0);
}

TEST(VirtualHidControlModelTest, FormatsRawHex) {
  EXPECT_EQ(control::raw_hex({}), L"");
  EXPECT_EQ(control::raw_hex({0x00, 0x0F, 0xA5, 0xFF}), L"000fa5ff");
}

TEST(VirtualHidControlModelTest, SummarizesProfileFeatures) {
  const auto generic = lvh::profiles::generic_gamepad();
  const auto dualsense = lvh::profiles::dualsense();
  const auto mouse = lvh::profiles::mouse();

  EXPECT_TRUE(control::supports_normalized_feedback(generic));
  EXPECT_TRUE(control::supports_normalized_feedback(dualsense));

  EXPECT_EQ(
    control::profile_feature_summary(generic),
    L"Features: battery no | rumble yes | trigger rumble no | RGB LED no | player LEDs no | adaptive triggers no | raw output yes"
  );
  EXPECT_EQ(
    control::profile_feature_summary(dualsense),
    L"Features: battery yes | rumble yes | trigger rumble no | RGB LED yes | player LEDs no | adaptive triggers yes | raw output yes"
  );
  EXPECT_EQ(
    control::device_feature_summary(mouse),
    L"Features: relative motion | five buttons | vertical wheel | horizontal wheel"
  );
  EXPECT_EQ(control::device_feature_summary(generic), control::profile_feature_summary(generic));
}

TEST(VirtualHidControlModelTest, UpdatesVisibleControlsForProfiles) {
  std::array<bool, control::button_choices.size()> visible_buttons {};
  auto battery_visible = false;

  const auto dualshock4 = lvh::profiles::dualshock4();
  EXPECT_TRUE(control::update_visible_controls_for_profile(dualshock4, visible_buttons, battery_visible));
  EXPECT_TRUE(visible_button(visible_buttons, lvh::GamepadButton::a));
  EXPECT_TRUE(visible_button(visible_buttons, lvh::GamepadButton::touchpad));
  EXPECT_FALSE(visible_button(visible_buttons, lvh::GamepadButton::misc1));
  EXPECT_FALSE(visible_button(visible_buttons, lvh::GamepadButton::paddle1));
  EXPECT_TRUE(battery_visible);

  EXPECT_FALSE(control::update_visible_controls_for_profile(dualshock4, visible_buttons, battery_visible));

  const auto generic = lvh::profiles::generic_gamepad();
  EXPECT_TRUE(control::update_visible_controls_for_profile(generic, visible_buttons, battery_visible));
  EXPECT_TRUE(visible_button(visible_buttons, lvh::GamepadButton::misc1));
  EXPECT_FALSE(visible_button(visible_buttons, lvh::GamepadButton::touchpad));
  EXPECT_FALSE(battery_visible);
}

TEST(VirtualHidControlModelTest, SummarizesOutputState) {
  using enum lvh::GamepadOutputKind;

  const auto generic = lvh::profiles::generic_gamepad();
  const auto dualsense = lvh::profiles::dualsense();

  control::OutputState state;
  EXPECT_EQ(
    control::output_summary(state, generic),
    L"Output: no reports received"
  );
  EXPECT_EQ(control::output_summary(state, dualsense), L"Output: no reports received");

  state.outputs.push_back({.sequence = 1, .output = output(raw_report)});
  EXPECT_EQ(control::output_summary(state, dualsense), L"Output: reports received");

  state.latest_raw_report = output(raw_report);
  EXPECT_EQ(control::output_summary(state, dualsense), L"Output: raw report");

  state.latest_rumble = output(rumble);
  state.latest_rumble->low_frequency_rumble = 10;
  state.latest_rumble->high_frequency_rumble = 20;
  state.latest_trigger_rumble = output(trigger_rumble);
  state.latest_trigger_rumble->left_trigger_rumble = 30;
  state.latest_trigger_rumble->right_trigger_rumble = 40;
  state.latest_rgb_led = output(rgb_led);
  state.latest_rgb_led->red = 1;
  state.latest_rgb_led->green = 2;
  state.latest_rgb_led->blue = 3;
  state.latest_adaptive_triggers = output(adaptive_triggers);
  state.latest_adaptive_triggers->adaptive_trigger_flags = 4;
  state.latest_player_leds = output(player_leds);
  state.latest_player_leds->player_leds = {true, false, true, false};
  state.latest_player_leds->flashing_player_leds = {false, true, false, true};

  EXPECT_EQ(
    control::output_summary(state, dualsense),
    L"Output: rumble low=10 high=20 | trigger rumble L=30 R=40 | RGB 1,2,3 | adaptive flags=4 | player LEDs solid=1010 flashing=0101"
  );
}

TEST(VirtualHidControlModelTest, RecordsOutputsAndMaintainsLatestSummaryFields) {
  control::OutputState state;
  auto next_sequence = std::uint64_t {7};

  auto rumble = output(lvh::GamepadOutputKind::rumble);
  rumble.low_frequency_rumble = 100;
  rumble.high_frequency_rumble = 200;
  control::record_output(state, rumble, next_sequence, 3);

  auto trigger_rumble = output(lvh::GamepadOutputKind::trigger_rumble);
  trigger_rumble.left_trigger_rumble = 300;
  trigger_rumble.right_trigger_rumble = 400;
  control::record_output(state, trigger_rumble, next_sequence, 3);

  auto rgb = output(lvh::GamepadOutputKind::rgb_led);
  rgb.red = 5;
  rgb.green = 6;
  rgb.blue = 7;
  control::record_output(state, rgb, next_sequence, 3);

  auto adaptive = output(lvh::GamepadOutputKind::adaptive_triggers);
  adaptive.adaptive_trigger_flags = 8;
  control::record_output(state, adaptive, next_sequence, 3);

  auto player_leds = output(lvh::GamepadOutputKind::player_leds);
  player_leds.player_leds = {true, false, true, false};
  control::record_output(state, player_leds, next_sequence, 3);

  auto raw = output(lvh::GamepadOutputKind::raw_report);
  raw.raw_report = {0x12, 0x34};
  control::record_output(state, raw, next_sequence, 3);

  ASSERT_EQ(state.outputs.size(), 3U);
  EXPECT_EQ(state.outputs.front().sequence, 10U);
  EXPECT_EQ(state.outputs.back().sequence, 12U);
  EXPECT_EQ(next_sequence, 13U);

  ASSERT_TRUE(state.latest_rumble.has_value());
  EXPECT_EQ(state.latest_rumble->low_frequency_rumble, 100);
  ASSERT_TRUE(state.latest_trigger_rumble.has_value());
  EXPECT_EQ(state.latest_trigger_rumble->right_trigger_rumble, 400);
  ASSERT_TRUE(state.latest_rgb_led.has_value());
  EXPECT_EQ(state.latest_rgb_led->blue, 7);
  ASSERT_TRUE(state.latest_adaptive_triggers.has_value());
  EXPECT_EQ(state.latest_adaptive_triggers->adaptive_trigger_flags, 8);
  ASSERT_TRUE(state.latest_player_leds.has_value());
  EXPECT_EQ(state.latest_player_leds->player_leds, (std::array {true, false, true, false}));
  ASSERT_TRUE(state.latest_raw_report.has_value());
  EXPECT_EQ(state.latest_raw_report->raw_report, (std::vector<std::uint8_t> {0x12, 0x34}));
}
