#include <gtest/gtest.h>
#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

TEST(ReportTest, NormalizesAxesAndTriggers) {
  EXPECT_EQ(lvh::reports::normalize_axis(-2.0F), -32768);
  EXPECT_EQ(lvh::reports::normalize_axis(-1.0F), -32768);
  EXPECT_EQ(lvh::reports::normalize_axis(0.0F), 0);
  EXPECT_EQ(lvh::reports::normalize_axis(1.0F), 32767);
  EXPECT_EQ(lvh::reports::normalize_axis(2.0F), 32767);

  EXPECT_EQ(lvh::reports::normalize_trigger(-1.0F), 0);
  EXPECT_EQ(lvh::reports::normalize_trigger(0.0F), 0);
  EXPECT_EQ(lvh::reports::normalize_trigger(1.0F), 255);
  EXPECT_EQ(lvh::reports::normalize_trigger(2.0F), 255);
}

TEST(ReportTest, EncodesHatSwitch) {
  lvh::ButtonSet buttons;
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 8);

  buttons.set(lvh::GamepadButton::dpad_up);
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 0);

  buttons.set(lvh::GamepadButton::dpad_right);
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 1);

  buttons.set(lvh::GamepadButton::dpad_down);
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 2);
}

TEST(ReportTest, PacksCommonGamepadReport) {
  auto profile = lvh::profiles::xbox_360();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.buttons.set(lvh::GamepadButton::start);
  state.buttons.set(lvh::GamepadButton::dpad_left);
  state.left_stick = {1.0F, -1.0F};
  state.right_stick = {0.5F, -0.5F};
  state.left_trigger = 0.25F;
  state.right_trigger = 1.0F;

  const auto report = lvh::reports::pack_input_report(profile, state);

  ASSERT_EQ(report.size(), profile.input_report_size);
  EXPECT_EQ(report[0], profile.report_id);
  EXPECT_EQ(report[1], 0x21);  // A + Start
  EXPECT_EQ(report[2], 0x00);
  EXPECT_EQ(report[3], 6);  // D-pad left
  EXPECT_EQ(report[4], 0xFF);
  EXPECT_EQ(report[5], 0x7F);
  EXPECT_EQ(report[6], 0x00);
  EXPECT_EQ(report[7], 0x80);
  EXPECT_EQ(report[12], 64);
  EXPECT_EQ(report[13], 255);
}
