/**
 * @file tests/fixtures/include/fixtures/macos_backend_test_hooks.hpp
 * @brief Private macOS backend test hooks.
 */
#pragma once

// standard includes
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

// lib includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail::test {

  /**
   * @brief Portable representation of a CoreGraphics point for tests.
   */
  struct MacosPoint {
    double x {};  ///< Horizontal coordinate.
    double y {};  ///< Vertical coordinate.
  };

  /**
   * @brief Portable representation of CoreGraphics mouse motion metadata for tests.
   */
  struct MacosMouseMotionResult {
    std::uint32_t button {};  ///< CoreGraphics mouse button value.
    std::uint32_t event_type {};  ///< CoreGraphics mouse event type value.
  };

  /**
   * @brief Portable view of one CoreGraphics keyboard event the macOS backend built.
   */
  struct MacosKeyEventResult {
    std::uint32_t event_type {};  ///< CoreGraphics event type value.
    std::int64_t key_code {};  ///< macOS virtual key code stored on the event.
    std::uint64_t flags {};  ///< Flags stored on the event.
    std::uint64_t tracked_flags {};  ///< Shared modifier state after the event was built.
  };

  /**
   * @brief Result set for macOS backend lifecycle utility coverage.
   */
  struct MacosBackendUtilityResult {
    BackendCapabilities capabilities;  ///< Backend capabilities reported by the macOS backend.
    OperationStatus keyboard_create_status;  ///< Keyboard creation status.
    OperationStatus keyboard_text_status;  ///< Non-empty keyboard text submit status.
    OperationStatus keyboard_empty_text_status;  ///< Empty keyboard text submit status.
    OperationStatus keyboard_invalid_text_status;  ///< Invalid UTF-8 keyboard text submit status.
    OperationStatus keyboard_close_status;  ///< Keyboard close status.
    OperationStatus keyboard_submit_after_close_status;  ///< Keyboard submit status after close.
    OperationStatus keyboard_text_after_close_status;  ///< Keyboard text submit status after close.
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
   * @brief Resolve the flags the macOS backend adds to a key event for the key itself.
   *
   * @param key_code Portable key code.
   * @return CoreGraphics event flags, or zero when the key is unmapped or carries none.
   */
  std::uint64_t macos_backend_implicit_key_flags(KeyboardKeyCode key_code);

  /**
   * @brief Build, without posting, the events the macOS backend would send for a key sequence.
   *
   * @param transitions Portable key codes paired with `true` for press and `false` for release.
   * @return One result per transition whose key code the backend maps.
   */
  std::vector<MacosKeyEventResult> macos_backend_key_events(const std::vector<std::pair<KeyboardKeyCode, bool>> &transitions);

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
   * @brief Convert an absolute mouse event to a macOS display location.
   *
   * @param event Mouse event to convert.
   * @param origin_x Display origin X coordinate.
   * @param origin_y Display origin Y coordinate.
   * @param width Display width.
   * @param height Display height.
   * @return Display location that the macOS backend will post.
   */
  MacosPoint macos_backend_absolute_mouse_location(
    const MouseEvent &event,
    double origin_x,
    double origin_y,
    double width,
    double height
  );

  /**
   * @brief Select CoreGraphics motion metadata for a mouse button state.
   *
   * @param left_down Whether the left button is held.
   * @param right_down Whether the right button is held.
   * @param middle_down Whether the middle button is held.
   * @return CoreGraphics button and motion event type values.
   */
  MacosMouseMotionResult macos_backend_mouse_motion(bool left_down, bool right_down, bool middle_down);

  /**
   * @brief Exercise macOS backend creation and unsupported-device paths.
   *
   * @return Lifecycle and unsupported-device statuses.
   */
  MacosBackendUtilityResult macos_backend_utilities();

}  // namespace lvh::detail::test
