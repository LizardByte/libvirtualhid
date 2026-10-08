/**
 * @file tests/fixtures/macos_backend_test_hooks.cpp
 * @brief macOS backend test hook definitions.
 */

// local includes
#include "fixtures/macos_backend_test_hooks.hpp"

// platform includes
#include <ApplicationServices/ApplicationServices.h>

namespace {

  thread_local lvh::detail::test::MacosMouseSubmissionResult *mouse_submissions = nullptr;  ///< Active mouse capture.
  thread_local bool fail_mouse_creation = false;  ///< Whether the next mouse allocation should fail.

  /**
   * @brief Replace cursor snapshots with deterministic, aged events during mouse tests.
   * @param source CoreGraphics event source.
   * @return Cursor snapshot, or null when allocation fails.
   */
  CGEventRef test_create_event(CGEventSourceRef source) {
    const auto event = CGEventCreate(source);
    if (event && mouse_submissions) {
      CGEventSetTimestamp(event, 1);
      CGEventSetLocation(event, CGPoint {40, 60});
    }
    return event;
  }

  /**
   * @brief Count mouse event allocations and optionally fail one allocation.
   * @param source CoreGraphics event source.
   * @param type Requested event type.
   * @param location Cursor location.
   * @param button CoreGraphics button number.
   * @return Newly created native mouse event, or null for the injected failure.
   */
  CGEventRef test_create_mouse_event(CGEventSourceRef source, CGEventType type, CGPoint location, CGMouseButton button) {
    if (mouse_submissions) {
      ++mouse_submissions->creation_attempts;
      if (fail_mouse_creation) {
        fail_mouse_creation = false;
        return nullptr;
      }
    }
    return CGEventCreateMouseEvent(source, type, location, button);
  }

  /**
   * @brief Use fixed display bounds during mouse tests.
   * @param display CoreGraphics display identifier.
   * @return Deterministic test bounds or the native display bounds.
   */
  CGRect test_display_bounds(CGDirectDisplayID display) {
    return mouse_submissions ? CGRect {CGPoint {10, 20}, CGSize {400, 200}} : CGDisplayBounds(display);
  }

  /**
   * @brief Capture native mouse event metadata without posting input to the desktop.
   * @param tap Event tap requested by the backend.
   * @param event Event that would be posted.
   */
  void test_post_event(CGEventTapLocation tap, CGEventRef event) {
    if (!mouse_submissions) {
      return;
    }
    const auto location = CGEventGetLocation(event);
    mouse_submissions->events.push_back({
      .tap_location = static_cast<std::uint32_t>(tap),
      .event_type = static_cast<std::uint32_t>(CGEventGetType(event)),
      .button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber),
      .click_count = CGEventGetIntegerValueField(event, kCGMouseEventClickState),
      .timestamp = CGEventGetTimestamp(event),
      .flags = static_cast<std::uint64_t>(CGEventGetFlags(event)),
      .location = {location.x, location.y},
      .delta = {CGEventGetDoubleValueField(event, kCGMouseEventDeltaX), CGEventGetDoubleValueField(event, kCGMouseEventDeltaY)},
    });
  }

  /**
   * @brief Count cursor updates without moving the physical cursor.
   * @param location Requested cursor position.
   * @return Success without changing the desktop.
   */
  CGError test_warp_cursor([[maybe_unused]] CGPoint location) {
    if (mouse_submissions) {
      ++mouse_submissions->cursor_warps;
    }
    return kCGErrorSuccess;
  }

  /**
   * @brief Restore the previous capture state after a mouse test, including on exceptions.
   */
  class MouseEventCapture {
  public:
    /**
     * @brief Activate one mouse capture.
     * @param result Result storage for the intercepted native calls.
     * @param fail_first_creation Whether to fail the next mouse event allocation.
     */
    MouseEventCapture(lvh::detail::test::MacosMouseSubmissionResult &result, bool fail_first_creation):
        previous_result_ {mouse_submissions},
        previous_failure_ {fail_mouse_creation} {
      mouse_submissions = &result;
      fail_mouse_creation = fail_first_creation;
    }

    /**
     * @brief Restore the enclosing capture state.
     */
    ~MouseEventCapture() {
      mouse_submissions = previous_result_;
      fail_mouse_creation = previous_failure_;
    }

  private:
    lvh::detail::test::MacosMouseSubmissionResult *previous_result_;  ///< Enclosing capture, when present.
    bool previous_failure_;  ///< Enclosing allocation failure setting.
  };

}  // namespace

