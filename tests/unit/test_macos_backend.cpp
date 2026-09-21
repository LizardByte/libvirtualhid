/**
 * @file tests/unit/test_macos_backend.cpp
 * @brief Unit tests for macOS backend internals.
 */

// lib includes
#include <libvirtualhid/libvirtualhid.hpp>

// local includes
#include "fixtures/fixtures.hpp"
#include "fixtures/macos_backend_test_hooks.hpp"

// platform includes
#include <Carbon/Carbon.h>

/**
 * @brief Test fixture for macOS backend internals.
 */
class MacosBackendTest: public MacOSTest {};

TEST_F(MacosBackendTest, TranslatesKeyboardKeys) {
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x08), kVK_Delete);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x09), kVK_Tab);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x0D), kVK_Return);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x1B), kVK_Escape);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x20), kVK_Space);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x21), kVK_PageUp);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x22), kVK_PageDown);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x25), kVK_LeftArrow);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x26), kVK_UpArrow);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x27), kVK_RightArrow);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x28), kVK_DownArrow);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x41), kVK_ANSI_A);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x5B), kVK_Command);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x5C), kVK_RightCommand);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0x6B), kVK_ANSI_KeypadPlus);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xA0), kVK_Shift);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xA1), kVK_RightShift);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xA2), kVK_Control);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xA3), kVK_RightControl);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xA4), kVK_Option);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xA5), kVK_RightOption);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xBB), kVK_ANSI_Equal);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xBD), kVK_ANSI_Minus);
  EXPECT_EQ(lvh::detail::test::macos_backend_key_code(0xDE), kVK_ANSI_Quote);
  EXPECT_FALSE(lvh::detail::test::macos_backend_key_code(0x13).has_value());
  EXPECT_FALSE(lvh::detail::test::macos_backend_key_code(0xFFFF).has_value());
}

TEST_F(MacosBackendTest, IdentifiesModifierKeys) {
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0x10));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0x11));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0x12));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0x5B));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0x5C));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0xA0));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0xA1));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0xA2));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0xA3));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0xA4));
  EXPECT_TRUE(lvh::detail::test::macos_backend_is_modifier_key(0xA5));
  EXPECT_FALSE(lvh::detail::test::macos_backend_is_modifier_key(0x41));
  EXPECT_FALSE(lvh::detail::test::macos_backend_is_modifier_key(0x13));
}

TEST_F(MacosBackendTest, AddsImplicitFlagsForFunctionAndKeypadKeys) {
  using lvh::detail::test::macos_backend_implicit_key_flags;
  constexpr std::uint64_t fn = kCGEventFlagMaskSecondaryFn;
  constexpr std::uint64_t numeric_pad = kCGEventFlagMaskNumericPad;

  EXPECT_EQ(macos_backend_implicit_key_flags(0x25), fn | numeric_pad);  // VKEY_LEFT
  EXPECT_EQ(macos_backend_implicit_key_flags(0x26), fn | numeric_pad);  // VKEY_UP
  EXPECT_EQ(macos_backend_implicit_key_flags(0x27), fn | numeric_pad);  // VKEY_RIGHT
  EXPECT_EQ(macos_backend_implicit_key_flags(0x28), fn | numeric_pad);  // VKEY_DOWN
  EXPECT_EQ(macos_backend_implicit_key_flags(0x21), fn);  // VKEY_PRIOR
  EXPECT_EQ(macos_backend_implicit_key_flags(0x22), fn);  // VKEY_NEXT
  EXPECT_EQ(macos_backend_implicit_key_flags(0x23), fn);  // VKEY_END
  EXPECT_EQ(macos_backend_implicit_key_flags(0x24), fn);  // VKEY_HOME
  EXPECT_EQ(macos_backend_implicit_key_flags(0x2D), fn);  // VKEY_INSERT
  EXPECT_EQ(macos_backend_implicit_key_flags(0x2E), fn);  // VKEY_DELETE
  EXPECT_EQ(macos_backend_implicit_key_flags(0x70), fn);  // VKEY_F1
  EXPECT_EQ(macos_backend_implicit_key_flags(0x83), fn);  // VKEY_F20
  EXPECT_EQ(macos_backend_implicit_key_flags(0x60), numeric_pad);  // VKEY_NUMPAD0
  EXPECT_EQ(macos_backend_implicit_key_flags(0x6B), numeric_pad);  // VKEY_ADD
  EXPECT_EQ(macos_backend_implicit_key_flags(0x6E), numeric_pad);  // VKEY_DECIMAL
  EXPECT_EQ(macos_backend_implicit_key_flags(0x0D), 0U);  // VKEY_RETURN
  EXPECT_EQ(macos_backend_implicit_key_flags(0x41), 0U);  // VKEY_A
  EXPECT_EQ(macos_backend_implicit_key_flags(0xA2), 0U);  // VKEY_LCONTROL
  EXPECT_EQ(macos_backend_implicit_key_flags(0x13), 0U);  // unmapped VKEY_PAUSE
  EXPECT_EQ(macos_backend_implicit_key_flags(0xFFFF), 0U);
}

