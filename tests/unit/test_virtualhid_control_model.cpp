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

  EXPECT_FALSE(control::supports_normalized_feedback(generic));
  EXPECT_TRUE(control::supports_normalized_feedback(dualsense));

  EXPECT_EQ(
    control::profile_feature_summary(generic),
    L"Features: battery no | rumble no | trigger rumble no | RGB LED no | adaptive triggers no | raw output no"
  );
  EXPECT_EQ(
    control::profile_feature_summary(dualsense),
    L"Features: battery yes | rumble yes | trigger rumble no | RGB LED yes | adaptive triggers yes | raw output yes"
  );
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
  const auto generic = lvh::profiles::generic_gamepad();
  const auto dualsense = lvh::profiles::dualsense();

  control::OutputState state;
  EXPECT_EQ(
    control::output_summary(state, generic),
    L"Output: no reports received | profile has no normalized feedback categories"
  );
  EXPECT_EQ(control::output_summary(state, dualsense), L"Output: no reports received");

  state.outputs.push_back({.sequence = 1, .output = output(lvh::GamepadOutputKind::raw_report)});
  EXPECT_EQ(control::output_summary(state, dualsense), L"Output: reports received");

  state.latest_raw_report = output(lvh::GamepadOutputKind::raw_report);
  EXPECT_EQ(control::output_summary(state, dualsense), L"Output: raw report");

  state.latest_rumble = output(lvh::GamepadOutputKind::rumble);
  state.latest_rumble->low_frequency_rumble = 10;
  state.latest_rumble->high_frequency_rumble = 20;
  state.latest_trigger_rumble = output(lvh::GamepadOutputKind::trigger_rumble);
  state.latest_trigger_rumble->left_trigger_rumble = 30;
  state.latest_trigger_rumble->right_trigger_rumble = 40;
  state.latest_rgb_led = output(lvh::GamepadOutputKind::rgb_led);
  state.latest_rgb_led->red = 1;
  state.latest_rgb_led->green = 2;
  state.latest_rgb_led->blue = 3;
  state.latest_adaptive_triggers = output(lvh::GamepadOutputKind::adaptive_triggers);
  state.latest_adaptive_triggers->adaptive_trigger_flags = 4;

  EXPECT_EQ(
    control::output_summary(state, dualsense),
    L"Output: rumble low=10 high=20 | trigger rumble L=30 R=40 | RGB 1,2,3 | adaptive flags=4"
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

  auto raw = output(lvh::GamepadOutputKind::raw_report);
  raw.raw_report = {0x12, 0x34};
  control::record_output(state, raw, next_sequence, 3);

  ASSERT_EQ(state.outputs.size(), 3U);
  EXPECT_EQ(state.outputs.front().sequence, 9U);
  EXPECT_EQ(state.outputs.back().sequence, 11U);
  EXPECT_EQ(next_sequence, 12U);

  ASSERT_TRUE(state.latest_rumble.has_value());
  EXPECT_EQ(state.latest_rumble->low_frequency_rumble, 100);
  ASSERT_TRUE(state.latest_trigger_rumble.has_value());
  EXPECT_EQ(state.latest_trigger_rumble->right_trigger_rumble, 400);
  ASSERT_TRUE(state.latest_rgb_led.has_value());
  EXPECT_EQ(state.latest_rgb_led->blue, 7);
  ASSERT_TRUE(state.latest_adaptive_triggers.has_value());
  EXPECT_EQ(state.latest_adaptive_triggers->adaptive_trigger_flags, 8);
  ASSERT_TRUE(state.latest_raw_report.has_value());
  EXPECT_EQ(state.latest_raw_report->raw_report, (std::vector<std::uint8_t> {0x12, 0x34}));
}
