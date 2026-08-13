/**
 * @file tests/fixtures/windows_broker_client_test_hooks.cpp
 * @brief Windows broker client test hook definitions.
 */

// local includes
#include "fixtures/windows_broker_client_test_hooks.hpp"

// system includes
#include <Windows.h>
#include <winsvc.h>

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>

namespace {

  constexpr auto broker_process_id = 42UL;

  struct FakeBrokerClientState {
    lvh::detail::test::BrokerServiceScenario scenario =
      lvh::detail::test::BrokerServiceScenario::success;
    DWORD last_error = ERROR_ACCESS_DENIED;
    std::uint32_t closed_pipe_handles = 0;
    std::uint32_t closed_service_handles = 0;
    std::uint32_t create_attempts = 0;
    std::uint32_t sleep_attempts = 0;
    std::uint32_t wait_attempts = 0;
    bool transacted = false;
  };

  FakeBrokerClientState &fake_state() {
    static FakeBrokerClientState state;
    return state;
  }

  HANDLE fake_pipe_handle() {
    static std::byte storage {};
    return &storage;
  }

  SC_HANDLE fake_service_manager_handle() {
    static SC_HANDLE__ storage {};
    return &storage;
  }

  SC_HANDLE fake_service_handle() {
    static SC_HANDLE__ storage {};
    return &storage;
  }

  BOOL WINAPI fake_close_handle(HANDLE) {
    ++fake_state().closed_pipe_handles;
    return TRUE;
  }

  BOOL WINAPI fake_close_service_handle(SC_HANDLE) {
    ++fake_state().closed_service_handles;
    return TRUE;
  }

  DWORD WINAPI fake_get_last_error() {
    return fake_state().last_error;
  }

  HANDLE WINAPI fake_create_file_a(
    LPCSTR,
    DWORD,
    DWORD,
    LPSECURITY_ATTRIBUTES,
    DWORD,
    DWORD,
    HANDLE
  ) {
    using enum lvh::detail::test::BrokerServiceScenario;

    ++fake_state().create_attempts;
    const auto scenario = fake_state().scenario;
    if ((scenario == pipe_unavailable_once && fake_state().create_attempts == 1U) || scenario == pipe_never_available) {
      fake_state().last_error = ERROR_FILE_NOT_FOUND;
      return INVALID_HANDLE_VALUE;
    }
    if (scenario == pipe_access_denied) {
      fake_state().last_error = ERROR_ACCESS_DENIED;
      return INVALID_HANDLE_VALUE;
    }
    if (fake_state().create_attempts == 1U && (scenario == pipe_busy_once || scenario == pipe_busy_timeout_once || scenario == pipe_busy_disappears_once || scenario == pipe_busy_failure)) {
      fake_state().last_error = ERROR_PIPE_BUSY;
      return INVALID_HANDLE_VALUE;
    }
    return fake_pipe_handle();
  }

  void WINAPI fake_sleep(DWORD) {
    ++fake_state().sleep_attempts;
    // The deterministic retry test must not wait in real time.
  }

  BOOL WINAPI fake_wait_named_pipe_a(LPCSTR, DWORD) {
    using enum lvh::detail::test::BrokerServiceScenario;

    ++fake_state().wait_attempts;
    switch (fake_state().scenario) {
      case pipe_busy_once:
        return TRUE;
      case pipe_busy_timeout_once:
        fake_state().last_error = ERROR_SEM_TIMEOUT;
        return FALSE;
      case pipe_busy_disappears_once:
        fake_state().last_error = ERROR_FILE_NOT_FOUND;
        return FALSE;
      case pipe_busy_failure:
      default:
        fake_state().last_error = ERROR_ACCESS_DENIED;
        return FALSE;
    }
  }

  BOOL WINAPI fake_get_named_pipe_server_process_id(HANDLE, PULONG process_id) {
    using enum lvh::detail::test::BrokerServiceScenario;

    if (fake_state().scenario == pipe_process_failure) {
      return FALSE;
    }

    *process_id = fake_state().scenario == zero_pipe_process ? 0UL : broker_process_id;
    return TRUE;
  }

