#include <libvirtualhid/profiles.hpp>

#include <gtest/gtest.h>

TEST(ProfileTest, BuiltInProfilesHaveDescriptors) {
  const auto profiles = lvh::profiles::built_in_gamepad_profiles();

  ASSERT_GE(profiles.size(), 4U);
  for(const auto& profile : profiles) {
    EXPECT_EQ(profile.device_type, lvh::DeviceType::gamepad);
    EXPECT_FALSE(profile.name.empty());
    EXPECT_NE(profile.vendor_id, 0);
    EXPECT_NE(profile.product_id, 0);
    EXPECT_NE(profile.report_id, 0);
    EXPECT_GE(profile.input_report_size, 14U);
    EXPECT_FALSE(profile.report_descriptor.empty());
  }
}

TEST(ProfileTest, SunshineProfilesArePresent) {
  const auto xbox_one = lvh::profiles::xbox_one();
  const auto dualsense = lvh::profiles::dualsense();
  const auto switch_pro = lvh::profiles::switch_pro();

  EXPECT_EQ(xbox_one.vendor_id, 0x045E);
  EXPECT_EQ(xbox_one.product_id, 0x02EA);
  EXPECT_TRUE(xbox_one.capabilities.supports_rumble);

  EXPECT_EQ(dualsense.vendor_id, 0x054C);
  EXPECT_TRUE(dualsense.capabilities.supports_motion);
  EXPECT_TRUE(dualsense.capabilities.supports_touchpad);
  EXPECT_TRUE(dualsense.capabilities.supports_rgb_led);
  EXPECT_TRUE(dualsense.capabilities.supports_adaptive_triggers);

  EXPECT_EQ(switch_pro.vendor_id, 0x057E);
  EXPECT_EQ(switch_pro.product_id, 0x2009);
}

TEST(ProfileTest, CanFindProfileByKind) {
  const auto profile = lvh::profiles::gamepad_profile(lvh::GamepadProfileKind::xbox_series);

  ASSERT_TRUE(profile.has_value());
  EXPECT_EQ(profile->gamepad_kind, lvh::GamepadProfileKind::xbox_series);
}
