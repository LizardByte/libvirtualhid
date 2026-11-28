/**
 * @file tests/fixtures/macos_backend_test_hooks.cpp
 * @brief macOS backend test hook definitions.
 */

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// local includes
#include "core/backend.hpp"
#include "fixtures/macos_backend_test_hooks.hpp"
#include "platform/macos/macos_broker_client.hpp"

// platform includes
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOLLEvent.h>

namespace {

  struct MouseEnvironment;

  /**
   * @brief Access the active mouse environment for the calling thread.
   *
   * @return Reference to the scoped environment pointer, or a null pointer when no environment is active.
   */
  MouseEnvironment *&mouse_environment() {
    static thread_local MouseEnvironment *environment = nullptr;
    return environment;
  }

  /**
   * @brief Scoped display geometry and captured mouse delivery for backend submission tests.
   */
  struct MouseEnvironment {
    explicit MouseEnvironment(const lvh::detail::test::MacosViewportBounds &bounds):
        display_bounds {bounds} {
      mouse_environment() = this;
    }

    MouseEnvironment(const MouseEnvironment &) = delete;
    MouseEnvironment &operator=(const MouseEnvironment &) = delete;
    MouseEnvironment(MouseEnvironment &&) = delete;
    MouseEnvironment &operator=(MouseEnvironment &&) = delete;

    ~MouseEnvironment() {
      mouse_environment() = nullptr;
    }

    lvh::detail::test::MacosViewportBounds display_bounds;
    lvh::detail::test::MacosPoint cursor_location;
    std::optional<lvh::detail::test::MacosPoint> posted_location;
    std::optional<lvh::detail::test::MacosPoint> warped_location;
    lvh::detail::test::MacosMouseEventsResult *events = nullptr;  ///< Optional capture of native event metadata.
    bool fail_first_creation = false;  ///< Whether to fail the next mouse event allocation.
    bool screen_locked = false;  ///< Session lock state reported to the backend.
    std::size_t source_creations {};  ///< HID-system-state event source creations the backend attempted.
    bool fail_next_source_creation = false;  ///< Whether to fail the next mouse event source creation.
  };

