// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/broker/libvirtualhid_broker.cpp
 * @brief Windows service boundary for licensed libvirtualhid driver access.
 */

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
// clang-format off
#include <AclAPI.h>
#include <Windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <sddl.h>
#include <winhttp.h>
// clang-format on

// local includes
#include "broker_request_validation.hpp"
#include "lvh_windows_broker_config.hpp"
#include "lvh_windows_broker_protocol.h"
#include "lvh_windows_github_actions_evaluation.hpp"

// lib includes
#include <lizardbyte/common/env.h>
#include <nlohmann/json.hpp>

// standard includes
#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace lvh::detail::windows_broker_service {

  using UniqueHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;
  using UniqueLocalMemory = std::unique_ptr<void, decltype(&::LocalFree)>;
  using UniqueRegistryKey = std::unique_ptr<std::remove_pointer_t<HKEY>, decltype(&::RegCloseKey)>;
  using UniqueWinHttpHandle = std::unique_ptr<void, decltype(&::WinHttpCloseHandle)>;

  constexpr auto service_name = L"libvirtualhid_broker";
  constexpr auto service_account_name = L"NT SERVICE\\libvirtualhid_broker";
  constexpr auto broker_instance_name = "libvirtualhid Windows broker";
  constexpr auto pipe_buffer_size = 8192U;
  constexpr auto pipe_connect_timeout = 1000U;
  constexpr auto pipe_io_timeout = 5000U;
  constexpr int polar_resolve_timeout = 5000;
  constexpr int polar_connect_timeout = 5000;
  constexpr int polar_send_timeout = 5000;
  constexpr int polar_receive_timeout = 10000;
  constexpr auto license_validation_interval = std::chrono::days {1};
  constexpr auto license_validation_retry_interval = std::chrono::seconds {60};
  constexpr auto license_outage_device_retention = std::chrono::hours {1};
  constexpr auto subscription_validation_max_age =
    license_validation_interval + license_outage_device_retention;
  constexpr std::size_t unvalidated_active_device_limit = 1U;
  constexpr auto boot_session_registry_path =
    L"SYSTEM\\CurrentControlSet\\Services\\libvirtualhid_broker\\Runtime";
  constexpr auto boot_session_registry_value = L"BootMarker";
  // Message-mode clients need the complete GENERIC_READ mapping plus individual write rights.
  constexpr auto pipe_client_granted_access = FILE_GENERIC_READ | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;
  constexpr auto pipe_security_descriptor = L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x0012018B;;;AU)";

  static_assert(pipe_client_granted_access == 0x0012018BU);
  static_assert((pipe_client_granted_access & FILE_CREATE_PIPE_INSTANCE) == 0U);

  static_assert(
    !lvh::windows::broker_config::polar_organization_id.empty() &&
      !lvh::windows::broker_config::allowed_benefits.empty(),
    "The Windows broker requires a Polar organization and at least one allowed benefit."
  );

  struct ServiceRuntime {
    SERVICE_STATUS_HANDLE status_handle = nullptr;
    SERVICE_STATUS status {
      .dwServiceType = SERVICE_WIN32_OWN_PROCESS,
      .dwCurrentState = SERVICE_STOPPED,
      .dwControlsAccepted = 0,
      .dwWin32ExitCode = NO_ERROR,
      .dwServiceSpecificExitCode = 0,
      .dwCheckPoint = 0,
      .dwWaitHint = 0,
    };
    HANDLE stop_event = nullptr;
  };

  ServiceRuntime &service_runtime() {
    static ServiceRuntime runtime;
    return runtime;
  }

  UniqueHandle make_unique_handle(HANDLE handle) {
    if (handle == INVALID_HANDLE_VALUE) {
      handle = nullptr;
    }
    return {handle, &::CloseHandle};
  }

  UniqueRegistryKey make_unique_registry_key(HKEY key) {
    return {key, &::RegCloseKey};
  }

  UniqueWinHttpHandle make_unique_winhttp_handle(HINTERNET handle) {
    return {handle, &::WinHttpCloseHandle};
  }

  using LicenseValidationClock = std::chrono::steady_clock;
  // Calendar time is anchored only by Polar's HTTPS Date header. Windows
  // uptime advances that anchor within one boot session, including across
  // broker restarts, without consulting the user-adjustable calendar clock.
  using LicenseCalendarClock = std::chrono::system_clock;

  using BootMarker = std::array<std::byte, 16>;

  std::optional<BootMarker> generate_boot_marker() {
    BootMarker marker {};
    if (::BCryptGenRandom(nullptr, std::bit_cast<PUCHAR>(marker.data()), static_cast<ULONG>(marker.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      return std::nullopt;
    }
    return marker;
  }

  std::string encode_boot_marker(const BootMarker &marker) {
    constexpr std::string_view hex = "0123456789abcdef";
    std::string encoded(marker.size() * 2U, '0');
    for (std::size_t index = 0; index < marker.size(); ++index) {
      const auto value = std::to_integer<unsigned>(marker[index]);
      encoded[index * 2U] = hex[value >> 4U];
      encoded[index * 2U + 1U] = hex[value & 0x0FU];
    }
    return encoded;
  }

  std::string load_or_create_boot_session_marker() {
    const auto generated = generate_boot_marker();
    if (!generated) {
      return {};
    }

    HKEY raw_key = nullptr;
    DWORD disposition = 0;
    if (::RegCreateKeyExW(HKEY_LOCAL_MACHINE, boot_session_registry_path, 0, nullptr, REG_OPTION_VOLATILE, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &raw_key, &disposition) != ERROR_SUCCESS) {
      // This process can still use the marker. A later process receives a new
      // marker and therefore fails yearly expiration checks closed.
      return encode_boot_marker(*generated);
    }
    auto key = make_unique_registry_key(raw_key);

    BootMarker stored {};
    DWORD type = 0;
    if (auto size = static_cast<DWORD>(stored.size()); disposition == REG_OPENED_EXISTING_KEY && ::RegQueryValueExW(key.get(), boot_session_registry_value, nullptr, &type, std::bit_cast<LPBYTE>(stored.data()), &size) == ERROR_SUCCESS && type == REG_BINARY && size == stored.size()) {
      return encode_boot_marker(stored);
    }

    static_cast<void>(::RegSetValueExW(key.get(), boot_session_registry_value, 0, REG_BINARY, std::bit_cast<const BYTE *>(generated->data()), static_cast<DWORD>(generated->size())));
    return encode_boot_marker(*generated);
  }

  std::optional<LicenseCalendarClock::time_point> license_server_time(
    const SYSTEMTIME &system_time
  ) {
    FILETIME file_time {};
    if (::SystemTimeToFileTime(&system_time, &file_time) == FALSE) {
      return std::nullopt;
    }
    const auto ticks = std::bit_cast<std::uint64_t>(file_time);
    constexpr std::uint64_t windows_to_unix_epoch_ticks = 116444736000000000ULL;
    constexpr std::uint64_t ticks_per_second = 10000000ULL;
    if (ticks < windows_to_unix_epoch_ticks) {
      return std::nullopt;
    }
    return LicenseCalendarClock::time_point {
      std::chrono::seconds {
        static_cast<std::int64_t>(
          (ticks - windows_to_unix_epoch_ticks) / ticks_per_second
        )
      }
    };
  }

  std::uint64_t license_calendar_timestamp(LicenseCalendarClock::time_point time) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           time.time_since_epoch()
    )
                           .count();
    return seconds > 0 ? static_cast<std::uint64_t>(seconds) : 0U;
  }

  std::optional<std::uint64_t> license_monotonic_calendar_timestamp(
    std::uint64_t calendar_anchor,
    std::uint64_t uptime_anchor,
    std::uint64_t current_uptime,
    bool same_boot_session
  ) {
    if (!same_boot_session || calendar_anchor == 0U || current_uptime < uptime_anchor) {
      return std::nullopt;
    }
    const auto elapsed_unsigned = (current_uptime - uptime_anchor) / 1000U;
    return calendar_anchor > std::numeric_limits<std::uint64_t>::max() - elapsed_unsigned ?
             std::optional<std::uint64_t> {std::numeric_limits<std::uint64_t>::max()} :
             std::optional<std::uint64_t> {calendar_anchor + elapsed_unsigned};
  }

  bool license_validation_is_current(
    std::uint64_t validated_at,
    std::uint64_t effective_timestamp
  ) {
    const auto maximum_age = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(
        subscription_validation_max_age
      )
        .count()
    );
    return validated_at != 0U && effective_timestamp >= validated_at &&
           effective_timestamp - validated_at < maximum_age;
  }

  bool unvalidated_device_creation_allowed(
    std::size_t active_licensed_devices
  ) {
    return active_licensed_devices < unvalidated_active_device_limit;
  }

  bool license_outage_retention_elapsed(
    LicenseValidationClock::duration elapsed
  ) {
    return elapsed >= license_outage_device_retention;
  }

  bool license_outage_device_should_be_revoked(
    bool github_actions_evaluation,
    bool limit_licensed_devices,
    std::size_t licensed_devices_kept
  ) {
    return limit_licensed_devices &&
           !github_actions_evaluation &&
           licensed_devices_kept >= unvalidated_active_device_limit;
  }

  bool broker_device_should_be_revoked(
    bool github_actions_evaluation,
    bool evaluation_expired,
    bool revoke_licensed_devices
  ) {
    return (evaluation_expired && github_actions_evaluation) ||
           (revoke_licensed_devices && !github_actions_evaluation);
  }

  bool complete_pending_pipe_io(
    HANDLE pipe,
    OVERLAPPED &overlapped,
    HANDLE requested_stop_event,
    DWORD timeout,
    DWORD &bytes_transferred
  ) {
    std::array<HANDLE, 2> wait_handles {
      overlapped.hEvent,
      requested_stop_event,
    };
    const auto handle_count = requested_stop_event == nullptr ? 1U : 2U;
    if (const auto wait_result = ::WaitForMultipleObjects(handle_count, wait_handles.data(), FALSE, timeout); wait_result == WAIT_OBJECT_0) {
      return ::GetOverlappedResult(
               pipe,
               &overlapped,
               &bytes_transferred,
               FALSE
             ) != FALSE;
    }

    static_cast<void>(::CancelIoEx(pipe, &overlapped));
    DWORD ignored = 0;
    static_cast<void>(::GetOverlappedResult(pipe, &overlapped, &ignored, TRUE));
    return false;
  }

  bool read_pipe_message(
    HANDLE pipe,
    std::span<std::byte> buffer,
    HANDLE requested_stop_event,
    DWORD &bytes_read
  ) {
    auto operation_event = make_unique_handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!operation_event) {
      return false;
    }

    OVERLAPPED overlapped {};
    overlapped.hEvent = operation_event.get();
    bytes_read = 0;
    if (::ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, &overlapped) != FALSE) {
      return true;
    }
    if (::GetLastError() != ERROR_IO_PENDING) {
      return false;
    }

    return complete_pending_pipe_io(
      pipe,
      overlapped,
      requested_stop_event,
      pipe_io_timeout,
      bytes_read
    );
  }

  bool write_pipe_message(
    HANDLE pipe,
    std::span<const std::byte> buffer,
    HANDLE requested_stop_event
  ) {
    auto operation_event = make_unique_handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!operation_event) {
      return false;
    }

    OVERLAPPED overlapped {};
    overlapped.hEvent = operation_event.get();
    const auto buffer_size = static_cast<DWORD>(buffer.size());
    DWORD bytes_written = 0;
    if (::WriteFile(pipe, buffer.data(), buffer_size, &bytes_written, &overlapped) != FALSE) {
      return bytes_written == buffer_size;
    }
    if (::GetLastError() != ERROR_IO_PENDING) {
      return false;
    }

    return complete_pending_pipe_io(
             pipe,
             overlapped,
             requested_stop_event,
             pipe_io_timeout,
             bytes_written
           ) &&
           bytes_written == buffer_size;
  }

  template<std::size_t Size>
  void copy_c_string(std::array<char, Size> &target, std::string_view value) {
    std::ranges::fill(target, '\0');
    const auto count = std::min(value.size(), Size - 1U);
    std::memcpy(target.data(), value.data(), count);
  }

  std::string windows_error_message(DWORD error_code) {
    std::array<char, 1024> message_buffer {};
    const auto message_size = ::FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      error_code,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      message_buffer.data(),
      static_cast<DWORD>(message_buffer.size()),
      nullptr
    );

    if (message_size == 0U) {
      std::ostringstream fallback;
      fallback << "Windows error " << error_code;
      return fallback.str();
    }

    std::string message {message_buffer.data(), message_size};
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
      message.pop_back();
    }
    return message;
  }

  std::optional<nlohmann::json> parse_json(std::string_view body) {
    auto parsed = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
    if (parsed.is_discarded()) {
      return std::nullopt;
    }
    return parsed;
  }

  std::string json_string_or_empty(const nlohmann::json &object, std::string_view key) {
    if (!object.is_object()) {
      return {};
    }
    const auto iter = object.find(std::string {key});
    if (iter == object.end() || !iter->is_string()) {
      return {};
    }
    return iter->get<std::string>();
  }

  std::string polar_detail_message(const nlohmann::json &detail) {
    if (detail.is_string()) {
      return detail.get<std::string>();
    }
    if (!detail.is_array()) {
      return {};
    }

    const auto entry = std::ranges::find_if(detail, [](const auto &candidate) {
      return !json_string_or_empty(candidate, "msg").empty();
    });
    return entry == detail.end() ? std::string {} : json_string_or_empty(*entry, "msg");
  }

  std::string polar_error_message(const nlohmann::json &body, std::string_view fallback) {
    if (const auto detail = body.find("detail"); detail != body.end()) {
      if (auto message = polar_detail_message(*detail); !message.empty()) {
        return message;
      }
    }

    const auto error = json_string_or_empty(body, "error");
    return error.empty() ? std::string {fallback} : error;
  }

  std::filesystem::path broker_state_path(std::string_view filename) {
    std::string program_data;
    if (!lizardbyte::common::get_env("ProgramData", program_data) || program_data.empty()) {
      program_data = R"(C:\ProgramData)";
    }

    auto root = std::filesystem::path {program_data};
    return root / "libvirtualhid" / filename;
  }

  std::filesystem::path license_state_path() {
    return broker_state_path("license.dat");
  }

  std::filesystem::path github_actions_evaluation_state_path() {
    return broker_state_path("github-actions-evaluation.dat");
  }

  std::optional<std::vector<std::byte>> lookup_account_sid(const wchar_t *account_name) {
    DWORD sid_size = 0;
    DWORD domain_size = 0;
    SID_NAME_USE sid_name_use {};
    static_cast<void>(::LookupAccountNameW(
      nullptr,
      account_name,
      nullptr,
      &sid_size,
      nullptr,
      &domain_size,
      &sid_name_use
    ));
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || sid_size == 0U) {
      return std::nullopt;
    }

    auto sid = std::vector<std::byte>(sid_size);
    if (auto domain = std::wstring(domain_size, L'\0'); ::LookupAccountNameW(nullptr, account_name, sid.data(), &sid_size, domain.data(), &domain_size, &sid_name_use) == FALSE) {
      return std::nullopt;
    }

    sid.resize(sid_size);
    return sid;
  }

  std::optional<UniqueLocalMemory> make_state_security_descriptor(
    std::string_view state_name,
    std::string &message
  ) {
    auto service_sid = lookup_account_sid(service_account_name);
    if (!service_sid || ::IsValidSid(service_sid->data()) == FALSE) {
      message = "Unable to resolve the broker service identity for " +
                std::string {state_name} + " state.";
      return std::nullopt;
    }

    LPWSTR raw_service_sid = nullptr;
    if (::ConvertSidToStringSidW(service_sid->data(), &raw_service_sid) == FALSE) {
      message = "Unable to format the broker service identity for " +
                std::string {state_name} + " state: " +
                windows_error_message(::GetLastError());
      return std::nullopt;
    }
    auto service_sid_text = UniqueLocalMemory {raw_service_sid, &::LocalFree};

    const auto sddl = std::format(
      L"O:SYD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;{})",
      static_cast<const wchar_t *>(service_sid_text.get())
    );
    PSECURITY_DESCRIPTOR raw_descriptor = nullptr;
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &raw_descriptor, nullptr) == FALSE) {
      message = "Unable to create the access policy for " +
                std::string {state_name} + " state: " +
                windows_error_message(::GetLastError());
      return std::nullopt;
    }

    return UniqueLocalMemory {raw_descriptor, &::LocalFree};
  }

  PACL security_descriptor_dacl(PSECURITY_DESCRIPTOR descriptor) {
    BOOL dacl_present = FALSE;
    BOOL dacl_defaulted = FALSE;
    PACL dacl = nullptr;
    if (::GetSecurityDescriptorDacl(descriptor, &dacl_present, &dacl, &dacl_defaulted) == FALSE || dacl_present == FALSE || dacl == nullptr) {
      return nullptr;
    }
    return dacl;
  }

  DWORD set_state_path_security(
    const std::filesystem::path &path,
    PSID owner,
    PACL dacl
  ) {
    auto mutable_path = path.native();
    return ::SetNamedSecurityInfoW(
      mutable_path.data(),
      SE_FILE_OBJECT,
      OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
        PROTECTED_DACL_SECURITY_INFORMATION,
      owner,
      nullptr,
      dacl,
      nullptr
    );
  }

  bool apply_state_path_acl(
    const std::filesystem::path &path,
    PSECURITY_DESCRIPTOR descriptor,
    std::string_view state_name,
    std::string &message
  ) {
    const auto dacl = security_descriptor_dacl(descriptor);
    if (dacl == nullptr) {
      message = "Unable to read the access policy for " +
                std::string {state_name} + " state.";
      return false;
    }

    BOOL owner_defaulted = FALSE;
    PSID owner = nullptr;
    if (::GetSecurityDescriptorOwner(descriptor, &owner, &owner_defaulted) == FALSE || owner == nullptr || ::IsValidSid(owner) == FALSE) {
      message = "Unable to read the owner policy for " +
                std::string {state_name} + " state.";
      return false;
    }

    if (const auto result = set_state_path_security(path, owner, dacl); result != ERROR_SUCCESS) {
      message = "Unable to restrict " + std::string {state_name} +
                " state access: " + windows_error_message(result);
      return false;
    }
    return true;
  }

  bool ensure_secure_state_directory(
    const std::filesystem::path &directory,
    PSECURITY_DESCRIPTOR descriptor,
    std::string_view state_name,
    std::string &message
  ) {
    if (SECURITY_ATTRIBUTES security_attributes {
          .nLength = sizeof(SECURITY_ATTRIBUTES),
          .lpSecurityDescriptor = descriptor,
          .bInheritHandle = FALSE,
        };
        ::CreateDirectoryW(directory.c_str(), &security_attributes) == FALSE && ::GetLastError() != ERROR_ALREADY_EXISTS) {
      message = "Unable to create " + std::string {state_name} +
                " state directory: " + windows_error_message(::GetLastError());
      return false;
    }

    if (const auto attributes = ::GetFileAttributesW(directory.c_str()); attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0U || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U) {
      message = "The " + std::string {state_name} +
                " state directory is missing or unsafe.";
      return false;
    }

    return apply_state_path_acl(
      directory,
      descriptor,
      state_name,
      message
    );
  }

  bool secure_existing_state_file(
    const std::filesystem::path &path,
    PSECURITY_DESCRIPTOR descriptor,
    std::string_view state_name,
    std::string &message
  ) {
    const auto attributes = ::GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
      return ::GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
      message = "The " + std::string {state_name} + " state file is unsafe.";
      return false;
    }

    return apply_state_path_acl(path, descriptor, state_name, message);
  }

  struct PolarLicenseState {
    std::string provider;
    std::string license_key;
    std::string activation_id;
    std::string license_key_id;
    std::string license_status;
    std::string organization_id;
    std::string benefit_id;
    std::string customer_email;
    std::uint32_t activation_limit = 0;
    // Audit timestamp supplied by Polar, not the local machine clock.
    std::uint64_t validated_at = 0;
    // The boot-session marker and uptime at validation let the broker advance
    // Polar time across service restarts without using the Windows wall clock.
    std::string boot_marker;
    std::uint64_t validated_uptime_ms = 0;
  };

  std::string serialize_license_state(const PolarLicenseState &state) {
    std::ostringstream serialized;
    serialized << "provider=" << state.provider << "\n";
    serialized << "license_key=" << state.license_key << "\n";
    serialized << "activation_id=" << state.activation_id << "\n";
    serialized << "license_key_id=" << state.license_key_id << "\n";
    serialized << "license_status=" << state.license_status << "\n";
    serialized << "organization_id=" << state.organization_id << "\n";
    serialized << "benefit_id=" << state.benefit_id << "\n";
    serialized << "customer_email=" << state.customer_email << "\n";
    serialized << "activation_limit=" << state.activation_limit << "\n";
    serialized << "validated_at=" << state.validated_at << "\n";
    serialized << "boot_marker=" << state.boot_marker << "\n";
    serialized << "validated_uptime_ms=" << state.validated_uptime_ms << "\n";
    return serialized.str();
  }

  std::optional<std::uint64_t> parse_uint64(std::string_view value) {
    std::uint64_t parsed = 0;
    if (const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed); result.ec != std::errc {} || result.ptr != value.data() + value.size()) {
      return std::nullopt;
    }
    return parsed;
  }

  std::optional<std::string> load_protected_state(
    const std::filesystem::path &path,
    std::string_view state_name
  ) {
    std::string security_message;
    if (auto security_descriptor = make_state_security_descriptor(state_name, security_message); !security_descriptor || !ensure_secure_state_directory(path.parent_path(), security_descriptor->get(), state_name, security_message) || !secure_existing_state_file(path, security_descriptor->get(), state_name, security_message)) {
      return std::nullopt;
    }

    std::ifstream input {path, std::ios::binary};
    if (!input) {
      return std::nullopt;
    }

    std::vector<std::uint8_t> encrypted {
      std::istreambuf_iterator<char> {input},
      std::istreambuf_iterator<char> {}
    };
    if (encrypted.empty()) {
      return std::nullopt;
    }

    DATA_BLOB encrypted_blob {
      .cbData = static_cast<DWORD>(encrypted.size()),
      .pbData = encrypted.data(),
    };
    DATA_BLOB plain_blob {};
    if (::CryptUnprotectData(&encrypted_blob, nullptr, nullptr, nullptr, nullptr, 0, &plain_blob) == FALSE) {
      return std::nullopt;
    }

    std::string serialized {
      reinterpret_cast<const char *>(plain_blob.pbData),
      plain_blob.cbData
    };
    static_cast<void>(::LocalFree(plain_blob.pbData));
    return serialized;
  }

  bool save_protected_state(
    const std::filesystem::path &path,
    std::string serialized,
    const wchar_t *description,
    std::string_view state_name,
    std::string &message
  ) {
    DATA_BLOB plain_blob {
      .cbData = static_cast<DWORD>(serialized.size()),
      .pbData = static_cast<BYTE *>(static_cast<void *>(serialized.data())),
    };
    DATA_BLOB encrypted_blob {};
    if (::CryptProtectData(&plain_blob, description, nullptr, nullptr, nullptr, CRYPTPROTECT_LOCAL_MACHINE, &encrypted_blob) == FALSE) {
      message = "Unable to protect " + std::string {state_name} + " state: " + windows_error_message(::GetLastError());
      return false;
    }

    auto encrypted_data = UniqueLocalMemory {encrypted_blob.pbData, &::LocalFree};
    auto security_descriptor = make_state_security_descriptor(state_name, message);
    if (!security_descriptor || !ensure_secure_state_directory(path.parent_path(), security_descriptor->get(), state_name, message) || !secure_existing_state_file(path, security_descriptor->get(), state_name, message)) {
      return false;
    }

    SECURITY_ATTRIBUTES security_attributes {
      .nLength = sizeof(SECURITY_ATTRIBUTES),
      .lpSecurityDescriptor = security_descriptor->get(),
      .bInheritHandle = FALSE,
    };
    const auto output = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, &security_attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
      message = "Unable to write " + std::string {state_name} +
                " state: " + windows_error_message(::GetLastError());
      return false;
    }

    DWORD bytes_written = 0;
    const auto persisted = ::WriteFile(output, encrypted_data.get(), encrypted_blob.cbData, &bytes_written, nullptr) != FALSE &&
                           bytes_written == encrypted_blob.cbData &&
                           ::FlushFileBuffers(output) != FALSE;
    const auto persist_error = persisted ? ERROR_SUCCESS : ::GetLastError();
    const auto closed = ::CloseHandle(output) != FALSE;
    const auto close_error = closed ? ERROR_SUCCESS : ::GetLastError();

    if (!persisted) {
      message = "Unable to persist " + std::string {state_name} +
                " state: " + windows_error_message(persist_error);
      return false;
    }

    if (!closed) {
      message = "Unable to close " + std::string {state_name} +
                " state: " + windows_error_message(close_error);
      return false;
    }

    return apply_state_path_acl(path, security_descriptor->get(), state_name, message);
  }

  PolarLicenseState deserialize_license_state(std::string_view serialized) {
    PolarLicenseState state;
    std::size_t offset = 0;
    while (offset < serialized.size()) {
      const auto line_end = serialized.find('\n', offset);
      const auto end = line_end == std::string_view::npos ? serialized.size() : line_end;
      const auto line = serialized.substr(offset, end - offset);
      if (const auto separator = line.find('='); separator != std::string_view::npos) {
        const auto key = line.substr(0, separator);
        const auto value = line.substr(separator + 1U);
        if (key == "provider") {
          state.provider = value;
        } else if (key == "license_key") {
          state.license_key = value;
        } else if (key == "activation_id") {
          state.activation_id = value;
        } else if (key == "license_key_id") {
          state.license_key_id = value;
        } else if (key == "license_status") {
          state.license_status = value;
        } else if (key == "organization_id") {
          state.organization_id = value;
        } else if (key == "benefit_id") {
          state.benefit_id = value;
        } else if (key == "customer_email") {
          state.customer_email = value;
        } else if (key == "activation_limit") {
          state.activation_limit = static_cast<std::uint32_t>(parse_uint64(value).value_or(0));
        } else if (key == "validated_at") {
          state.validated_at = parse_uint64(value).value_or(0);
        } else if (key == "boot_marker") {
          state.boot_marker = value;
        } else if (key == "validated_uptime_ms") {
          state.validated_uptime_ms = parse_uint64(value).value_or(0);
        }
      }
      if (line_end == std::string_view::npos) {
        break;
      }
      offset = line_end + 1U;
    }
    return state;
  }

  std::optional<PolarLicenseState> load_license_state() {
    const auto serialized = load_protected_state(license_state_path(), "license");
    if (!serialized) {
      return std::nullopt;
    }

    auto state = deserialize_license_state(*serialized);
    if (state.provider != "polar" || state.license_key.empty() || state.activation_id.empty()) {
      return std::nullopt;
    }
    return state;
  }

  bool save_license_state(const PolarLicenseState &state, std::string &message) {
    return save_protected_state(
      license_state_path(),
      serialize_license_state(state),
      L"libvirtualhid broker license",
      "license",
      message
    );
  }

  struct GitHubActionsEvaluationState {
    lvh::windows::github_actions_evaluation::Clock::time_point started_at;
  };

  std::string serialize_github_actions_evaluation_state(
    const GitHubActionsEvaluationState &state
  ) {
    const auto started_at = std::chrono::duration_cast<std::chrono::seconds>(
      state.started_at.time_since_epoch()
    );
    return std::format("started_at={}\n", started_at.count());
  }

  std::optional<GitHubActionsEvaluationState> deserialize_github_actions_evaluation_state(
    std::string_view serialized
  ) {
    constexpr std::string_view prefix = "started_at=";
    if (!serialized.starts_with(prefix)) {
      return std::nullopt;
    }

    const auto line_end = serialized.find('\n');
    const auto value = serialized.substr(
      prefix.size(),
      line_end == std::string_view::npos ? std::string_view::npos : line_end - prefix.size()
    );
    const auto started_at = parse_uint64(value);
    if (!started_at || *started_at > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return std::nullopt;
    }

    return GitHubActionsEvaluationState {
      .started_at = lvh::windows::github_actions_evaluation::Clock::time_point {
        std::chrono::seconds {static_cast<std::int64_t>(*started_at)}
      },
    };
  }

  std::optional<GitHubActionsEvaluationState> load_github_actions_evaluation_state() {
    const auto serialized = load_protected_state(
      github_actions_evaluation_state_path(),
      "GitHub Actions evaluation"
    );
    if (!serialized) {
      return std::nullopt;
    }
    return deserialize_github_actions_evaluation_state(*serialized);
  }

  bool save_github_actions_evaluation_state(
    const GitHubActionsEvaluationState &state,
    std::string &message
  ) {
    return save_protected_state(
      github_actions_evaluation_state_path(),
      serialize_github_actions_evaluation_state(state),
      L"libvirtualhid GitHub Actions evaluation",
      "GitHub Actions evaluation",
      message
    );
  }

  void delete_license_state() {
    std::error_code ignored;
    std::filesystem::remove(license_state_path(), ignored);
  }

  std::string default_instance_name() {
    std::array<char, MAX_COMPUTERNAME_LENGTH + 1U> computer_name {};
    if (auto size = static_cast<DWORD>(computer_name.size()); ::GetComputerNameA(computer_name.data(), &size) != FALSE && size > 0U) {
      return std::string {computer_name.data(), size};
    }
    return "Windows PC";
  }

  struct PolarApiResult {
    bool transport_ok = false;
    DWORD http_status = 0;
    std::optional<LicenseCalendarClock::time_point> server_time;
    std::string body;
    std::string error;
  };

  PolarApiResult post_polar_license_request(
    std::wstring_view endpoint,
    const nlohmann::json &request_body
  ) {
    PolarApiResult result;
    const auto session = make_unique_winhttp_handle(::WinHttpOpen(L"libvirtualhid-broker/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
      result.error = "WinHttpOpen failed: " + windows_error_message(::GetLastError());
      return result;
    }
    if (::WinHttpSetTimeouts(session.get(), polar_resolve_timeout, polar_connect_timeout, polar_send_timeout, polar_receive_timeout) == FALSE) {
      result.error = "WinHttpSetTimeouts failed: " + windows_error_message(::GetLastError());
      return result;
    }

    const auto connection = make_unique_winhttp_handle(::WinHttpConnect(session.get(), L"api.polar.sh", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
      result.error = "WinHttpConnect failed: " + windows_error_message(::GetLastError());
      return result;
    }

    const auto request = make_unique_winhttp_handle(::WinHttpOpenRequest(connection.get(), L"POST", std::wstring {endpoint}.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) {
      result.error = "WinHttpOpenRequest failed: " + windows_error_message(::GetLastError());
      return result;
    }

    auto body = request_body.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (constexpr auto headers = L"Accept: application/json\r\nContent-Type: application/json\r\n"; ::WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1), body.data(), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) == FALSE) {
      result.error = "WinHttpSendRequest failed: " + windows_error_message(::GetLastError());
      return result;
    }

    if (::WinHttpReceiveResponse(request.get(), nullptr) == FALSE) {
      result.error = "WinHttpReceiveResponse failed: " + windows_error_message(::GetLastError());
      return result;
    }

    DWORD status_size = sizeof(result.http_status);
    static_cast<void>(::WinHttpQueryHeaders(
      request.get(),
      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
      WINHTTP_HEADER_NAME_BY_INDEX,
      &result.http_status,
      &status_size,
      WINHTTP_NO_HEADER_INDEX
    ));

    SYSTEMTIME server_time {};
    if (DWORD server_time_size = sizeof(server_time); ::WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_DATE | WINHTTP_QUERY_FLAG_SYSTEMTIME, WINHTTP_HEADER_NAME_BY_INDEX, &server_time, &server_time_size, WINHTTP_NO_HEADER_INDEX) != FALSE) {
      result.server_time = license_server_time(server_time);
    }

    for (;;) {
      DWORD available = 0;
      if (::WinHttpQueryDataAvailable(request.get(), &available) == FALSE) {
        result.error = "WinHttpQueryDataAvailable failed: " + windows_error_message(::GetLastError());
        return result;
      }
      if (available == 0U) {
        break;
      }

      const auto old_size = result.body.size();
      result.body.resize(old_size + available);
      DWORD read = 0;
      if (::WinHttpReadData(request.get(), result.body.data() + old_size, available, &read) == FALSE) {
        result.error = "WinHttpReadData failed: " + windows_error_message(::GetLastError());
        return result;
      }
      result.body.resize(old_size + read);
    }

    result.transport_ok = true;
    if (result.http_status >= 400U) {
      if (const auto parsed = parse_json(result.body)) {
        result.error = polar_error_message(*parsed, "The license service returned an error.");
      } else {
        result.error = "The license service returned an error.";
      }
    }
    return result;
  }

  std::uint32_t json_uint32_or_zero(const nlohmann::json &object, std::string_view key) {
    if (!object.is_object()) {
      return 0;
    }
    const auto iter = object.find(std::string {key});
    if (iter == object.end() || iter->is_null()) {
      return 0;
    }
    if (iter->is_number_unsigned()) {
      return static_cast<std::uint32_t>(std::min(
        iter->get<std::uint64_t>(),
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
      ));
    }
    if (iter->is_number_integer()) {
      const auto value = iter->get<std::int64_t>();
      if (value > 0) {
        return static_cast<std::uint32_t>(std::min(
          static_cast<std::uint64_t>(value),
          static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
        ));
      }
    }
    return 0;
  }

  PolarLicenseState license_state_from_json(
    const nlohmann::json &body,
    std::string_view fallback_license_key,
    std::string_view fallback_activation_id
  ) {
    PolarLicenseState state;
    state.provider = "polar";
    state.license_key = json_string_or_empty(body, "key");
    state.activation_id = fallback_activation_id;
    state.license_key_id = json_string_or_empty(body, "id");
    state.license_status = json_string_or_empty(body, "status");
    state.organization_id = json_string_or_empty(body, "organization_id");
    state.benefit_id = json_string_or_empty(body, "benefit_id");
    state.activation_limit = json_uint32_or_zero(body, "limit_activations");

    if (const auto customer = body.find("customer"); customer != body.end() && customer->is_object()) {
      state.customer_email = json_string_or_empty(*customer, "email");
    }

    if (const auto activation = body.find("activation"); activation != body.end() && activation->is_object()) {
      const auto activation_id = json_string_or_empty(*activation, "id");
      if (!activation_id.empty()) {
        state.activation_id = activation_id;
      }
    }
    if (state.license_key.empty()) {
      state.license_key = fallback_license_key;
    }
    return state;
  }

  const lvh::windows::broker_config::PolarBenefit *polar_benefit(
    std::string_view benefit_id
  ) {
    const auto benefit = std::ranges::find_if(
      lvh::windows::broker_config::allowed_benefits,
      [benefit_id](const auto &candidate) {
        return candidate.id == benefit_id;
      }
    );
    return benefit == lvh::windows::broker_config::allowed_benefits.end() ?
             nullptr :
             std::to_address(benefit);
  }

  std::string_view plan_name_for_benefit(std::string_view benefit_id) {
    const auto *benefit = polar_benefit(benefit_id);
    return benefit == nullptr ? std::string_view {} : benefit->plan_name;
  }

  bool session_token_matches(
    const LvhWindowsSessionToken &lhs,
    const LvhWindowsSessionToken &rhs
  ) {
    return std::ranges::equal(lhs.bytes, rhs.bytes);
  }

  LvhWindowsDestroyDeviceRequest make_destroy_device_request(
    std::uint64_t driver_device_id,
    const LvhWindowsSessionToken &session_token
  ) {
    auto request = LvhWindowsDestroyDeviceRequest {};
    request.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
    request.size = sizeof(request);
    request.driver_device_id = driver_device_id;
    request.session_token = session_token;
    return request;
  }

  template<typename DestroyDevice, typename ForgetDevice>
  void destroy_cleanup_candidates(
    std::span<const LvhWindowsDestroyDeviceRequest> requests,
    DestroyDevice &&destroy_device,
    ForgetDevice &&forget_device
  ) {
    for (const auto &request : requests) {
      if (destroy_device(request)) {
        forget_device(request);
      }
    }
  }

  DWORD pipe_client_process_id(HANDLE pipe) {
    ULONG process_id = 0;
    if (::GetNamedPipeClientProcessId(pipe, &process_id) == FALSE) {
      return 0;
    }
    return process_id;
  }

  UniqueHandle open_client_process(DWORD process_id) {
    if (process_id == 0U) {
      return make_unique_handle(nullptr);
    }
    return make_unique_handle(::OpenProcess(SYNCHRONIZE | PROCESS_DUP_HANDLE, FALSE, process_id));
  }

  UniqueHandle duplicate_client_handle(
    HANDLE client_process,
    std::uint64_t client_handle_value
  ) {
    const auto native_handle_value = static_cast<std::uintptr_t>(client_handle_value);
    if (client_process == nullptr || static_cast<std::uint64_t>(native_handle_value) != client_handle_value || native_handle_value == 0U) {
      return make_unique_handle(nullptr);
    }

    auto duplicated_handle = HANDLE {};
    if (::DuplicateHandle(client_process, std::bit_cast<HANDLE>(native_handle_value), ::GetCurrentProcess(), &duplicated_handle, 0, FALSE, DUPLICATE_SAME_ACCESS) == FALSE) {
      return make_unique_handle(nullptr);
    }
    return make_unique_handle(duplicated_handle);
  }

  LvhWindowsBrokerStatusCode broker_status_from_protocol(std::uint32_t status) {
    using enum LvhWindowsBrokerStatusCode;

    switch (status) {
      case LVH_WINDOWS_STATUS_SUCCESS:
        return success;
      case LVH_WINDOWS_STATUS_INVALID_ARGUMENT:
        return invalid_argument;
      case LVH_WINDOWS_STATUS_UNSUPPORTED_PROFILE:
        return unsupported_profile;
      case LVH_WINDOWS_STATUS_DEVICE_NOT_FOUND:
        return device_not_found;
      case LVH_WINDOWS_STATUS_BACKEND_FAILURE:
      default:
        return backend_failure;
    }
  }

  LvhWindowsBrokerStatusCode activation_failure_status(DWORD http_status) {
    using enum LvhWindowsBrokerStatusCode;

    if (http_status == 403U) {
      return activation_limit_reached;
    }
    if (http_status == 404U || http_status == 422U) {
      return license_invalid;
    }
    return backend_failure;
  }

  class DriverChannel {
  public:
    DriverChannel() = default;

    DriverChannel(const DriverChannel &) = delete;
    DriverChannel &operator=(const DriverChannel &) = delete;

    bool open() {
      if (handle_) {
        return true;
      }

      for (const auto *path : {LVH_WINDOWS_CONTROL_DEVICE_PATH, LVH_WINDOWS_GLOBAL_CONTROL_DEVICE_PATH}) {
        const auto handle = ::CreateFileA(
          path,
          GENERIC_READ | GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE,
          nullptr,
          OPEN_EXISTING,
          FILE_ATTRIBUTE_NORMAL,
          nullptr
        );
        if (handle != INVALID_HANDLE_VALUE) {
          path_ = path;
          handle_ = make_unique_handle(handle);
          return true;
        }
      }

      last_error_ = ::GetLastError();
      return false;
    }

    bool create_device(
      HANDLE control_handle,
      LvhWindowsCreateDeviceRequest request,
      LvhWindowsCreateDeviceResponse &response,
      LvhWindowsBrokerStatusCode &status,
      std::string &message
    ) const {
      if (control_handle == nullptr || control_handle == INVALID_HANDLE_VALUE) {
        status = LvhWindowsBrokerStatusCode::backend_unavailable;
        message = "The requesting client control handle is unavailable.";
        return false;
      }

      auto operation_event = make_unique_handle(::CreateEventA(nullptr, TRUE, FALSE, nullptr));
      if (!operation_event) {
        status = LvhWindowsBrokerStatusCode::backend_failure;
        message = "Unable to create a Windows driver request event: " + windows_error_message(::GetLastError());
        return false;
      }

      OVERLAPPED overlapped {};
      overlapped.hEvent = operation_event.get();
      DWORD bytes_returned = 0;
      if (::DeviceIoControl(control_handle, LVH_WINDOWS_IOCTL_CREATE_DEVICE, &request, sizeof(request), &response, sizeof(response), nullptr, &overlapped) == FALSE && ::GetLastError() != ERROR_IO_PENDING) {
        status = LvhWindowsBrokerStatusCode::backend_failure;
        message = "create Windows virtual HID device: " + windows_error_message(::GetLastError());
        return false;
      }
      if (::GetOverlappedResult(control_handle, &overlapped, &bytes_returned, TRUE) == FALSE) {
        status = LvhWindowsBrokerStatusCode::backend_failure;
        message = "create Windows virtual HID device: " + windows_error_message(::GetLastError());
        return false;
      }

      if (bytes_returned < sizeof(response)) {
        status = LvhWindowsBrokerStatusCode::backend_failure;
        message = "Windows driver returned a truncated device response";
        return false;
      }

      status = broker_status_from_protocol(response.status);
      if (status != LvhWindowsBrokerStatusCode::success) {
        message = "Windows driver rejected virtual HID device creation";
        return false;
      }

      if (response.device_path[0] == '\0') {
        copy_c_string(response.device_path, path_);
      }

      message.clear();
      return true;
    }

    bool destroy_device(
      LvhWindowsDestroyDeviceRequest request,
      LvhWindowsBrokerStatusCode &status,
      std::string &message
    ) {
      using enum LvhWindowsBrokerStatusCode;

      if (!open()) {
        status = backend_unavailable;
        message = "Windows UMDF control device is unavailable: " + windows_error_message(last_error_);
        return false;
      }

      if (DWORD bytes_returned = 0; ::DeviceIoControl(handle_.get(), LVH_WINDOWS_IOCTL_DESTROY_DEVICE, &request, sizeof(request), nullptr, 0, &bytes_returned, nullptr) == FALSE) {
        status = backend_failure;
        message = "destroy Windows virtual HID device: " + windows_error_message(::GetLastError());
        return false;
      }

      status = success;
      message.clear();
      return true;
    }

    bool reset_devices(
      LvhWindowsBrokerStatusCode &status,
      std::string &message
    ) {
      using enum LvhWindowsBrokerStatusCode;

      if (!open()) {
        status = backend_unavailable;
        message = "Windows UMDF control device is unavailable: " + windows_error_message(last_error_);
        return false;
      }

      LvhWindowsResetDevicesRequest request {};
      request.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
      request.size = sizeof(request);
      if (DWORD bytes_returned = 0; ::DeviceIoControl(handle_.get(), LVH_WINDOWS_IOCTL_RESET_DEVICES, &request, sizeof(request), nullptr, 0, &bytes_returned, nullptr) == FALSE) {
        status = backend_failure;
        message = "reset Windows virtual HID devices: " + windows_error_message(::GetLastError());
        return false;
      }

      status = success;
      message.clear();
      return true;
    }

  private:
    UniqueHandle handle_ {nullptr, &::CloseHandle};
    std::string path_;
    DWORD last_error_ = ERROR_FILE_NOT_FOUND;
  };

  class BrokerState {
  public:
    struct DeviceRecord {
      LvhWindowsSessionToken session_token {};
      DWORD owner_process_id {};
      UniqueHandle owner_process {nullptr, &::CloseHandle};
      bool github_actions_evaluation = false;
    };

    BrokerState() {
      auto ignored_status = LvhWindowsBrokerStatusCode::success;
      std::string ignored_message;
      static_cast<void>(reconcile_driver_state(ignored_status, ignored_message));
      if (license_state_) {
        std::lock_guard lock {mutex_};
        schedule_license_validation_locked(LicenseValidationClock::now(), true);
      }
      license_validation_thread_ = std::jthread {
        [this](std::stop_token stop_token) {
          license_validation_loop(stop_token);
        }
      };
    }

    ~BrokerState() {
      license_validation_thread_.request_stop();
      license_validation_wakeup_.notify_all();
      if (license_validation_thread_.joinable()) {
        license_validation_thread_.join();
      }
    }

    void fill_license_status(LvhWindowsBrokerLicenseStatus &license) const {
      std::lock_guard lock {mutex_};
      fill_license_status_locked(license);
    }

    LvhWindowsBrokerStatusResponse handle_status() const {
      LvhWindowsBrokerStatusResponse response {};
      response.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
      response.size = sizeof(response);
      response.status = std::to_underlying(LvhWindowsBrokerStatusCode::success);
      fill_license_status(response.license);
      copy_c_string(response.message, "Broker is running.");
      return response;
    }

    LvhWindowsBrokerCreateDeviceResponse handle_create(
      const LvhWindowsBrokerCreateDeviceRequest &request,
      DWORD client_process_id
    ) {
      LvhWindowsBrokerCreateDeviceResponse response {};
      response.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
      response.size = sizeof(response);
      response.device.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
      response.device.size = sizeof(response.device);

      if (!lvh::windows::broker_validation::valid_request(request)) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
        copy_c_string(response.message, "Invalid broker create request.");
        fill_license_status(response.license);
        return response;
      }

      auto reconciliation_status = LvhWindowsBrokerStatusCode::success;
      if (std::string reconciliation_message; !reconcile_driver_state(reconciliation_status, reconciliation_message)) {
        response.status = std::to_underlying(reconciliation_status);
        copy_c_string(response.message, reconciliation_message);
        fill_license_status(response.license);
        return response;
      }

      auto owner_process = open_client_process(client_process_id);
      if (!owner_process) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::backend_failure);
        copy_c_string(response.message, "Unable to open the requesting client process.");
        fill_license_status(response.license);
        return response;
      }

      auto client_control_handle = duplicate_client_handle(
        owner_process.get(),
        request.client_control_handle
      );
      if (!client_control_handle) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
        copy_c_string(response.message, "Unable to duplicate the requesting client control handle.");
        fill_license_status(response.license);
        return response;
      }

      const auto [authorization_status, github_actions_evaluation] = authorize_device_create(
        response.license,
        response.message
      );
      if (authorization_status != LvhWindowsBrokerStatusCode::success) {
        response.status = std::to_underlying(authorization_status);
        return response;
      }

      auto status = LvhWindowsBrokerStatusCode::success;
      if (std::string message; !driver_.create_device(client_control_handle.get(), request.device, response.device, status, message)) {
        response.status = std::to_underlying(status);
        fill_license_status(response.license);
        copy_c_string(response.message, message);
        return response;
      }

      {
        std::lock_guard lock {mutex_};
        devices_.try_emplace(
          response.device.driver_device_id,
          DeviceRecord {
            .session_token = response.device.session_token,
            .owner_process_id = client_process_id,
            .owner_process = std::move(owner_process),
            .github_actions_evaluation = github_actions_evaluation,
          }
        );
        fill_license_status_locked(response.license);
      }

      response.status = std::to_underlying(LvhWindowsBrokerStatusCode::success);
      copy_c_string(
        response.message,
        github_actions_evaluation ?
          "Created virtual HID device using the GitHub Actions evaluation window." :
          "Created virtual HID device."
      );
      return response;
    }

    LvhWindowsBrokerDestroyDeviceResponse handle_destroy(
      const LvhWindowsBrokerDestroyDeviceRequest &request
    ) {
      LvhWindowsBrokerDestroyDeviceResponse response {};
      response.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
      response.size = sizeof(response);

      if (!lvh::windows::broker_validation::valid_request(request)) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
        copy_c_string(response.message, "Invalid broker destroy request.");
        fill_license_status(response.license);
        return response;
      }

      {
        std::lock_guard lock {mutex_};
        const auto iter = devices_.find(request.device.driver_device_id);
        if (iter == devices_.end()) {
          response.status = std::to_underlying(LvhWindowsBrokerStatusCode::device_not_found);
          fill_license_status_locked(response.license);
          copy_c_string(response.message, "Broker does not own this virtual device.");
          return response;
        }
        if (!session_token_matches(iter->second.session_token, request.device.session_token)) {
          response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
          fill_license_status_locked(response.license);
          copy_c_string(response.message, "Invalid virtual device session token.");
          return response;
        }
      }

      auto status = LvhWindowsBrokerStatusCode::success;
      if (std::string message; !driver_.destroy_device(request.device, status, message)) {
        response.status = std::to_underlying(status);
        fill_license_status(response.license);
        copy_c_string(response.message, message);
        return response;
      }

      {
        std::lock_guard lock {mutex_};
        devices_.erase(request.device.driver_device_id);
        fill_license_status_locked(response.license);
      }

      response.status = std::to_underlying(LvhWindowsBrokerStatusCode::success);
      copy_c_string(response.message, "Destroyed virtual device.");
      return response;
    }

    void cleanup_devices() {
      auto reconciliation_status = LvhWindowsBrokerStatusCode::success;
      if (std::string reconciliation_message; !reconcile_driver_state(reconciliation_status, reconciliation_message)) {
        return;
      }

      std::vector<LvhWindowsDestroyDeviceRequest> cleanup_requests;
      {
        std::lock_guard lock {mutex_};
        const auto now = lvh::windows::github_actions_evaluation::Clock::now();
        const auto evaluation_expired = github_actions_ &&
                                        github_actions_evaluation_state_ &&
                                        !license_is_active_locked() &&
                                        !lvh::windows::github_actions_evaluation::active(
                                          github_actions_evaluation_state_->started_at,
                                          now
                                        );
        const auto license_now = LicenseValidationClock::now();
        if (subscription_validation_has_elapsed_locked()) {
          revoke_licensed_devices_ = true;
        }
        const auto revoke_licensed_devices = revoke_licensed_devices_;
        const auto limit_licensed_devices =
          license_validation_unavailable_since_ &&
          license_outage_retention_elapsed(
            license_now - *license_validation_unavailable_since_
          );
        std::size_t licensed_devices_kept = 0;
        for (const auto &[driver_device_id, device] : devices_) {
          const auto wait_result = ::WaitForSingleObject(device.owner_process.get(), 0);
          const auto owner_gone = wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED;
          const auto revoke_for_license = broker_device_should_be_revoked(
            device.github_actions_evaluation,
            evaluation_expired,
            revoke_licensed_devices
          );
          if (const auto over_outage_limit = license_outage_device_should_be_revoked(device.github_actions_evaluation, limit_licensed_devices, licensed_devices_kept); owner_gone || revoke_for_license || over_outage_limit) {
            cleanup_requests.push_back(make_destroy_device_request(driver_device_id, device.session_token));
            continue;
          }
          if (!device.github_actions_evaluation) {
            ++licensed_devices_kept;
          }
        }
      }

      destroy_cleanup_candidates(
        cleanup_requests,
        [this](const auto &request) {
          auto status = LvhWindowsBrokerStatusCode::success;
          std::string message;
          return driver_.destroy_device(request, status, message);
        },
        [this](const auto &request) {
          std::lock_guard lock {mutex_};
          const auto iter = devices_.find(request.driver_device_id);
          if (iter != devices_.end() && session_token_matches(iter->second.session_token, request.session_token)) {
            devices_.erase(iter);
          }
        }
      );

      {
        std::lock_guard lock {mutex_};
        if (revoke_licensed_devices_ && active_licensed_device_count_locked() == 0U) {
          revoke_licensed_devices_ = false;
        }
      }
    }

    LvhWindowsBrokerLicenseResponse handle_activate_license(
      const LvhWindowsBrokerLicenseRequest &request
    ) {
      LvhWindowsBrokerLicenseResponse response {};
      response.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
      response.size = sizeof(response);

      if (!lvh::windows::broker_validation::valid_request(
            request,
            LvhWindowsBrokerRequestType::activate_license
          )) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
        copy_c_string(response.message, "Invalid broker activate request.");
        fill_license_status(response.license);
        return response;
      }

      const std::string license_key {request.license_key.data()};
      const auto instance_name = request.instance_name[0] == '\0' ? default_instance_name() : std::string {request.instance_name.data()};
      if (license_key.empty()) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
        copy_c_string(response.message, "License key is required.");
        fill_license_status(response.license);
        return response;
      }

      std::lock_guard operation_lock {license_operation_mutex_};

      auto api_result = post_polar_license_request(
        L"/v1/customer-portal/license-keys/activate",
        nlohmann::json {
          {"key", license_key},
          {"organization_id", std::string {lvh::windows::broker_config::polar_organization_id}},
          {"label", instance_name},
        }
      );
      if (!api_result.transport_ok) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::network_unavailable);
        copy_c_string(response.message, api_result.error);
        fill_license_status(response.license);
        return response;
      }

      if (api_result.http_status != 200U) {
        response.status = std::to_underlying(activation_failure_status(api_result.http_status));
        copy_c_string(
          response.message,
          api_result.error.empty() ? "License activation failed." : api_result.error
        );
        fill_license_status(response.license);
        return response;
      }

      const auto parsed = parse_json(api_result.body);
      if (!parsed) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::backend_failure);
        copy_c_string(response.message, "The license activation response was not valid JSON.");
        fill_license_status(response.license);
        return response;
      }

      const auto activation_id = json_string_or_empty(*parsed, "id");
      const auto license_key_body = parsed->find("license_key");
      if (activation_id.empty() || license_key_body == parsed->end() || !license_key_body->is_object()) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::backend_failure);
        copy_c_string(response.message, "The license activation response was missing license state.");
        fill_license_status(response.license);
        return response;
      }

      auto new_state = license_state_from_json(*license_key_body, license_key, activation_id);
      if (new_state.license_status != "granted") {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::license_invalid);
        copy_c_string(response.message, "The license service did not grant this license key.");
        fill_license_status(response.license);
        return response;
      }

      if (!license_allowed(new_state)) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::license_invalid);
        copy_c_string(response.message, "License organization or benefit is not allowed for this driver.");
        fill_license_status(response.license);
        return response;
      }

      std::string authorization_error;
      if (const auto authorization_result = accept_trusted_license_time(new_state, api_result.server_time, authorization_error); authorization_result != TrustedLicenseTimeResult::success) {
        response.status = std::to_underlying(
          authorization_result == TrustedLicenseTimeResult::license_invalid ?
            LvhWindowsBrokerStatusCode::license_invalid :
            LvhWindowsBrokerStatusCode::backend_failure
        );
        copy_c_string(response.message, authorization_error);
        fill_license_status(response.license);
        return response;
      }

      if (std::string save_error; !save_license_state(new_state, save_error)) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::backend_failure);
        copy_c_string(response.message, save_error);
        fill_license_status(response.license);
        return response;
      }

      {
        std::lock_guard lock {mutex_};
        license_state_ = std::move(new_state);
        license_online_confirmed_ = true;
        license_validation_unavailable_since_.reset();
        schedule_license_validation_locked(LicenseValidationClock::now(), false);
        fill_license_status_locked(response.license);
      }
      response.status = std::to_underlying(LvhWindowsBrokerStatusCode::success);
      copy_c_string(response.message, "License activated on this machine.");
      return response;
    }

    LvhWindowsBrokerLicenseResponse handle_validate_license(
      const LvhWindowsBrokerLicenseRequest &request
    ) {
      LvhWindowsBrokerLicenseResponse response {};
      response.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
      response.size = sizeof(response);

      if (!lvh::windows::broker_validation::valid_request(
            request,
            LvhWindowsBrokerRequestType::validate_license
          )) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
        copy_c_string(response.message, "Invalid broker validate request.");
        fill_license_status(response.license);
        return response;
      }

      const auto [status, message] = validate_saved_license();
      schedule_after_license_validation(status);
      response.status = std::to_underlying(status);
      fill_license_status(response.license);
      copy_c_string(response.message, message);
      return response;
    }

    LvhWindowsBrokerLicenseResponse handle_deactivate_license(
      const LvhWindowsBrokerLicenseRequest &request
    ) {
      LvhWindowsBrokerLicenseResponse response {};
      response.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
      response.size = sizeof(response);

      if (!lvh::windows::broker_validation::valid_request(
            request,
            LvhWindowsBrokerRequestType::deactivate_license
          )) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
        copy_c_string(response.message, "Invalid broker deactivate request.");
        fill_license_status(response.license);
        return response;
      }

      std::lock_guard operation_lock {license_operation_mutex_};

      PolarLicenseState state;
      {
        std::lock_guard lock {mutex_};
        if (!license_state_) {
          response.status = std::to_underlying(LvhWindowsBrokerStatusCode::success);
          fill_license_status_locked(response.license);
          copy_c_string(response.message, "No machine license is active.");
          return response;
        }
        state = *license_state_;
      }

      auto api_result = post_polar_license_request(
        L"/v1/customer-portal/license-keys/deactivate",
        nlohmann::json {
          {"key", state.license_key},
          {"organization_id", std::string {lvh::windows::broker_config::polar_organization_id}},
          {"activation_id", state.activation_id},
        }
      );
      if (!api_result.transport_ok) {
        response.status = std::to_underlying(LvhWindowsBrokerStatusCode::network_unavailable);
        copy_c_string(response.message, api_result.error);
        fill_license_status(response.license);
        return response;
      }

      if (api_result.http_status != 204U) {
        if (api_result.http_status == 404U) {
          invalidate_license();
          response.status = std::to_underlying(LvhWindowsBrokerStatusCode::success);
          fill_license_status(response.license);
          copy_c_string(response.message, "The machine activation was already absent; local license state was removed.");
          return response;
        }
        response.status = std::to_underlying(
          LvhWindowsBrokerStatusCode::backend_failure
        );
        copy_c_string(
          response.message,
          api_result.error.empty() ? "License deactivation failed." : api_result.error
        );
        fill_license_status(response.license);
        return response;
      }

      delete_license_state();
      {
        std::lock_guard lock {mutex_};
        license_state_.reset();
        next_license_validation_attempt_.reset();
        license_online_confirmed_ = false;
        license_validation_unavailable_since_.reset();
        revoke_licensed_devices_ = true;
        license_validation_wakeup_.notify_all();
        fill_license_status_locked(response.license);
      }
      response.status = std::to_underlying(LvhWindowsBrokerStatusCode::success);
      copy_c_string(response.message, "License deactivated on this machine.");
      return response;
    }

  private:
    enum class TrustedLicenseTimeResult {
      success,
      license_invalid,
      backend_failure,
    };

    bool reconcile_driver_state(
      LvhWindowsBrokerStatusCode &status,
      std::string &message
    ) {
      {
        std::lock_guard lock {mutex_};
        if (driver_state_reconciled_) {
          status = LvhWindowsBrokerStatusCode::success;
          message.clear();
          return true;
        }
      }

      if (!driver_.reset_devices(status, message)) {
        return false;
      }

      std::lock_guard lock {mutex_};
      devices_.clear();
      driver_state_reconciled_ = true;
      return true;
    }

    void schedule_license_validation_locked(
      LicenseValidationClock::time_point now,
      bool immediately
    ) {
      if (!license_state_) {
        next_license_validation_attempt_.reset();
      } else if (immediately) {
        next_license_validation_attempt_ = now;
      } else {
        next_license_validation_attempt_ = now + license_validation_interval;
      }
      license_validation_wakeup_.notify_all();
    }

    void schedule_after_license_validation(
      LvhWindowsBrokerStatusCode status
    ) {
      std::lock_guard lock {mutex_};
      if (!license_state_) {
        next_license_validation_attempt_.reset();
      } else {
        next_license_validation_attempt_ = LicenseValidationClock::now() +
                                           (status == LvhWindowsBrokerStatusCode::success ?
                                              license_validation_interval :
                                              license_validation_retry_interval);
      }
      license_validation_wakeup_.notify_all();
    }

    void license_validation_loop(std::stop_token stop_token) {
      while (!stop_token.stop_requested()) {
        bool validation_due = false;
        {
          std::unique_lock lock {mutex_};
          static_cast<void>(license_validation_wakeup_.wait_for(
            lock,
            std::chrono::minutes {1},
            [this, stop_token] {
              return stop_token.stop_requested() ||
                     (license_state_ && next_license_validation_attempt_ &&
                      LicenseValidationClock::now() >= *next_license_validation_attempt_);
            }
          ));
          if (stop_token.stop_requested()) {
            return;
          }
          const auto now = LicenseValidationClock::now();
          validation_due = license_state_ &&
                           next_license_validation_attempt_ &&
                           now >= *next_license_validation_attempt_;
          if (validation_due) {
            next_license_validation_attempt_ = now + license_validation_retry_interval;
          }
        }

        if (!validation_due) {
          continue;
        }
        const auto [status, ignored_message] = validate_saved_license();
        static_cast<void>(ignored_message);
        schedule_after_license_validation(status);
      }
    }

    bool license_allowed(const PolarLicenseState &state) const {
      return !lvh::windows::broker_config::polar_organization_id.empty() &&
             state.organization_id == lvh::windows::broker_config::polar_organization_id &&
             !plan_name_for_benefit(state.benefit_id).empty();
    }

    TrustedLicenseTimeResult accept_trusted_license_time(
      PolarLicenseState &state,
      const std::optional<LicenseCalendarClock::time_point> &server_time,
      std::string &message
    ) const {
      using enum TrustedLicenseTimeResult;

      if (!server_time) {
        message = "The license service response did not include trusted server time.";
        return backend_failure;
      }
      if (const auto *benefit = polar_benefit(state.benefit_id); benefit == nullptr) {
        message = "License organization or benefit is not allowed for this driver.";
        return license_invalid;
      }
      state.validated_at = license_calendar_timestamp(*server_time);
      state.boot_marker = boot_session_marker_;
      state.validated_uptime_ms = ::GetTickCount64();
      if (state.boot_marker.empty()) {
        message = "Unable to establish a trusted Windows boot-session clock.";
        return backend_failure;
      }
      message.clear();
      return success;
    }

    std::optional<std::uint64_t> license_effective_timestamp_locked() const {
      if (!license_state_) {
        return std::nullopt;
      }
      return license_monotonic_calendar_timestamp(
        license_state_->validated_at,
        license_state_->validated_uptime_ms,
        ::GetTickCount64(),
        !boot_session_marker_.empty() &&
          license_state_->boot_marker == boot_session_marker_
      );
    }

    bool license_authorization_is_current_locked() const {
      if (!license_state_ || license_state_->license_status != "granted") {
        return false;
      }
      const auto *benefit = polar_benefit(license_state_->benefit_id);
      if (benefit == nullptr) {
        return false;
      }
      if (!benefit->subscription_backed) {
        return true;
      }
      const auto effective_timestamp = license_effective_timestamp_locked();
      return effective_timestamp && license_validation_is_current(
                                      license_state_->validated_at,
                                      *effective_timestamp
                                    );
    }

    bool subscription_validation_has_elapsed_locked() const {
      if (!license_state_) {
        return false;
      }
      const auto *benefit = polar_benefit(license_state_->benefit_id);
      const auto effective_timestamp = license_effective_timestamp_locked();
      if (benefit == nullptr || !benefit->subscription_backed || !effective_timestamp.has_value()) {
        return false;
      }
      return !license_validation_is_current(
        license_state_->validated_at,
        *effective_timestamp
      );
    }

    void mark_license_validation_unavailable() {
      std::lock_guard lock {mutex_};
      if (license_state_) {
        license_online_confirmed_ = false;
        if (!license_validation_unavailable_since_) {
          license_validation_unavailable_since_ = LicenseValidationClock::now();
        }
      }
    }

    std::size_t active_licensed_device_count_locked() const {
      return static_cast<std::size_t>(std::ranges::count_if(
        devices_,
        [](const auto &entry) {
          return !entry.second.github_actions_evaluation;
        }
      ));
    }

    bool license_is_active_locked() const {
      return license_state_ &&
             license_allowed(*license_state_) &&
             license_authorization_is_current_locked();
    }

    void invalidate_license() {
      {
        std::lock_guard lock {mutex_};
        license_state_.reset();
        next_license_validation_attempt_.reset();
        license_online_confirmed_ = false;
        license_validation_unavailable_since_.reset();
        revoke_licensed_devices_ = true;
        license_validation_wakeup_.notify_all();
      }
      delete_license_state();
    }

    std::pair<LvhWindowsBrokerStatusCode, std::string> validate_saved_license() {
      std::lock_guard operation_lock {license_operation_mutex_};
      PolarLicenseState state;
      {
        std::lock_guard lock {mutex_};
        if (!license_state_) {
          return {
            LvhWindowsBrokerStatusCode::license_invalid,
            "No license is activated on this machine.",
          };
        }
        state = *license_state_;
      }

      auto api_result = post_polar_license_request(
        L"/v1/customer-portal/license-keys/validate",
        nlohmann::json {
          {"key", state.license_key},
          {"organization_id", std::string {lvh::windows::broker_config::polar_organization_id}},
          {"activation_id", state.activation_id},
        }
      );
      if (!api_result.transport_ok) {
        mark_license_validation_unavailable();
        return {
          LvhWindowsBrokerStatusCode::network_unavailable,
          api_result.error,
        };
      }

      if (api_result.http_status != 200U) {
        if (api_result.http_status == 404U) {
          invalidate_license();
        } else {
          mark_license_validation_unavailable();
        }
        return {
          api_result.http_status == 404U ?
            LvhWindowsBrokerStatusCode::license_invalid :
            LvhWindowsBrokerStatusCode::backend_failure,
          api_result.error.empty() ? "License validation failed." : api_result.error,
        };
      }

      const auto parsed = parse_json(api_result.body);
      if (!parsed) {
        mark_license_validation_unavailable();
        return {
          LvhWindowsBrokerStatusCode::backend_failure,
          "The license validation response was not valid JSON.",
        };
      }

      auto new_state = license_state_from_json(*parsed, state.license_key, {});
      if (new_state.activation_id != state.activation_id) {
        invalidate_license();
        return {
          LvhWindowsBrokerStatusCode::license_invalid,
          "The license service did not validate this machine activation.",
        };
      }
      if (new_state.license_status != "granted") {
        const auto error = new_state.license_status == "disabled" ?
                             "License disabled." :
                             "License revoked.";
        invalidate_license();
        return {
          LvhWindowsBrokerStatusCode::license_invalid,
          error,
        };
      }

      if (!license_allowed(new_state)) {
        invalidate_license();
        return {
          LvhWindowsBrokerStatusCode::license_invalid,
          "License organization or benefit is not allowed for this driver.",
        };
      }

      std::string authorization_error;
      if (const auto authorization_result = accept_trusted_license_time(new_state, api_result.server_time, authorization_error); authorization_result != TrustedLicenseTimeResult::success) {
        if (authorization_result == TrustedLicenseTimeResult::license_invalid) {
          invalidate_license();
        } else {
          mark_license_validation_unavailable();
        }
        return {
          authorization_result == TrustedLicenseTimeResult::license_invalid ?
            LvhWindowsBrokerStatusCode::license_invalid :
            LvhWindowsBrokerStatusCode::backend_failure,
          authorization_error,
        };
      }

      if (std::string save_error; !save_license_state(new_state, save_error)) {
        mark_license_validation_unavailable();
        return {
          LvhWindowsBrokerStatusCode::backend_failure,
          save_error,
        };
      }

      {
        std::lock_guard lock {mutex_};
        license_state_ = std::move(new_state);
        license_online_confirmed_ = true;
        license_validation_unavailable_since_.reset();
      }
      return {
        LvhWindowsBrokerStatusCode::success,
        "License validated.",
      };
    }

    std::pair<LvhWindowsBrokerStatusCode, bool> authorize_device_create(
      LvhWindowsBrokerLicenseStatus &license,
      std::array<char, LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE> &message
    ) {
      {
        std::lock_guard lock {mutex_};
        if (!license_state_) {
          if (!github_actions_) {
            fill_license_status_locked(license);
            copy_c_string(message, "An active license is required to create virtual HID devices.");
            return {LvhWindowsBrokerStatusCode::license_required, false};
          }

          const auto now = lvh::windows::github_actions_evaluation::Clock::now();
          if (!github_actions_evaluation_state_) {
            GitHubActionsEvaluationState evaluation_state {.started_at = now};
            if (std::string save_error; !save_github_actions_evaluation_state(evaluation_state, save_error)) {
              fill_license_status_locked(license);
              copy_c_string(message, save_error);
              return {LvhWindowsBrokerStatusCode::backend_failure, false};
            }
            github_actions_evaluation_state_ = evaluation_state;
          }

          fill_license_status_locked(license);
          if (!lvh::windows::github_actions_evaluation::active(
                github_actions_evaluation_state_->started_at,
                now
              )) {
            copy_c_string(message, "The five-minute GitHub Actions evaluation window has expired.");
            return {LvhWindowsBrokerStatusCode::license_required, false};
          }

          const auto remaining = lvh::windows::github_actions_evaluation::remaining(
            github_actions_evaluation_state_->started_at,
            now
          );
          copy_c_string(
            message,
            std::format("GitHub Actions evaluation active for {} more seconds.", remaining.count())
          );
          return {LvhWindowsBrokerStatusCode::success, true};
        }

        if (!license_allowed(*license_state_) || !license_authorization_is_current_locked()) {
          fill_license_status_locked(license);
          copy_c_string(message, license.message.data());
          return {LvhWindowsBrokerStatusCode::license_invalid, false};
        }

        if (license_online_confirmed_) {
          fill_license_status_locked(license);
          copy_c_string(message, "License validated online.");
          return {LvhWindowsBrokerStatusCode::success, false};
        }

        if (unvalidated_device_creation_allowed(active_licensed_device_count_locked())) {
          fill_license_status_locked(license);
          copy_c_string(message, "Polar validation is unavailable; one active device is permitted.");
          return {LvhWindowsBrokerStatusCode::success, false};
        }

        fill_license_status_locked(license);
        copy_c_string(message, "Polar validation is unavailable; the one-device fallback is already in use.");
        return {LvhWindowsBrokerStatusCode::network_unavailable, false};
      }
    }

    void fill_license_status_locked(LvhWindowsBrokerLicenseStatus &license) const {
      license.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
      license.size = sizeof(license);
      license.active_devices = static_cast<std::uint32_t>(devices_.size());
      license.free_active_device_limit = 0;

      if (!license_state_) {
        license.state = std::to_underlying(LvhWindowsBrokerLicenseState::free);
        license.activation_limit = 0;
        license.activation_usage = 0;
        copy_c_string(license.customer_email, "");
        if (!github_actions_) {
          copy_c_string(license.plan_name, "Unlicensed");
          copy_c_string(license.message, "An active license is required to create virtual HID devices.");
          return;
        }

        copy_c_string(license.plan_name, "GitHub Actions Evaluation");
        if (!github_actions_evaluation_state_) {
          copy_c_string(
            license.message,
            "Five-minute GitHub Actions evaluation starts with the first virtual HID device creation."
          );
          return;
        }

        if (const auto remaining = lvh::windows::github_actions_evaluation::remaining(github_actions_evaluation_state_->started_at, lvh::windows::github_actions_evaluation::Clock::now()); remaining > std::chrono::seconds::zero()) {
          copy_c_string(
            license.message,
            std::format("GitHub Actions evaluation active for {} more seconds.", remaining.count())
          );
        } else {
          copy_c_string(
            license.message,
            "GitHub Actions evaluation expired; an active license is required."
          );
        }
        return;
      }

      license.activation_limit = license_state_->activation_limit;
      license.activation_usage = license_state_->activation_id.empty() ? 0U : 1U;
      const auto plan_name = plan_name_for_benefit(license_state_->benefit_id);
      copy_c_string(license.plan_name, plan_name.empty() ? "Licensed" : plan_name);
      copy_c_string(license.customer_email, license_state_->customer_email);

      if (!license_allowed(*license_state_)) {
        license.state = std::to_underlying(LvhWindowsBrokerLicenseState::invalid);
        copy_c_string(license.message, "License organization or benefit is not allowed for this driver.");
      } else if (license_state_->license_status == "disabled") {
        license.state = std::to_underlying(LvhWindowsBrokerLicenseState::disabled);
        copy_c_string(license.message, "License disabled.");
      } else if (license_state_->license_status == "revoked") {
        license.state = std::to_underlying(LvhWindowsBrokerLicenseState::invalid);
        copy_c_string(license.message, "License revoked.");
      } else if (!license_authorization_is_current_locked()) {
        license.state = std::to_underlying(LvhWindowsBrokerLicenseState::invalid);
        if (!license_effective_timestamp_locked().has_value()) {
          copy_c_string(license.message, "Reconnect to Polar after Windows restarts to validate the license.");
        } else {
          copy_c_string(license.message, "Reconnect to Polar to validate the yearly subscription.");
        }
      } else if (license_state_->license_status == "granted") {
        license.state = std::to_underlying(LvhWindowsBrokerLicenseState::licensed);
        copy_c_string(
          license.message,
          license_online_confirmed_ ?
            "Licensed." :
            "Polar validation unavailable; existing devices are retained for one hour and new creation is limited to one active device."
        );
      } else {
        license.state = std::to_underlying(LvhWindowsBrokerLicenseState::invalid);
        copy_c_string(license.message, "License is not granted.");
      }
    }

    mutable std::mutex mutex_;
    std::mutex license_operation_mutex_;
    std::condition_variable license_validation_wakeup_;
    const bool github_actions_ = lizardbyte::common::is_github_actions();
    const std::string boot_session_marker_ {load_or_create_boot_session_marker()};
    std::map<std::uint64_t, DeviceRecord> devices_;
    DriverChannel driver_;
    std::optional<PolarLicenseState> license_state_ {load_license_state()};
    std::optional<LicenseValidationClock::time_point> next_license_validation_attempt_;
    std::optional<LicenseValidationClock::time_point> license_validation_unavailable_since_;
    std::optional<GitHubActionsEvaluationState> github_actions_evaluation_state_ {
      github_actions_ ? load_github_actions_evaluation_state() : std::nullopt
    };
    bool license_online_confirmed_ = false;
    bool revoke_licensed_devices_ = false;
    bool driver_state_reconciled_ = false;
    std::jthread license_validation_thread_;
  };

  BrokerState &broker_state() {
    static BrokerState state;
    return state;
  }

  template<typename Response>
  bool write_response(
    HANDLE pipe,
    const Response &response,
    HANDLE requested_stop_event
  ) {
    if (!write_pipe_message(
          pipe,
          std::as_bytes(std::span<const Response> {&response, 1}),
          requested_stop_event
        )) {
      return false;
    }

    // DisconnectNamedPipe discards responses that the client has not read yet.
    // A well-behaved one-request client closes its handle after reading the
    // response, so wait for that close before disconnecting the server end. The
    // overlapped read retains the normal I/O timeout and stop-event handling,
    // preventing an uncooperative client from blocking the service indefinitely.
    std::array<std::byte, 1> extra_request {};
    DWORD bytes_read = 0;
    static_cast<void>(read_pipe_message(
      pipe,
      extra_request,
      requested_stop_event,
      bytes_read
    ));
    return true;
  }

  template<typename Request, std::size_t Size>
  Request request_from_buffer(const std::array<std::byte, Size> &buffer) {
    Request request {};
    std::memcpy(&request, buffer.data(), sizeof(request));
    return request;
  }

  void handle_pipe_client(HANDLE pipe, HANDLE requested_stop_event) {
    broker_state().cleanup_devices();

    const auto send_response = [pipe, requested_stop_event](const auto &response) {
      return write_response(pipe, response, requested_stop_event);
    };

    std::array<std::byte, pipe_buffer_size> request_buffer {};
    DWORD bytes_read = 0;
    if (!read_pipe_message(pipe, request_buffer, requested_stop_event, bytes_read) || bytes_read < sizeof(LvhWindowsBrokerRequestHeader)) {
      auto response = broker_state().handle_status();
      response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
      copy_c_string(response.message, "Broker request was empty or truncated.");
      static_cast<void>(send_response(response));
      return;
    }

    const auto header = request_from_buffer<LvhWindowsBrokerRequestHeader>(request_buffer);
    if (header.version != LVH_WINDOWS_BROKER_PROTOCOL_VERSION || header.size != bytes_read) {
      auto response = broker_state().handle_status();
      response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
      copy_c_string(response.message, "Broker request header is invalid.");
      static_cast<void>(send_response(response));
      return;
    }

    switch (static_cast<LvhWindowsBrokerRequestType>(header.type)) {
      case LvhWindowsBrokerRequestType::status:
        if (header.size == sizeof(LvhWindowsBrokerStatusRequest)) {
          const auto request = request_from_buffer<LvhWindowsBrokerStatusRequest>(request_buffer);
          if (lvh::windows::broker_validation::valid_request(request)) {
            static_cast<void>(send_response(broker_state().handle_status()));
            return;
          }
        }
        break;

      case LvhWindowsBrokerRequestType::create_device:
        if (header.size == sizeof(LvhWindowsBrokerCreateDeviceRequest)) {
          const auto request = request_from_buffer<LvhWindowsBrokerCreateDeviceRequest>(request_buffer);
          static_cast<void>(send_response(
            broker_state().handle_create(request, pipe_client_process_id(pipe))
          ));
          return;
        }
        break;

      case LvhWindowsBrokerRequestType::destroy_device:
        if (header.size == sizeof(LvhWindowsBrokerDestroyDeviceRequest)) {
          const auto request = request_from_buffer<LvhWindowsBrokerDestroyDeviceRequest>(request_buffer);
          static_cast<void>(send_response(broker_state().handle_destroy(request)));
          return;
        }
        break;

      case LvhWindowsBrokerRequestType::activate_license:
        if (header.size == sizeof(LvhWindowsBrokerLicenseRequest)) {
          const auto request = request_from_buffer<LvhWindowsBrokerLicenseRequest>(request_buffer);
          static_cast<void>(send_response(broker_state().handle_activate_license(request)));
          return;
        }
        break;

      case LvhWindowsBrokerRequestType::validate_license:
        if (header.size == sizeof(LvhWindowsBrokerLicenseRequest)) {
          const auto request = request_from_buffer<LvhWindowsBrokerLicenseRequest>(request_buffer);
          static_cast<void>(send_response(broker_state().handle_validate_license(request)));
          return;
        }
        break;

      case LvhWindowsBrokerRequestType::deactivate_license:
        if (header.size == sizeof(LvhWindowsBrokerLicenseRequest)) {
          const auto request = request_from_buffer<LvhWindowsBrokerLicenseRequest>(request_buffer);
          static_cast<void>(send_response(broker_state().handle_deactivate_license(request)));
          return;
        }
        break;
    }

    auto response = broker_state().handle_status();
    response.status = std::to_underlying(LvhWindowsBrokerStatusCode::invalid_argument);
    copy_c_string(response.message, "Broker request type or size is unsupported.");
    static_cast<void>(send_response(response));
  }

  std::optional<UniqueHandle> wait_for_pipe_client(HANDLE requested_stop_event) {
    PSECURITY_DESCRIPTOR raw_security_descriptor = nullptr;
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(pipe_security_descriptor, SDDL_REVISION_1, &raw_security_descriptor, nullptr) == FALSE) {
      return std::nullopt;
    }
    auto security_descriptor = std::unique_ptr<void, decltype(&::LocalFree)> {
      raw_security_descriptor,
      &::LocalFree,
    };
    SECURITY_ATTRIBUTES security_attributes {
      .nLength = sizeof(SECURITY_ATTRIBUTES),
      .lpSecurityDescriptor = security_descriptor.get(),
      .bInheritHandle = FALSE,
    };

    auto pipe = make_unique_handle(::CreateNamedPipeA(LVH_WINDOWS_BROKER_PIPE_PATH, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE, PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, PIPE_UNLIMITED_INSTANCES, pipe_buffer_size, pipe_buffer_size, pipe_connect_timeout, &security_attributes));
    if (!pipe) {
      return std::nullopt;
    }

    auto connected_event = make_unique_handle(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!connected_event) {
      return std::nullopt;
    }

    OVERLAPPED overlapped {};
    overlapped.hEvent = connected_event.get();
    if (::ConnectNamedPipe(pipe.get(), &overlapped) == FALSE) {
      const auto error = ::GetLastError();
      if (error == ERROR_PIPE_CONNECTED) {
        return pipe;
      }
      if (error != ERROR_IO_PENDING) {
        return std::nullopt;
      }

      DWORD ignored = 0;
      if (!complete_pending_pipe_io(
            pipe.get(),
            overlapped,
            requested_stop_event,
            pipe_connect_timeout,
            ignored
          )) {
        return std::nullopt;
      }
    }

    return pipe;
  }

  void report_service_status(DWORD current_state, DWORD win32_exit_code = NO_ERROR, DWORD wait_hint = 0) {
    auto &runtime = service_runtime();
    if (runtime.status_handle == nullptr) {
      return;
    }

    runtime.status.dwCurrentState = current_state;
    runtime.status.dwWin32ExitCode = win32_exit_code;
    runtime.status.dwWaitHint = wait_hint;
    runtime.status.dwControlsAccepted = current_state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    if (current_state == SERVICE_RUNNING || current_state == SERVICE_STOPPED) {
      runtime.status.dwCheckPoint = 0;
    } else {
      ++runtime.status.dwCheckPoint;
    }

    static_cast<void>(SetServiceStatus(runtime.status_handle, &runtime.status));
  }

  DWORD run_broker_loop(HANDLE requested_stop_event) {
    if (requested_stop_event == nullptr) {
      return ERROR_INVALID_HANDLE;
    }

    while (WaitForSingleObject(requested_stop_event, 0) == WAIT_TIMEOUT) {
      broker_state().cleanup_devices();

      auto pipe = wait_for_pipe_client(requested_stop_event);
      if (!pipe) {
        continue;
      }

      handle_pipe_client(pipe->get(), requested_stop_event);
      static_cast<void>(::DisconnectNamedPipe(pipe->get()));
    }

    return ERROR_SUCCESS;
  }

  BOOL WINAPI console_control_handler(DWORD control_type) {
    switch (control_type) {
      case CTRL_C_EVENT:
      case CTRL_BREAK_EVENT:
      case CTRL_CLOSE_EVENT:
        if (const auto event = service_runtime().stop_event; event != nullptr) {
          static_cast<void>(::SetEvent(event));
          return TRUE;
        }
        return FALSE;

      default:
        return FALSE;
    }
  }

  void WINAPI service_control_handler(DWORD control_code) {
    switch (control_code) {
      case SERVICE_CONTROL_STOP:
      case SERVICE_CONTROL_SHUTDOWN:
        report_service_status(SERVICE_STOP_PENDING, NO_ERROR, 1000);
        if (const auto event = service_runtime().stop_event; event != nullptr) {
          static_cast<void>(SetEvent(event));
        }
        return;

      default:
        return;
    }
  }

  void WINAPI service_main(DWORD argc, wchar_t **argv) {
    static_cast<void>(argc);
    static_cast<void>(argv);

    auto &runtime = service_runtime();
    runtime.status_handle = RegisterServiceCtrlHandlerW(service_name, service_control_handler);
    if (runtime.status_handle == nullptr) {
      return;
    }

    report_service_status(SERVICE_START_PENDING, NO_ERROR, 1000);
    runtime.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (runtime.stop_event == nullptr) {
      report_service_status(SERVICE_STOPPED, GetLastError());
      return;
    }

    report_service_status(SERVICE_RUNNING);
    const auto result = run_broker_loop(runtime.stop_event);
    static_cast<void>(CloseHandle(runtime.stop_event));
    runtime.stop_event = nullptr;
    report_service_status(SERVICE_STOPPED, result);
  }

  int run_console() {
    auto &runtime = service_runtime();
    runtime.stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (runtime.stop_event == nullptr) {
      return static_cast<int>(GetLastError());
    }

    static_cast<void>(SetConsoleCtrlHandler(console_control_handler, TRUE));
    const auto result = run_broker_loop(runtime.stop_event);
    static_cast<void>(SetConsoleCtrlHandler(console_control_handler, FALSE));
    static_cast<void>(CloseHandle(runtime.stop_event));
    runtime.stop_event = nullptr;
    return static_cast<int>(result);
  }

}  // namespace lvh::detail::windows_broker_service

int main(int argc, char **argv) {
  if (argc > 1 && std::string_view {argv[1]} == "--console") {
    return lvh::detail::windows_broker_service::run_console();
  }
  if (argc > 1 && std::string_view {argv[1]} == "--service-name") {
    (void) lvh::detail::windows_broker_service::broker_instance_name;
    return 0;
  }

  std::wstring mutable_service_name {
    lvh::detail::windows_broker_service::service_name,
  };
  if (std::array<SERVICE_TABLE_ENTRYW, 2> dispatch_table {{
        {
          mutable_service_name.data(),
          lvh::detail::windows_broker_service::service_main,
        },
        {nullptr, nullptr},
      }};
      StartServiceCtrlDispatcherW(dispatch_table.data()) != FALSE) {
    return 0;
  }

  const auto error = GetLastError();
  if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
    return lvh::detail::windows_broker_service::run_console();
  }

  return static_cast<int>(error);
}
