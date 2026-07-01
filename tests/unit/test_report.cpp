/**
 * @file tests/unit/test_report.cpp
 * @brief Unit tests for gamepad report packing.
 */

// standard includes
#include <cstdint>
#include <span>
#include <vector>

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

namespace {
  std::uint32_t test_crc32(std::span<const std::uint8_t> buffer, std::uint32_t seed = 0) {
    auto crc = seed ^ 0xFFFFFFFFU;
    for (const auto byte : buffer) {
      crc ^= byte;
      for (auto bit = 0; bit < 8; ++bit) {
        const auto mask = 0U - (crc & 1U);
        crc = (crc >> 1U) ^ (0xEDB88320U & mask);
      }
    }
    return crc ^ 0xFFFFFFFFU;
  }

  std::uint32_t test_playstation_crc_seed(std::uint8_t seed) {
    return test_crc32(std::span {&seed, 1U});
  }

  std::uint32_t read_u32_le(const std::vector<std::uint8_t> &bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
  }
}  // namespace

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
  using enum lvh::GamepadButton;

  lvh::ButtonSet buttons;
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 8);

  buttons.set(dpad_up);
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 0);

  buttons.set(dpad_right);
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 1);

  buttons.set(dpad_down);
  EXPECT_EQ(lvh::reports::hat_from_buttons(buttons), 2);
}

