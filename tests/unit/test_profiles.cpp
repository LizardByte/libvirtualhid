/**
 * @file tests/unit/test_profiles.cpp
 * @brief Unit tests for built-in gamepad profiles.
 */

// standard includes
#include <algorithm>
#include <array>

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/profiles.hpp>

TEST(ProfileTest, BuiltInProfilesHaveDescriptors) {
  const auto profiles = lvh::profiles::built_in_gamepad_profiles();

  ASSERT_GE(profiles.size(), 4U);
  for (const auto &profile : profiles) {
    EXPECT_EQ(profile.device_type, lvh::DeviceType::gamepad);
    EXPECT_FALSE(profile.name.empty());
    EXPECT_NE(profile.vendor_id, 0);
    EXPECT_NE(profile.product_id, 0);
    EXPECT_NE(profile.report_id, 0);
    EXPECT_GE(profile.input_report_size, 10U);
    EXPECT_FALSE(profile.report_descriptor.empty());
  }
}

TEST(ProfileTest, StreamingControllerProfilesArePresent) {
  const auto xbox_one = lvh::profiles::xbox_one();
  const auto dualshock4 = lvh::profiles::dualshock4();
  const auto dualsense = lvh::profiles::dualsense();
  const auto switch_pro = lvh::profiles::switch_pro();

  EXPECT_EQ(xbox_one.vendor_id, 0x045E);
  EXPECT_EQ(xbox_one.product_id, 0x02EA);
  EXPECT_TRUE(xbox_one.capabilities.supports_rumble);
  EXPECT_EQ(xbox_one.input_report_size, 10U);

  const std::array<std::uint8_t, 9> byte_axis_descriptor {
    0x15,
    0x00,
    0x26,
    0xFF,
    0x00,
    0x75,
    0x08,
    0x95,
    0x04,
  };
  EXPECT_TRUE(std::ranges::search(xbox_one.report_descriptor, byte_axis_descriptor).begin() != xbox_one.report_descriptor.end());

  const std::array<std::uint8_t, 8> stick_usage_descriptor {
    0x09,
    0x30,
    0x09,
    0x31,
    0x09,
    0x32,
    0x09,
    0x35,
  };
  EXPECT_TRUE(std::ranges::search(xbox_one.report_descriptor, stick_usage_descriptor).begin() != xbox_one.report_descriptor.end());

  const std::array<std::uint8_t, 4> trigger_slider_descriptor {
    0x09,
    0x36,
    0x09,
    0x36,
  };
  EXPECT_TRUE(std::ranges::search(xbox_one.report_descriptor, trigger_slider_descriptor).begin() != xbox_one.report_descriptor.end());

  EXPECT_EQ(dualshock4.vendor_id, 0x054C);
  EXPECT_EQ(dualshock4.product_id, 0x05C4);
  EXPECT_EQ(dualshock4.input_report_size, 64U);
  EXPECT_EQ(dualshock4.output_report_size, 32U);
  EXPECT_TRUE(dualshock4.capabilities.supports_motion);
  EXPECT_TRUE(dualshock4.capabilities.supports_touchpad);
  EXPECT_TRUE(dualshock4.capabilities.supports_rgb_led);
  EXPECT_TRUE(dualshock4.capabilities.supports_battery);
  EXPECT_FALSE(dualshock4.capabilities.supports_adaptive_triggers);
  EXPECT_EQ(dualshock4.manufacturer, "Sony Interactive Entertainment");

  const auto dualshock4_bluetooth = lvh::profiles::dualshock4_bluetooth();
  EXPECT_EQ(dualshock4_bluetooth.bus_type, lvh::BusType::bluetooth);
  EXPECT_EQ(dualshock4_bluetooth.report_id, 0x11);
  EXPECT_EQ(dualshock4_bluetooth.input_report_size, 78U);
  EXPECT_EQ(dualshock4_bluetooth.output_report_size, 78U);
  EXPECT_NE(dualshock4_bluetooth.report_descriptor, dualshock4.report_descriptor);

  EXPECT_EQ(dualsense.vendor_id, 0x054C);
  EXPECT_TRUE(dualsense.capabilities.supports_motion);
  EXPECT_TRUE(dualsense.capabilities.supports_touchpad);
  EXPECT_TRUE(dualsense.capabilities.supports_rgb_led);
  EXPECT_TRUE(dualsense.capabilities.supports_adaptive_triggers);
  EXPECT_GT(dualsense.input_report_size, 14U);
  EXPECT_GT(dualsense.output_report_size, 5U);
  EXPECT_EQ(dualsense.manufacturer, "Sony Interactive Entertainment");

  const auto dualsense_bluetooth = lvh::profiles::dualsense_bluetooth();
  EXPECT_EQ(dualsense_bluetooth.bus_type, lvh::BusType::bluetooth);
  EXPECT_EQ(dualsense_bluetooth.report_id, 0x31);
  EXPECT_EQ(dualsense_bluetooth.input_report_size, 78U);
  EXPECT_EQ(dualsense_bluetooth.output_report_size, 78U);
  EXPECT_NE(dualsense_bluetooth.report_descriptor, dualsense.report_descriptor);

  EXPECT_EQ(switch_pro.vendor_id, 0x057E);
  EXPECT_EQ(switch_pro.product_id, 0x2009);
}

