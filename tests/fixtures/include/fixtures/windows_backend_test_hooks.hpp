/**
 * @file tests/fixtures/include/fixtures/windows_backend_test_hooks.hpp
 * @brief Private Windows backend test hooks.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// lib includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail::test {

  struct WindowsBackendLifecycleResult {
    BackendCapabilities capabilities;
    OperationStatus create_status;
    OperationStatus submit_status;
    OperationStatus close_status;
    OperationStatus second_close_status;
    OperationStatus submit_after_close_status;
    bool saw_output = false;
    GamepadOutput last_output;
    std::vector<DeviceNode> device_nodes;
    std::size_t create_requests = 0;
    std::size_t submit_requests = 0;
    std::size_t destroy_requests = 0;
  };

  struct WindowsBackendFailureResult {
    OperationStatus invalid_argument_status;
    OperationStatus unsupported_profile_status;
    OperationStatus device_closed_status;
    OperationStatus backend_failure_status;
    OperationStatus transport_failure_status;
    OperationStatus unavailable_status;
    OperationStatus xbox_360_unsupported_status;
    OperationStatus oversized_descriptor_status;
    OperationStatus oversized_input_report_status;
    OperationStatus oversized_output_report_status;
    OperationStatus empty_nodes_create_status;
    OperationStatus oversized_submit_status;
    std::vector<DeviceNode> empty_device_nodes;
  };

  struct WindowsGenericPidOrderingResult {
    bool completed = false;
    std::vector<std::uint16_t> strengths;
  };

  struct WindowsBackendUtilityResult {
    std::vector<std::string> default_device_paths;
    std::vector<std::string> custom_device_paths;
    OperationStatus formatted_error_status;
    OperationStatus fallback_error_status;
    OperationStatus keyboard_create_status;
    OperationStatus keyboard_close_status;
    OperationStatus keyboard_submit_after_close_status;
    OperationStatus mouse_create_status;
    OperationStatus mouse_close_status;
    OperationStatus mouse_submit_after_close_status;
    bool timeout_result = true;
  };

  struct WindowsSendInputRecord {
    std::uint32_t type = 0;
    std::uint16_t virtual_key = 0;
    std::uint16_t scan_code = 0;
    std::uint32_t key_flags = 0;
    std::int32_t mouse_x = 0;
    std::int32_t mouse_y = 0;
    std::uint32_t mouse_data = 0;
    std::uint32_t mouse_flags = 0;
  };

  struct WindowsKeyboardSendInputResult {
    OperationStatus down_status;
    OperationStatus up_status;
    OperationStatus text_status;
    OperationStatus empty_text_status;
    OperationStatus invalid_text_status;
    OperationStatus failure_status;
    OperationStatus normalized_status;
    OperationStatus invalid_profile_status;
    WindowsSendInputRecord normalized_input;
  };

  struct WindowsMouseSendInputResult {
    OperationStatus relative_status;
    OperationStatus absolute_status;
    OperationStatus degenerate_absolute_status;
    OperationStatus left_button_status;
    OperationStatus middle_button_status;
    OperationStatus right_button_status;
    OperationStatus side_button_status;
    OperationStatus extra_button_status;
    OperationStatus vertical_scroll_status;
    OperationStatus horizontal_scroll_status;
    OperationStatus failure_status;
    OperationStatus invalid_profile_status;
  };

  struct WindowsUnsupportedInputResult {
    OperationStatus trackpad_status;
  };

  struct WindowsSyntheticPointerRecord {
    std::uint32_t type = 0;
    std::uint32_t count = 0;
    std::uint32_t pointer_flags = 0;
    std::uint32_t pointer_id = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t touch_mask = 0;
    std::uint32_t touch_pressure = 0;
    std::uint32_t touch_orientation = 0;
    std::uint32_t pen_mask = 0;
    std::uint32_t pen_flags = 0;
    std::int32_t pen_tilt_x = 0;
    std::int32_t pen_tilt_y = 0;
  };

  struct WindowsSyntheticPointerResult {
    BackendCapabilities capabilities;
    OperationStatus touchscreen_create_status;
    OperationStatus touchscreen_place_status;
    OperationStatus touchscreen_release_status;
    OperationStatus touchscreen_invalid_profile_status;
    OperationStatus touchscreen_failure_status;
    OperationStatus pen_tablet_create_status;
    OperationStatus pen_tablet_button_status;
    OperationStatus pen_tablet_tool_status;
    OperationStatus pen_tablet_invalid_profile_status;
    std::size_t created_devices = 0;
    std::size_t destroyed_devices = 0;
    std::vector<WindowsSyntheticPointerRecord> injected_pointers;
  };

  struct WindowsBackendSendInputResult {
    WindowsKeyboardSendInputResult keyboard;
    WindowsMouseSendInputResult mouse;
    WindowsUnsupportedInputResult unsupported;
    WindowsSyntheticPointerResult synthetic;
    std::vector<WindowsSendInputRecord> sent_inputs;
  };

  WindowsBackendLifecycleResult windows_backend_fake_channel_lifecycle();
  WindowsGenericPidOrderingResult windows_backend_generic_pid_callback_ordering();
  WindowsBackendFailureResult windows_backend_fake_channel_failures();
  WindowsBackendUtilityResult windows_backend_fake_channel_utilities();
  WindowsBackendSendInputResult windows_backend_send_input_devices();

}  // namespace lvh::detail::test
