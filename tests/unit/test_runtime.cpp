/**
 * @file tests/unit/test_runtime.cpp
 * @brief Unit tests for runtime and virtual gamepad handles.
 */

// lib includes
#include <libvirtualhid/libvirtualhid.hpp>

// local includes
#include "fixtures/fixtures.hpp"

/**
 * @brief Test fixture for Linux runtime integration tests.
 */
class LinuxRuntimeTest: public LinuxTest {};

TEST(RuntimeTest, FakeBackendReportsCapabilities) {
  auto runtime = lvh::Runtime::create();

  EXPECT_EQ(runtime->backend_kind(), lvh::BackendKind::fake);
  EXPECT_EQ(runtime->capabilities().backend_name, "fake");
  EXPECT_TRUE(runtime->capabilities().supports_gamepad);
  EXPECT_TRUE(runtime->capabilities().supports_keyboard);
  EXPECT_TRUE(runtime->capabilities().supports_mouse);
  EXPECT_TRUE(runtime->capabilities().supports_touchscreen);
  EXPECT_TRUE(runtime->capabilities().supports_trackpad);
  EXPECT_TRUE(runtime->capabilities().supports_pen_tablet);
  EXPECT_TRUE(runtime->capabilities().supports_output_reports);
  EXPECT_FALSE(runtime->capabilities().requires_installed_driver);
}

TEST(RuntimeTest, PlatformDefaultReportsCurrentPlatformCapabilities) {
  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);

  EXPECT_EQ(runtime->backend_kind(), lvh::BackendKind::platform_default);

#if defined(__linux__)
  EXPECT_EQ(runtime->capabilities().backend_name, "linux-uhid-uinput");
  EXPECT_FALSE(runtime->capabilities().requires_installed_driver);
#elif defined(_WIN32)
  EXPECT_EQ(runtime->capabilities().backend_name, "windows-umdf");
  EXPECT_TRUE(runtime->capabilities().requires_installed_driver);
  EXPECT_FALSE(runtime->capabilities().supports_keyboard);
  EXPECT_FALSE(runtime->capabilities().supports_mouse);
  EXPECT_FALSE(runtime->capabilities().supports_touchscreen);
  EXPECT_FALSE(runtime->capabilities().supports_trackpad);
  EXPECT_FALSE(runtime->capabilities().supports_pen_tablet);

  if (!runtime->capabilities().supports_gamepad) {
    auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
    EXPECT_FALSE(created);
    EXPECT_EQ(created.status.code(), lvh::ErrorCode::backend_unavailable);
  }
#else
  EXPECT_EQ(runtime->capabilities().backend_name, "platform-default-unimplemented");
  EXPECT_FALSE(runtime->capabilities().supports_gamepad);

  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
  EXPECT_FALSE(created);
  EXPECT_EQ(created.status.code(), lvh::ErrorCode::backend_unavailable);
#endif
}

