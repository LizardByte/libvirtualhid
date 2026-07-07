/**
 * @file tests/unit/test_macos_backend.cpp
 * @brief Unit tests for macOS backend internals.
 */

// lib includes
#include <libvirtualhid/libvirtualhid.hpp>

// local includes
#include "fixtures/fixtures.hpp"

#if defined(__APPLE__) && defined(__MACH__)
  #include "fixtures/macos_backend_test_hooks.hpp"

// platform includes
  #include <Carbon/Carbon.h>
#endif

/**
 * @brief Test fixture for macOS backend internals.
 */
class MacosBackendTest: public MacOSTest {};

#if defined(__APPLE__) && defined(__MACH__)
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

TEST_F(MacosBackendTest, ConvertsScrollSettings) {
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_lines_per_detent(0.0), 1);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_lines_per_detent(0.3125), 5);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_lines_per_detent(1.0), 14);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_pixels(120, 10, 5), 50);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_pixels(-240, 10, 5), -100);
  EXPECT_EQ(lvh::detail::test::macos_backend_scroll_pixels(120, 0, 0), 1);
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
  EXPECT_EQ(result.keyboard_text_status.code(), lvh::ErrorCode::unsupported_profile);
  ASSERT_TRUE(result.keyboard_close_status.ok()) << result.keyboard_close_status.message();
  EXPECT_EQ(result.keyboard_submit_after_close_status.code(), lvh::ErrorCode::device_closed);
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
#endif
