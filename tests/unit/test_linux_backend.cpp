/**
 * @file tests/unit/test_linux_backend.cpp
 * @brief Unit tests for Linux backend internals.
 */

// standard includes
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

// platform includes
#if defined(__linux__)
  #include <linux/input.h>
  #if defined(LIBVIRTUALHID_HAVE_XTEST)
    #include <X11/keysym.h>
  #endif
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

  EXPECT_EQ(lvh::detail::test::linux_pen_tool(lvh::PenToolType::pen), BTN_TOOL_PEN);
  EXPECT_EQ(lvh::detail::test::linux_pen_tool(lvh::PenToolType::eraser), BTN_TOOL_RUBBER);
  EXPECT_EQ(lvh::detail::test::linux_pen_tool(lvh::PenToolType::brush), BTN_TOOL_BRUSH);
  EXPECT_EQ(lvh::detail::test::linux_pen_tool(lvh::PenToolType::pencil), BTN_TOOL_PENCIL);
  EXPECT_EQ(lvh::detail::test::linux_pen_tool(lvh::PenToolType::airbrush), BTN_TOOL_AIRBRUSH);
  EXPECT_EQ(lvh::detail::test::linux_pen_tool(lvh::PenToolType::touch), BTN_TOUCH);
  EXPECT_EQ(lvh::detail::test::linux_pen_tool(lvh::PenToolType::unchanged), -1);
  EXPECT_EQ(lvh::detail::test::linux_pen_tool(static_cast<lvh::PenToolType>(255)), -1);
  EXPECT_EQ(lvh::detail::test::linux_pen_button(lvh::PenButton::primary), BTN_STYLUS);
  EXPECT_EQ(lvh::detail::test::linux_pen_button(lvh::PenButton::secondary), BTN_STYLUS2);
  EXPECT_EQ(lvh::detail::test::linux_pen_button(static_cast<lvh::PenButton>(255)), BTN_STYLUS);
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

