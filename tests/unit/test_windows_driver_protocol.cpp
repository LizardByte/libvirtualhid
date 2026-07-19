/**
 * @file tests/unit/test_windows_driver_protocol.cpp
 * @brief Unit tests for private Windows driver identity and controller protocol helpers.
 */

// local includes
#include "fixtures/fixtures.hpp"
#include "playstation_feature_protocol.hpp"
#include "switch_pro_protocol.hpp"
#include "windows_device_identity.hpp"

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

  class WindowsDriverProtocolTest: public WindowsTest {};

  std::vector<std::wstring> hardware_id_entries(const std::wstring &hardware_ids) {
    std::vector<std::wstring> entries;
    auto offset = std::size_t {};
    while (offset < hardware_ids.size() && hardware_ids[offset] != L'\0') {
      const auto end = hardware_ids.find(L'\0', offset);
      entries.emplace_back(hardware_ids.substr(offset, end - offset));
      offset = end + 1U;
    }
    return entries;
  }

  LvhWindowsCreateGamepadRequest series_request() {
    LvhWindowsCreateGamepadRequest request {};
    request.gamepad_kind = LVH_WINDOWS_GAMEPAD_XBOX_SERIES;
    request.hardware_ids.vendor_id = 0x045E;
    request.hardware_ids.product_id = 0x0B12;
    request.hardware_ids.device_version = 0x0500;
    return request;
  }

  LvhWindowsCreateGamepadRequest playstation_request(
    std::uint32_t gamepad_kind,
    std::uint32_t bus_type = LVH_WINDOWS_BUS_USB,
    std::string_view stable_id = "10:20:30:40:50:60"
  ) {
    LvhWindowsCreateGamepadRequest request {};
    request.client_device_id = 0x12345678U;
    request.gamepad_kind = gamepad_kind;
    request.bus_type = bus_type;
    request.report_sizes.stable_id_size = static_cast<std::uint32_t>(stable_id.size());
    std::ranges::copy(stable_id, request.stable_id);
    return request;
  }

  std::array<std::uint8_t, lvh::detail::windows::switch_pro_report_size> switch_output_report(
    std::uint8_t report_id,
    std::uint8_t command
  ) {
    std::array<std::uint8_t, lvh::detail::windows::switch_pro_report_size> report {};
    report[0] = report_id;
    if (report_id == 0x80U) {
      report[1] = command;
    } else {
      report[1] = 0x07;
      report[10] = command;
    }
    return report;
  }

  void write_u32(std::span<std::uint8_t> report, std::size_t offset, std::uint32_t value) {
    report[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    report[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    report[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    report[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
  }

}  // namespace

TEST_F(WindowsDriverProtocolTest, SeriesUsesXInputHidMatchIdAndPreservesPublicIdentity) {
  const auto request = series_request();

  EXPECT_EQ(lvh::detail::windows::xinputhid_match_product_id(request), 0x0B13);
  const auto entries = hardware_id_entries(lvh::detail::windows::make_hardware_ids(request));
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0], L"HID\\VID_045E&PID_0B13&IG_00");
  EXPECT_EQ(entries[1], L"HID\\VID_045E&PID_0B12&REV_0500");
  EXPECT_EQ(entries[2], L"HID\\VID_045E&PID_0B12");
}

TEST_F(WindowsDriverProtocolTest, NonSeriesXboxUsesItsPublicIdentityForMatching) {
  auto request = series_request();
  request.gamepad_kind = LVH_WINDOWS_GAMEPAD_XBOX_ONE;
  request.hardware_ids.product_id = 0x02EA;
  request.hardware_ids.device_version = 0x0408;

  EXPECT_EQ(lvh::detail::windows::xinputhid_match_product_id(request), 0x02EA);
  const auto entries = hardware_id_entries(lvh::detail::windows::make_hardware_ids(request));
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0], L"HID\\VID_045E&PID_02EA&IG_00");
  EXPECT_EQ(entries[1], L"HID\\VID_045E&PID_02EA&REV_0408");
  EXPECT_EQ(entries[2], L"HID\\VID_045E&PID_02EA");
}

