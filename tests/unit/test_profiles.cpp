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
    EXPECT_GE(profile.input_report_size, 9U);
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
  EXPECT_EQ(xbox_one.bus_type, lvh::BusType::usb);
  EXPECT_EQ(xbox_one.manufacturer, "Microsoft");
  EXPECT_TRUE(xbox_one.capabilities.supports_rumble);
  EXPECT_EQ(xbox_one.report_id, 0);
  EXPECT_EQ(xbox_one.input_report_size, 17U);

  const auto xbox_series = lvh::profiles::xbox_series();
  EXPECT_EQ(xbox_series.vendor_id, 0x045E);
  EXPECT_EQ(xbox_series.product_id, 0x0B12);
  EXPECT_EQ(xbox_series.bus_type, lvh::BusType::usb);
  EXPECT_EQ(xbox_series.name, "Xbox Controller");
  EXPECT_EQ(xbox_series.manufacturer, "Microsoft");
  EXPECT_EQ(xbox_series.report_id, 0);
  EXPECT_EQ(xbox_series.input_report_size, 17U);

  const std::array<std::uint8_t, 12> xbox_gip_button_descriptor {
    0x05,
    0x09,
    0x19,
    0x01,
    0x29,
    0x0A,
    0x95,
    0x0A,
    0x75,
    0x01,
    0x81,
    0x02,
  };
  EXPECT_TRUE(
    std::ranges::search(xbox_one.report_descriptor, xbox_gip_button_descriptor).begin() != xbox_one.report_descriptor.end()
  );

  const std::array<std::uint8_t, 11> xbox_gip_stick_axis_descriptor {
    0x15,
    0x00,
    0x27,
    0xFF,
    0xFF,
    0x00,
    0x00,
    0x95,
    0x02,
    0x75,
    0x10,
  };
  EXPECT_TRUE(
    std::ranges::search(xbox_one.report_descriptor, xbox_gip_stick_axis_descriptor).begin() != xbox_one.report_descriptor.end()
  );

  const std::array<std::uint8_t, 9> right_stick_usage_descriptor {
    0x09,
    0x33,
    0x09,
    0x34,
    0x15,
    0x00,
    0x27,
    0xFF,
    0xFF,
  };
  EXPECT_TRUE(
    std::ranges::search(xbox_one.report_descriptor, right_stick_usage_descriptor).begin() != xbox_one.report_descriptor.end()
  );

  const std::array<std::uint8_t, 9> trigger_usage_descriptor {
    0x09,
    0x32,
    0x15,
    0x00,
    0x26,
    0xFF,
    0x03,
    0x95,
    0x01,
  };
  EXPECT_TRUE(
    std::ranges::search(xbox_one.report_descriptor, trigger_usage_descriptor).begin() != xbox_one.report_descriptor.end()
  );

  const std::array<std::uint8_t, 9> right_trigger_usage_descriptor {
    0x09,
    0x35,
    0x15,
    0x00,
    0x26,
    0xFF,
    0x03,
    0x95,
    0x01,
  };
  EXPECT_TRUE(
    std::ranges::search(xbox_one.report_descriptor, right_trigger_usage_descriptor).begin() != xbox_one.report_descriptor.end()
  );

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
  EXPECT_EQ(dualsense.name, "Wireless Controller");
  EXPECT_TRUE(dualsense.capabilities.supports_motion);
  EXPECT_TRUE(dualsense.capabilities.supports_touchpad);
  EXPECT_TRUE(dualsense.capabilities.supports_rgb_led);
  EXPECT_TRUE(dualsense.capabilities.supports_adaptive_triggers);
  EXPECT_GT(dualsense.input_report_size, 14U);
  EXPECT_GT(dualsense.output_report_size, 5U);
  EXPECT_EQ(dualsense.manufacturer, "Sony Interactive Entertainment");

  const auto dualsense_bluetooth = lvh::profiles::dualsense_bluetooth();
  EXPECT_EQ(dualsense_bluetooth.bus_type, lvh::BusType::bluetooth);
  EXPECT_EQ(dualsense_bluetooth.name, "Wireless Controller");
  EXPECT_EQ(dualsense_bluetooth.report_id, 0x31);
  EXPECT_EQ(dualsense_bluetooth.input_report_size, 78U);
  EXPECT_EQ(dualsense_bluetooth.output_report_size, 78U);
  EXPECT_NE(dualsense_bluetooth.report_descriptor, dualsense.report_descriptor);

  EXPECT_EQ(switch_pro.vendor_id, 0x057E);
  EXPECT_EQ(switch_pro.product_id, 0x2009);
  EXPECT_EQ(switch_pro.name, "Pro Controller");
  EXPECT_EQ(switch_pro.manufacturer, "Nintendo Co., Ltd.");
  EXPECT_EQ(switch_pro.report_id, 0x30);
  EXPECT_EQ(switch_pro.input_report_size, 64U);
  EXPECT_EQ(switch_pro.output_report_size, 64U);
  EXPECT_FALSE(switch_pro.capabilities.supports_rumble);
  EXPECT_TRUE(switch_pro.capabilities.supports_motion);
  EXPECT_TRUE(switch_pro.capabilities.supports_battery);

  const auto generic = lvh::profiles::generic_gamepad();
  const std::array<std::uint8_t, 12> standard_button_descriptor {
    0x05,
    0x09,
    0x19,
    0x01,
    0x29,
    0x10,
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x01,
  };
  EXPECT_TRUE(
    std::ranges::search(generic.report_descriptor, standard_button_descriptor).begin() != generic.report_descriptor.end()
  );

  const std::array<std::uint8_t, 12> standard_axis_order {
    0x09,
    0x30,
    0x09,
    0x31,
    0x09,
    0x33,
    0x09,
    0x34,
    0x09,
    0x32,
    0x09,
    0x35,
  };
  EXPECT_TRUE(
    std::ranges::search(generic.report_descriptor, standard_axis_order).begin() != generic.report_descriptor.end()
  );
  EXPECT_NE(switch_pro.report_descriptor, generic.report_descriptor);

  const std::array<std::uint8_t, 2> switch_pro_report_id_descriptor {0x85, 0x30};
  EXPECT_TRUE(
    std::ranges::search(switch_pro.report_descriptor, switch_pro_report_id_descriptor).begin() !=
    switch_pro.report_descriptor.end()
  );

  const std::array<std::uint8_t, 12> switch_pro_button_descriptor {
    0x19,
    0x01,
    0x29,
    0x0A,
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x01,
    0x95,
    0x0A,
  };
  EXPECT_TRUE(
    std::ranges::search(switch_pro.report_descriptor, switch_pro_button_descriptor).begin() !=
    switch_pro.report_descriptor.end()
  );
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