TEST(ReportTest, PacksCommonGamepadReport) {
  auto profile = lvh::profiles::xbox_360();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.buttons.set(lvh::GamepadButton::start);
  state.buttons.set(lvh::GamepadButton::dpad_left);
  state.buttons.set(lvh::GamepadButton::guide);
  state.buttons.set(lvh::GamepadButton::misc1);
  state.left_stick = {1.0F, -1.0F};
  state.right_stick = {0.5F, -0.5F};
  state.left_trigger = 0.25F;
  state.right_trigger = 1.0F;

  const auto report = lvh::reports::pack_input_report(profile, state);

  ASSERT_EQ(report.size(), profile.input_report_size);
  EXPECT_EQ(report[0], profile.report_id);
  EXPECT_EQ(report[1], 255);  // A
  EXPECT_EQ(report[2], 0);  // B
  EXPECT_EQ(report[6], 0);  // Right shoulder
  EXPECT_EQ(report[7], 64);  // Left trigger
  EXPECT_EQ(report[8], 255);  // Right trigger
  EXPECT_EQ(report[9], 0);  // Back
  EXPECT_EQ(report[10], 255);  // Start
  EXPECT_EQ(report[15], 255);  // D-pad left
  EXPECT_EQ(report[17], 255);  // Guide
  EXPECT_EQ(report[18], 255);  // Misc/share
  EXPECT_EQ(report[19], 255);  // Left stick X
  EXPECT_EQ(report[20], 255);  // Left stick Y
  EXPECT_EQ(report[21], 191);  // Right stick X
  EXPECT_EQ(report[22], 191);  // Right stick Y
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

TEST(ReportTest, PacksDualSenseBluetoothReportWithCrc) {
  const auto profile = lvh::profiles::dualsense_bluetooth();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.left_stick = {1.0F, -1.0F};
  state.right_trigger = 1.0F;
  state.battery = lvh::GamepadBattery {.state = lvh::GamepadBatteryState::full, .percentage = 100};

  const auto report = lvh::reports::pack_input_report(profile, state);

  ASSERT_EQ(report.size(), profile.input_report_size);
  EXPECT_EQ(report[0], 0x31);
  EXPECT_EQ(report[1], 0x00);
  EXPECT_EQ(report[2], 255);
  EXPECT_EQ(report[3], 0);
  EXPECT_EQ(report[7], 255);
  EXPECT_EQ(report[9] & 0x20, 0x20);
  EXPECT_EQ(report[10] & 0x08, 0x08);
  EXPECT_EQ(report[54] & 0x0F, 10);
  EXPECT_EQ(report[55], 0x0C);

  const auto crc_offset = report.size() - 4U;
  const auto expected_crc = test_crc32(std::span {report}.first(crc_offset), test_playstation_crc_seed(0xA1));
  EXPECT_EQ(read_u32_le(report, crc_offset), expected_crc);
}

TEST(ReportTest, PacksDualShock4UsbReport) {
  using enum lvh::GamepadButton;

  const auto profile = lvh::profiles::dualshock4_usb();

  lvh::GamepadState state;
  state.buttons.set(x);
  state.buttons.set(a);
  state.buttons.set(b);
  state.buttons.set(y);
  state.buttons.set(left_shoulder);
  state.buttons.set(right_shoulder);
  state.buttons.set(back);
  state.buttons.set(start);
  state.buttons.set(left_stick);
  state.buttons.set(right_stick);
  state.buttons.set(guide);
  state.buttons.set(touchpad);
  state.left_stick = {1.0F, -1.0F};
  state.right_stick = {0.0F, 0.0F};
  state.left_trigger = 1.0F;
  state.right_trigger = 0.5F;
  state.acceleration = lvh::Vector3 {.x = 0.0F, .y = 9.80665F, .z = 0.0F};
  state.gyroscope = lvh::Vector3 {.x = 1.0F, .y = 2.0F, .z = 3.0F};
  state.battery = lvh::GamepadBattery {.state = lvh::GamepadBatteryState::charging, .percentage = 80};
  state.touchpad_contacts[0] = {.id = 3, .active = true, .x = 0.5F, .y = 0.25F};

  const auto report = lvh::reports::pack_input_report(profile, state);

  ASSERT_EQ(report.size(), profile.input_report_size);
  EXPECT_EQ(report[0], 0x01);
  EXPECT_EQ(report[1], 255);
  EXPECT_EQ(report[2], 0);
  EXPECT_EQ(report[3], 128);
  EXPECT_EQ(report[4], 128);
  EXPECT_EQ(report[5], 0xF8);
  EXPECT_EQ(report[6], 0xFF);
  EXPECT_EQ(report[7], 0x03);
  EXPECT_EQ(report[8], 255);
  EXPECT_EQ(report[9], 128);
  EXPECT_EQ(report[12], 204);
  EXPECT_EQ(report[13], 20);
  EXPECT_EQ(report[21], 0x10);
  EXPECT_EQ(report[22], 0x27);
  EXPECT_EQ(report[30], 0x18);
  EXPECT_EQ(report[33], 1);
  EXPECT_EQ(report[35] & 0x7F, 3);
  EXPECT_EQ(report[35] & 0x80, 0);
}

TEST(ReportTest, PacksDualShock4BluetoothReportWithCrc) {
  const auto profile = lvh::profiles::dualshock4_bluetooth();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::touchpad);
  state.battery = lvh::GamepadBattery {.state = lvh::GamepadBatteryState::full, .percentage = 100};

  const auto report = lvh::reports::pack_input_report(profile, state);

  ASSERT_EQ(report.size(), profile.input_report_size);
  EXPECT_EQ(report[0], 0x11);
  EXPECT_EQ(report[3], 128);
  EXPECT_EQ(report[4], 128);
  EXPECT_EQ(report[9], 0x02);
  EXPECT_EQ(report[32], 0x1B);
  EXPECT_EQ(report[35], 1);

  const auto crc_offset = report.size() - 4U;
  const auto expected_crc = test_crc32(std::span {report}.first(crc_offset), test_playstation_crc_seed(0xA1));
  EXPECT_EQ(read_u32_le(report, crc_offset), expected_crc);
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

TEST(ReportTest, ParsesDualSenseBluetoothOutputReportEvents) {
  const auto profile = lvh::profiles::dualsense_bluetooth();
  std::vector<std::uint8_t> report(profile.output_report_size, 0);
  report[0] = 0x31;
  report[1] = 0x02;
  report[2] = 0x0D;
  report[3] = 0x04;
  report[4] = 0x80;
  report[5] = 0x40;
  report[12] = 0x26;
  report[13] = 1;
  report[23] = 0x21;
  report[24] = 2;
  report[46] = 0x11;
  report[47] = 0x22;
  report[48] = 0x33;
  const auto crc_offset = report.size() - 4U;
  const auto crc = test_crc32(std::span {report}.first(crc_offset), test_playstation_crc_seed(0xA2));
  report[crc_offset] = static_cast<std::uint8_t>(crc & 0xFFU);
  report[crc_offset + 1U] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
  report[crc_offset + 2U] = static_cast<std::uint8_t>((crc >> 16U) & 0xFFU);
  report[crc_offset + 3U] = static_cast<std::uint8_t>((crc >> 24U) & 0xFFU);

  const auto outputs = lvh::reports::parse_output_reports(profile, report);

  ASSERT_EQ(outputs.size(), 3U);
  EXPECT_EQ(outputs[0].kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(outputs[1].kind, lvh::GamepadOutputKind::rgb_led);
  EXPECT_EQ(outputs[1].red, 0x11);
  EXPECT_EQ(outputs[1].green, 0x22);
  EXPECT_EQ(outputs[1].blue, 0x33);
  EXPECT_EQ(outputs[2].kind, lvh::GamepadOutputKind::adaptive_triggers);
}

TEST(ReportTest, ParsesDualShock4OutputReportEvents) {
  const auto profile = lvh::profiles::dualshock4_usb();
  std::vector<std::uint8_t> report(profile.output_report_size, 0);
  report[0] = 0x05;
  report[1] = 0x03;
  report[4] = 0x80;
  report[5] = 0x40;
  report[6] = 0x11;
  report[7] = 0x22;
  report[8] = 0x33;

  const auto outputs = lvh::reports::parse_output_reports(profile, report);

  ASSERT_EQ(outputs.size(), 2U);
  EXPECT_EQ(outputs[0].kind, lvh::GamepadOutputKind::rumble);
  EXPECT_GT(outputs[0].low_frequency_rumble, 0U);
  EXPECT_GT(outputs[0].high_frequency_rumble, 0U);
  EXPECT_EQ(outputs[1].kind, lvh::GamepadOutputKind::rgb_led);
  EXPECT_EQ(outputs[1].red, 0x11);
  EXPECT_EQ(outputs[1].green, 0x22);
  EXPECT_EQ(outputs[1].blue, 0x33);
}

TEST(ReportTest, ParsesDualShock4BluetoothOutputReportEvents) {
  const auto profile = lvh::profiles::dualshock4_bluetooth();
  std::vector<std::uint8_t> report(profile.output_report_size, 0);
  report[0] = 0x11;
  report[1] = 0xC0;
  report[3] = 0x03;
  report[6] = 0x80;
  report[7] = 0x40;
  report[8] = 0x11;
  report[9] = 0x22;
  report[10] = 0x33;
  const auto crc_offset = report.size() - 4U;
  const auto crc = test_crc32(std::span {report}.first(crc_offset), test_playstation_crc_seed(0xA2));
  report[crc_offset] = static_cast<std::uint8_t>(crc & 0xFFU);
  report[crc_offset + 1U] = static_cast<std::uint8_t>((crc >> 8U) & 0xFFU);
  report[crc_offset + 2U] = static_cast<std::uint8_t>((crc >> 16U) & 0xFFU);
  report[crc_offset + 3U] = static_cast<std::uint8_t>((crc >> 24U) & 0xFFU);

  const auto outputs = lvh::reports::parse_output_reports(profile, report);

  ASSERT_EQ(outputs.size(), 2U);
  EXPECT_EQ(outputs[0].kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(outputs[1].kind, lvh::GamepadOutputKind::rgb_led);
  EXPECT_EQ(outputs[1].red, 0x11);
  EXPECT_EQ(outputs[1].green, 0x22);
  EXPECT_EQ(outputs[1].blue, 0x33);
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
