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
  };

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
  }

  CGError mouse_warp_cursor_position(CGPoint point) {
    if (!mouse_environment()) {
      return CGWarpMouseCursorPosition(point);
    }
    mouse_environment()->warped_location = {.x = point.x, .y = point.y};
    return kCGErrorSuccess;
  }

}  // namespace

// Load the backend's dependencies before these macros, then compile it in a
// separate namespace so instrumented inline symbols remain isolated.
#define macos macos_backend_test
#define CGMainDisplayID mouse_main_display_id
#define CGDisplayBounds mouse_display_bounds
#define CGEventGetLocation mouse_event_location
#define CGEventPost mouse_event_post
#define CGWarpMouseCursorPosition mouse_warp_cursor_position
#define create_platform_backend create_platform_backend_for_macos_backend_test_hooks
#include "../../src/platform/macos/macos_backend.cpp"
#undef create_platform_backend
#undef CGWarpMouseCursorPosition
#undef CGEventPost
#undef CGEventGetLocation
#undef CGDisplayBounds
#undef CGMainDisplayID
#undef macos

namespace lvh::detail::test {

  namespace macos = lvh::detail::macos_backend_test;

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