  /**
   * @brief Report the scoped session lock state instead of the real login session.
   *
   * @return Session dictionary owned by the caller, or null when the session is unavailable.
   */
  CFDictionaryRef mouse_session_dictionary() {
    if (!mouse_environment()) {
      return CGSessionCopyCurrentDictionary();
    }
    const auto session = CFDictionaryCreateMutable(kCFAllocatorDefault, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (session) {
      CFDictionarySetValue(session, CFSTR("CGSSessionScreenIsLocked"), mouse_environment()->screen_locked ? kCFBooleanTrue : kCFBooleanFalse);
    }
    return session;
  }

  /**
   * @brief Count mouse event source creations and optionally fail the next one.
   *
   * @param state Requested event source state.
   * @return New native event source, or null when creation fails.
   */
  CGEventSourceRef mouse_create_event_source(CGEventSourceStateID state) {
    if (mouse_environment() && state == kCGEventSourceStateHIDSystemState) {
      ++mouse_environment()->source_creations;
      if (mouse_environment()->fail_next_source_creation) {
        mouse_environment()->fail_next_source_creation = false;
        return nullptr;
      }
    }
    return CGEventSourceCreate(state);
  }

  /**
   * @brief Age cursor snapshots while capturing fresh mouse event submissions.
   *
   * @param source CoreGraphics event source.
   * @return Native cursor snapshot, or null when allocation fails.
   */
  CGEventRef mouse_create_event(CGEventSourceRef source) {
    const auto event = CGEventCreate(source);
    if (event && mouse_environment() && mouse_environment()->events) {
      CGEventSetTimestamp(event, 1);
    }
    return event;
  }

  /**
   * @brief Count native mouse event allocations and optionally fail the first allocation.
   *
   * @param source CoreGraphics event source.
   * @param type Requested event type.
   * @param location Cursor location.
   * @param button CoreGraphics button number.
   * @return New native mouse event, or null for the injected failure.
   */
  CGEventRef mouse_create_mouse_event(CGEventSourceRef source, CGEventType type, CGPoint location, CGMouseButton button) {
    if (mouse_environment() && mouse_environment()->events) {
      ++mouse_environment()->events->creation_attempts;
      if (mouse_environment()->fail_first_creation) {
        mouse_environment()->fail_first_creation = false;
        return nullptr;
      }
    }
    return CGEventCreateMouseEvent(source, type, location, button);
  }

  /**
   * @brief Read the local-event suppression copied into a native event source.
   *
   * @param event Mouse event whose source is inspected.
   * @return Suppression interval in seconds, or no value when source creation fails.
   */
  std::optional<double> mouse_event_suppression_interval(CGEventRef event) {
    const auto source = CGEventCreateSourceFromEvent(event);
    if (!source) {
      return std::nullopt;
    }
    const auto interval = CGEventSourceGetLocalEventsSuppressionInterval(source);
    CFRelease(source);
    return interval;
  }

  CGDirectDisplayID mouse_main_display_id() {
    return mouse_environment() ? 42 : CGMainDisplayID();
  }

  CGRect mouse_display_bounds(CGDirectDisplayID display) {
    if (!mouse_environment()) {
      return CGDisplayBounds(display);
    }
    const auto &bounds = mouse_environment()->display_bounds;
    return CGRect {{bounds.origin_x, bounds.origin_y}, {bounds.width, bounds.height}};
  }

  CGPoint mouse_event_location(CGEventRef event) {
    if (!mouse_environment()) {
      return CGEventGetLocation(event);
    }
    return CGPoint {mouse_environment()->cursor_location.x, mouse_environment()->cursor_location.y};
  }

  void mouse_event_post(CGEventTapLocation tap, CGEventRef event) {
    if (!mouse_environment()) {
      CGEventPost(tap, event);
      return;
    }
    const auto point = CGEventGetLocation(event);
    mouse_environment()->posted_location = {.x = point.x, .y = point.y};
    if (mouse_environment()->events) {
      mouse_environment()->events->events.push_back({
        .tap_location = static_cast<std::uint32_t>(tap),
        .event_type = static_cast<std::uint32_t>(CGEventGetType(event)),
        .button = CGEventGetIntegerValueField(event, kCGMouseEventButtonNumber),
        .click_count = CGEventGetIntegerValueField(event, kCGMouseEventClickState),
        .timestamp = CGEventGetTimestamp(event),
        .flags = static_cast<std::uint64_t>(CGEventGetFlags(event)),
        .suppression_interval = mouse_event_suppression_interval(event),
        .location = {point.x, point.y},
        .delta = {CGEventGetDoubleValueField(event, kCGMouseEventDeltaX), CGEventGetDoubleValueField(event, kCGMouseEventDeltaY)},
      });
    }
  }

  CGError mouse_warp_cursor_position(CGPoint point) {
    if (!mouse_environment()) {
      return CGWarpMouseCursorPosition(point);
    }
    mouse_environment()->warped_location = {.x = point.x, .y = point.y};
    if (mouse_environment()->events) {
      ++mouse_environment()->events->cursor_warps;
    }
    return kCGErrorSuccess;
  }

}  // namespace

// Load the backend's dependencies before these macros, then compile it in a
// separate namespace so instrumented inline symbols remain isolated.
#define macos macos_backend_test
#define CGMainDisplayID mouse_main_display_id
#define CGEventCreate mouse_create_event
#define CGEventCreateMouseEvent mouse_create_mouse_event
#define CGDisplayBounds mouse_display_bounds
#define CGEventGetLocation mouse_event_location
#define CGEventPost mouse_event_post
#define CGWarpMouseCursorPosition mouse_warp_cursor_position
#define CGSessionCopyCurrentDictionary mouse_session_dictionary
#define CGEventSourceCreate mouse_create_event_source
#define create_platform_backend create_platform_backend_for_macos_backend_test_hooks
#include "../../src/platform/macos/macos_backend.cpp"
#undef create_platform_backend
#undef CGEventSourceCreate
#undef CGSessionCopyCurrentDictionary
#undef CGWarpMouseCursorPosition
#undef CGEventPost
#undef CGEventGetLocation
#undef CGDisplayBounds
#undef CGEventCreateMouseEvent
#undef CGEventCreate
#undef CGMainDisplayID
#undef macos

namespace lvh::detail::test {

