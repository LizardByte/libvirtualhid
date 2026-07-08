/**
 * @file tests/fixtures/macos_backend_test_hooks.cpp
 * @brief macOS backend test hook definitions.
 */

// local includes
#include "fixtures/macos_backend_test_hooks.hpp"

#define create_platform_backend create_platform_backend_for_macos_backend_test_hooks
#include "../../src/platform/macos/macos_backend.cpp"
#undef create_platform_backend

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

  int macos_backend_scroll_lines_per_detent(double scale) {
    return macos::scroll_lines_per_detent(scale);
  }

  int macos_backend_scroll_pixels(std::int32_t high_resolution_distance, int pixels_per_line, int lines_per_detent) {
    return macos::scroll_pixels(high_resolution_distance, pixels_per_line, lines_per_detent);
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
      result.keyboard_text_status = keyboard.keyboard->type_text({.text = "A"});
      result.keyboard_close_status = keyboard.keyboard->close();
      result.keyboard_submit_after_close_status = keyboard.keyboard->submit({.key_code = 0x41, .pressed = true});
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