  SC_HANDLE WINAPI fake_open_service_manager(LPCWSTR, LPCWSTR, DWORD) {
    if (fake_state().scenario == lvh::detail::test::BrokerServiceScenario::service_manager_failure) {
      return nullptr;
    }
    return fake_service_manager_handle();
  }

  SC_HANDLE WINAPI fake_open_service(SC_HANDLE, LPCWSTR, DWORD) {
    if (fake_state().scenario == lvh::detail::test::BrokerServiceScenario::service_failure) {
      return nullptr;
    }
    return fake_service_handle();
  }

  BOOL WINAPI fake_query_service_status(
    SC_HANDLE,
    SC_STATUS_TYPE,
    LPBYTE buffer,
    DWORD,
    LPDWORD bytes_needed
  ) {
    using enum lvh::detail::test::BrokerServiceScenario;

    if (fake_state().scenario == service_query_failure) {
      return FALSE;
    }

    SERVICE_STATUS_PROCESS status {};
    status.dwCurrentState = fake_state().scenario == service_stopped ? SERVICE_STOPPED : SERVICE_RUNNING;
    status.dwProcessId = fake_state().scenario == service_process_mismatch ? broker_process_id + 1UL : broker_process_id;
    *bytes_needed = sizeof(status);
    std::memcpy(buffer, &status, sizeof(status));
    return TRUE;
  }

  BOOL WINAPI fake_set_named_pipe_handle_state(HANDLE, LPDWORD, LPDWORD, LPDWORD) {
    return TRUE;
  }

  BOOL WINAPI fake_transact_named_pipe(
    HANDLE,
    std::byte *,
    DWORD,
    std::byte *response,
    DWORD response_size,
    LPDWORD bytes_read,
    LPOVERLAPPED
  ) {
    std::memset(response, 0, response_size);
    *bytes_read = response_size;
    fake_state().transacted = true;
    return TRUE;
  }

}  // namespace

#define CloseHandle fake_close_handle
#define CloseServiceHandle fake_close_service_handle
#define CreateFileA fake_create_file_a
#define GetLastError fake_get_last_error
#define GetNamedPipeServerProcessId fake_get_named_pipe_server_process_id
#define OpenSCManagerW fake_open_service_manager
#define OpenServiceW fake_open_service
#define QueryServiceStatusEx fake_query_service_status
#define SetNamedPipeHandleState fake_set_named_pipe_handle_state
#define Sleep fake_sleep
#define TransactNamedPipe fake_transact_named_pipe
#define WaitNamedPipeA fake_wait_named_pipe_a
#define call_bytes call_bytes_for_windows_broker_client_test_hooks
#define make_request_header make_request_header_for_windows_broker_client_test_hooks
#define response_status response_status_for_windows_broker_client_test_hooks
#include "../../src/platform/windows/windows_broker_client.cpp"
#undef CloseHandle
#undef CloseServiceHandle
#undef CreateFileA
#undef GetLastError
#undef GetNamedPipeServerProcessId
#undef OpenSCManagerW
#undef OpenServiceW
#undef QueryServiceStatusEx
#undef SetNamedPipeHandleState
#undef Sleep
#undef TransactNamedPipe
#undef WaitNamedPipeA
#undef call_bytes
#undef make_request_header
#undef response_status

namespace lvh::detail::test {

  BrokerServiceVerificationResult verify_broker_service_scenario(
    BrokerServiceScenario scenario
  ) {
    fake_state() = {
      .scenario = scenario,
    };

    std::array<std::byte, 1> request {};
    std::array<std::byte, 1> response {};
    auto status = windows_broker::call_bytes_for_windows_broker_client_test_hooks(
      std::span<const std::byte> {request},
      std::span<std::byte> {response},
      "Test broker request"
    );

    return {
      .status = std::move(status),
      .closed_pipe_handles = fake_state().closed_pipe_handles,
      .closed_service_handles = fake_state().closed_service_handles,
      .create_attempts = fake_state().create_attempts,
      .sleep_attempts = fake_state().sleep_attempts,
      .wait_attempts = fake_state().wait_attempts,
      .transacted = fake_state().transacted,
    };
  }

}  // namespace lvh::detail::test