  namespace macos = lvh::detail::macos_backend_test;

  std::optional<double> macos_backend_mouse_event_suppression_interval() {
    const auto result = macos_backend_mouse_events({{.kind = MouseEventKind::relative_motion, .x = 1, .y = 1}});
    return result.events.empty() ? std::nullopt : result.events.front().suppression_interval;
  }

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

  MacosViewportBounds macos_backend_mouse_viewport_bounds(const PointerViewport &viewport) {
    const auto bounds = macos::mouse_viewport_bounds(viewport);
    return {
      .origin_x = bounds.origin.x,
      .origin_y = bounds.origin.y,
      .width = bounds.size.width,
      .height = bounds.size.height,
    };
  }

  std::vector<MacosMouseSubmissionResult> macos_backend_mouse_submissions(
    const PointerViewport &viewport,
    const MacosViewportBounds &initial_display_bounds,
    const std::vector<MacosMouseSubmission> &submissions
  ) {
    MouseEnvironment environment {initial_display_bounds};
    auto backend = create_platform_backend_for_macos_backend_test_hooks();
    CreateMouseOptions options;
    options.profile.device_type = DeviceType::mouse;
    options.viewport = viewport;
    auto created = backend->create_mouse(1, options);
    if (!created) {
      return {{.status = created.status, .posted_location = std::nullopt, .warped_location = std::nullopt}};
    }

    std::vector<MacosMouseSubmissionResult> results;
    for (const auto &submission : submissions) {
      environment.display_bounds = submission.display_bounds;
      environment.cursor_location = submission.cursor_location;
      environment.posted_location.reset();
      environment.warped_location.reset();
      const auto status = created.mouse->submit(submission.event);
      results.emplace_back(status, environment.posted_location, environment.warped_location);
    }
    return results;
  }

  MacosMouseMotionResult macos_backend_mouse_motion(bool left_down, bool right_down, bool middle_down) {
    const auto motion = macos::macos_mouse_motion({left_down, right_down, middle_down});
    return {
      .button = static_cast<std::uint32_t>(motion.button),
      .event_type = static_cast<std::uint32_t>(motion.event_type),
    };
  }

  MacosMouseEventsResult macos_backend_mouse_events(const std::vector<MouseEvent> &events, bool fail_first_creation, bool source_available) {
    MacosMouseEventsResult result;
    MouseEnvironment environment {{.origin_x = 10, .origin_y = 20, .width = 400, .height = 200}};
    environment.cursor_location = {40, 60};
    environment.events = &result;
    environment.fail_first_creation = fail_first_creation;

    auto state = std::make_shared<macos::MacosInputState>();
    state->keyboard_flags = kCGEventFlagMaskShift | kCGEventFlagMaskControl;
    if (!source_available && state->source) {
      CFRelease(state->source);
      state->source = nullptr;
    }

    macos::MacosMouse mouse {std::move(state), {}};
    for (const auto &event : events) {
      result.statuses.push_back(mouse.submit(event));
    }
    return result;
  }

  std::vector<MacosMouseLockSubmissionResult> macos_backend_mouse_lock_submissions(
    const std::vector<MacosMouseLockSubmission> &submissions
  ) {
    MacosMouseEventsResult captured;
    MouseEnvironment environment {{.origin_x = 0, .origin_y = 0, .width = 400, .height = 200}};
    environment.events = &captured;

    const auto state = std::make_shared<macos::MacosInputState>();
    macos::MacosMouse mouse {state, {}};

    std::vector<MacosMouseLockSubmissionResult> results;
    for (const auto &submission : submissions) {
      environment.screen_locked = submission.screen_locked;
      environment.fail_next_source_creation = submission.fail_source_creation;
      if (submission.poll_due) {
        state->last_lock_check = {};
      }
      captured.events.clear();
      const auto status = mouse.submit({.kind = MouseEventKind::relative_motion, .x = 1, .y = 1});
      results.push_back({
        .status = status,
        .source_creations = environment.source_creations,
        .suppression_interval = captured.events.empty() ? std::nullopt : captured.events.back().suppression_interval,
      });
    }
    return results;
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
