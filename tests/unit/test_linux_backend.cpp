/**
 * @file tests/unit/test_linux_backend.cpp
 * @brief Unit tests for Linux backend internals.
 */

// standard includes
#include <cstdint>
#include <string>
#include <vector>

// platform includes
#if defined(__linux__)
  #include <linux/input.h>
#endif

// lib includes
#include <libvirtualhid/libvirtualhid.hpp>

// local includes
#include "fixtures/fixtures.hpp"

#if defined(__linux__)
  #include "fixtures/linux_backend_test_hooks.hpp"
#endif

/**
 * @brief Test fixture for Linux backend internals.
 */
class LinuxBackendTest: public LinuxTest {};

#if defined(__linux__)
TEST_F(LinuxBackendTest, TranslatesKeyboardKeys) {
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x08), KEY_BACKSPACE);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x09), KEY_TAB);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x0D), KEY_ENTER);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x10), KEY_LEFTSHIFT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x11), KEY_LEFTCTRL);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x12), KEY_LEFTALT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x14), KEY_CAPSLOCK);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x1B), KEY_ESC);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x20), KEY_SPACE);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x21), KEY_PAGEUP);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x22), KEY_PAGEDOWN);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x23), KEY_END);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x24), KEY_HOME);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x25), KEY_LEFT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x26), KEY_UP);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x27), KEY_RIGHT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x28), KEY_DOWN);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x2C), KEY_SYSRQ);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x2D), KEY_INSERT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x2E), KEY_DELETE);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x5B), KEY_LEFTMETA);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x5C), KEY_RIGHTMETA);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x90), KEY_NUMLOCK);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x91), KEY_SCROLLLOCK);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xA0), KEY_LEFTSHIFT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xA1), KEY_RIGHTSHIFT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xA2), KEY_LEFTCTRL);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xA3), KEY_RIGHTCTRL);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xA4), KEY_LEFTALT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xA5), KEY_RIGHTALT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xBA), KEY_SEMICOLON);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xBB), KEY_EQUAL);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xBC), KEY_COMMA);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xBD), KEY_MINUS);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xBE), KEY_DOT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xBF), KEY_SLASH);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xC0), KEY_GRAVE);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xDB), KEY_LEFTBRACE);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xDC), KEY_BACKSLASH);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xDD), KEY_RIGHTBRACE);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xDE), KEY_APOSTROPHE);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0xE2), KEY_102ND);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x30), KEY_0);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x31), KEY_1);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x39), KEY_9);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x41), KEY_A);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x42), KEY_B);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x5A), KEY_Z);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x60), KEY_KP0);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x61), KEY_KP1);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x69), KEY_KP9);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x6A), KEY_KPASTERISK);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x6B), KEY_KPPLUS);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x6D), KEY_KPMINUS);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x6E), KEY_KPDOT);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x6F), KEY_KPSLASH);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x70), KEY_F1);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x7B), KEY_F12);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x87), KEY_F24);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0), -1);
  EXPECT_EQ(lvh::detail::test::linux_key_code(0x88), -1);
}

TEST_F(LinuxBackendTest, TranslatesMouseButtonsAndBusTypes) {
  EXPECT_EQ(lvh::detail::test::linux_mouse_button(lvh::MouseButton::left), BTN_LEFT);
  EXPECT_EQ(lvh::detail::test::linux_mouse_button(lvh::MouseButton::middle), BTN_MIDDLE);
  EXPECT_EQ(lvh::detail::test::linux_mouse_button(lvh::MouseButton::right), BTN_RIGHT);
  EXPECT_EQ(lvh::detail::test::linux_mouse_button(lvh::MouseButton::side), BTN_SIDE);
  EXPECT_EQ(lvh::detail::test::linux_mouse_button(lvh::MouseButton::extra), BTN_EXTRA);
  EXPECT_EQ(lvh::detail::test::linux_mouse_button(static_cast<lvh::MouseButton>(255)), BTN_LEFT);

  EXPECT_EQ(lvh::detail::test::linux_uhid_bus(lvh::BusType::unknown), BUS_USB);
  EXPECT_EQ(lvh::detail::test::linux_uhid_bus(lvh::BusType::usb), BUS_USB);
  EXPECT_EQ(lvh::detail::test::linux_uhid_bus(lvh::BusType::bluetooth), BUS_BLUETOOTH);
  EXPECT_EQ(lvh::detail::test::linux_uinput_bus(lvh::BusType::bluetooth), BUS_BLUETOOTH);
}