TEST_F(LinuxBackendTest, CoversLinuxDiscoveryAndIdentityHelpers) {
  EXPECT_EQ(lvh::detail::test::linux_dualsense_mac_address("02:03:04:05:06:07", 0x1020304), "02:03:04:05:06:07");
  EXPECT_EQ(lvh::detail::test::linux_dualsense_mac_address("02-03-04-05-06-07", 0x1020304), "02:00:01:02:03:04");
  EXPECT_EQ(lvh::detail::test::linux_dualsense_mac_address("ff:00:100:00:00:00", 0x1020304), "02:00:01:02:03:04");

  const auto temp_dir = std::filesystem::current_path() / "cmake-build-linux-backend-test-scratch";
  std::filesystem::remove_all(temp_dir);
  std::filesystem::create_directories(temp_dir);
  const auto first_line_path = temp_dir / "libvirtualhid-linux-first-line-test.txt";
  const auto uevent_path = temp_dir / "libvirtualhid-linux-hidraw-uevent-test.txt";

  {
    std::ofstream file {first_line_path};
    file << "libvirtualhid first line\nsecond line\n";
  }
  EXPECT_TRUE(lvh::detail::test::linux_first_line_matches(first_line_path.string(), "libvirtualhid first line"));
  EXPECT_FALSE(lvh::detail::test::linux_first_line_matches(first_line_path.string(), "other"));
  EXPECT_TRUE(lvh::detail::test::linux_first_line_missing((temp_dir / "libvirtualhid-missing-first-line-test.txt").string()));

  {
    std::ofstream file {uevent_path};
    file << "HID_ID=0003:0000:0000\nHID_NAME=libvirtualhid hidraw\n";
  }
  EXPECT_TRUE(lvh::detail::test::linux_hidraw_name_matches(uevent_path.string(), "libvirtualhid hidraw"));
  EXPECT_FALSE(lvh::detail::test::linux_hidraw_name_matches(uevent_path.string(), "other"));
  EXPECT_FALSE(lvh::detail::test::linux_hidraw_name_matches((temp_dir / "libvirtualhid-missing-uevent-test.txt").string(), "other"));
  EXPECT_TRUE(lvh::detail::test::linux_discover_nodes_by_name("").empty());
  static_cast<void>(lvh::detail::test::linux_discover_nodes_by_name("libvirtualhid missing device"));
  EXPECT_EQ(lvh::detail::test::linux_empty_device_nodes_count(), 0U);

  const auto sysfs_root = temp_dir / "libvirtualhid-linux-sysfs-test";
  const auto input_root = sysfs_root / "input";
  const auto hidraw_root = sysfs_root / "hidraw";
  std::filesystem::remove_all(sysfs_root);
  std::filesystem::create_directories(input_root / "event0" / "device");
  std::filesystem::create_directories(input_root / "js0" / "device");
  std::filesystem::create_directories(input_root / "mouse0" / "device");
  std::filesystem::create_directories(input_root / "event1" / "device");
  std::filesystem::create_directories(hidraw_root / "hidraw0" / "device");
  std::filesystem::create_directories(hidraw_root / "hidraw1" / "device");
  std::filesystem::create_directories(hidraw_root / "hidraw2" / "device");
  {
    std::ofstream file {input_root / "event0" / "device" / "name"};
    file << "libvirtualhid device\n";
  }
  {
    std::ofstream file {input_root / "js0" / "device" / "name"};
    file << "libvirtualhid device\n";
  }
  {
    std::ofstream file {input_root / "mouse0" / "device" / "name"};
    file << "libvirtualhid device\n";
  }
  {
    std::ofstream file {input_root / "event1" / "device" / "name"};
    file << "other device\n";
  }
  {
    std::ofstream file {hidraw_root / "hidraw0" / "device" / "uevent"};
    file << "HID_NAME=libvirtualhid device\n";
  }
  {
    std::ofstream file {hidraw_root / "hidraw1" / "device" / "uevent"};
    file << "HID_NAME=other device\n";
  }
  {
    std::ofstream file {hidraw_root / "hidraw2" / "device" / "uevent"};
    file << "HID_ID=0003:0000:0000\n";
  }
  const auto nodes = lvh::detail::test::linux_discover_nodes_by_name(
    "libvirtualhid device",
    input_root.string(),
    hidraw_root.string()
  );
  EXPECT_TRUE(std::ranges::any_of(nodes, [](const auto &node) {
    return node.kind == lvh::DeviceNodeKind::input_event && node.path == "/dev/input/event0";
  }));
  EXPECT_TRUE(std::ranges::any_of(nodes, [](const auto &node) {
    return node.kind == lvh::DeviceNodeKind::joystick && node.path == "/dev/input/js0";
  }));
  EXPECT_TRUE(std::ranges::any_of(nodes, [](const auto &node) {
    return node.kind == lvh::DeviceNodeKind::hidraw && node.path == "/dev/hidraw0";
  }));
  EXPECT_TRUE(std::ranges::any_of(nodes, [sysfs_root](const auto &node) {
    return node.kind == lvh::DeviceNodeKind::sysfs && node.path.starts_with(sysfs_root.string());
  }));

  std::filesystem::remove_all(temp_dir);
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

TEST_F(LinuxBackendTest, PipeBackedUinputTouchDevicesEmitEvents) {
  const lvh::TouchContact contact {
    .id = 7,
    .x = 0.5F,
    .y = 0.25F,
    .pressure = 0.75F,
    .orientation = 45,
  };

  auto result = lvh::detail::test::linux_uinput_touchscreen_contact_pipe(contact);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  ASSERT_GE(result.events.size(), 10U);
  EXPECT_EQ(result.events[0].type, EV_ABS);
  EXPECT_EQ(result.events[0].code, ABS_MT_SLOT);
  EXPECT_EQ(result.events[1].type, EV_ABS);
  EXPECT_EQ(result.events[1].code, ABS_MT_TRACKING_ID);
  EXPECT_EQ(result.events[2].type, EV_KEY);
  EXPECT_EQ(result.events[2].code, BTN_TOUCH);
  EXPECT_EQ(result.events[2].value, 1);
  EXPECT_EQ(result.events[3].type, EV_ABS);
  EXPECT_EQ(result.events[3].code, ABS_X);
  EXPECT_EQ(result.events[4].type, EV_ABS);
  EXPECT_EQ(result.events[4].code, ABS_MT_POSITION_X);

  result = lvh::detail::test::linux_uinput_trackpad_contact_pipe(contact);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  const auto saw_left_button = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_LEFT && event.value == 1;
  });
  const auto saw_finger_tool = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_FINGER && event.value == 1;
  });
  EXPECT_TRUE(saw_left_button);
  EXPECT_TRUE(saw_finger_tool);

  const lvh::PenToolState tool {
    .tool = lvh::PenToolType::pen,
    .x = 0.25F,
    .y = 0.5F,
    .pressure = 0.5F,
    .distance = -1.0F,
    .tilt_x = 45.0F,
    .tilt_y = -45.0F,
  };
  result = lvh::detail::test::linux_uinput_pen_tablet_tool_pipe(tool);
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  const auto saw_pen_tool = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_PEN && event.value == 1;
  });
  const auto saw_pressure = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_ABS && event.code == ABS_PRESSURE && event.value > 0;
  });
  const auto saw_stylus = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_STYLUS && event.value == 1;
  });
  EXPECT_TRUE(saw_pen_tool);
  EXPECT_TRUE(saw_pressure);
  EXPECT_TRUE(saw_stylus);
}

