/**
 * @file tests/unit/test_freebsd_backend.cpp
 * @brief FreeBSD uinput backend integration tests.
 */

// standard includes
#include <array>
#include <memory>
#include <vector>

// lib includes
#include <gtest/gtest.h>

// local includes
#include <libvirtualhid/gamepad_adapter.hpp>
#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/runtime.hpp>

namespace {

  std::unique_ptr<lvh::Runtime> create_platform_runtime() {
    lvh::RuntimeOptions options;
    options.backend = lvh::BackendKind::platform_default;
    return lvh::Runtime::create(options);
  }

  void expect_uinput_node(const std::vector<lvh::DeviceNode> &nodes) {
    ASSERT_FALSE(nodes.empty());
    EXPECT_EQ(nodes.front().kind, lvh::DeviceNodeKind::input_event);
    EXPECT_TRUE(
      nodes.front().path.starts_with("/dev/input/event") ||
      nodes.front().path.starts_with("/dev/event")
    ) << nodes.front().path;
  }

}  // namespace

TEST(FreeBsdBackendTest, ReportsTheUinputDeviceSurface) {
  auto runtime = create_platform_runtime();
  ASSERT_NE(runtime, nullptr);

  const auto &capabilities = runtime->capabilities();
  EXPECT_EQ(capabilities.backend_name, "freebsd-uinput");
  EXPECT_TRUE(capabilities.supports_virtual_hid);
  EXPECT_TRUE(capabilities.supports_gamepad);
  EXPECT_TRUE(capabilities.supports_keyboard);
  EXPECT_TRUE(capabilities.supports_mouse);
  EXPECT_TRUE(capabilities.supports_touchscreen);
  EXPECT_TRUE(capabilities.supports_trackpad);
  EXPECT_TRUE(capabilities.supports_pen_tablet);
  EXPECT_TRUE(capabilities.supports_output_reports);
  EXPECT_FALSE(capabilities.requires_installed_driver);
}

TEST(FreeBsdBackendTest, CreatesEveryGamepadWithTheExpectedPlayStationSubset) {
  auto runtime = create_platform_runtime();
  ASSERT_NE(runtime, nullptr);

  const std::array profiles {
    lvh::profiles::generic_gamepad(),
    lvh::profiles::xbox_360(),
    lvh::profiles::xbox_one(),
    lvh::profiles::xbox_series(),
    lvh::profiles::dualshock4(),
    lvh::profiles::dualsense(),
    lvh::profiles::switch_pro(),
    lvh::profiles::steam_deck(),
  };

  for (const auto &profile : profiles) {
    SCOPED_TRACE(profile.name);
    auto created = runtime->create_gamepad(profile);
    ASSERT_TRUE(created) << created.status.message();

    const auto &effective_profile = created.gamepad->profile();
    const auto support = lvh::gamepad_profile_support(effective_profile);
    EXPECT_TRUE(support.supports_rumble);
    EXPECT_FALSE(support.supports_motion);
    EXPECT_FALSE(support.supports_touchpad);
    EXPECT_FALSE(support.supports_battery);
    EXPECT_FALSE(support.supports_rgb_led);
    EXPECT_FALSE(support.supports_adaptive_triggers);
    EXPECT_EQ(effective_profile.output_report_size, 0U);
    EXPECT_FALSE(lvh::supports_gamepad_output(effective_profile, lvh::GamepadOutputKind::raw_report));

    if (
      effective_profile.gamepad_kind == lvh::GamepadProfileKind::dualshock4 ||
      effective_profile.gamepad_kind == lvh::GamepadProfileKind::dualsense
    ) {
      EXPECT_FALSE(support.supports_touchpad_button);
    }

    expect_uinput_node(created.gamepad->device_nodes());
    EXPECT_TRUE(created.gamepad->submit({}).ok());
    EXPECT_TRUE(created.gamepad->close().ok());
  }
}

TEST(FreeBsdBackendTest, CreatesEveryNonGamepadUinputDevice) {
  auto runtime = create_platform_runtime();
  ASSERT_NE(runtime, nullptr);

  auto keyboard = runtime->create_keyboard();
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  expect_uinput_node(keyboard.keyboard->device_nodes());
  EXPECT_TRUE(keyboard.keyboard->close().ok());

  auto mouse = runtime->create_mouse();
  ASSERT_TRUE(mouse) << mouse.status.message();
  expect_uinput_node(mouse.mouse->device_nodes());
  EXPECT_TRUE(mouse.mouse->close().ok());

  auto touchscreen = runtime->create_touchscreen();
  ASSERT_TRUE(touchscreen) << touchscreen.status.message();
  expect_uinput_node(touchscreen.touchscreen->device_nodes());
  EXPECT_TRUE(touchscreen.touchscreen->close().ok());

  auto trackpad = runtime->create_trackpad();
  ASSERT_TRUE(trackpad) << trackpad.status.message();
  expect_uinput_node(trackpad.trackpad->device_nodes());
  EXPECT_TRUE(trackpad.trackpad->close().ok());

  auto pen_tablet = runtime->create_pen_tablet();
  ASSERT_TRUE(pen_tablet) << pen_tablet.status.message();
  expect_uinput_node(pen_tablet.pen_tablet->device_nodes());
  EXPECT_TRUE(pen_tablet.pen_tablet->close().ok());
}
