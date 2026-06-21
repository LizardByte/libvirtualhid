/**
 * @file src/platform/windows/windows_backend_test_hooks.hpp
 * @brief Private Windows backend test hooks.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
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
    OperationStatus empty_nodes_create_status;
    OperationStatus oversized_submit_status;
    std::vector<DeviceNode> empty_device_nodes;
  };

  WindowsBackendLifecycleResult windows_backend_fake_channel_lifecycle();
  WindowsBackendFailureResult windows_backend_fake_channel_failures();

}  // namespace lvh::detail::test
