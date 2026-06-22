/**
 * @file tests/unit/test_windows_protocol.cpp
 * @brief Unit tests for the Windows control protocol helpers.
 */

// local includes
#include "fixtures/fixtures.hpp"
#include "platform/windows/control_protocol.hpp"

// standard includes
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <libvirtualhid/profiles.hpp>

namespace {

  lvh::DeviceProfile minimal_gamepad_profile() {
    lvh::DeviceProfile profile;
    profile.device_type = lvh::DeviceType::gamepad;
    profile.gamepad_kind = lvh::GamepadProfileKind::generic;
    profile.bus_type = lvh::BusType::usb;
    profile.vendor_id = 0x1209;
    profile.product_id = 0x0001;
    profile.version = 0x0001;
    profile.report_id = 1;
    profile.input_report_size = 4;
    profile.output_report_size = 2;
    profile.name = "test gamepad";
    profile.manufacturer = "test manufacturer";
    profile.report_descriptor = {0x05, 0x01, 0x09, 0x05};
    return profile;
  }

}  // namespace

TEST(WindowsProtocolTest, ExposesStableProtocolConstants) {
  EXPECT_STREQ(lvh::detail::windows::default_control_device_path.data(), R"(\\.\LibVirtualHid)");

  EXPECT_EQ(LVH_WINDOWS_IOCTL_CREATE_GAMEPAD, 0x8000E000U);
  EXPECT_EQ(LVH_WINDOWS_IOCTL_DESTROY_DEVICE, 0x8000E004U);
  EXPECT_EQ(LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT, 0x8000E008U);
  EXPECT_EQ(LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT, 0x8000600CU);

  EXPECT_EQ(sizeof(LvhWindowsGamepadHardwareIds), 14U);
  EXPECT_EQ(sizeof(LvhWindowsGamepadReportSizes), 24U);
  EXPECT_EQ(sizeof(LvhWindowsCreateGamepadRequest), 1474U);
  EXPECT_EQ(sizeof(LvhWindowsCreateGamepadResponse), 284U);
  EXPECT_EQ(sizeof(LvhWindowsDestroyDeviceRequest), 16U);
  EXPECT_EQ(sizeof(LvhWindowsSubmitInputReportRequest), 280U);
  EXPECT_EQ(sizeof(LvhWindowsOutputReportEvent), 280U);
}

TEST(WindowsProtocolTest, MapsBusTypesAndGamepadKinds) {
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(lvh::BusType::unknown), LVH_WINDOWS_BUS_UNKNOWN);
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(lvh::BusType::usb), LVH_WINDOWS_BUS_USB);
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(lvh::BusType::bluetooth), LVH_WINDOWS_BUS_BLUETOOTH);
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(static_cast<lvh::BusType>(255)), LVH_WINDOWS_BUS_UNKNOWN);

  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::generic),
    LVH_WINDOWS_GAMEPAD_GENERIC
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::xbox_360),
    LVH_WINDOWS_GAMEPAD_XBOX_360
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::xbox_one),
    LVH_WINDOWS_GAMEPAD_XBOX_ONE
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::xbox_series),
    LVH_WINDOWS_GAMEPAD_XBOX_SERIES
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::dualshock4),
    LVH_WINDOWS_GAMEPAD_DUALSHOCK4
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::dualsense),
    LVH_WINDOWS_GAMEPAD_DUALSENSE
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::switch_pro),
    LVH_WINDOWS_GAMEPAD_SWITCH_PRO
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(static_cast<lvh::GamepadProfileKind>(255)),
    LVH_WINDOWS_GAMEPAD_GENERIC
  );
}

TEST(WindowsProtocolTest, BuildsCapabilityFlags) {
  lvh::GamepadProfileCapabilities capabilities;
  EXPECT_EQ(lvh::detail::windows::gamepad_flags(capabilities), 0U);

  capabilities.supports_rumble = true;
  capabilities.supports_motion = true;
  capabilities.supports_touchpad = true;
  capabilities.supports_rgb_led = true;
  capabilities.supports_battery = true;
  capabilities.supports_adaptive_triggers = true;

  const auto flags = lvh::detail::windows::gamepad_flags(capabilities);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS, 0U);
}