TEST(ProfileTest, RumbleProfilesExposeOutputReports) {
  const auto generic = lvh::profiles::generic_gamepad();
  const auto xbox_360 = lvh::profiles::xbox_360();

  EXPECT_FALSE(generic.capabilities.supports_rumble);
  EXPECT_EQ(generic.output_report_size, 0U);

  EXPECT_TRUE(xbox_360.capabilities.supports_rumble);
  EXPECT_EQ(xbox_360.output_report_size, 5U);
  ASSERT_GE(xbox_360.report_descriptor.size(), 3U);
  EXPECT_EQ(xbox_360.report_descriptor[xbox_360.report_descriptor.size() - 3U], 0x91);
  EXPECT_EQ(xbox_360.report_descriptor[xbox_360.report_descriptor.size() - 2U], 0x02);
  EXPECT_EQ(xbox_360.report_descriptor.back(), 0xC0);
}

TEST(ProfileTest, CanFindProfileByKind) {
  const auto profile = lvh::profiles::gamepad_profile(lvh::GamepadProfileKind::xbox_series);

  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile->gamepad_kind, lvh::GamepadProfileKind::xbox_series);
}

TEST(ProfileTest, PointerProfilesArePresent) {
  const auto keyboard = lvh::profiles::keyboard();
  const auto mouse = lvh::profiles::mouse();
  const auto touchscreen = lvh::profiles::touchscreen();
  const auto trackpad = lvh::profiles::trackpad();
  const auto pen_tablet = lvh::profiles::pen_tablet();

  EXPECT_EQ(keyboard.device_type, lvh::DeviceType::keyboard);
  EXPECT_FALSE(keyboard.name.empty());
  EXPECT_NE(keyboard.vendor_id, 0);
  EXPECT_NE(keyboard.product_id, 0);

  EXPECT_EQ(mouse.device_type, lvh::DeviceType::mouse);
  EXPECT_FALSE(mouse.name.empty());
  EXPECT_NE(mouse.vendor_id, 0);
  EXPECT_NE(mouse.product_id, 0);

  EXPECT_EQ(touchscreen.device_type, lvh::DeviceType::touchscreen);
  EXPECT_FALSE(touchscreen.name.empty());
  EXPECT_NE(touchscreen.product_id, 0);

  EXPECT_EQ(trackpad.device_type, lvh::DeviceType::trackpad);
  EXPECT_FALSE(trackpad.name.empty());
  EXPECT_NE(trackpad.product_id, 0);

  EXPECT_EQ(pen_tablet.device_type, lvh::DeviceType::pen_tablet);
  EXPECT_FALSE(pen_tablet.name.empty());
  EXPECT_NE(pen_tablet.product_id, 0);
}