TEST_F(LinuxBackendTest, ScalesAbsoluteAxesAndScrollSteps) {
  EXPECT_EQ(lvh::detail::test::linux_absolute_axis(-1, 100), 0);
  EXPECT_EQ(lvh::detail::test::linux_absolute_axis(0, 100), 0);
  EXPECT_EQ(lvh::detail::test::linux_absolute_axis(50, 100), 32767);
  EXPECT_EQ(lvh::detail::test::linux_absolute_axis(100, 100), 65535);
  EXPECT_EQ(lvh::detail::test::linux_absolute_axis(101, 100), 65535);
  EXPECT_EQ(lvh::detail::test::linux_absolute_axis(1, 0), 0);

  EXPECT_EQ(lvh::detail::test::linux_legacy_scroll_steps(0), 0);
  EXPECT_EQ(lvh::detail::test::linux_legacy_scroll_steps(1), 1);
  EXPECT_EQ(lvh::detail::test::linux_legacy_scroll_steps(-1), -1);
  EXPECT_EQ(lvh::detail::test::linux_legacy_scroll_steps(119), 1);
  EXPECT_EQ(lvh::detail::test::linux_legacy_scroll_steps(120), 1);
  EXPECT_EQ(lvh::detail::test::linux_legacy_scroll_steps(240), 2);
  EXPECT_EQ(lvh::detail::test::linux_legacy_scroll_steps(-240), -2);
}

TEST_F(LinuxBackendTest, DecodesTextHelpers) {
  EXPECT_EQ(
    lvh::detail::test::linux_decode_utf8("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80"),
    (std::vector<std::uint32_t> {0x41, 0xE9, 0x20AC, 0x1F600})
  );
  EXPECT_EQ(
    lvh::detail::test::linux_decode_utf8(std::string {"A\xFF"
                                                      "B"}),
    (std::vector<std::uint32_t> {0x41, 0x42})
  );
  EXPECT_EQ(
    lvh::detail::test::linux_decode_utf8(std::string {"A\xC3"
                                                      "B"}),
    (std::vector<std::uint32_t> {0x41, 0x42})
  );
  EXPECT_TRUE(lvh::detail::test::linux_decode_utf8("\xF0\x9F").empty());
  EXPECT_EQ(lvh::detail::test::linux_uppercase_hex(0x1F600), "1F600");
  EXPECT_EQ(lvh::detail::test::linux_hex_digit_key_code('0'), 0x30);
  EXPECT_EQ(lvh::detail::test::linux_hex_digit_key_code('9'), 0x39);
  EXPECT_EQ(lvh::detail::test::linux_hex_digit_key_code('A'), 0x41);
  EXPECT_EQ(lvh::detail::test::linux_hex_digit_key_code('F'), 0x46);
}

