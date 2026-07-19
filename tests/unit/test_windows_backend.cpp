/**
 * @file tests/unit/test_windows_backend.cpp
 * @brief Unit tests for the Windows backend control-channel integration.
 */

#ifndef DOXYGEN
  #if defined(WINVER) && WINVER < 0x0A00
    #undef WINVER
  #endif
  #ifndef WINVER
    #define WINVER 0x0A00
  #endif
  #if defined(_WIN32_WINNT) && _WIN32_WINNT < 0x0A00
    #undef _WIN32_WINNT
  #endif
  #ifndef _WIN32_WINNT
    #define _WIN32_WINNT 0x0A00
  #endif
  #if defined(NTDDI_VERSION) && NTDDI_VERSION < 0x0A000006
    #undef NTDDI_VERSION
  #endif
  #ifndef NTDDI_VERSION
    #define NTDDI_VERSION 0x0A000006
  #endif
#endif

// local includes
#include "fixtures/fixtures.hpp"
#include "fixtures/windows_backend_test_hooks.hpp"

// standard includes
#include <algorithm>
#include <vector>

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
#include <Windows.h>

namespace {

  class WindowsBackendTest: public WindowsTest {};

  void expect_ok(const lvh::OperationStatus &status) {
    EXPECT_TRUE(status.ok()) << status.message();
  }

  template<typename Predicate>
  const lvh::detail::test::WindowsSyntheticPointerRecord *find_synthetic_pointer(
    const std::vector<lvh::detail::test::WindowsSyntheticPointerRecord> &records,
    Predicate predicate
  ) {
    const auto position = std::find_if(records.begin(), records.end(), predicate);
    return position == records.end() ? nullptr : &*position;
  }

}  // namespace

