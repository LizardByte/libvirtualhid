/**
 * @file tests/fixtures/include/fixtures/macos_backend_test_hooks.hpp
 * @brief Private macOS backend test hooks.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>

// lib includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail::test {

  /**
   * @brief Result set for macOS backend lifecycle utility coverage.
   */
  struct MacosBackendUtilityResult {
    BackendCapabilities capabilities;  ///< Backend capabilities reported by the macOS backend.
    OperationStatus keyboard_create_status;  ///< Keyboard creation status.
    OperationStatus keyboard_text_status;  ///< Non-empty keyboard text submit status.
    OperationStatus keyboard_close_status;  ///< Keyboard close status.
    OperationStatus keyboard_submit_after_close_status;  ///< Keyboard submit status after close.
    OperationStatus keyboard_invalid_profile_status;  ///< Keyboard creation status for a non-keyboard profile.
    OperationStatus mouse_create_status;  ///< Mouse creation status.
    OperationStatus mouse_close_status;  ///< Mouse close status.
    OperationStatus mouse_submit_after_close_status;  ///< Mouse submit status after close.
    OperationStatus mouse_invalid_profile_status;  ///< Mouse creation status for a non-mouse profile.
    OperationStatus gamepad_status;  ///< Gamepad creation status.
    OperationStatus touchscreen_status;  ///< Touchscreen creation status.
    OperationStatus trackpad_status;  ///< Trackpad creation status.
    OperationStatus pen_tablet_status;  ///< Pen tablet creation status.
  };

  /**
   * @brief Translate a portable key code with the macOS backend map.
   *
   * @param key_code Portable key code.
   * @return macOS virtual key code when supported.
   */
  std::optional<std::uint16_t> macos_backend_key_code(KeyboardKeyCode key_code);

  /**
   * @brief Check whether a portable key code maps to a macOS modifier key.
   *
   * @param key_code Portable key code.
   * @return `true` when the mapped key is a modifier.
   */
  bool macos_backend_is_modifier_key(KeyboardKeyCode key_code);

  /**
   * @brief Convert a macOS scroll-wheel scaling value to lines per detent.
   *
   * @param scale macOS scroll-wheel scaling value.
   * @return Logical lines per wheel detent.
   */
  int macos_backend_scroll_lines_per_detent(double scale);

  /**
   * @brief Convert high-resolution scroll distance to CoreGraphics pixels.
   *
   * @param high_resolution_distance Wheel delta in high-resolution units.
   * @param pixels_per_line Pixel distance represented by one logical line.
   * @param lines_per_detent Logical lines represented by one wheel detent.
   * @return Pixel distance to send to CoreGraphics.
   */
  int macos_backend_scroll_pixels(std::int32_t high_resolution_distance, int pixels_per_line, int lines_per_detent);

  /**
   * @brief Exercise macOS backend creation and unsupported-device paths.
   *
   * @return Lifecycle and unsupported-device statuses.
   */
  MacosBackendUtilityResult macos_backend_utilities();

}  // namespace lvh::detail::test