TEST_F(LinuxBackendTest, HandlesUhidInvalidFileDescriptorPaths) {
  EXPECT_EQ(
    lvh::detail::test::linux_uhid_create_with_descriptor_size(
      lvh::detail::test::linux_uhid_descriptor_limit() + 1
    )
      .code(),
    lvh::ErrorCode::unsupported_profile
  );
  EXPECT_EQ(lvh::detail::test::linux_uhid_create_with_descriptor_size(1).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(lvh::detail::test::linux_uhid_submit_report_size(1).code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(
    lvh::detail::test::linux_uhid_submit_report_size(lvh::detail::test::linux_uhid_input_limit() + 1).code(),
    lvh::ErrorCode::invalid_argument
  );
  EXPECT_EQ(lvh::detail::test::linux_uhid_submit_after_close().code(), lvh::ErrorCode::device_closed);
}

TEST_F(LinuxBackendTest, HandlesUinputKeyboardInvalidFileDescriptorPaths) {
  EXPECT_EQ(lvh::detail::test::linux_uinput_keyboard_create_invalid_fd().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_keyboard_submit_invalid_fd({.key_code = 0, .pressed = true}).code(),
    lvh::ErrorCode::invalid_argument
  );
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_keyboard_submit_invalid_fd({.key_code = 0x41, .pressed = true}).code(),
    lvh::ErrorCode::device_closed
  );
  EXPECT_TRUE(lvh::detail::test::linux_uinput_keyboard_type_text_invalid_fd("").ok());
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_keyboard_type_text_invalid_fd("A").code(),
    lvh::ErrorCode::device_closed
  );
  EXPECT_EQ(lvh::detail::test::linux_uinput_keyboard_submit_after_close().code(), lvh::ErrorCode::device_closed);
}

TEST_F(LinuxBackendTest, PipeBackedUinputKeyboardEmitsEvents) {
  EXPECT_EQ(lvh::detail::test::linux_copy_string_char_buffer("abcdef"), "abcd");

  const auto result = lvh::detail::test::linux_uinput_keyboard_submit_pipe({.key_code = 0x41, .pressed = true});
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  ASSERT_EQ(result.events.size(), 2U);
  EXPECT_EQ(result.events[0].type, EV_KEY);
  EXPECT_EQ(result.events[0].code, KEY_A);
  EXPECT_EQ(result.events[0].value, 1);
  EXPECT_EQ(result.events[1].type, EV_SYN);
  EXPECT_EQ(result.events[1].code, SYN_REPORT);
  EXPECT_EQ(result.events[1].value, 0);

  EXPECT_EQ(lvh::detail::test::linux_uinput_user_device_invalid_fd().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(lvh::detail::test::linux_uinput_user_device_pipe().code(), lvh::ErrorCode::backend_failure);
}

TEST_F(LinuxBackendTest, HandlesUinputMouseInvalidFileDescriptorPaths) {
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_create_invalid_fd().code(), lvh::ErrorCode::backend_failure);

  lvh::MouseEvent event;
  event.kind = static_cast<lvh::MouseEventKind>(255);
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::invalid_argument);

  event.kind = lvh::MouseEventKind::relative_motion;
  event.x = 5;
  event.y = 0;
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::device_closed);

  event.x = 0;
  event.y = 5;
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::device_closed);

  event.y = 0;
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::device_closed);

  event.kind = lvh::MouseEventKind::absolute_motion;
  event.x = 50;
  event.y = 75;
  event.width = 100;
  event.height = 100;
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::device_closed);

  event.kind = lvh::MouseEventKind::button;
  event.button = lvh::MouseButton::side;
  event.pressed = true;
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::device_closed);

  event.kind = lvh::MouseEventKind::vertical_scroll;
  event.high_resolution_scroll = 120;
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::device_closed);

  event.kind = lvh::MouseEventKind::horizontal_scroll;
  event.high_resolution_scroll = -120;
  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_invalid_fd(event).code(), lvh::ErrorCode::device_closed);

  EXPECT_EQ(lvh::detail::test::linux_uinput_mouse_submit_after_close().code(), lvh::ErrorCode::device_closed);
}

