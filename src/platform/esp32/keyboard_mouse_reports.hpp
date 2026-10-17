/**
 * @file src/platform/esp32/keyboard_mouse_reports.hpp
 * @brief HID keyboard and mouse reports for the ESP32-S3 bridge keyboard/mouse slot.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// local includes
#include "platform/esp32/esp32_bridge_protocol.h"

#include <libvirtualhid/types.hpp>

namespace lvh::detail::esp32 {

  using KeyboardReport = std::array<std::uint8_t, LVHB_KM_KEYBOARD_REPORT_SIZE>;  ///< id, modifiers, reserved, 6 keys.
  using RelativeMouseReport = std::array<std::uint8_t, LVHB_KM_RELATIVE_REPORT_SIZE>;  ///< id, buttons, x, y, wheel, pan.
  using AbsoluteMouseReport = std::array<std::uint8_t, LVHB_KM_ABSOLUTE_REPORT_SIZE>;  ///< id, buttons, x, y.

  inline constexpr std::uint16_t absolute_pointer_max = 32767;  ///< Logical maximum of the absolute pointer axes.
  inline constexpr std::int32_t wheel_detent = 120;  ///< High-resolution scroll units per wheel detent.

  /**
   * @brief Report descriptor with keyboard (id 1), relative mouse (id 2), and absolute pointer (id 3) collections.
   *
   * @return Report descriptor bytes.
   */
  std::vector<std::uint8_t> keyboard_mouse_report_descriptor();

  /**
   * @brief Neutral reports for the slot as a u8-length-prefixed list.
   *
   * @return Keyboard and relative mouse reports with nothing pressed.
   */
  std::vector<std::uint8_t> keyboard_mouse_neutral_reports();

  /**
   * @brief Map a Windows virtual-key code to a HID Keyboard/Keypad usage.
   *
   * @param virtual_key Windows virtual-key code, US English layout positions.
   * @param extended Extended-key flag, used for keypad Enter and right Ctrl/Alt.
   * @return HID usage, or empty for keys without a keyboard-page usage.
   */
  std::optional<std::uint8_t> hid_usage_from_virtual_key(KeyboardKeyCode virtual_key, bool extended);

  /**
   * @brief Map a printable ASCII character to a US-layout key usage.
   *
   * @param character ASCII character.
   * @return Usage and whether Shift is needed, or empty for unmapped characters.
   */
  std::optional<std::pair<std::uint8_t, bool>> hid_usage_from_ascii(char character);

  /**
   * @brief Pressed-key state of a 6-key-rollover HID keyboard.
   */
  class KeyboardReportState {
  public:
    /**
     * @brief Apply a key transition.
     *
     * @param usage HID usage.
     * @param pressed Whether the key is pressed.
     * @return `true` when the report changed.
     */
    bool apply(std::uint8_t usage, bool pressed);

    /**
     * @brief Build the current report.
     *
     * @return Keyboard report.
     */
    KeyboardReport report() const;

    /**
     * @brief Build the current report with an extra key held, for text typing.
     *
     * @param usage Extra key usage, 0 for none.
     * @param shift Whether to add Left Shift.
     * @return Keyboard report.
     */
    KeyboardReport report_with(std::uint8_t usage, bool shift) const;

  private:
    std::uint8_t modifiers_ = 0;  ///< Pressed modifier bits.
    std::vector<std::uint8_t> keys_;  ///< Pressed non-modifier usages, at most six.
  };

  /**
   * @brief Build a relative mouse report.
   *
   * @param buttons Button bit mask, bit 0 is left.
   * @param x Horizontal motion.
   * @param y Vertical motion.
   * @param wheel Vertical wheel detents, positive away from the user.
   * @param pan Horizontal wheel detents, positive to the right.
   * @return Relative mouse report.
   */
  RelativeMouseReport make_relative_mouse_report(std::uint8_t buttons, std::int16_t x, std::int16_t y, std::int8_t wheel, std::int8_t pan);

  /**
   * @brief Build an absolute pointer report.
   *
   * @param buttons Button bit mask.
   * @param x Horizontal position, 0 to #absolute_pointer_max.
   * @param y Vertical position, 0 to #absolute_pointer_max.
   * @return Absolute pointer report.
   */
  AbsoluteMouseReport make_absolute_mouse_report(std::uint8_t buttons, std::uint16_t x, std::uint16_t y);

  /**
   * @brief Scale a source coordinate onto the absolute pointer axis.
   *
   * The axis spans the whole virtual desktop, like `SendInput` with
   * `MOUSEEVENTF_VIRTUALDESK`. A non-empty viewport maps the source into that
   * part of the desktop.
   *
   * @param value Source coordinate.
   * @param source_dimension Source coordinate-space dimension.
   * @param viewport_offset Target viewport offset in desktop pixels.
   * @param viewport_dimension Target viewport dimension, 0 for the whole desktop.
   * @param desktop_offset Virtual desktop offset in desktop pixels.
   * @param desktop_dimension Virtual desktop dimension.
   * @return Axis value in 0..#absolute_pointer_max.
   */
  std::uint16_t scale_absolute_pointer_axis(
    float value,
    std::int32_t source_dimension,
    std::int32_t viewport_offset,
    std::int32_t viewport_dimension,
    std::int32_t desktop_offset,
    std::int32_t desktop_dimension
  );

}  // namespace lvh::detail::esp32