TEST_F(MacosBackendTest, BuildsControlArrowEventsWithFnFlags) {
  using lvh::detail::test::macos_backend_key_events;
  constexpr std::uint64_t control = kCGEventFlagMaskControl;
  constexpr std::uint64_t fn = kCGEventFlagMaskSecondaryFn;
  constexpr std::uint64_t numeric_pad = kCGEventFlagMaskNumericPad;
  constexpr std::uint64_t checked = kCGEventFlagMaskShift | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate |
                                    kCGEventFlagMaskCommand | fn | numeric_pad;

  // Control+Up as a client sends it, then Control+A for contrast.
  const auto events = macos_backend_key_events({
    {0xA2, true},  // VKEY_LCONTROL down
    {0x26, true},  // VKEY_UP down
    {0x26, false},  // VKEY_UP up
    {0x41, true},  // VKEY_A down
    {0x41, false},  // VKEY_A up
    {0xA2, false},  // VKEY_LCONTROL up
  });
  ASSERT_EQ(events.size(), 6U);

  EXPECT_EQ(events[0].event_type, kCGEventFlagsChanged);
  EXPECT_EQ(events[0].flags & checked, control);

  // The arrow event carries the flags a physical arrow key reports, on top of the held Control.
  EXPECT_EQ(events[1].event_type, kCGEventKeyDown);
  EXPECT_EQ(events[1].key_code, kVK_UpArrow);
  EXPECT_EQ(events[1].flags & checked, control | fn | numeric_pad);
  EXPECT_EQ(events[2].event_type, kCGEventKeyUp);
  EXPECT_EQ(events[2].flags & checked, control | fn | numeric_pad);

  // The per-key flags never enter the shared modifier state, so Control+A is unchanged.
  EXPECT_EQ(events[1].tracked_flags & checked, control);
  EXPECT_EQ(events[2].tracked_flags & checked, control);
  EXPECT_EQ(events[3].event_type, kCGEventKeyDown);
  EXPECT_EQ(events[3].key_code, kVK_ANSI_A);
  EXPECT_EQ(events[3].flags & checked, control);
  EXPECT_EQ(events[4].flags & checked, control);

  EXPECT_EQ(events[5].event_type, kCGEventFlagsChanged);
  EXPECT_EQ(events[5].flags & checked, 0U);
  EXPECT_EQ(events[5].tracked_flags & checked, 0U);
}

TEST_F(MacosBackendTest, BuildsKeypadEventsWithNumericPadFlag) {
  using lvh::detail::test::macos_backend_key_events;
  constexpr std::uint64_t fn = kCGEventFlagMaskSecondaryFn;
  constexpr std::uint64_t numeric_pad = kCGEventFlagMaskNumericPad;

  const auto events = macos_backend_key_events({
    {0x67, true},  // VKEY_NUMPAD7 down
    {0x0D, true},  // VKEY_RETURN down
  });
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(events[0].key_code, kVK_ANSI_Keypad7);
  EXPECT_EQ(events[0].flags & (fn | numeric_pad), numeric_pad);
  EXPECT_EQ(events[1].key_code, kVK_Return);
  EXPECT_EQ(events[1].flags & (fn | numeric_pad), 0U);
}

TEST_F(MacosBackendTest, ConvertsScrollSettings) {
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_lines_per_detent(0.0), 1);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_lines_per_detent(0.3125), 5);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_lines_per_detent(1.0), 14);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_pixels(120, 10, 5), 50);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_pixels(-240, 10, 5), -100);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_pixels(120, 0, 0), 1);
}