TEST_F(LinuxBackendTest, PipeBackedUinputMouseEmitsEvents) {
  lvh::MouseEvent event;
  event.kind = lvh::MouseEventKind::relative_motion;
  event.x = 5;
  event.y = -2;
  auto result = lvh::detail::test::linux_uinput_mouse_submit_pipe(event);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  ASSERT_EQ(result.events.size(), 3U);
  EXPECT_EQ(result.events[0].type, EV_REL);
  EXPECT_EQ(result.events[0].code, REL_X);
  EXPECT_EQ(result.events[0].value, 5);
  EXPECT_EQ(result.events[1].type, EV_REL);
  EXPECT_EQ(result.events[1].code, REL_Y);
  EXPECT_EQ(result.events[1].value, -2);
  EXPECT_EQ(result.events[2].type, EV_SYN);

  event = {};
  event.kind = lvh::MouseEventKind::absolute_motion;
  event.x = 50;
  event.y = 100;
  event.width = 100;
  event.height = 100;
  result = lvh::detail::test::linux_uinput_mouse_submit_pipe(event);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  ASSERT_EQ(result.events.size(), 3U);
  EXPECT_EQ(result.events[0].type, EV_ABS);
  EXPECT_EQ(result.events[0].code, ABS_X);
  EXPECT_EQ(result.events[0].value, 32767);
  EXPECT_EQ(result.events[1].type, EV_ABS);
  EXPECT_EQ(result.events[1].code, ABS_Y);
  EXPECT_EQ(result.events[1].value, 65535);
  EXPECT_EQ(result.events[2].type, EV_SYN);

  event = {};
  event.kind = lvh::MouseEventKind::button;
  event.button = lvh::MouseButton::extra;
  event.pressed = true;
  result = lvh::detail::test::linux_uinput_mouse_submit_pipe(event);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  ASSERT_EQ(result.events.size(), 2U);
  EXPECT_EQ(result.events[0].type, EV_KEY);
  EXPECT_EQ(result.events[0].code, BTN_EXTRA);
  EXPECT_EQ(result.events[0].value, 1);
  EXPECT_EQ(result.events[1].type, EV_SYN);

  event = {};
  event.kind = lvh::MouseEventKind::vertical_scroll;
  event.high_resolution_scroll = 120;
  result = lvh::detail::test::linux_uinput_mouse_submit_pipe(event);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  ASSERT_EQ(result.events.size(), 2U);
  EXPECT_EQ(result.events[0].type, EV_REL);
  #if defined(REL_WHEEL_HI_RES)
  EXPECT_EQ(result.events[0].code, REL_WHEEL_HI_RES);
  EXPECT_EQ(result.events[0].value, 120);
  #else
  EXPECT_EQ(result.events[0].code, REL_WHEEL);
  EXPECT_EQ(result.events[0].value, 1);
  #endif
  EXPECT_EQ(result.events[1].type, EV_SYN);

  event = {};
  event.kind = lvh::MouseEventKind::horizontal_scroll;
  event.high_resolution_scroll = -120;
  result = lvh::detail::test::linux_uinput_mouse_submit_pipe(event);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  ASSERT_EQ(result.events.size(), 2U);
  EXPECT_EQ(result.events[0].type, EV_REL);
  #if defined(REL_HWHEEL_HI_RES)
  EXPECT_EQ(result.events[0].code, REL_HWHEEL_HI_RES);
  EXPECT_EQ(result.events[0].value, -120);
  #else
  EXPECT_EQ(result.events[0].code, REL_HWHEEL);
  EXPECT_EQ(result.events[0].value, -1);
  #endif
  EXPECT_EQ(result.events[1].type, EV_SYN);
}