TEST(RuntimeTest, CreatesSubmitsAndClosesGamepad) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());

  ASSERT_TRUE(created);
  ASSERT_NE(created.gamepad, nullptr);
  EXPECT_TRUE(created.gamepad->is_open());
  EXPECT_TRUE(created.gamepad->device_nodes().empty());
  EXPECT_EQ(runtime->active_device_count(), 1U);

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::b);
  state.left_stick.x = 2.0F;
  state.left_trigger = 2.0F;

  EXPECT_TRUE(created.gamepad->submit(state).ok());
  EXPECT_EQ(created.gamepad->submit_count(), 1U);
  EXPECT_EQ(created.gamepad->last_submitted_state().left_stick.x, 1.0F);
  EXPECT_EQ(created.gamepad->last_submitted_state().left_trigger, 1.0F);
  EXPECT_FALSE(created.gamepad->last_input_report().empty());

  EXPECT_TRUE(created.gamepad->close().ok());
  EXPECT_FALSE(created.gamepad->is_open());
  EXPECT_EQ(runtime->active_device_count(), 0U);
  EXPECT_EQ(created.gamepad->submit(state).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, DispatchesOutputCallback) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_gamepad(lvh::profiles::dualsense());
  ASSERT_TRUE(created);

  lvh::GamepadOutput received;
  bool was_called = false;
  created.gamepad->set_output_callback([&received, &was_called](const lvh::GamepadOutput &output) {
    received = output;
    was_called = true;
  });

  lvh::GamepadOutput output;
  output.kind = lvh::GamepadOutputKind::rumble;
  output.low_frequency_rumble = 123;
  output.high_frequency_rumble = 456;

  EXPECT_TRUE(created.gamepad->dispatch_output(output).ok());
  EXPECT_TRUE(was_called);
  EXPECT_EQ(received.kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(received.low_frequency_rumble, 123);
  EXPECT_EQ(received.high_frequency_rumble, 456);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesKeyboard) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_keyboard();

  ASSERT_TRUE(created);
  ASSERT_NE(created.keyboard, nullptr);
  EXPECT_TRUE(created.keyboard->is_open());
  EXPECT_TRUE(created.keyboard->device_nodes().empty());
  EXPECT_EQ(created.keyboard->profile().device_type, lvh::DeviceType::keyboard);
  EXPECT_EQ(runtime->active_device_count(), 1U);

  EXPECT_TRUE(created.keyboard->press(0x41).ok());
  EXPECT_EQ(created.keyboard->submit_count(), 1U);
  EXPECT_EQ(created.keyboard->last_submitted_event().key_code, 0x41);
  EXPECT_TRUE(created.keyboard->last_submitted_event().pressed);

  EXPECT_TRUE(created.keyboard->release(0x41).ok());
  EXPECT_TRUE(created.keyboard->type_text({.text = "A"}).ok());
  EXPECT_EQ(created.keyboard->submit_count(), 3U);

  EXPECT_EQ(created.keyboard->press(0).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_TRUE(created.keyboard->close().ok());
  EXPECT_FALSE(created.keyboard->is_open());
  EXPECT_EQ(runtime->active_device_count(), 0U);
  EXPECT_EQ(created.keyboard->press(0x41).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesMouse) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_mouse();

  ASSERT_TRUE(created);
  ASSERT_NE(created.mouse, nullptr);
  EXPECT_TRUE(created.mouse->is_open());
  EXPECT_TRUE(created.mouse->device_nodes().empty());
  EXPECT_EQ(created.mouse->profile().device_type, lvh::DeviceType::mouse);
  EXPECT_EQ(runtime->active_device_count(), 1U);

  EXPECT_TRUE(created.mouse->move_relative(10, -5).ok());
  EXPECT_EQ(created.mouse->submit_count(), 1U);
  EXPECT_EQ(created.mouse->last_submitted_event().kind, lvh::MouseEventKind::relative_motion);
  EXPECT_EQ(created.mouse->last_submitted_event().x, 10);
  EXPECT_EQ(created.mouse->last_submitted_event().y, -5);

  EXPECT_TRUE(created.mouse->move_absolute(100, 200, 1920, 1080).ok());
  EXPECT_TRUE(created.mouse->button(lvh::MouseButton::right, true).ok());
  EXPECT_TRUE(created.mouse->vertical_scroll(120).ok());
  EXPECT_TRUE(created.mouse->horizontal_scroll(-120).ok());
  EXPECT_EQ(created.mouse->submit_count(), 5U);

  EXPECT_EQ(created.mouse->move_absolute(1, 1, 0, 0).code(), lvh::ErrorCode::invalid_argument);
  EXPECT_TRUE(created.mouse->close().ok());
  EXPECT_FALSE(created.mouse->is_open());
  EXPECT_EQ(runtime->active_device_count(), 0U);
  EXPECT_EQ(created.mouse->move_relative(1, 1).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesTouchDevices) {
  auto runtime = lvh::Runtime::create();

  auto touchscreen = runtime->create_touchscreen();
  ASSERT_TRUE(touchscreen);
  ASSERT_NE(touchscreen.touchscreen, nullptr);
  EXPECT_EQ(touchscreen.touchscreen->profile().device_type, lvh::DeviceType::touchscreen);

  lvh::TouchContact contact {
    .id = 1,
    .x = 0.5F,
    .y = 0.25F,
    .pressure = 1.0F,
    .orientation = 10,
  };
  EXPECT_TRUE(touchscreen.touchscreen->place_contact(contact).ok());
  EXPECT_TRUE(touchscreen.touchscreen->release_contact(contact.id).ok());
  EXPECT_EQ(touchscreen.touchscreen->submit_count(), 2U);
  EXPECT_TRUE(touchscreen.touchscreen->close().ok());
  EXPECT_EQ(touchscreen.touchscreen->place_contact(contact).code(), lvh::ErrorCode::device_closed);

  auto trackpad = runtime->create_trackpad();
  ASSERT_TRUE(trackpad);
  ASSERT_NE(trackpad.trackpad, nullptr);
  EXPECT_EQ(trackpad.trackpad->profile().device_type, lvh::DeviceType::trackpad);
  EXPECT_TRUE(trackpad.trackpad->place_contact(contact).ok());
  EXPECT_TRUE(trackpad.trackpad->button(true).ok());
  EXPECT_TRUE(trackpad.trackpad->release_contact(contact.id).ok());
  EXPECT_EQ(trackpad.trackpad->submit_count(), 3U);
  EXPECT_TRUE(trackpad.trackpad->close().ok());
  EXPECT_EQ(trackpad.trackpad->button(false).code(), lvh::ErrorCode::device_closed);
}

TEST(RuntimeTest, CreatesSubmitsAndClosesPenTablet) {
  auto runtime = lvh::Runtime::create();
  auto created = runtime->create_pen_tablet();

  ASSERT_TRUE(created);
  ASSERT_NE(created.pen_tablet, nullptr);
  EXPECT_EQ(created.pen_tablet->profile().device_type, lvh::DeviceType::pen_tablet);

  lvh::PenToolState tool {
    .tool = lvh::PenToolType::pen,
    .x = 0.25F,
    .y = 0.5F,
    .pressure = 0.75F,
    .distance = -1.0F,
    .tilt_x = 45.0F,
    .tilt_y = -45.0F,
  };

  EXPECT_TRUE(created.pen_tablet->place_tool(tool).ok());
  EXPECT_TRUE(created.pen_tablet->button(lvh::PenButton::primary, true).ok());
  EXPECT_EQ(created.pen_tablet->submit_count(), 2U);
  EXPECT_EQ(created.pen_tablet->last_submitted_tool().tool, lvh::PenToolType::pen);
  EXPECT_TRUE(created.pen_tablet->close().ok());
  EXPECT_EQ(created.pen_tablet->button(lvh::PenButton::primary, false).code(), lvh::ErrorCode::device_closed);
}

TEST_F(LinuxRuntimeTest, LinuxUhidSmokeTestRequiresPrerequisites) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad);

  auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
  ASSERT_TRUE(created) << created.status.message();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.left_stick.x = 0.25F;
  state.right_trigger = 1.0F;

  EXPECT_TRUE(created.gamepad->submit(state).ok());
  EXPECT_TRUE(created.gamepad->close().ok());
}