TEST_F(LinuxBackendTest, PipeBackedUinputTouchDevicesCoverStateTransitions) {
  auto result = lvh::detail::test::linux_uinput_trackpad_multi_contact_pipe();
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  const auto saw_doubletap = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_DOUBLETAP && event.value == 1;
  });
  const auto saw_tripletap = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_TRIPLETAP && event.value == 1;
  });
  const auto saw_quadtap = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_QUADTAP && event.value == 1;
  });
  const auto saw_quinttap = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_QUINTTAP && event.value == 1;
  });
  EXPECT_TRUE(saw_doubletap);
  EXPECT_TRUE(saw_tripletap);
  EXPECT_TRUE(saw_quadtap);
  EXPECT_TRUE(saw_quinttap);

  EXPECT_EQ(lvh::detail::test::linux_uinput_touchscreen_invalid_contacts().code(), lvh::ErrorCode::invalid_argument);

  result = lvh::detail::test::linux_uinput_pen_tablet_transition_pipe();
  ASSERT_TRUE(result.status.ok()) << result.status.message();
  const auto saw_rubber = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_RUBBER && event.value == 1;
  });
  const auto saw_pen_release = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_TOOL_PEN && event.value == 0;
  });
  const auto saw_distance = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_ABS && event.code == ABS_DISTANCE && event.value > 0;
  });
  const auto saw_secondary_button = std::ranges::any_of(result.events, [](const auto &event) {
    return event.type == EV_KEY && event.code == BTN_STYLUS2 && event.value == 1;
  });
  EXPECT_TRUE(saw_rubber);
  EXPECT_TRUE(saw_pen_release);
  EXPECT_TRUE(saw_distance);
  EXPECT_TRUE(saw_secondary_button);
  EXPECT_EQ(lvh::detail::test::linux_uinput_pen_tablet_closed_status().code(), lvh::ErrorCode::device_closed);
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

TEST_F(LinuxBackendTest, SocketpairBackedDualSenseRepliesToFeatureReports) {
  const auto result = lvh::detail::test::linux_dualsense_uhid_socketpair_reports();
  EXPECT_TRUE(result.create_status.ok()) << result.create_status.message();
  EXPECT_TRUE(result.close_status.ok()) << result.close_status.message();
  EXPECT_TRUE(result.saw_create);
  EXPECT_TRUE(result.saw_dualsense_calibration);
  EXPECT_TRUE(result.saw_dualsense_pairing);
  EXPECT_TRUE(result.saw_dualsense_firmware);
}

TEST_F(LinuxBackendTest, SocketpairBackedDualSenseBluetoothFramesReports) {
  const auto result = lvh::detail::test::linux_dualsense_bluetooth_uhid_socketpair_reports();
  EXPECT_TRUE(result.create_status.ok()) << result.create_status.message();
  EXPECT_TRUE(result.close_status.ok()) << result.close_status.message();
  EXPECT_TRUE(result.saw_create);
  EXPECT_TRUE(result.saw_dualsense_bluetooth_input);
  EXPECT_TRUE(result.saw_dualsense_pairing);
  EXPECT_TRUE(result.saw_dualsense_feature_crc);
}

TEST_F(LinuxBackendTest, SocketpairBackedDualShock4RepliesToFeatureReports) {
  const auto result = lvh::detail::test::linux_dualshock4_uhid_socketpair_reports();
  EXPECT_TRUE(result.create_status.ok()) << result.create_status.message();
  EXPECT_TRUE(result.close_status.ok()) << result.close_status.message();
  EXPECT_TRUE(result.saw_create);
  EXPECT_TRUE(result.saw_dualshock4_calibration);
  EXPECT_TRUE(result.saw_dualshock4_pairing);
  EXPECT_TRUE(result.saw_dualshock4_firmware);
}

