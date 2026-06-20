/**
 * @file tests/unit/test_windows_protocol.cpp
 * @brief Unit tests for the Windows control protocol helpers.
 */

// local includes
#include "fixtures/fixtures.hpp"
#include "platform/windows/control_protocol.hpp"

// standard includes
#include <cstdint>
#include <vector>

// lib includes
#include <libvirtualhid/profiles.hpp>

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
  EXPECT_EQ(request.vendor_id, options.profile.vendor_id);
  EXPECT_EQ(request.product_id, options.profile.product_id);
  EXPECT_EQ(request.device_version, options.profile.version);
  EXPECT_EQ(request.report_id, options.profile.report_id);
  EXPECT_EQ(request.input_report_size, options.profile.input_report_size);
  EXPECT_EQ(request.output_report_size, options.profile.output_report_size);
  EXPECT_EQ(request.report_descriptor_size, options.profile.report_descriptor.size());
  EXPECT_EQ(request.report_descriptor[0], options.profile.report_descriptor[0]);
  EXPECT_STREQ(request.name, options.profile.name.c_str());
  EXPECT_STREQ(request.manufacturer, options.profile.manufacturer.c_str());
  EXPECT_STREQ(request.stable_id, options.metadata.stable_id.c_str());
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY, 0U);
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
