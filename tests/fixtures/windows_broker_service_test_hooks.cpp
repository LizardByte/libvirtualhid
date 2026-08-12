/**
 * @file tests/fixtures/windows_broker_service_test_hooks.cpp
 * @brief Windows broker persistence and Polar transport test hook definitions.
 */

// local includes
#include "fixtures/windows_broker_service_test_hooks.hpp"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
// clang-format off
#include <Windows.h>
#include <AclAPI.h>
#include <dpapi.h>
#include <winhttp.h>
// clang-format on

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

  using lvh::detail::test::BrokerPersistenceFailure;
  using lvh::detail::test::BrokerPolarScenario;

  struct BrokerServiceTestState {
    BrokerPersistenceFailure persistence_failure = BrokerPersistenceFailure::dpapi;
    HANDLE persistence_file = INVALID_HANDLE_VALUE;
    BrokerPolarScenario polar_scenario = BrokerPolarScenario::success;
    std::size_t polar_body_offset = 0;
  };

  BrokerServiceTestState &broker_service_test_state() {
    static BrokerServiceTestState state;
    return state;
  }

  constexpr std::string_view structured_error_body =
    R"({"detail":[{"msg":"License activation was rejected."}]})";
  constexpr std::string_view malformed_error_body = "not-json";
  constexpr std::string_view success_body = R"({"status":"granted"})";

  std::string_view polar_body() {
    switch (broker_service_test_state().polar_scenario) {
      case BrokerPolarScenario::structured_error:
        return structured_error_body;
      case BrokerPolarScenario::malformed_error:
        return malformed_error_body;
      default:
        return success_body;
    }
  }

  BOOL WINAPI broker_test_crypt_protect_data(
    DATA_BLOB *plain_text,
    LPCWSTR description,
    DATA_BLOB *optional_entropy,
    std::byte *reserved,
    CRYPTPROTECT_PROMPTSTRUCT *prompt,
    DWORD flags,
    DATA_BLOB *cipher_text
  ) {
    if (broker_service_test_state().persistence_failure == BrokerPersistenceFailure::dpapi) {
      ::SetLastError(ERROR_ACCESS_DENIED);
      return FALSE;
    }
    return ::CryptProtectData(
      plain_text,
      description,
      optional_entropy,
      reserved,
      prompt,
      flags,
      cipher_text
    );
  }

  BOOL WINAPI broker_test_create_directory_w(
    LPCWSTR path,
    LPSECURITY_ATTRIBUTES
  ) {
    return ::CreateDirectoryW(path, nullptr);
  }

  HANDLE WINAPI broker_test_create_file_w(
    LPCWSTR filename,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES,
    DWORD creation_disposition,
    DWORD flags_and_attributes,
    HANDLE template_file
  ) {
    if (broker_service_test_state().persistence_failure == BrokerPersistenceFailure::create_file) {
      ::SetLastError(ERROR_ACCESS_DENIED);
      return INVALID_HANDLE_VALUE;
    }
    broker_service_test_state().persistence_file = ::CreateFileW(
      filename,
      desired_access,
      share_mode,
      nullptr,
      creation_disposition,
      flags_and_attributes,
      template_file
    );
    return broker_service_test_state().persistence_file;
  }

  BOOL WINAPI broker_test_write_file(
    HANDLE file,
    const std::byte *buffer,
    DWORD bytes_to_write,
    LPDWORD bytes_written,
    LPOVERLAPPED overlapped
  ) {
    if (file == broker_service_test_state().persistence_file && broker_service_test_state().persistence_failure == BrokerPersistenceFailure::partial_write) {
      const auto partial_size = bytes_to_write == 0U ? 0U : bytes_to_write - 1U;
      const auto result = ::WriteFile(
        file,
        buffer,
        partial_size,
        bytes_written,
        overlapped
      );
      ::SetLastError(ERROR_WRITE_FAULT);
      return result;
    }
    return ::WriteFile(file, buffer, bytes_to_write, bytes_written, overlapped);
  }

  BOOL WINAPI broker_test_flush_file_buffers(HANDLE file) {
    if (file == broker_service_test_state().persistence_file && broker_service_test_state().persistence_failure == BrokerPersistenceFailure::flush) {
      ::SetLastError(ERROR_WRITE_FAULT);
      return FALSE;
    }
    return ::FlushFileBuffers(file);
  }

  BOOL WINAPI broker_test_close_handle(HANDLE handle) {
    if (handle == broker_service_test_state().persistence_file) {
      broker_service_test_state().persistence_file = INVALID_HANDLE_VALUE;
      const auto closed = ::CloseHandle(handle);
      if (broker_service_test_state().persistence_failure == BrokerPersistenceFailure::close) {
        ::SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
      }
      return closed;
    }
    return ::CloseHandle(handle);
  }

  BOOL WINAPI broker_test_lookup_account_name_w(
    LPCWSTR,
    LPCWSTR,
    PSID sid,
    LPDWORD sid_size,
    LPWSTR domain,
    LPDWORD domain_size,
    PSID_NAME_USE sid_name_use
  ) {
    std::array<DWORD, SECURITY_MAX_SID_SIZE / sizeof(DWORD)> system_sid {};
    auto system_sid_size = static_cast<DWORD>(sizeof(system_sid));
    if (::CreateWellKnownSid(WinLocalSystemSid, nullptr, system_sid.data(), &system_sid_size) == FALSE) {
      return FALSE;
    }

    if (sid == nullptr || sid_size == nullptr || *sid_size < system_sid_size || domain == nullptr || domain_size == nullptr || *domain_size < 1U) {
      if (sid_size != nullptr) {
        *sid_size = system_sid_size;
      }
      if (domain_size != nullptr) {
        *domain_size = 1U;
      }
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }

    if (::CopySid(system_sid_size, sid, system_sid.data()) == FALSE) {
      return FALSE;
    }
    domain[0] = L'\0';
    *domain_size = 0U;
    *sid_name_use = SidTypeWellKnownGroup;
    return TRUE;
  }

  DWORD WINAPI broker_test_set_named_security_info_w(
    LPWSTR,
    SE_OBJECT_TYPE,
    SECURITY_INFORMATION,
    PSID,
    PSID,
    PACL,
    PACL
  ) {
    return ERROR_SUCCESS;
  }

  HINTERNET WINAPI broker_test_win_http_open(
    LPCWSTR,
    DWORD,
    LPCWSTR,
    LPCWSTR,
    DWORD
  ) {
    if (broker_service_test_state().polar_scenario == BrokerPolarScenario::open_failure) {
      ::SetLastError(ERROR_WINHTTP_CANNOT_CONNECT);
      return nullptr;
    }
    static std::byte session {};
    return &session;
  }

  HINTERNET WINAPI broker_test_win_http_connect(
    HINTERNET,
    LPCWSTR,
    INTERNET_PORT,
    DWORD
  ) {
    if (broker_service_test_state().polar_scenario == BrokerPolarScenario::connect_failure) {
      ::SetLastError(ERROR_WINHTTP_CANNOT_CONNECT);
      return nullptr;
    }
    static std::byte connection {};
    return &connection;
  }

  HINTERNET WINAPI broker_test_win_http_open_request(
    HINTERNET,
    LPCWSTR,
    LPCWSTR,
    LPCWSTR,
    LPCWSTR,
    LPCWSTR const *,
    DWORD
  ) {
    if (broker_service_test_state().polar_scenario == BrokerPolarScenario::request_failure) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return nullptr;
    }
    static std::byte request {};
    return &request;
  }

  BOOL WINAPI broker_test_win_http_send_request(
    HINTERNET,
    LPCWSTR,
    DWORD,
    std::byte *,
    DWORD,
    DWORD,
    DWORD_PTR
  ) {
    if (broker_service_test_state().polar_scenario == BrokerPolarScenario::send_failure) {
      ::SetLastError(ERROR_WINHTTP_CONNECTION_ERROR);
      return FALSE;
    }
    return TRUE;
  }

  BOOL WINAPI broker_test_win_http_receive_response(HINTERNET, std::byte *) {
    if (broker_service_test_state().polar_scenario == BrokerPolarScenario::receive_failure) {
      ::SetLastError(ERROR_WINHTTP_CONNECTION_ERROR);
      return FALSE;
    }
    return TRUE;
  }

  BOOL WINAPI broker_test_win_http_query_headers(
    HINTERNET,
    DWORD,
    LPCWSTR,
    std::byte *buffer,
    LPDWORD buffer_length,
    LPDWORD
  ) {
    if (buffer == nullptr || buffer_length == nullptr || *buffer_length < sizeof(DWORD)) {
      ::SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
    }
    const auto status =
      broker_service_test_state().polar_scenario == BrokerPolarScenario::structured_error ||
          broker_service_test_state().polar_scenario == BrokerPolarScenario::malformed_error ?
        422U :
        200U;
    std::memcpy(buffer, &status, sizeof(status));
    *buffer_length = sizeof(status);
    return TRUE;
  }

  BOOL WINAPI broker_test_win_http_query_data_available(
    HINTERNET,
    LPDWORD available
  ) {
    if (available == nullptr) {
      ::SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    *available = static_cast<DWORD>(polar_body().size() - broker_service_test_state().polar_body_offset);
    return TRUE;
  }

  BOOL WINAPI broker_test_win_http_read_data(
    HINTERNET,
    std::byte *buffer,
    DWORD bytes_to_read,
    LPDWORD bytes_read
  ) {
    const auto body = polar_body();
    const auto count = std::min<std::size_t>(
      bytes_to_read,
      body.size() - broker_service_test_state().polar_body_offset
    );
    std::memcpy(buffer, body.data() + broker_service_test_state().polar_body_offset, count);
    broker_service_test_state().polar_body_offset += count;
    *bytes_read = static_cast<DWORD>(count);
    return TRUE;
  }

  BOOL WINAPI broker_test_win_http_close_handle(HINTERNET) {
    return TRUE;
  }

}  // namespace

