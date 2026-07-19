/**
 * @file tests/unit/test_profiles.cpp
 * @brief Unit tests for built-in gamepad profiles.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

namespace {
  struct PackedButtonCase {
    lvh::GamepadButton button;
    std::uint16_t bit;
  };

  struct PackedByteButtonCase {
    lvh::GamepadButton button;
    std::size_t offset;
    std::uint8_t mask;
  };

  template<std::size_t Size>
  void expect_descriptor_contains(
    const lvh::DeviceProfile &profile,
    const std::array<std::uint8_t, Size> &expected
  ) {
    const auto match = std::ranges::search(profile.report_descriptor, expected);
    EXPECT_NE(match.begin(), profile.report_descriptor.end());
  }

  std::uint16_t read_u16_le(const std::vector<std::uint8_t> &report, std::size_t offset) {
    const auto low = std::byte {report[offset]};
    const auto high = std::byte {report[offset + 1U]};
    return static_cast<std::uint16_t>(
      std::to_integer<std::uint16_t>(low) | (std::to_integer<std::uint16_t>(high) << 8U)
    );
  }

  void expect_packed_button_bits(
    const lvh::DeviceProfile &profile,
    const std::vector<PackedButtonCase> &button_cases,
    std::size_t report_offset
  ) {
    for (const auto &[button, bit] : button_cases) {
      lvh::GamepadState state;
      state.buttons.set(button);
      const auto report = lvh::reports::pack_input_report(profile, state);

      ASSERT_GT(report.size(), report_offset + 1U);
      EXPECT_EQ(read_u16_le(report, report_offset), static_cast<std::uint16_t>(1U << bit))
        << "logical button " << static_cast<unsigned>(std::to_underlying(button));
    }
  }
}  // namespace

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

TEST(ProfileTest, BuiltInProfilesUseDefaultDeviceNames) {
  EXPECT_EQ(lvh::profiles::generic_gamepad().name, "(libvirtualhid) Generic Controller");
  EXPECT_EQ(lvh::profiles::xbox_360().name, "(libvirtualhid) X-Box 360 Controller");
  EXPECT_EQ(lvh::profiles::xbox_one().name, "(libvirtualhid) X-Box One Controller");
  EXPECT_EQ(lvh::profiles::xbox_series().name, "(libvirtualhid) X-Box Series Controller");
  EXPECT_EQ(lvh::profiles::dualshock4().name, "(libvirtualhid) PS4 Controller");
  EXPECT_EQ(lvh::profiles::dualsense().name, "(libvirtualhid) PS5 Controller");
  EXPECT_EQ(lvh::profiles::switch_pro().name, "(libvirtualhid) Nintendo Pro Controller");
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
  EXPECT_EQ(xbox_one.output_report_size, 8U);

  const auto xbox_series = lvh::profiles::xbox_series();
  EXPECT_EQ(xbox_series.vendor_id, 0x045E);
  EXPECT_EQ(xbox_series.product_id, 0x0B12);
  EXPECT_EQ(xbox_series.bus_type, lvh::BusType::usb);
  EXPECT_EQ(xbox_series.manufacturer, "Microsoft");
  EXPECT_EQ(xbox_series.report_id, 0);
  EXPECT_EQ(xbox_series.input_report_size, 17U);
  EXPECT_EQ(xbox_series.output_report_size, 8U);

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
  EXPECT_EQ(dualshock4.version, 0x0100);
  EXPECT_EQ(dualshock4.input_report_size, 64U);
  EXPECT_EQ(dualshock4.output_report_size, 32U);
  EXPECT_TRUE(dualshock4.capabilities.supports_motion);
  EXPECT_TRUE(dualshock4.capabilities.supports_touchpad);
  EXPECT_TRUE(dualshock4.capabilities.supports_rgb_led);
  EXPECT_TRUE(dualshock4.capabilities.supports_battery);
  EXPECT_FALSE(dualshock4.capabilities.supports_adaptive_triggers);
  EXPECT_EQ(dualshock4.manufacturer, "Sony Computer Entertainment");

  const auto dualshock4_bluetooth = lvh::profiles::dualshock4_bluetooth();
  EXPECT_EQ(dualshock4_bluetooth.bus_type, lvh::BusType::bluetooth);
  EXPECT_EQ(dualshock4_bluetooth.version, 0x0100);
  EXPECT_EQ(dualshock4_bluetooth.name, "(libvirtualhid) PS4 Controller");
  EXPECT_EQ(dualshock4_bluetooth.manufacturer, "Sony Computer Entertainment");
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
  EXPECT_EQ(dualsense_bluetooth.name, "(libvirtualhid) PS5 Controller");
  EXPECT_EQ(dualsense_bluetooth.report_id, 0x31);
  EXPECT_EQ(dualsense_bluetooth.input_report_size, 78U);
  EXPECT_EQ(dualsense_bluetooth.output_report_size, 78U);
  EXPECT_NE(dualsense_bluetooth.report_descriptor, dualsense.report_descriptor);

  EXPECT_EQ(switch_pro.vendor_id, 0x057E);
  EXPECT_EQ(switch_pro.product_id, 0x2009);
  EXPECT_EQ(switch_pro.manufacturer, "Nintendo Co., Ltd.");
  EXPECT_EQ(switch_pro.report_id, 0x30);
  EXPECT_EQ(switch_pro.input_report_size, 64U);
  EXPECT_EQ(switch_pro.output_report_size, 64U);
  EXPECT_TRUE(switch_pro.capabilities.supports_rumble);
  EXPECT_TRUE(switch_pro.capabilities.supports_motion);
  EXPECT_TRUE(switch_pro.capabilities.supports_battery);

  const auto generic = lvh::profiles::generic_gamepad();
  const std::array<std::uint8_t, 16> standard_button_descriptor {
    0x05,
    0x09,  // Usage Page (Button)
    0x19,
    0x01,  // Usage Minimum (Button 1)
    0x29,
    0x10,  // Usage Maximum (Button 16)
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x01,
    0x95,
    0x10,
    0x81,
    0x02,
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
}

TEST(ProfileTest, NativeControllerDescriptorsUseContiguousButtonUsages) {
  const auto xbox_one = lvh::profiles::xbox_one();
  const auto xbox_series = lvh::profiles::xbox_series();
  const auto switch_pro = lvh::profiles::switch_pro();

  constexpr std::array<std::uint8_t, 12> xbox_one_buttons {
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
  constexpr std::array<std::uint8_t, 12> xbox_series_buttons {
    0x05,
    0x09,
    0x19,
    0x01,
    0x29,
    0x0C,
    0x95,
    0x0C,
    0x75,
    0x01,
    0x81,
    0x02,
  };
  constexpr std::array<std::uint8_t, 20> switch_primary_buttons {
    0x05,
    0x09,
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
    0x55,
    0x00,
    0x65,
    0x00,
    0x81,
    0x02,
  };
  constexpr std::array<std::uint8_t, 16> switch_system_buttons {
    0x05,
    0x09,
    0x19,
    0x0B,
    0x29,
    0x0E,
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x01,
    0x95,
    0x04,
    0x81,
    0x02,
  };
  constexpr std::array<std::uint8_t, 16> switch_compatibility_buttons {
    0x05,
    0x09,
    0x19,
    0x0F,
    0x29,
    0x12,
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x01,
    0x95,
    0x04,
    0x81,
    0x02,
  };

  expect_descriptor_contains(xbox_one, xbox_one_buttons);
  expect_descriptor_contains(xbox_series, xbox_series_buttons);
  expect_descriptor_contains(switch_pro, switch_primary_buttons);
  expect_descriptor_contains(switch_pro, switch_system_buttons);
  expect_descriptor_contains(switch_pro, switch_compatibility_buttons);
}

TEST(ProfileTest, NativeControllerPackedButtonsUseExpectedBitSlots) {
  using enum lvh::GamepadButton;

  const auto xbox_one = lvh::profiles::xbox_one();
  const auto xbox_series = lvh::profiles::xbox_series();
  const auto switch_pro = lvh::profiles::switch_pro();

  const std::vector<PackedButtonCase> xbox_button_cases {
    {a, 0U},
    {b, 1U},
    {x, 2U},
    {y, 3U},
    {left_shoulder, 4U},
    {right_shoulder, 5U},
    {back, 6U},
    {start, 7U},
    {left_stick, 8U},
    {right_stick, 9U},
  };
  expect_packed_button_bits(xbox_one, xbox_button_cases, 12U);
  expect_packed_button_bits(xbox_series, xbox_button_cases, 12U);
  expect_packed_button_bits(xbox_series, {{misc1, 11U}}, 12U);

  for (const auto &profile : {xbox_one, xbox_series}) {
    lvh::GamepadState state;
    state.buttons.set(guide);
    const auto report = lvh::reports::pack_input_report(profile, state);

    ASSERT_GT(report.size(), 15U);
    EXPECT_EQ(read_u16_le(report, 12U), 0U);
    EXPECT_EQ(report[15], 1U);
  }

  const std::vector<PackedByteButtonCase> switch_button_cases {
    {a, 3U, 0x08U},
    {b, 3U, 0x04U},
    {x, 3U, 0x02U},
    {y, 3U, 0x01U},
    {right_shoulder, 3U, 0x40U},
    {back, 4U, 0x01U},
    {start, 4U, 0x02U},
    {right_stick, 4U, 0x04U},
    {left_stick, 4U, 0x08U},
    {guide, 4U, 0x10U},
    {misc1, 4U, 0x20U},
    {dpad_down, 5U, 0x01U},
    {dpad_up, 5U, 0x02U},
    {dpad_right, 5U, 0x04U},
    {dpad_left, 5U, 0x08U},
    {left_shoulder, 5U, 0x40U},
  };
  for (const auto &[button, offset, mask] : switch_button_cases) {
    lvh::GamepadState state;
    state.buttons.set(button);
    const auto report = lvh::reports::pack_input_report(switch_pro, state);

    ASSERT_GT(report.size(), offset);
    EXPECT_EQ(report[offset], mask) << "logical button " << static_cast<unsigned>(std::to_underlying(button));
  }

  lvh::GamepadState trigger_state;
  trigger_state.left_trigger = 1.0F;
  trigger_state.right_trigger = 1.0F;
  const auto trigger_report = lvh::reports::pack_input_report(switch_pro, trigger_state);
  ASSERT_GT(trigger_report.size(), 5U);
  EXPECT_EQ(trigger_report[3], 0x80U);
  EXPECT_EQ(trigger_report[5], 0x80U);
}

TEST(ProfileTest, SwitchProPacksNativeFullControllerState) {
  const auto profile = lvh::profiles::switch_pro();
  lvh::GamepadState state;
  state.left_stick = {.x = 1.0F, .y = 0.0F};
  state.right_stick = {.x = 0.0F, .y = -1.0F};

  const auto report = lvh::reports::pack_input_report(profile, state);
  ASSERT_EQ(report.size(), 64U);
  EXPECT_EQ(report[0], 0x30U);
  EXPECT_EQ(report[2], 0x81U);

  // Nintendo packs each stick as two little-endian 12-bit values across three bytes.
  EXPECT_EQ(report[6], 0xFFU);
  EXPECT_EQ(report[7], 0x0FU);
  EXPECT_EQ(report[8], 0x80U);
  EXPECT_EQ(report[9], 0x00U);
  EXPECT_EQ(report[10], 0xF8U);
  EXPECT_EQ(report[11], 0xFFU);

  state.battery = lvh::GamepadBattery {.state = lvh::GamepadBatteryState::full, .percentage = 100};
  EXPECT_EQ(lvh::reports::pack_input_report(profile, state)[2], 0x81U);
  state.battery = lvh::GamepadBattery {.state = lvh::GamepadBatteryState::charging, .percentage = 50};
  EXPECT_EQ(lvh::reports::pack_input_report(profile, state)[2], 0x51U);
}

TEST(ProfileTest, RumbleProfilesExposeOutputReports) {
  const auto generic = lvh::profiles::generic_gamepad();
  const auto xbox_360 = lvh::profiles::xbox_360();
  const auto xbox_one = lvh::profiles::xbox_one();
  const auto xbox_series = lvh::profiles::xbox_series();

  EXPECT_TRUE(generic.capabilities.supports_rumble);
  EXPECT_EQ(generic.output_report_size, 9U);
  constexpr std::array<std::uint8_t, 16> pid_rumble_descriptor {
    0x05,
    0x0F,
    0x09,
    0x97,
    0x15,
    0x00,
    0x25,
    0x01,
    0x75,
    0x04,
    0x95,
    0x01,
    0x91,
    0x02,
    0x15,
    0x00,
  };
  expect_descriptor_contains(generic, pid_rumble_descriptor);

  EXPECT_TRUE(xbox_360.capabilities.supports_rumble);
  EXPECT_EQ(xbox_360.output_report_size, 5U);
  ASSERT_GE(xbox_360.report_descriptor.size(), 3U);
  EXPECT_EQ(xbox_360.report_descriptor[xbox_360.report_descriptor.size() - 3U], 0x91);
  EXPECT_EQ(xbox_360.report_descriptor[xbox_360.report_descriptor.size() - 2U], 0x02);
  EXPECT_EQ(xbox_360.report_descriptor.back(), 0xC0);

  EXPECT_EQ(xbox_one.output_report_size, 8U);
  EXPECT_EQ(xbox_series.output_report_size, 8U);
  expect_descriptor_contains(xbox_one, pid_rumble_descriptor);
  expect_descriptor_contains(xbox_series, pid_rumble_descriptor);
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