#define CGEventCreate test_create_event
#define CGEventCreateMouseEvent test_create_mouse_event
#define CGDisplayBounds test_display_bounds
#define CGEventPost test_post_event
#define CGWarpMouseCursorPosition test_warp_cursor
#define create_platform_backend create_platform_backend_for_macos_backend_test_hooks
#include "../../src/platform/macos/macos_backend.cpp"
#undef create_platform_backend
#undef CGWarpMouseCursorPosition
#undef CGEventPost
#undef CGDisplayBounds
#undef CGEventCreateMouseEvent
#undef CGEventCreate

namespace lvh::detail::test {

  std::optional<std::uint16_t> macos_backend_key_code(KeyboardKeyCode key_code) {
    const auto mapped = macos::macos_key_code(key_code);
    if (!mapped) {
      return std::nullopt;
    }

    return static_cast<std::uint16_t>(*mapped);
  }

  bool macos_backend_is_modifier_key(KeyboardKeyCode key_code) {
    const auto mapped = macos::macos_key_code(key_code);
    if (!mapped) {
      return false;
    }

    macos::ModifierFlags flags;
    return macos::modifier_flags_for_key(*mapped, flags);
  }

  std::uint64_t macos_backend_implicit_key_flags(KeyboardKeyCode key_code) {
    const auto mapped = macos::macos_key_code(key_code);
    if (!mapped) {
      return 0;
    }

    return static_cast<std::uint64_t>(macos::implicit_key_flags(*mapped));
  }

  std::vector<MacosKeyEventResult> macos_backend_key_events(const std::vector<std::pair<KeyboardKeyCode, bool>> &transitions) {
    macos::MacosInputState state;
    std::lock_guard lock {state.keyboard_mutex};

    std::vector<MacosKeyEventResult> results;
    for (const auto &[key_code, pressed] : transitions) {
      const auto mapped = macos::macos_key_code(key_code);
      if (!mapped) {
        continue;
      }

      const auto event = macos::create_keyboard_event(state, *mapped, pressed);
      if (!event) {
        continue;
      }

      results.push_back({
        .event_type = static_cast<std::uint32_t>(CGEventGetType(event)),
        .key_code = CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode),
        .flags = static_cast<std::uint64_t>(CGEventGetFlags(event)),
        .tracked_flags = static_cast<std::uint64_t>(state.keyboard_flags),
      });
      CFRelease(event);
    }