TEST_F(LinuxBackendTest, SocketpairBackedDualShock4BluetoothFramesReports) {
  const auto result = lvh::detail::test::linux_dualshock4_bluetooth_uhid_socketpair_reports();
  EXPECT_TRUE(result.create_status.ok()) << result.create_status.message();
  EXPECT_TRUE(result.close_status.ok()) << result.close_status.message();
  EXPECT_TRUE(result.saw_create);
  EXPECT_TRUE(result.saw_dualshock4_bluetooth_input);
  EXPECT_TRUE(result.saw_dualshock4_calibration);
  EXPECT_TRUE(result.saw_dualshock4_pairing);
  EXPECT_TRUE(result.saw_dualshock4_feature_crc);
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
  EXPECT_EQ(
    lvh::detail::test::linux_backend_keyboard_fake_create_failure_without_fallback().code(),
    lvh::ErrorCode::backend_failure
  );
  const auto keyboard_fallback_status = lvh::detail::test::linux_backend_keyboard_fake_fallback_success();
  EXPECT_TRUE(keyboard_fallback_status.ok() || keyboard_fallback_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto mouse_open_status = lvh::detail::test::linux_backend_mouse_fake_open_failure();
  EXPECT_TRUE(mouse_open_status.ok() || mouse_open_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto mouse_create_status = lvh::detail::test::linux_backend_mouse_fake_create_failure();
  EXPECT_TRUE(mouse_create_status.ok() || mouse_create_status.code() == lvh::ErrorCode::backend_failure);
  EXPECT_EQ(
    lvh::detail::test::linux_backend_mouse_fake_create_failure_without_fallback().code(),
    lvh::ErrorCode::backend_failure
  );
  const auto mouse_fallback_status = lvh::detail::test::linux_backend_mouse_fake_fallback_success();
  EXPECT_TRUE(mouse_fallback_status.ok() || mouse_fallback_status.code() == lvh::ErrorCode::backend_unavailable);

  EXPECT_EQ(lvh::detail::test::linux_backend_touchscreen_fake_open_failure().code(), lvh::ErrorCode::backend_unavailable);
  EXPECT_EQ(lvh::detail::test::linux_backend_touchscreen_fake_create_failure().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(lvh::detail::test::linux_backend_trackpad_fake_open_failure().code(), lvh::ErrorCode::backend_unavailable);
  EXPECT_EQ(lvh::detail::test::linux_backend_trackpad_fake_create_failure().code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(lvh::detail::test::linux_backend_pen_tablet_fake_open_failure().code(), lvh::ErrorCode::backend_unavailable);
  EXPECT_EQ(lvh::detail::test::linux_backend_pen_tablet_fake_create_failure().code(), lvh::ErrorCode::backend_failure);

  const auto result = lvh::detail::test::linux_backend_create_all_fake_success();
  EXPECT_TRUE(result.capabilities.supports_virtual_hid);
  EXPECT_TRUE(result.capabilities.supports_gamepad);
  EXPECT_TRUE(result.capabilities.supports_keyboard);
  EXPECT_TRUE(result.capabilities.supports_mouse);
  EXPECT_TRUE(result.capabilities.supports_touchscreen);
  EXPECT_TRUE(result.capabilities.supports_trackpad);
  EXPECT_TRUE(result.capabilities.supports_pen_tablet);
  EXPECT_TRUE(result.capabilities.supports_output_reports);
  EXPECT_TRUE(result.gamepad_status.ok()) << result.gamepad_status.message();
  EXPECT_TRUE(result.gamepad_close_status.ok()) << result.gamepad_close_status.message();
  EXPECT_TRUE(result.keyboard_status.ok()) << result.keyboard_status.message();
  EXPECT_TRUE(result.keyboard_close_status.ok()) << result.keyboard_close_status.message();
  EXPECT_TRUE(result.mouse_status.ok()) << result.mouse_status.message();
  EXPECT_TRUE(result.mouse_close_status.ok()) << result.mouse_close_status.message();
  EXPECT_TRUE(result.touchscreen_status.ok()) << result.touchscreen_status.message();
  EXPECT_TRUE(result.touchscreen_close_status.ok()) << result.touchscreen_close_status.message();
  EXPECT_TRUE(result.trackpad_status.ok()) << result.trackpad_status.message();
  EXPECT_TRUE(result.trackpad_close_status.ok()) << result.trackpad_close_status.message();
  EXPECT_TRUE(result.pen_tablet_status.ok()) << result.pen_tablet_status.message();
  EXPECT_TRUE(result.pen_tablet_close_status.ok()) << result.pen_tablet_close_status.message();
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

TEST_F(LinuxBackendTest, FakeUinputConstructionCoversCapabilitiesAndFailureBranches) {
  const auto has_type = [](const lvh::detail::test::LinuxLibevdevCreationResult &result, std::uint32_t type) {
    return std::ranges::find(result.event_types, type) != result.event_types.end();
  };
  const auto has_property = [](const lvh::detail::test::LinuxLibevdevCreationResult &result, std::uint32_t property) {
    return std::ranges::find(result.properties, property) != result.properties.end();
  };
  const auto find_code = [](const lvh::detail::test::LinuxLibevdevCreationResult &result, std::uint32_t type, std::uint32_t code) {
    const auto it = std::ranges::find_if(result.event_codes, [type, code](const auto &event_code) {
      return event_code.type == type && event_code.code == code;
    });
    return it == result.event_codes.end() ? nullptr : std::to_address(it);
  };

  const auto keyboard = lvh::detail::test::linux_uinput_create_fake_libevdev_device(lvh::DeviceType::keyboard);
  ASSERT_TRUE(keyboard.status.ok()) << keyboard.status.message();
  EXPECT_EQ(keyboard.name, lvh::profiles::keyboard().name);
  EXPECT_EQ(keyboard.bustype, BUS_USB);
  EXPECT_TRUE(has_type(keyboard, EV_KEY));
  EXPECT_NE(find_code(keyboard, EV_KEY, KEY_A), nullptr);
  EXPECT_EQ(keyboard.destroy_count, 1U);

  const auto mouse = lvh::detail::test::linux_uinput_create_fake_libevdev_device(lvh::DeviceType::mouse);
  ASSERT_TRUE(mouse.status.ok()) << mouse.status.message();
  EXPECT_TRUE(has_type(mouse, EV_KEY));
  EXPECT_TRUE(has_type(mouse, EV_REL));
  EXPECT_TRUE(has_type(mouse, EV_ABS));
  EXPECT_NE(find_code(mouse, EV_KEY, BTN_LEFT), nullptr);
  EXPECT_NE(find_code(mouse, EV_REL, REL_X), nullptr);
  const auto *mouse_x = find_code(mouse, EV_ABS, ABS_X);
  ASSERT_NE(mouse_x, nullptr);
  EXPECT_TRUE(mouse_x->has_absinfo);
  EXPECT_EQ(mouse_x->maximum, 65535);

  const auto touchscreen = lvh::detail::test::linux_uinput_create_fake_libevdev_device(lvh::DeviceType::touchscreen);
  ASSERT_TRUE(touchscreen.status.ok()) << touchscreen.status.message();
  EXPECT_TRUE(has_property(touchscreen, INPUT_PROP_DIRECT));
  EXPECT_NE(find_code(touchscreen, EV_KEY, BTN_TOUCH), nullptr);
  const auto *touch_slot = find_code(touchscreen, EV_ABS, ABS_MT_SLOT);
  ASSERT_NE(touch_slot, nullptr);
  EXPECT_TRUE(touch_slot->has_absinfo);
  EXPECT_EQ(touch_slot->maximum, 15);

  const auto trackpad = lvh::detail::test::linux_uinput_create_fake_libevdev_device(lvh::DeviceType::trackpad);
  ASSERT_TRUE(trackpad.status.ok()) << trackpad.status.message();
  EXPECT_TRUE(has_property(trackpad, INPUT_PROP_POINTER));
  EXPECT_TRUE(has_property(trackpad, INPUT_PROP_BUTTONPAD));
  EXPECT_NE(find_code(trackpad, EV_KEY, BTN_LEFT), nullptr);
  EXPECT_NE(find_code(trackpad, EV_KEY, BTN_TOOL_DOUBLETAP), nullptr);

  const auto pen_tablet = lvh::detail::test::linux_uinput_create_fake_libevdev_device(lvh::DeviceType::pen_tablet);
  ASSERT_TRUE(pen_tablet.status.ok()) << pen_tablet.status.message();
  EXPECT_TRUE(has_property(pen_tablet, INPUT_PROP_POINTER));
  EXPECT_TRUE(has_property(pen_tablet, INPUT_PROP_DIRECT));
  EXPECT_NE(find_code(pen_tablet, EV_KEY, BTN_TOOL_PEN), nullptr);
  const auto *pen_x = find_code(pen_tablet, EV_ABS, ABS_X);
  ASSERT_NE(pen_x, nullptr);
  EXPECT_TRUE(pen_x->has_absinfo);
  EXPECT_EQ(pen_x->maximum, 19200);
  EXPECT_EQ(pen_x->fuzz, 1);
  EXPECT_EQ(pen_x->resolution, 28);
  const auto *pen_pressure = find_code(pen_tablet, EV_ABS, ABS_PRESSURE);
  ASSERT_NE(pen_pressure, nullptr);
  EXPECT_EQ(pen_pressure->maximum, 4096);
  const auto *pen_tilt = find_code(pen_tablet, EV_ABS, ABS_TILT_X);
  ASSERT_NE(pen_tilt, nullptr);
  EXPECT_EQ(pen_tilt->minimum, -90);
  EXPECT_EQ(pen_tilt->maximum, 90);
  EXPECT_EQ(pen_tilt->resolution, 28);

  EXPECT_EQ(
    lvh::detail::test::linux_uinput_create_fake_libevdev_allocation_failure(lvh::DeviceType::mouse).code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_create_fake_libevdev_event_type_failure(lvh::DeviceType::keyboard).code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_create_fake_libevdev_event_code_failure(lvh::DeviceType::mouse).code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_create_fake_libevdev_property_failure(lvh::DeviceType::trackpad).code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_create_fake_libevdev_create_failure(lvh::DeviceType::pen_tablet).code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(
    lvh::detail::test::linux_uinput_create_fake_libevdev_device(lvh::DeviceType::gamepad).status.code(),
    lvh::ErrorCode::unsupported_profile
  );

  EXPECT_EQ(
    lvh::detail::test::linux_uinput_keyboard_submit_fake_write_failure().code(),
    lvh::ErrorCode::backend_failure
  );
  EXPECT_EQ(lvh::detail::test::linux_uinput_keyboard_submit_fake_short_write().code(), lvh::ErrorCode::backend_failure);
  EXPECT_TRUE(lvh::detail::test::linux_uinput_keyboard_type_text_fake_success().ok());
  for (const auto fail_call : {1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23}) {
    EXPECT_EQ(
      lvh::detail::test::linux_uinput_keyboard_type_text_fake_write_failure(fail_call).code(),
      lvh::ErrorCode::backend_failure
    );
  }
  EXPECT_TRUE(lvh::detail::test::linux_uinput_keyboard_auto_repeat_fake_success().ok());
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

TEST_F(LinuxBackendTest, XTestFallbackCoversKeyboardAndMousePaths) {
  const auto keyboard_status = lvh::detail::test::linux_xtest_keyboard_submit_success();
  EXPECT_TRUE(keyboard_status.ok() || keyboard_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto keyboard_invalid_status = lvh::detail::test::linux_xtest_keyboard_submit_invalid();
  EXPECT_TRUE(keyboard_invalid_status.code() == lvh::ErrorCode::invalid_argument || keyboard_invalid_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto keyboard_closed_status = lvh::detail::test::linux_xtest_keyboard_submit_closed();
  EXPECT_TRUE(keyboard_closed_status.code() == lvh::ErrorCode::device_closed || keyboard_closed_status.code() == lvh::ErrorCode::backend_unavailable);

  const auto keyboard_text_status = lvh::detail::test::linux_xtest_keyboard_type_text_success();
  EXPECT_TRUE(keyboard_text_status.ok() || keyboard_text_status.code() == lvh::ErrorCode::backend_unavailable);

  EXPECT_EQ(
    lvh::detail::test::linux_xtest_keyboard_create_query_failure().code(),
    lvh::ErrorCode::backend_unavailable
  );
  for (const auto fail_call : {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}) {
    const auto status = lvh::detail::test::linux_xtest_keyboard_type_text_fake_keycode_failure(fail_call);
    EXPECT_TRUE(status.code() == lvh::ErrorCode::invalid_argument || status.code() == lvh::ErrorCode::backend_unavailable);
  }

  const auto mouse_status = lvh::detail::test::linux_xtest_mouse_submit_success();
  EXPECT_TRUE(mouse_status.ok() || mouse_status.code() == lvh::ErrorCode::backend_unavailable);

  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_create_query_failure().code(), lvh::ErrorCode::backend_unavailable);

  const auto mouse_closed_status = lvh::detail::test::linux_xtest_mouse_submit_closed();
  EXPECT_TRUE(mouse_closed_status.code() == lvh::ErrorCode::device_closed || mouse_closed_status.code() == lvh::ErrorCode::backend_unavailable);

  #if defined(LIBVIRTUALHID_HAVE_XTEST)
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x08), XK_BackSpace);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x09), XK_Tab);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x0D), XK_Return);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x10), XK_Shift_L);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x11), XK_Control_L);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x12), XK_Alt_L);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x14), XK_Caps_Lock);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x1B), XK_Escape);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x20), XK_space);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x21), XK_Page_Up);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x22), XK_Page_Down);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x23), XK_End);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x24), XK_Home);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x25), XK_Left);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x26), XK_Up);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x27), XK_Right);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x28), XK_Down);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x2D), XK_Insert);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x2E), XK_Delete);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x5B), XK_Super_L);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x5C), XK_Super_R);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x90), XK_Num_Lock);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x91), XK_Scroll_Lock);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xA1), XK_Shift_R);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xA3), XK_Control_R);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xA5), XK_Alt_R);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xBA), XK_semicolon);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xBB), XK_equal);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xBC), XK_comma);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xBD), XK_minus);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xBE), XK_period);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xBF), XK_slash);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xC0), XK_grave);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xDB), XK_bracketleft);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xDC), XK_backslash);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xDD), XK_bracketright);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0xDE), XK_apostrophe);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x30), XK_0);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x39), XK_9);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x41), XK_a);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x5A), XK_z);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x60), XK_KP_0);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x69), XK_KP_9);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x6A), XK_KP_Multiply);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x6B), XK_KP_Add);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x6D), XK_KP_Subtract);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x6E), XK_KP_Decimal);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x6F), XK_KP_Divide);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x70), XK_F1);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x87), XK_F24);
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x88), 0UL);

  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_button(lvh::MouseButton::left), 1);
  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_button(lvh::MouseButton::middle), 2);
  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_button(lvh::MouseButton::right), 3);
  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_button(lvh::MouseButton::side), 8);
  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_button(lvh::MouseButton::extra), 9);
  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_button(static_cast<lvh::MouseButton>(255)), 1);
  #else
  EXPECT_EQ(lvh::detail::test::linux_xtest_keysym(0x41), 0UL);
  EXPECT_EQ(lvh::detail::test::linux_xtest_mouse_button(lvh::MouseButton::left), 1);
  #endif
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