TEST(WindowsProtocolTest, CopyHelpersTruncateAndZeroFill) {
  LvhWindowsCreateGamepadRequest create_request {};
  create_request.name[0] = 'x';
  create_request.name[1] = 'x';
  create_request.name[2] = 'x';
  create_request.name[3] = 'x';
  create_request.name[4] = 'x';
  const std::string oversized_name(LVH_WINDOWS_MAX_DEVICE_NAME_SIZE + 5U, 'a');
  EXPECT_EQ(
    lvh::detail::windows::copy_string(create_request.name, oversized_name),
    LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U
  );
  EXPECT_EQ(
    std::string_view {create_request.name},
    std::string_view {oversized_name}.substr(0U, LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U)
  );
  EXPECT_EQ(create_request.name[LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U], '\0');

  LvhWindowsSubmitInputReportRequest submit_request {};
  submit_request.report[0] = 0xFF;
  submit_request.report[1] = 0xFF;
  submit_request.report[2] = 0xFF;
  submit_request.report[3] = 0xFF;
  submit_request.report[4] = 0xFF;
  const std::vector<std::uint8_t> source {1, 2};
  EXPECT_EQ(lvh::detail::windows::copy_bytes(submit_request.report, source), 2U);
  EXPECT_EQ(submit_request.report[0], 1U);
  EXPECT_EQ(submit_request.report[1], 2U);
  EXPECT_EQ(submit_request.report[2], 0U);
  EXPECT_EQ(submit_request.report[3], 0U);
  EXPECT_EQ(submit_request.report[4], 0U);
}

TEST(WindowsProtocolTest, PacksGamepadCreateRequest) {
  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::dualsense_bluetooth();
  options.metadata.stable_id = "client-0-controller-1";

  const auto request = lvh::detail::windows::make_create_gamepad_request(42, options);

  EXPECT_EQ(request.version, LVH_WINDOWS_CONTROL_PROTOCOL_VERSION);
  EXPECT_EQ(request.size, sizeof(request));
  EXPECT_EQ(request.client_device_id, 42U);
  EXPECT_EQ(request.bus_type, LVH_WINDOWS_BUS_BLUETOOTH);
  EXPECT_EQ(request.gamepad_kind, LVH_WINDOWS_GAMEPAD_DUALSENSE);
  EXPECT_EQ(request.hardware_ids.vendor_id, options.profile.vendor_id);
  EXPECT_EQ(request.hardware_ids.product_id, options.profile.product_id);
  EXPECT_EQ(request.hardware_ids.device_version, options.profile.version);
  EXPECT_EQ(request.hardware_ids.report_id, options.profile.report_id);
  EXPECT_EQ(request.report_sizes.input_report_size, options.profile.input_report_size);
  EXPECT_EQ(request.report_sizes.output_report_size, options.profile.output_report_size);
  EXPECT_EQ(request.report_sizes.report_descriptor_size, options.profile.report_descriptor.size());
  EXPECT_EQ(request.report_descriptor[0], options.profile.report_descriptor[0]);
  EXPECT_STREQ(request.name, options.profile.name.c_str());
  EXPECT_STREQ(request.manufacturer, options.profile.manufacturer.c_str());
  EXPECT_STREQ(request.stable_id, options.metadata.stable_id.c_str());
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY, 0U);
}

TEST(WindowsProtocolTest, PacksGenericUnknownBusGamepadCreateRequestWithoutOptionalFlags) {
  lvh::CreateGamepadOptions options;
  options.profile = minimal_gamepad_profile();
  options.profile.bus_type = lvh::BusType::unknown;
  options.profile.output_report_size = 0;

  const auto request = lvh::detail::windows::make_create_gamepad_request(7, options);

  EXPECT_EQ(request.bus_type, LVH_WINDOWS_BUS_UNKNOWN);
  EXPECT_EQ(request.gamepad_kind, LVH_WINDOWS_GAMEPAD_GENERIC);
  EXPECT_EQ(request.flags, 0U);
  EXPECT_EQ(request.report_sizes.output_report_size, 0U);
  EXPECT_EQ(request.report_sizes.name_size, options.profile.name.size());
  EXPECT_EQ(request.report_sizes.manufacturer_size, options.profile.manufacturer.size());
  EXPECT_EQ(request.report_sizes.stable_id_size, 0U);
}