TEST_F(WindowsDriverProtocolTest, SwitchProRepliesToUsbStatusAndHandshakeCommands) {
  auto status_report = switch_output_report(0x80, 0x01);
  const auto status_reply = lvh::detail::windows::make_switch_pro_reply(status_report);
  ASSERT_TRUE(status_reply.has_value());
  EXPECT_EQ(status_reply->at(0), 0x81);
  EXPECT_EQ(status_reply->at(1), 0x01);
  EXPECT_EQ(status_reply->at(3), 0x03);
  EXPECT_TRUE(std::ranges::any_of(status_reply->begin() + 4, status_reply->begin() + 10, [](auto value) {
    return value != 0U;
  }));

  for (const auto command : {0x02U, 0x03U}) {
    const auto report = switch_output_report(0x80, static_cast<std::uint8_t>(command));
    const auto reply = lvh::detail::windows::make_switch_pro_reply(report);
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->at(0), 0x81);
    EXPECT_EQ(reply->at(1), command);
  }

  const auto force_usb = switch_output_report(0x80, 0x04);
  EXPECT_FALSE(lvh::detail::windows::make_switch_pro_reply(force_usb).has_value());
}

TEST_F(WindowsDriverProtocolTest, SwitchProAcknowledgesInitializationSubcommands) {
  for (const auto subcommand : {0x03U, 0x30U, 0x38U, 0x40U, 0x41U, 0x48U}) {
    const auto report = switch_output_report(0x01, static_cast<std::uint8_t>(subcommand));
    const auto reply = lvh::detail::windows::make_switch_pro_reply(report);
    ASSERT_TRUE(reply.has_value());
    EXPECT_EQ(reply->at(0), 0x21);
    EXPECT_EQ(reply->at(1), 0x07);
    EXPECT_EQ(reply->at(2), 0x81);
    EXPECT_EQ(reply->at(13), 0x80);
    EXPECT_EQ(reply->at(14), subcommand);
    EXPECT_EQ(reply->at(6), 0x00);
    EXPECT_EQ(reply->at(7), 0x08);
    EXPECT_EQ(reply->at(8), 0x80);
    EXPECT_EQ(reply->at(9), 0x00);
    EXPECT_EQ(reply->at(10), 0x08);
    EXPECT_EQ(reply->at(11), 0x80);
  }

  const auto rumble_only = switch_output_report(0x10, 0x00);
  EXPECT_FALSE(lvh::detail::windows::make_switch_pro_reply(rumble_only).has_value());
}

TEST_F(WindowsDriverProtocolTest, SwitchProReturnsDeviceInfoAndFactoryCalibration) {
  auto device_info = switch_output_report(0x01, 0x02);
  const auto device_reply = lvh::detail::windows::make_switch_pro_reply(device_info);
  ASSERT_TRUE(device_reply.has_value());
  EXPECT_EQ(device_reply->at(13), 0x82);
  EXPECT_EQ(device_reply->at(14), 0x02);
  EXPECT_EQ(device_reply->at(17), 0x03);

  auto stick_read = switch_output_report(0x01, 0x10);
  write_u32(stick_read, 11U, 0x603D);
  stick_read[15] = 18U;
  const auto stick_reply = lvh::detail::windows::make_switch_pro_reply(stick_read);
  ASSERT_TRUE(stick_reply.has_value());
  EXPECT_EQ(stick_reply->at(13), 0x90);
  EXPECT_EQ(stick_reply->at(14), 0x10);
  EXPECT_TRUE(std::equal(stick_read.begin() + 11, stick_read.begin() + 16, stick_reply->begin() + 15));
  constexpr std::array<std::uint8_t, 18> expected_stick_calibration {
    0xFF,
    0xF7,
    0x7F,
    0x00,
    0x08,
    0x80,
    0xFF,
    0xF7,
    0x7F,
    0x00,
    0x08,
    0x80,
    0xFF,
    0xF7,
    0x7F,
    0xFF,
    0xF7,
    0x7F,
  };
  EXPECT_TRUE(std::equal(expected_stick_calibration.begin(), expected_stick_calibration.end(), stick_reply->begin() + 20));

  auto imu_read = switch_output_report(0x01, 0x10);
  write_u32(imu_read, 11U, 0x6020);
  imu_read[15] = 24U;
  const auto imu_reply = lvh::detail::windows::make_switch_pro_reply(imu_read);
  ASSERT_TRUE(imu_reply.has_value());
  EXPECT_TRUE(std::ranges::any_of(imu_reply->begin() + 20, imu_reply->begin() + 44, [](auto value) {
    return value != 0U;
  }));

  auto user_read = switch_output_report(0x01, 0x10);
  write_u32(user_read, 11U, 0x8010);
  user_read[15] = 22U;
  const auto user_reply = lvh::detail::windows::make_switch_pro_reply(user_read);
  ASSERT_TRUE(user_reply.has_value());
  EXPECT_TRUE(std::ranges::all_of(user_reply->begin() + 20, user_reply->begin() + 42, [](auto value) {
    return value == 0U;
  }));
}