TEST_F(MacosBackendTest, ConvertsAbsoluteMouseCoordinates) {
  lvh::MouseEvent event {
    .kind = lvh::MouseEventKind::absolute_motion,
    .x = 40,
    .y = 50,
    .width = 200,
    .height = 100,
  };

  auto location = lvh::detail::test::macos_backend_absolute_mouse_location(event, 10.0, 20.0, 400.0, 200.0);
  EXPECT_DOUBLE_EQ(location.x, 90.0);
  EXPECT_DOUBLE_EQ(location.y, 120.0);

  event.x = 250;
  event.y = -10;
  location = lvh::detail::test::macos_backend_absolute_mouse_location(event, 10.0, 20.0, 400.0, 200.0);
  EXPECT_DOUBLE_EQ(location.x, 410.0);
  EXPECT_DOUBLE_EQ(location.y, 20.0);

  event = {
    .kind = lvh::MouseEventKind::absolute_motion,
    .absolute_x = 0.25F,
    .absolute_y = 0.75F,
    .has_fractional_absolute_coordinates = true,
    .width = 1,
    .height = 1,
  };
  location = lvh::detail::test::macos_backend_absolute_mouse_location(event, 10.0, 20.0, 400.0, 200.0);
  EXPECT_DOUBLE_EQ(location.x, 110.0);
  EXPECT_DOUBLE_EQ(location.y, 170.0);
}

TEST_F(MacosBackendTest, SelectsMouseMotionMetadataForHeldButtons) {
  using lvh::detail::test::macos_backend_mouse_motion;

  auto motion = macos_backend_mouse_motion(false, false, false);
  EXPECT_EQ(motion.button, kCGMouseButtonLeft);
  EXPECT_EQ(motion.event_type, kCGEventMouseMoved);

  motion = macos_backend_mouse_motion(true, false, false);
  EXPECT_EQ(motion.button, kCGMouseButtonLeft);
  EXPECT_EQ(motion.event_type, kCGEventLeftMouseDragged);

  motion = macos_backend_mouse_motion(false, true, false);
  EXPECT_EQ(motion.button, kCGMouseButtonRight);
  EXPECT_EQ(motion.event_type, kCGEventRightMouseDragged);

  motion = macos_backend_mouse_motion(false, false, true);
  EXPECT_EQ(motion.button, kCGMouseButtonCenter);
  EXPECT_EQ(motion.event_type, kCGEventOtherMouseDragged);
}

TEST_F(MacosBackendTest, ReportsCapabilitiesAndUnsupportedDevices) {
  const auto result = lvh::detail::test::macos_backend_utilities();

  EXPECT_EQ(result.capabilities.backend_name, "macos-coregraphics");
  EXPECT_FALSE(result.capabilities.supports_virtual_hid);
  EXPECT_FALSE(result.capabilities.supports_gamepad);
  EXPECT_TRUE(result.capabilities.supports_keyboard);
  EXPECT_TRUE(result.capabilities.supports_mouse);
  EXPECT_FALSE(result.capabilities.supports_touchscreen);
  EXPECT_FALSE(result.capabilities.supports_trackpad);
  EXPECT_FALSE(result.capabilities.supports_pen_tablet);
  EXPECT_FALSE(result.capabilities.supports_output_reports);
  EXPECT_FALSE(result.capabilities.requires_installed_driver);

  ASSERT_TRUE(result.keyboard_create_status.ok()) << result.keyboard_create_status.message();
  EXPECT_TRUE(result.keyboard_text_status.ok()) << result.keyboard_text_status.message();
  EXPECT_TRUE(result.keyboard_empty_text_status.ok()) << result.keyboard_empty_text_status.message();
  EXPECT_EQ(result.keyboard_invalid_text_status.code(), lvh::ErrorCode::invalid_argument);
  ASSERT_TRUE(result.keyboard_close_status.ok()) << result.keyboard_close_status.message();
  EXPECT_EQ(result.keyboard_submit_after_close_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(result.keyboard_text_after_close_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(result.keyboard_invalid_profile_status.code(), lvh::ErrorCode::unsupported_profile);

  ASSERT_TRUE(result.mouse_create_status.ok()) << result.mouse_create_status.message();
  ASSERT_TRUE(result.mouse_close_status.ok()) << result.mouse_close_status.message();
  EXPECT_EQ(result.mouse_submit_after_close_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(result.mouse_invalid_profile_status.code(), lvh::ErrorCode::unsupported_profile);

  EXPECT_EQ(result.gamepad_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.touchscreen_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.trackpad_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.pen_tablet_status.code(), lvh::ErrorCode::unsupported_profile);
}