TEST(WindowsProtocolTest, TruncatesOversizedGamepadCreateRequestFields) {
  lvh::CreateGamepadOptions options;
  options.profile = minimal_gamepad_profile();
  options.profile.input_report_size = LVH_WINDOWS_MAX_INPUT_REPORT_SIZE + 1U;
  options.profile.output_report_size = LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE + 1U;
  options.profile.report_descriptor.assign(LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE + 5U, 0xAB);
  options.profile.name.assign(LVH_WINDOWS_MAX_DEVICE_NAME_SIZE + 5U, 'n');
  options.profile.manufacturer.assign(LVH_WINDOWS_MAX_MANUFACTURER_SIZE + 5U, 'm');
  options.metadata.stable_id.assign(LVH_WINDOWS_MAX_STABLE_ID_SIZE + 5U, 's');

  const auto request = lvh::detail::windows::make_create_gamepad_request(9, options);

  EXPECT_EQ(request.report_sizes.input_report_size, LVH_WINDOWS_MAX_INPUT_REPORT_SIZE);
  EXPECT_EQ(request.report_sizes.output_report_size, LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE);
  EXPECT_EQ(request.report_sizes.report_descriptor_size, LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE);
  EXPECT_EQ(request.report_descriptor[0], 0xABU);
  EXPECT_EQ(request.report_descriptor[LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE - 1U], 0xABU);

  EXPECT_EQ(request.report_sizes.name_size, LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U);
  EXPECT_EQ(request.name[LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 2U], 'n');
  EXPECT_EQ(request.name[LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U], '\0');

  EXPECT_EQ(request.report_sizes.manufacturer_size, LVH_WINDOWS_MAX_MANUFACTURER_SIZE - 1U);
  EXPECT_EQ(request.manufacturer[LVH_WINDOWS_MAX_MANUFACTURER_SIZE - 2U], 'm');
  EXPECT_EQ(request.manufacturer[LVH_WINDOWS_MAX_MANUFACTURER_SIZE - 1U], '\0');

  EXPECT_EQ(request.report_sizes.stable_id_size, LVH_WINDOWS_MAX_STABLE_ID_SIZE - 1U);
  EXPECT_EQ(request.stable_id[LVH_WINDOWS_MAX_STABLE_ID_SIZE - 2U], 's');
  EXPECT_EQ(request.stable_id[LVH_WINDOWS_MAX_STABLE_ID_SIZE - 1U], '\0');
}

TEST(WindowsProtocolTest, PacksSubmitAndDestroyRequests) {
  const std::vector<std::uint8_t> report {1, 2, 3, 4, 5};

  const auto submit = lvh::detail::windows::make_submit_input_report_request(17, report);
  EXPECT_EQ(submit.version, LVH_WINDOWS_CONTROL_PROTOCOL_VERSION);
  EXPECT_EQ(submit.size, sizeof(submit));
  EXPECT_EQ(submit.driver_device_id, 17U);
  EXPECT_EQ(submit.report_size, report.size());
  EXPECT_EQ(submit.report[0], report[0]);
  EXPECT_EQ(submit.report[4], report[4]);

  const auto destroy = lvh::detail::windows::make_destroy_device_request(17);
  EXPECT_EQ(destroy.version, LVH_WINDOWS_CONTROL_PROTOCOL_VERSION);
  EXPECT_EQ(destroy.size, sizeof(destroy));
  EXPECT_EQ(destroy.driver_device_id, 17U);
}

TEST(WindowsProtocolTest, SubmitInputReportTruncatesAndZeroFills) {
  std::vector<std::uint8_t> oversized_report(LVH_WINDOWS_MAX_INPUT_REPORT_SIZE + 3U, 0x7F);
  const auto oversized = lvh::detail::windows::make_submit_input_report_request(19, oversized_report);

  EXPECT_EQ(oversized.report_size, LVH_WINDOWS_MAX_INPUT_REPORT_SIZE);
  EXPECT_EQ(oversized.report[0], 0x7FU);
  EXPECT_EQ(oversized.report[LVH_WINDOWS_MAX_INPUT_REPORT_SIZE - 1U], 0x7FU);

  const auto empty = lvh::detail::windows::make_submit_input_report_request(19, {});
  EXPECT_EQ(empty.report_size, 0U);
  EXPECT_EQ(empty.report[0], 0U);
  EXPECT_EQ(empty.report[LVH_WINDOWS_MAX_INPUT_REPORT_SIZE - 1U], 0U);
}