TEST_F(LinuxBackendTest, SocketpairBackedUhidGamepadRoundTripsEvents) {
  const auto result = lvh::detail::test::linux_uhid_socketpair_roundtrip();
  EXPECT_TRUE(result.create_status.ok()) << result.create_status.message();
  EXPECT_TRUE(result.submit_status.ok()) << result.submit_status.message();
  EXPECT_TRUE(result.close_status.ok()) << result.close_status.message();
  EXPECT_TRUE(result.saw_create);
  EXPECT_TRUE(result.saw_input);
  EXPECT_TRUE(result.saw_get_report_reply);
  EXPECT_TRUE(result.saw_set_report_reply);
  EXPECT_TRUE(result.saw_destroy);
  EXPECT_GE(result.output_callback_count, 2U);
  EXPECT_EQ(result.last_output.kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(result.last_output.low_frequency_rumble, 0x5678);
  EXPECT_EQ(result.last_output.high_frequency_rumble, 0x1234);
}

TEST_F(LinuxBackendTest, FakeLinuxBackendCreatesAllDeviceTypes) {
  const auto unavailable = lvh::detail::test::linux_backend_fake_unavailable_capabilities();
  EXPECT_FALSE(unavailable.supports_virtual_hid);
  EXPECT_FALSE(unavailable.supports_gamepad);
  EXPECT_FALSE(unavailable.supports_output_reports);
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
  EXPECT_TRUE(unavailable.supports_keyboard);
  EXPECT_TRUE(unavailable.supports_mouse);
  EXPECT_TRUE(unavailable.supports_xtest_fallback);
  #else
  EXPECT_FALSE(unavailable.supports_keyboard);
  EXPECT_FALSE(unavailable.supports_mouse);
  EXPECT_FALSE(unavailable.supports_xtest_fallback);
  #endif

  EXPECT_EQ(lvh::detail::test::linux_backend_gamepad_fake_open_failure().code(), lvh::ErrorCode::backend_unavailable);
  EXPECT_EQ(lvh::detail::test::linux_backend_gamepad_fake_create_failure().code(), lvh::ErrorCode::backend_failure);

  const auto keyboard_open_status = lvh::detail::test::linux_backend_keyboard_fake_open_failure();
  EXPECT_TRUE(keyboard_open_status.ok() || keyboard_open_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto keyboard_create_status = lvh::detail::test::linux_backend_keyboard_fake_create_failure();
  EXPECT_TRUE(keyboard_create_status.ok() || keyboard_create_status.code() == lvh::ErrorCode::backend_failure);
  const auto keyboard_fallback_status = lvh::detail::test::linux_backend_keyboard_fake_fallback_success();
  EXPECT_TRUE(keyboard_fallback_status.ok() || keyboard_fallback_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto mouse_open_status = lvh::detail::test::linux_backend_mouse_fake_open_failure();
  EXPECT_TRUE(mouse_open_status.ok() || mouse_open_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto mouse_create_status = lvh::detail::test::linux_backend_mouse_fake_create_failure();
  EXPECT_TRUE(mouse_create_status.ok() || mouse_create_status.code() == lvh::ErrorCode::backend_failure);
  const auto mouse_fallback_status = lvh::detail::test::linux_backend_mouse_fake_fallback_success();
  EXPECT_TRUE(mouse_fallback_status.ok() || mouse_fallback_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto result = lvh::detail::test::linux_backend_create_all_fake_success();
  EXPECT_TRUE(result.capabilities.supports_virtual_hid);
  EXPECT_TRUE(result.capabilities.supports_gamepad);
  EXPECT_TRUE(result.capabilities.supports_keyboard);
  EXPECT_TRUE(result.capabilities.supports_mouse);
  EXPECT_TRUE(result.capabilities.supports_output_reports);
  EXPECT_TRUE(result.gamepad_status.ok()) << result.gamepad_status.message();
  EXPECT_TRUE(result.gamepad_close_status.ok()) << result.gamepad_close_status.message();
  EXPECT_TRUE(result.keyboard_status.ok()) << result.keyboard_status.message();
  EXPECT_TRUE(result.keyboard_close_status.ok()) << result.keyboard_close_status.message();
  EXPECT_TRUE(result.mouse_status.ok()) << result.mouse_status.message();
  EXPECT_TRUE(result.mouse_close_status.ok()) << result.mouse_close_status.message();
}

TEST_F(LinuxBackendTest, FakeUhidSyscallsCoverFailureBranches) {
  EXPECT_EQ(lvh::detail::test::linux_uhid_submit_fake_write_failure().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(lvh::detail::test::linux_uhid_submit_fake_short_write().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(lvh::detail::test::linux_uhid_close_fake_write_failure().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(lvh::detail::test::linux_uhid_close_fake_close_failure().code(), lvh::ErrorCode::backend_failure);
  EXPECT_TRUE(lvh::detail::test::linux_uhid_read_loop_fake_retry_branches().ok());
  EXPECT_TRUE(lvh::detail::test::linux_uhid_read_loop_fake_poll_errors().ok());
  EXPECT_TRUE(lvh::detail::test::linux_uhid_read_loop_fake_read_error().ok());
  EXPECT_TRUE(lvh::detail::test::linux_uhid_read_loop_fake_output_without_callback().ok());
}

TEST_F(LinuxBackendTest, FakeUinputSyscallsCoverFailureBranches) {
  for (const auto fail_call : {1, 2}) {
    EXPECT_EQ(
      lvh::detail::test::linux_uinput_keyboard_create_fake_ioctl_failure(fail_call).code(),
      lvh::ErrorCode::backend_failure
    );
  }

  for (const auto fail_call : {1, 2, 3, 4, 9, 11, 12, 13, 14}) {
    EXPECT_EQ(
      lvh::detail::test::linux_uinput_mouse_create_fake_ioctl_failure(fail_call).code(),
      lvh::ErrorCode::backend_failure
    );
  }

  EXPECT_EQ(lvh::detail::test::linux_uinput_user_device_fake_short_write().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(lvh::detail::test::linux_uinput_user_device_fake_create_failure().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_keyboard_submit_fake_write_failure().code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(lvh::detail::test::linux_uinput_keyboard_submit_fake_short_write().code(), lvh::ErrorCode::backend_failure);
  EXPECT_TRUE(lvh::detail::test::linux_uinput_keyboard_type_text_fake_success().ok());
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_keyboard_close_fake_close_failure().code(),
    lvh::ErrorCode::backend_failure
  );

  lvh::MouseEvent event;
  event.kind = lvh::MouseEventKind::relative_motion;
  event.x = 1;
  event.y = 1;
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_mouse_submit_fake_write_failure(event).code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_mouse_submit_fake_short_write(event).code(),
    lvh::ErrorCode::backend_failure
  );
}

TEST_F(LinuxBackendTest, PlatformRuntimeReportsUnavailableDeviceCreationWhenNodesAreMissing) {
  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(options);

  if (!runtime->capabilities().supports_gamepad) {
    auto gamepad = runtime->create_gamepad(lvh::profiles::xbox_360());
    EXPECT_FALSE(gamepad);
    EXPECT_EQ(gamepad.status.code(), lvh::ErrorCode::backend_unavailable);
  }

  if (!runtime->capabilities().supports_keyboard) {
    auto keyboard = runtime->create_keyboard();
    EXPECT_FALSE(keyboard);
    EXPECT_EQ(keyboard.status.code(), lvh::ErrorCode::backend_unavailable);
  }

  if (!runtime->capabilities().supports_mouse) {
    auto mouse = runtime->create_mouse();
    EXPECT_FALSE(mouse);
    EXPECT_EQ(mouse.status.code(), lvh::ErrorCode::backend_unavailable);
  }
}
#else
TEST_F(LinuxBackendTest, TranslatesKeyboardKeys) {}

TEST_F(LinuxBackendTest, TranslatesMouseButtonsAndBusTypes) {}

TEST_F(LinuxBackendTest, ScalesAbsoluteAxesAndScrollSteps) {}

TEST_F(LinuxBackendTest, DecodesTextHelpers) {}

TEST_F(LinuxBackendTest, HandlesUhidInvalidFileDescriptorPaths) {}

TEST_F(LinuxBackendTest, HandlesUinputKeyboardInvalidFileDescriptorPaths) {}

TEST_F(LinuxBackendTest, PipeBackedUinputKeyboardEmitsEvents) {}

TEST_F(LinuxBackendTest, HandlesUinputMouseInvalidFileDescriptorPaths) {}

TEST_F(LinuxBackendTest, PipeBackedUinputMouseEmitsEvents) {}

TEST_F(LinuxBackendTest, SocketpairBackedUhidGamepadRoundTripsEvents) {}

TEST_F(LinuxBackendTest, FakeLinuxBackendCreatesAllDeviceTypes) {}

TEST_F(LinuxBackendTest, FakeUhidSyscallsCoverFailureBranches) {}

TEST_F(LinuxBackendTest, FakeUinputSyscallsCoverFailureBranches) {}

TEST_F(LinuxBackendTest, PlatformRuntimeReportsUnavailableDeviceCreationWhenNodesAreMissing) {}
#endif