#define CloseHandle broker_test_close_handle
#define CreateDirectoryW broker_test_create_directory_w
#define CreateFileW broker_test_create_file_w
#define CryptProtectData(plain_text, description, optional_entropy, reserved, prompt, flags, cipher_text) \
  broker_test_crypt_protect_data(plain_text, description, optional_entropy, static_cast<std::byte *>(static_cast<void *>(reserved)), prompt, flags, cipher_text)
#define FlushFileBuffers broker_test_flush_file_buffers
#define LookupAccountNameW broker_test_lookup_account_name_w
#define SetNamedSecurityInfoW broker_test_set_named_security_info_w
#define WinHttpCloseHandle broker_test_win_http_close_handle
#define WinHttpConnect broker_test_win_http_connect
#define WinHttpOpen broker_test_win_http_open
#define WinHttpOpenRequest broker_test_win_http_open_request
#define WinHttpQueryDataAvailable broker_test_win_http_query_data_available
#define WinHttpQueryHeaders(request, info_level, name, buffer, buffer_length, index) \
  broker_test_win_http_query_headers(request, info_level, name, static_cast<std::byte *>(static_cast<void *>(buffer)), buffer_length, index)
#define WinHttpReadData(request, buffer, bytes_to_read, bytes_read) \
  broker_test_win_http_read_data(request, static_cast<std::byte *>(static_cast<void *>(buffer)), bytes_to_read, bytes_read)