TEST_F(LinuxBackendTest, CoversLinuxDiscoveryAndIdentityHelpers) {}

TEST_F(LinuxBackendTest, HandlesUhidInvalidFileDescriptorPaths) {}

TEST_F(LinuxBackendTest, HandlesUinputKeyboardInvalidFileDescriptorPaths) {}

TEST_F(LinuxBackendTest, PipeBackedUinputKeyboardEmitsEvents) {}

TEST_F(LinuxBackendTest, HandlesUinputMouseInvalidFileDescriptorPaths) {}

TEST_F(LinuxBackendTest, PipeBackedUinputMouseEmitsEvents) {}

TEST_F(LinuxBackendTest, PipeBackedUinputTouchDevicesEmitEvents) {}

TEST_F(LinuxBackendTest, PipeBackedUinputTouchDevicesCoverStateTransitions) {}

TEST_F(LinuxBackendTest, SocketpairBackedUhidGamepadRoundTripsEvents) {}

TEST_F(LinuxBackendTest, SocketpairBackedDualSenseRepliesToFeatureReports) {}

TEST_F(LinuxBackendTest, SocketpairBackedDualSenseBluetoothFramesReports) {}

TEST_F(LinuxBackendTest, FakeLinuxBackendCreatesAllDeviceTypes) {}

TEST_F(LinuxBackendTest, FakeUhidSyscallsCoverFailureBranches) {}

TEST_F(LinuxBackendTest, FakeUinputConstructionCoversCapabilitiesAndFailureBranches) {}

TEST_F(LinuxBackendTest, XTestFallbackCoversKeyboardAndMousePaths) {}

TEST_F(LinuxBackendTest, PlatformRuntimeReportsUnavailableDeviceCreationWhenNodesAreMissing) {}
#endif