    return results;
  }

  int macos_backend_scroll_lines_per_detent(double scale) {
    return macos::scroll_lines_per_detent(scale);
  }

  int macos_backend_scroll_pixels(std::int32_t high_resolution_distance, int pixels_per_line, int lines_per_detent) {
    return macos::scroll_pixels(high_resolution_distance, pixels_per_line, lines_per_detent);
  }

  MacosPoint macos_backend_absolute_mouse_location(
    const MouseEvent &event,
    double origin_x,
    double origin_y,
    double width,
    double height
  ) {
    const auto location = macos::absolute_mouse_location(
      event,
      CGRect {
        .origin = CGPoint {origin_x, origin_y},
        .size = CGSize {width, height}
      }
    );
    return {.x = location.x, .y = location.y};
  }

  MacosMouseMotionResult macos_backend_mouse_motion(bool left_down, bool right_down, bool middle_down) {
    const auto motion = macos::macos_mouse_motion({left_down, right_down, middle_down});
    return {
      .button = static_cast<std::uint32_t>(motion.button),
      .event_type = static_cast<std::uint32_t>(motion.event_type),
    };
  }

  MacosMouseSubmissionResult macos_backend_mouse_events(const std::vector<MouseEvent> &events, bool fail_first_creation, bool source_available) {
    MacosMouseSubmissionResult result;
    MouseEventCapture capture {result, fail_first_creation};
    auto state = std::make_shared<macos::MacosInputState>();
    state->keyboard_flags = kCGEventFlagMaskShift | kCGEventFlagMaskControl;
    if (!source_available && state->source) {
      CFRelease(state->source);
      state->source = nullptr;
    }

    macos::MacosMouse mouse {std::move(state)};
    for (const auto &event : events) {
      result.statuses.push_back(mouse.submit(event));
    }
    return result;
  }

  MacosBackendUtilityResult macos_backend_utilities() {
    auto backend = create_platform_backend_for_macos_backend_test_hooks();
    MacosBackendUtilityResult result;
    result.capabilities = backend->capabilities();

    CreateKeyboardOptions keyboard_options;
    keyboard_options.profile.device_type = DeviceType::keyboard;
    keyboard_options.profile.name = "libvirtualhid test keyboard";
    auto keyboard = backend->create_keyboard(1, keyboard_options);
    result.keyboard_create_status = keyboard.status;
    if (keyboard) {
      result.keyboard_text_status = keyboard.keyboard->type_text({.text = "Text \x{E2}\x{98}\x{80} \x{F0}\x{9F}\x{98}\x{80}"});
      result.keyboard_empty_text_status = keyboard.keyboard->type_text({.text = ""});
      result.keyboard_invalid_text_status = keyboard.keyboard->type_text({.text = std::string(1U, static_cast<char>(0xFF))});
      result.keyboard_close_status = keyboard.keyboard->close();
      result.keyboard_submit_after_close_status = keyboard.keyboard->submit({.key_code = 0x41, .pressed = true});
      result.keyboard_text_after_close_status = keyboard.keyboard->type_text({.text = "A"});
    }

    CreateKeyboardOptions invalid_keyboard_options;
    invalid_keyboard_options.profile.device_type = DeviceType::mouse;
    result.keyboard_invalid_profile_status = backend->create_keyboard(2, invalid_keyboard_options).status;

    CreateMouseOptions mouse_options;
    mouse_options.profile.device_type = DeviceType::mouse;
    mouse_options.profile.name = "libvirtualhid test mouse";
    auto mouse = backend->create_mouse(3, mouse_options);
    result.mouse_create_status = mouse.status;
    if (mouse) {
      result.mouse_close_status = mouse.mouse->close();
      result.mouse_submit_after_close_status = mouse.mouse->submit({.kind = MouseEventKind::relative_motion, .x = 1, .y = 1});
    }

    CreateMouseOptions invalid_mouse_options;
    invalid_mouse_options.profile.device_type = DeviceType::keyboard;
    result.mouse_invalid_profile_status = backend->create_mouse(4, invalid_mouse_options).status;

    CreateGamepadOptions gamepad_options;
    gamepad_options.profile.device_type = DeviceType::gamepad;
    result.gamepad_status = backend->create_gamepad(5, gamepad_options).status;

    CreateTouchscreenOptions touchscreen_options;
    touchscreen_options.profile.device_type = DeviceType::touchscreen;
    result.touchscreen_status = backend->create_touchscreen(6, touchscreen_options).status;

    CreateTrackpadOptions trackpad_options;
    trackpad_options.profile.device_type = DeviceType::trackpad;
    result.trackpad_status = backend->create_trackpad(7, trackpad_options).status;

    CreatePenTabletOptions pen_options;
    pen_options.profile.device_type = DeviceType::pen_tablet;
    result.pen_tablet_status = backend->create_pen_tablet(8, pen_options).status;

    return result;
  }

}  // namespace lvh::detail::test
