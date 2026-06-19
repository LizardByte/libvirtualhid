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

TEST(ReportTest, PacksDualSenseUsbReport) {
  auto profile = lvh::profiles::dualsense_usb();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.buttons.set(lvh::GamepadButton::left_shoulder);
  state.left_stick = {1.0F, -1.0F};
  state.right_stick = {0.0F, 0.0F};
  state.left_trigger = 1.0F;
  state.acceleration = lvh::Vector3 {.x = 1.0F, .y = 2.0F, .z = 3.0F};
  state.gyroscope = lvh::Vector3 {.x = 4.0F, .y = 5.0F, .z = 6.0F};
  state.battery = lvh::GamepadBattery {.state = lvh::GamepadBatteryState::charging, .percentage = 80};
  state.touchpad_contacts[0] = {.id = 3, .active = true, .x = 0.5F, .y = 0.25F};

  const auto report = lvh::reports::pack_input_report(profile, state);

  ASSERT_EQ(report.size(), profile.input_report_size);
  EXPECT_EQ(report[0], 1);
  EXPECT_EQ(report[1], 255);
  EXPECT_EQ(report[2], 0);
  EXPECT_EQ(report[5], 255);
  EXPECT_EQ(report[8] & 0x20, 0x20);
  EXPECT_EQ(report[9] & 0x05, 0x05);
  EXPECT_EQ(report[33] & 0x7F, 3);
  EXPECT_EQ(report[33] & 0x80, 0);
  EXPECT_EQ(report[53] & 0x0F, 8);
  EXPECT_EQ(report[53] >> 4, 1);
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

TEST(ReportTest, ParsesDualSenseOutputReportEvents) {
  const auto profile = lvh::profiles::dualsense_usb();
  std::vector<std::uint8_t> report(profile.output_report_size, 0);
  report[0] = 0x02;
  report[1] = 0x0D;
  report[2] = 0x04;
  report[3] = 0x80;
  report[4] = 0x40;
  report[11] = 0x26;
  report[12] = 1;
  report[22] = 0x21;
  report[23] = 2;
  report[45] = 0x11;
  report[46] = 0x22;
  report[47] = 0x33;

  const auto outputs = lvh::reports::parse_output_reports(profile, report);

  ASSERT_EQ(outputs.size(), 3U);
  EXPECT_EQ(outputs[0].kind, lvh::GamepadOutputKind::rumble);
  EXPECT_GT(outputs[0].low_frequency_rumble, 0U);
  EXPECT_GT(outputs[0].high_frequency_rumble, 0U);
  EXPECT_EQ(outputs[1].kind, lvh::GamepadOutputKind::rgb_led);
  EXPECT_EQ(outputs[1].red, 0x11);
  EXPECT_EQ(outputs[1].green, 0x22);
  EXPECT_EQ(outputs[1].blue, 0x33);
  EXPECT_EQ(outputs[2].kind, lvh::GamepadOutputKind::adaptive_triggers);
  EXPECT_EQ(outputs[2].adaptive_trigger_flags, 0x0C);
  EXPECT_EQ(outputs[2].right_trigger_effect_type, 0x26);
  EXPECT_EQ(outputs[2].right_trigger_effect[0], 1);
  EXPECT_EQ(outputs[2].left_trigger_effect_type, 0x21);
  EXPECT_EQ(outputs[2].left_trigger_effect[0], 2);
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