#define WinHttpReceiveResponse(request, reserved) \
  broker_test_win_http_receive_response(request, static_cast<std::byte *>(static_cast<void *>(reserved)))
#define WinHttpSendRequest(request, headers, headers_length, optional, optional_length, total_length, context) \
  broker_test_win_http_send_request(request, headers, headers_length, static_cast<std::byte *>(static_cast<void *>(optional)), optional_length, total_length, context)
#define WriteFile(file, buffer, bytes_to_write, bytes_written, overlapped) \
  broker_test_write_file(file, static_cast<const std::byte *>(static_cast<const void *>(buffer)), bytes_to_write, bytes_written, overlapped)
#define main libvirtualhid_broker_test_main
#include "../../src/platform/windows/broker/libvirtualhid_broker.cpp"
#undef CloseHandle
#undef CreateDirectoryW
#undef CreateFileW
#undef CryptProtectData
#undef FlushFileBuffers
#undef LookupAccountNameW
#undef SetNamedSecurityInfoW
#undef WinHttpCloseHandle
#undef WinHttpConnect
#undef WinHttpOpen
#undef WinHttpOpenRequest
#undef WinHttpQueryDataAvailable
#undef WinHttpQueryHeaders
#undef WinHttpReadData
#undef WinHttpReceiveResponse
#undef WinHttpSendRequest
#undef WriteFile
#undef main

namespace lvh::detail::test {

  BrokerPersistenceFailureResult broker_persistence_failure(
    BrokerPersistenceFailure failure
  ) {
    broker_service_test_state().persistence_failure = failure;
    broker_service_test_state().persistence_file = INVALID_HANDLE_VALUE;

    std::array<wchar_t, MAX_PATH> temporary_root {};
    if (const auto root_size = ::GetTempPathW(static_cast<DWORD>(temporary_root.size()), temporary_root.data()); root_size == 0U || root_size >= temporary_root.size()) {
      return {.message = "Unable to resolve a temporary test directory."};
    }

    const auto directory = std::filesystem::path {temporary_root.data()} /
                           std::format("libvirtualhid-broker-test-{}", ::GetCurrentProcessId());
    const auto path = directory / "state.dat";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);

    std::string message;
    const auto saved = lvh::detail::windows_broker_service::save_protected_state(
      path,
      "test-state",
      L"libvirtualhid broker test",
      "test",
      message
    );

    std::filesystem::remove_all(directory, ignored);
    broker_service_test_state().persistence_file = INVALID_HANDLE_VALUE;
    return {.saved = saved, .message = std::move(message)};
  }

  BrokerPolarResult broker_polar_scenario(BrokerPolarScenario scenario) {
    broker_service_test_state().polar_scenario = scenario;
    broker_service_test_state().polar_body_offset = 0;
    const auto result =
      lvh::detail::windows_broker_service::post_polar_license_request(
        L"/test",
        nlohmann::json {{"key", "test-key"}}
      );
    return {
      .transport_ok = result.transport_ok,
      .http_status = result.http_status,
      .body = result.body,
      .error = result.error,
    };
  }

}  // namespace lvh::detail::test
