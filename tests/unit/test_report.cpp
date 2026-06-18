/**
 * @file tests/unit/test_report.cpp
 * @brief Unit tests for gamepad report packing.
 */

// standard includes
#include <cstdint>
#include <vector>

// local includes
#include "fixtures/fixtures.hpp"

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

TEST(ReportTest, ParsesRumbleOutputReport) {
  const auto profile = lvh::profiles::xbox_360();
  const std::vector<std::uint8_t> report {profile.report_id, 0x34, 0x12, 0xCD, 0xAB};

  const auto output = lvh::reports::parse_output_report(profile, report);

  EXPECT_EQ(output.kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(output.low_frequency_rumble, 0x1234);
  EXPECT_EQ(output.high_frequency_rumble, 0xABCD);
  EXPECT_EQ(output.raw_report, report);
}

TEST(ReportTest, KeepsUnrecognizedOutputReportsRaw) {
  const auto rumble_profile = lvh::profiles::xbox_360();
  const std::vector<std::uint8_t> wrong_report_id {0x7F, 0x34, 0x12, 0xCD, 0xAB};

  const auto wrong_id_output = lvh::reports::parse_output_report(rumble_profile, wrong_report_id);

  EXPECT_EQ(wrong_id_output.kind, lvh::GamepadOutputKind::raw_report);
  EXPECT_EQ(wrong_id_output.low_frequency_rumble, 0U);
  EXPECT_EQ(wrong_id_output.high_frequency_rumble, 0U);
  EXPECT_EQ(wrong_id_output.raw_report, wrong_report_id);

  const auto generic_profile = lvh::profiles::generic_gamepad();
  const std::vector<std::uint8_t> generic_report {generic_profile.report_id, 0x34, 0x12, 0xCD, 0xAB};

  const auto generic_output = lvh::reports::parse_output_report(generic_profile, generic_report);

  EXPECT_EQ(generic_output.kind, lvh::GamepadOutputKind::raw_report);
  EXPECT_EQ(generic_output.low_frequency_rumble, 0U);
  EXPECT_EQ(generic_output.high_frequency_rumble, 0U);
  EXPECT_EQ(generic_output.raw_report, generic_report);
}