TEST_F(LinuxRuntimeTest, LinuxUinputSmokeTestRequiresPrerequisites) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);
  const auto &capabilities = runtime->capabilities();

  ASSERT_TRUE(capabilities.supports_keyboard);
  auto keyboard = runtime->create_keyboard();
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  EXPECT_TRUE(keyboard.keyboard->press(0x41).ok());
  EXPECT_TRUE(keyboard.keyboard->release(0x41).ok());

  ASSERT_TRUE(capabilities.supports_mouse);
  auto mouse = runtime->create_mouse();
  ASSERT_TRUE(mouse) << mouse.status.message();
  EXPECT_TRUE(mouse.mouse->move_relative(1, 1).ok());
  EXPECT_TRUE(mouse.mouse->vertical_scroll(120).ok());

  ASSERT_TRUE(capabilities.supports_touchscreen);
  auto touchscreen = runtime->create_touchscreen();
  ASSERT_TRUE(touchscreen) << touchscreen.status.message();
  EXPECT_TRUE(touchscreen.touchscreen->place_contact({.id = 1, .x = 0.5F, .y = 0.25F, .pressure = 1.0F}).ok());
  EXPECT_TRUE(touchscreen.touchscreen->release_contact(1).ok());

  ASSERT_TRUE(capabilities.supports_trackpad);
  auto trackpad = runtime->create_trackpad();
  ASSERT_TRUE(trackpad) << trackpad.status.message();
  EXPECT_TRUE(trackpad.trackpad->place_contact({.id = 1, .x = 0.5F, .y = 0.25F, .pressure = 1.0F}).ok());
  EXPECT_TRUE(trackpad.trackpad->button(true).ok());
  EXPECT_TRUE(trackpad.trackpad->button(false).ok());
  EXPECT_TRUE(trackpad.trackpad->release_contact(1).ok());

  ASSERT_TRUE(capabilities.supports_pen_tablet);
  auto pen_tablet = runtime->create_pen_tablet();
  ASSERT_TRUE(pen_tablet) << pen_tablet.status.message();
  EXPECT_TRUE(
    pen_tablet.pen_tablet
      ->place_tool({.tool = lvh::PenToolType::pen, .x = 0.5F, .y = 0.25F, .pressure = 0.75F, .tilt_x = 10.0F})
      .ok()
  );
  EXPECT_TRUE(pen_tablet.pen_tablet->button(lvh::PenButton::primary, true).ok());
  EXPECT_TRUE(pen_tablet.pen_tablet->button(lvh::PenButton::primary, false).ok());
}