TEST_F(WindowsDriverProtocolTest, DualShock4ReturnsCalibrationPairingAndFirmwareFeatures) {
  const auto request = playstation_request(LVH_WINDOWS_GAMEPAD_DUALSHOCK4);

  const auto calibration = lvh::detail::windows::make_playstation_feature_report(request, 0x02);
  ASSERT_TRUE(calibration.has_value());
  ASSERT_EQ(calibration->size(), 37U);
  EXPECT_EQ(calibration->front(), 0x02);
  EXPECT_EQ(calibration->at(7), 0x10);
  EXPECT_EQ(calibration->at(8), 0x27);
  EXPECT_FALSE(lvh::detail::windows::make_playstation_feature_report(request, 0x05).has_value());

  const auto pairing = lvh::detail::windows::make_playstation_feature_report(request, 0x12);
  ASSERT_TRUE(pairing.has_value());
  ASSERT_EQ(pairing->size(), 16U);
  constexpr std::array<std::uint8_t, 6> expected_mac {0x60, 0x50, 0x40, 0x30, 0x20, 0x10};
  EXPECT_TRUE(std::equal(expected_mac.begin(), expected_mac.end(), pairing->begin() + 1));

  const auto firmware = lvh::detail::windows::make_playstation_feature_report(request, 0xA3);
  ASSERT_TRUE(firmware.has_value());
  EXPECT_EQ(firmware->size(), 49U);
  EXPECT_EQ(firmware->front(), 0xA3);
}

TEST_F(WindowsDriverProtocolTest, DualSenseReturnsCalibrationPairingAndFirmwareFeatures) {
  const auto request = playstation_request(LVH_WINDOWS_GAMEPAD_DUALSENSE);

  const auto calibration = lvh::detail::windows::make_playstation_feature_report(request, 0x05);
  ASSERT_TRUE(calibration.has_value());
  EXPECT_EQ(calibration->size(), 41U);
  EXPECT_EQ(calibration->front(), 0x05);

  const auto pairing = lvh::detail::windows::make_playstation_feature_report(request, 0x09);
  ASSERT_TRUE(pairing.has_value());
  ASSERT_EQ(pairing->size(), 20U);
  constexpr std::array<std::uint8_t, 6> expected_mac {0x60, 0x50, 0x40, 0x30, 0x20, 0x10};
  EXPECT_TRUE(std::equal(expected_mac.begin(), expected_mac.end(), pairing->begin() + 1));

  const auto firmware = lvh::detail::windows::make_playstation_feature_report(request, 0x20);
  ASSERT_TRUE(firmware.has_value());
  EXPECT_EQ(firmware->size(), 64U);
  EXPECT_EQ(firmware->front(), 0x20);
  EXPECT_FALSE(lvh::detail::windows::make_playstation_feature_report(request, 0xFF).has_value());
}

TEST_F(WindowsDriverProtocolTest, BluetoothPlayStationFeaturesCarryCrcAndGeneratedPairingMac) {
  auto request = playstation_request(LVH_WINDOWS_GAMEPAD_DUALSENSE, LVH_WINDOWS_BUS_BLUETOOTH, "not-a-mac");

  const auto pairing = lvh::detail::windows::make_playstation_feature_report(request, 0x09);
  ASSERT_TRUE(pairing.has_value());
  ASSERT_EQ(pairing->size(), 20U);
  constexpr std::array<std::uint8_t, 6> expected_generated_mac {0x78, 0x56, 0x34, 0x12, 0x00, 0x02};
  EXPECT_TRUE(std::equal(expected_generated_mac.begin(), expected_generated_mac.end(), pairing->begin() + 1));
  EXPECT_TRUE(std::ranges::any_of(pairing->end() - 4, pairing->end(), [](auto value) {
    return value != 0U;
  }));

  request.gamepad_kind = LVH_WINDOWS_GAMEPAD_DUALSHOCK4;
  const auto bluetooth_calibration = lvh::detail::windows::make_playstation_feature_report(request, 0x05);
  ASSERT_TRUE(bluetooth_calibration.has_value());
  EXPECT_EQ(bluetooth_calibration->size(), 41U);
  EXPECT_TRUE(std::ranges::any_of(bluetooth_calibration->end() - 4, bluetooth_calibration->end(), [](auto value) {
    return value != 0U;
  }));
}
