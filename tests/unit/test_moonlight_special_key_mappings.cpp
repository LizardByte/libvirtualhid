/**
 * @file tests/unit/test_moonlight_special_key_mappings.cpp
 * @brief Regression tests for Moonlight layout-sensitive scancode mapping.
 */

#include "streaming/input/keyboard_special_keys.h"

#include <gtest/gtest.h>

TEST(MoonlightSpecialKeyMappings, UsQwertyAndUkIsoKeepNonusBackslashNormalized) {
  const auto mapping = mapMoonlightSpecialScancode(SDL_SCANCODE_NONUSBACKSLASH, false, false);
  ASSERT_TRUE(mapping.handled);
  EXPECT_EQ(mapping.key_code, 0xE2);
  EXPECT_EQ(mapping.flags, 0);
}

TEST(MoonlightSpecialKeyMappings, GermanAndFrenchIsoInternational1StaysNormalized) {
  const auto mapping = mapMoonlightSpecialScancode(SDL_SCANCODE_INTERNATIONAL1, false, false);
  ASSERT_TRUE(mapping.handled);
  EXPECT_EQ(mapping.key_code, 0xE2);
  EXPECT_EQ(mapping.flags, 0);
}

TEST(MoonlightSpecialKeyMappings, JapaneseJisRoUsesNonNormalizedIdentity) {
  const auto international1 = mapMoonlightSpecialScancode(SDL_SCANCODE_INTERNATIONAL1, true, false);
  ASSERT_TRUE(international1.handled);
  EXPECT_EQ(international1.key_code, 0xE2);
  EXPECT_EQ(international1.flags, SS_KBE_FLAG_NON_NORMALIZED);

  const auto ro_backslash = mapMoonlightSpecialScancode(SDL_SCANCODE_BACKSLASH, true, true);
  ASSERT_TRUE(ro_backslash.handled);
  EXPECT_EQ(ro_backslash.key_code, 0xE2);
  EXPECT_EQ(ro_backslash.flags, SS_KBE_FLAG_NON_NORMALIZED);

  const auto nonus_on_jis = mapMoonlightSpecialScancode(SDL_SCANCODE_NONUSBACKSLASH, true, true);
  ASSERT_TRUE(nonus_on_jis.handled);
  EXPECT_EQ(nonus_on_jis.key_code, 0xE2);
  EXPECT_EQ(nonus_on_jis.flags, 0);
}

TEST(MoonlightSpecialKeyMappings, UsQwertyBackslashMapsToOem5) {
  const auto mapping = mapMoonlightSpecialScancode(SDL_SCANCODE_BACKSLASH, false, false);
  ASSERT_TRUE(mapping.handled);
  EXPECT_EQ(mapping.key_code, 0xDC);
  EXPECT_EQ(mapping.flags, 0);
}

TEST(MoonlightSpecialKeyMappings, KoreanLangKeysUseDistinctProtocolFlags) {
  const auto lang1 = mapMoonlightSpecialScancode(SDL_SCANCODE_LANG1, false, false);
  ASSERT_TRUE(lang1.handled);
  EXPECT_EQ(lang1.key_code, 0x15);
  EXPECT_EQ(lang1.flags, static_cast<std::uint8_t>(SS_KBE_FLAG_NON_NORMALIZED | SS_KBE_FLAG_LANG1));

  const auto lang2 = mapMoonlightSpecialScancode(SDL_SCANCODE_LANG2, false, false);
  ASSERT_TRUE(lang2.handled);
  EXPECT_EQ(lang2.key_code, 0x19);
  EXPECT_EQ(lang2.flags, static_cast<std::uint8_t>(SS_KBE_FLAG_NON_NORMALIZED | SS_KBE_FLAG_LANG2));
}