TEST_F(WindowsBackendTest, FakeChannelExercisesLifecycleSubmitCloseAndOutput) {
  const auto result = lvh::detail::test::windows_backend_fake_channel_lifecycle();

  EXPECT_EQ(result.capabilities.backend_name, "windows-umdf");
  EXPECT_TRUE(result.capabilities.requires_installed_driver);
  EXPECT_TRUE(result.capabilities.supports_virtual_hid);
  EXPECT_TRUE(result.capabilities.supports_gamepad);
  EXPECT_TRUE(result.capabilities.supports_keyboard);
  EXPECT_TRUE(result.capabilities.supports_mouse);
  EXPECT_TRUE(result.capabilities.supports_output_reports);

  ASSERT_TRUE(result.create_status.ok()) << result.create_status.message();
  ASSERT_TRUE(result.submit_status.ok()) << result.submit_status.message();
  ASSERT_TRUE(result.close_status.ok()) << result.close_status.message();
  EXPECT_TRUE(result.second_close_status.ok());
  EXPECT_EQ(result.submit_after_close_status.code(), lvh::ErrorCode::device_closed);

  ASSERT_EQ(result.device_nodes.size(), 1U);
  EXPECT_EQ(result.device_nodes.front().kind, lvh::DeviceNodeKind::other);
  EXPECT_EQ(result.device_nodes.front().path, R"(\\.\LibVirtualHid#100)");

  EXPECT_EQ(result.create_requests, 1U);
  EXPECT_EQ(result.submit_requests, 1U);
  EXPECT_EQ(result.destroy_requests, 1U);

  ASSERT_TRUE(result.saw_output);
  EXPECT_EQ(result.last_output.kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(result.last_output.low_frequency_rumble, 49151U);
  EXPECT_EQ(result.last_output.high_frequency_rumble, 65535U);
  ASSERT_EQ(result.last_output.raw_report.size(), 8U);
  EXPECT_EQ(result.last_output.raw_report[0], 0x03U);
}

TEST_F(WindowsBackendTest, GenericPidTimerCannotDeliverStaleStopAfterNewStart) {
  const auto result = lvh::detail::test::windows_backend_generic_pid_callback_ordering();

  ASSERT_TRUE(result.completed);
  ASSERT_GE(result.strengths.size(), 3U);
  EXPECT_GT(result.strengths[0], 0U);
  EXPECT_EQ(result.strengths[1], 0U);
  EXPECT_GT(result.strengths[2], 0U);
}

TEST_F(WindowsBackendTest, FakeChannelCoversCreateFailureBranches) {
  const auto result = lvh::detail::test::windows_backend_fake_channel_failures();

  EXPECT_EQ(result.invalid_argument_status.code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(result.unsupported_profile_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.device_closed_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_EQ(result.backend_failure_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(result.transport_failure_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(result.unavailable_status.code(), lvh::ErrorCode::backend_unavailable);
  EXPECT_EQ(result.xbox_360_unsupported_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.oversized_descriptor_status.code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(result.oversized_input_report_status.code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(result.oversized_output_report_status.code(), lvh::ErrorCode::invalid_argument);

  ASSERT_TRUE(result.empty_nodes_create_status.ok()) << result.empty_nodes_create_status.message();
  EXPECT_TRUE(result.empty_device_nodes.empty());
  EXPECT_EQ(result.oversized_submit_status.code(), lvh::ErrorCode::invalid_argument);
}

TEST_F(WindowsBackendTest, UtilityHookCoversEnvironmentErrorAndThreadBranches) {
  const auto result = lvh::detail::test::windows_backend_fake_channel_utilities();

  ASSERT_GE(result.default_device_paths.size(), 2U);
  EXPECT_EQ(result.default_device_paths[result.default_device_paths.size() - 2U], R"(\\.\LibVirtualHid)");
  EXPECT_EQ(result.default_device_paths[result.default_device_paths.size() - 1U], R"(\\.\Global\LibVirtualHid)");
  ASSERT_EQ(result.custom_device_paths.size(), 1U);
  EXPECT_EQ(result.custom_device_paths[0], R"(\\.\LibVirtualHid-Test)");
  EXPECT_EQ(result.formatted_error_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_EQ(result.fallback_error_status.code(), lvh::ErrorCode::backend_failure);
  EXPECT_NE(result.formatted_error_status.message().find("format known Windows error:"), std::string::npos);
  EXPECT_NE(
    result.fallback_error_status.message().find("format unknown Windows error: Windows error 3758096385"),
    std::string::npos
  );
  EXPECT_TRUE(result.keyboard_create_status.ok()) << result.keyboard_create_status.message();
  EXPECT_TRUE(result.keyboard_close_status.ok()) << result.keyboard_close_status.message();
  EXPECT_EQ(result.keyboard_submit_after_close_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_TRUE(result.mouse_create_status.ok()) << result.mouse_create_status.message();
  EXPECT_TRUE(result.mouse_close_status.ok()) << result.mouse_close_status.message();
  EXPECT_EQ(result.mouse_submit_after_close_status.code(), lvh::ErrorCode::device_closed);
  EXPECT_FALSE(result.timeout_result);
}

TEST_F(WindowsBackendTest, SendInputDevicesTranslateKeyboardMouseFailuresAndUnsupportedProfiles) {
  const auto result = lvh::detail::test::windows_backend_send_input_devices();

  expect_ok(result.keyboard.down_status);
  expect_ok(result.keyboard.up_status);
  expect_ok(result.keyboard.text_status);
  expect_ok(result.keyboard.empty_text_status);
  EXPECT_EQ(result.keyboard.invalid_text_status.code(), lvh::ErrorCode::invalid_argument);
  EXPECT_EQ(result.keyboard.failure_status.code(), lvh::ErrorCode::backend_failure);
  expect_ok(result.keyboard.normalized_status);
  EXPECT_EQ(result.keyboard.normalized_input.type, INPUT_KEYBOARD);
  EXPECT_EQ(result.keyboard.normalized_input.virtual_key, 0U);
  EXPECT_EQ(result.keyboard.normalized_input.scan_code, 30U);
  EXPECT_EQ(result.keyboard.normalized_input.key_flags, KEYEVENTF_SCANCODE);

  expect_ok(result.mouse.relative_status);
  expect_ok(result.mouse.absolute_status);
  expect_ok(result.mouse.degenerate_absolute_status);
  expect_ok(result.mouse.left_button_status);
  expect_ok(result.mouse.middle_button_status);
  expect_ok(result.mouse.right_button_status);
  expect_ok(result.mouse.side_button_status);
  expect_ok(result.mouse.extra_button_status);
  expect_ok(result.mouse.vertical_scroll_status);
  expect_ok(result.mouse.horizontal_scroll_status);
  EXPECT_EQ(result.mouse.failure_status.code(), lvh::ErrorCode::backend_failure);

  EXPECT_EQ(result.keyboard.invalid_profile_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.mouse.invalid_profile_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.unsupported.trackpad_status.code(), lvh::ErrorCode::unsupported_profile);

  EXPECT_TRUE(result.synthetic.capabilities.supports_touchscreen);
  EXPECT_TRUE(result.synthetic.capabilities.supports_pen_tablet);
  expect_ok(result.synthetic.touchscreen_create_status);
  expect_ok(result.synthetic.touchscreen_place_status);
  expect_ok(result.synthetic.touchscreen_release_status);
  EXPECT_EQ(result.synthetic.touchscreen_invalid_profile_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.synthetic.touchscreen_failure_status.code(), lvh::ErrorCode::backend_failure);
  expect_ok(result.synthetic.pen_tablet_create_status);
  expect_ok(result.synthetic.pen_tablet_button_status);
  expect_ok(result.synthetic.pen_tablet_tool_status);
  EXPECT_EQ(result.synthetic.pen_tablet_invalid_profile_status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(result.synthetic.created_devices, 2U);
  EXPECT_EQ(result.synthetic.destroyed_devices, 2U);
  ASSERT_GE(result.synthetic.injected_pointers.size(), 4U);
  const auto first_touch = find_synthetic_pointer(result.synthetic.injected_pointers, [](const auto &record) {
    return record.type == PT_TOUCH && record.pointer_id == 7U && (record.pointer_flags & POINTER_FLAG_DOWN) != 0U;
  });
  ASSERT_NE(first_touch, nullptr);
  EXPECT_EQ(first_touch->x, 200);
  EXPECT_EQ(first_touch->y, 350);
  EXPECT_EQ(first_touch->touch_pressure, 768U);

  const auto released_touch = find_synthetic_pointer(result.synthetic.injected_pointers, [](const auto &record) {
    return record.type == PT_TOUCH && record.pointer_id == 7U && (record.pointer_flags & POINTER_FLAG_UP) != 0U;
  });
  ASSERT_NE(released_touch, nullptr);

  const auto failed_touch = find_synthetic_pointer(result.synthetic.injected_pointers, [](const auto &record) {
    return record.type == PT_TOUCH && record.pointer_id == 8U;
  });
  ASSERT_NE(failed_touch, nullptr);

  const auto pen_tool = find_synthetic_pointer(result.synthetic.injected_pointers, [](const auto &record) {
    return record.type == PT_PEN && (record.pointer_flags & POINTER_FLAG_DOWN) != 0U;
  });
  ASSERT_NE(pen_tool, nullptr);
  EXPECT_EQ(pen_tool->x, 110);
  EXPECT_EQ(pen_tool->y, 95);
  EXPECT_EQ(pen_tool->pen_flags & PEN_FLAG_ERASER, PEN_FLAG_ERASER);
  EXPECT_EQ(pen_tool->pen_flags & PEN_FLAG_BARREL, PEN_FLAG_BARREL);

  ASSERT_EQ(result.sent_inputs.size(), 18U);
  EXPECT_EQ(result.sent_inputs[0].type, INPUT_KEYBOARD);
  EXPECT_EQ(result.sent_inputs[0].virtual_key, 0x41U);
  EXPECT_EQ(result.sent_inputs[0].key_flags, 0U);
  EXPECT_EQ(result.sent_inputs[1].virtual_key, 0x41U);
  EXPECT_EQ(result.sent_inputs[1].key_flags, KEYEVENTF_KEYUP);
  EXPECT_EQ(result.sent_inputs[2].scan_code, static_cast<std::uint16_t>(L'A'));
  EXPECT_EQ(result.sent_inputs[2].key_flags, KEYEVENTF_UNICODE);
  EXPECT_EQ(result.sent_inputs[3].scan_code, static_cast<std::uint16_t>(L'z'));
  EXPECT_EQ(result.sent_inputs[3].key_flags, KEYEVENTF_UNICODE);
  EXPECT_EQ(result.sent_inputs[4].scan_code, static_cast<std::uint16_t>(L'A'));
  EXPECT_EQ(result.sent_inputs[4].key_flags, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP);
  EXPECT_EQ(result.sent_inputs[5].scan_code, static_cast<std::uint16_t>(L'z'));
  EXPECT_EQ(result.sent_inputs[5].key_flags, KEYEVENTF_UNICODE | KEYEVENTF_KEYUP);
  EXPECT_EQ(result.sent_inputs[6].virtual_key, 0x42U);

  EXPECT_EQ(result.sent_inputs[7].type, INPUT_MOUSE);
  EXPECT_EQ(result.sent_inputs[7].mouse_flags, MOUSEEVENTF_MOVE);
  EXPECT_EQ(result.sent_inputs[7].mouse_x, 11);
  EXPECT_EQ(result.sent_inputs[7].mouse_y, -12);
  EXPECT_EQ(result.sent_inputs[8].mouse_flags, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK);
  EXPECT_EQ(result.sent_inputs[8].mouse_x, 32768);
  EXPECT_EQ(result.sent_inputs[8].mouse_y, 32768);
  EXPECT_EQ(result.sent_inputs[9].mouse_flags, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK);
  EXPECT_EQ(result.sent_inputs[9].mouse_x, 0);
  EXPECT_EQ(result.sent_inputs[9].mouse_y, 0);
  EXPECT_EQ(result.sent_inputs[10].mouse_flags, MOUSEEVENTF_LEFTDOWN);
  EXPECT_EQ(result.sent_inputs[11].mouse_flags, MOUSEEVENTF_MIDDLEUP);
  EXPECT_EQ(result.sent_inputs[12].mouse_flags, MOUSEEVENTF_RIGHTDOWN);
  EXPECT_EQ(result.sent_inputs[13].mouse_flags, MOUSEEVENTF_XDOWN);
  EXPECT_EQ(result.sent_inputs[13].mouse_data, XBUTTON1);
  EXPECT_EQ(result.sent_inputs[14].mouse_flags, MOUSEEVENTF_XUP);
  EXPECT_EQ(result.sent_inputs[14].mouse_data, XBUTTON2);
  EXPECT_EQ(result.sent_inputs[15].mouse_flags, MOUSEEVENTF_WHEEL);
  EXPECT_EQ(result.sent_inputs[15].mouse_data, 120U);
  EXPECT_EQ(result.sent_inputs[16].mouse_flags, MOUSEEVENTF_HWHEEL);
  EXPECT_EQ(result.sent_inputs[16].mouse_data, static_cast<std::uint32_t>(static_cast<DWORD>(-240)));
  EXPECT_EQ(result.sent_inputs[17].mouse_flags, MOUSEEVENTF_MOVE);
  EXPECT_EQ(result.sent_inputs[17].mouse_x, 1);
  EXPECT_EQ(result.sent_inputs[17].mouse_y, 1);
}
