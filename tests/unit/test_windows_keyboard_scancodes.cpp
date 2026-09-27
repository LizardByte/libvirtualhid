/**
 * @file tests/unit/test_windows_keyboard_scancodes.cpp
 * @brief Regression tests for Windows keyboard scan-code and extended-prefix handling.
 */

#ifndef DOXYGEN
  #if defined(WINVER) && WINVER < 0x0A00
    #undef WINVER
  #endif
  #ifndef WINVER
    #define WINVER 0x0A00
  #endif
  #if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0A00
    #undef _WIN32_WINNT
  #endif
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0A00
  #endif
#endif

#include "fixtures/fixtures.hpp"
#include "fixtures/windows_backend_test_hooks.hpp"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

namespace {

  class WindowsKeyboardScanCodeTest: public WindowsTest {};

  bool has_extended_flag(const lvh::detail::test::WindowsSendInputRecord &record) {
    return (record.key_flags & KEYEVENTF_EXTENDEDKEY) != 0U;
  }

  bool uses_scancode(const lvh::detail::test::WindowsSendInputRecord &record) {
    return (record.key_flags & KEYEVENTF_SCANCODE) != 0U;
  }

}  // namespace

TEST_F(WindowsKeyboardScanCodeTest, NormalizedKeysDoNotInferExtendedPrefixFromLowByte) {
  const auto main_enter = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_RETURN,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(main_enter));
  EXPECT_EQ(main_enter.scan_code, 0x1CU);
  EXPECT_FALSE(has_extended_flag(main_enter));

  const auto slash = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_OEM_2,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(slash));
  EXPECT_EQ(slash.scan_code, 0x53U);
  EXPECT_FALSE(has_extended_flag(slash));

  const auto backslash = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_OEM_5,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(backslash));
  EXPECT_EQ(backslash.scan_code, 0x2BU);
  EXPECT_FALSE(has_extended_flag(backslash));

  const auto left_alt = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_MENU,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(left_alt));
  EXPECT_EQ(left_alt.scan_code, 0x38U);
  EXPECT_FALSE(has_extended_flag(left_alt));
}

TEST_F(WindowsKeyboardScanCodeTest, NormalizedExtendedKeysKeepActualPrefix) {
  const auto keypad_divide = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_DIVIDE,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(keypad_divide));
  EXPECT_EQ(keypad_divide.scan_code, 0x35U);
  EXPECT_TRUE(has_extended_flag(keypad_divide));

  const auto right_alt = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_RMENU,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(right_alt));
  EXPECT_EQ(right_alt.scan_code, 0x38U);
  EXPECT_TRUE(has_extended_flag(right_alt));

  const auto home = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_HOME,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(home));
  EXPECT_EQ(home.scan_code, 0x47U);
  EXPECT_FALSE(has_extended_flag(home));

  const auto keypad_home = lvh::detail::test::windows_submit_keyboard_event({
    .key_code = VK_NUMPAD7,
    .pressed = true,
    .uses_normalized_key_code = true,
  });
  ASSERT_TRUE(uses_scancode(keypad_home));
  EXPECT_EQ(keypad_home.scan_code, 0x47U);
  EXPECT_FALSE(has_extended_flag(keypad_home));
}

TEST_F(WindowsKeyboardScanCodeTest, ExplicitScanCodeUsesProvidedExtendedPrefix) {
  const auto keypad_enter = lvh::detail::test::windows_submit_keyboard_event({
    .scan_code = 0x1C,
    .scan_code_extended = true,
    .pressed = true,
  });
  ASSERT_TRUE(uses_scancode(keypad_enter));
  EXPECT_EQ(keypad_enter.scan_code, 0x1CU);
  EXPECT_TRUE(has_extended_flag(keypad_enter));

  const auto main_enter = lvh::detail::test::windows_submit_keyboard_event({
    .scan_code = 0x1C,
    .scan_code_extended = false,
    .pressed = true,
  });
  ASSERT_TRUE(uses_scancode(main_enter));
  EXPECT_EQ(main_enter.scan_code, 0x1CU);
  EXPECT_FALSE(has_extended_flag(main_enter));
}

TEST_F(WindowsKeyboardScanCodeTest, MapVirtualKeyExPreservesExtendedPrefix) {
  const auto mapped_divide = lvh::detail::test::windows_map_active_layout_scan_code(VK_DIVIDE);
  EXPECT_EQ(mapped_divide.scan_code, 0x35U);
  EXPECT_TRUE(mapped_divide.extended);

  const auto mapped_return = lvh::detail::test::windows_map_active_layout_scan_code(VK_RETURN);
  EXPECT_EQ(mapped_return.scan_code, 0x1CU);
  EXPECT_FALSE(mapped_return.extended);
}
