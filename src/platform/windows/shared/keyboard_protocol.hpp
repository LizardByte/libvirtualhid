// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/shared/keyboard_protocol.hpp
 * @brief Canonical report framing for Windows driver-backed keyboards.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>

namespace lvh::detail::windows {

  /**
   * @brief Standard keyboard descriptor used by the Windows VHF path.
   *
   * The matching input report has no report-ID prefix. It contains one
   * modifier byte, one reserved byte, and sixteen simultaneous keyboard-page
   * usages. The one-byte output report exposes the standard keyboard LEDs.
   */
  inline constexpr auto keyboard_report_descriptor = std::to_array<std::uint8_t>({
    0x05,
    0x01,  // Usage Page (Generic Desktop)
    0x09,
    0x06,  // Usage (Keyboard)
    0xA1,
    0x01,  // Collection (Application)
    0x05,
    0x07,  // Usage Page (Keyboard/Keypad)
    0x19,
    0xE0,  // Usage Minimum (Keyboard Left Control)
    0x29,
    0xE7,  // Usage Maximum (Keyboard Right GUI)
    0x15,
    0x00,  // Logical Minimum (0)
    0x25,
    0x01,  // Logical Maximum (1)
    0x75,
    0x01,  // Report Size (1)
    0x95,
    0x08,  // Report Count (8)
    0x81,
    0x02,  // Input (Data,Var,Abs) - modifiers
    0x95,
    0x01,  // Report Count (1)
    0x75,
    0x08,  // Report Size (8)
    0x81,
    0x03,  // Input (Cnst,Var,Abs) - reserved
    0x95,
    0x05,  // Report Count (5)
    0x75,
    0x01,  // Report Size (1)
    0x05,
    0x08,  // Usage Page (LEDs)
    0x19,
    0x01,  // Usage Minimum (Num Lock)
    0x29,
    0x05,  // Usage Maximum (Kana)
    0x91,
    0x02,  // Output (Data,Var,Abs)
    0x95,
    0x01,  // Report Count (1)
    0x75,
    0x03,  // Report Size (3)
    0x91,
    0x03,  // Output (Cnst,Var,Abs) - padding
    0x05,
    0x07,  // Usage Page (Keyboard/Keypad)
    0x15,
    0x00,  // Logical Minimum (0)
    0x26,
    0x81,
    0x00,  // Logical Maximum (Keyboard Volume Down)
    0x19,
    0x00,  // Usage Minimum (Reserved/no event)
    0x2A,
    0x81,
    0x00,  // Usage Maximum (Keyboard Volume Down)
    0x75,
    0x08,  // Report Size (8)
    0x95,
    0x10,  // Report Count (16)
    0x81,
    0x00,  // Input (Data,Array,Abs)
    0xC0,  // End Collection
  });

}  // namespace lvh::detail::windows
