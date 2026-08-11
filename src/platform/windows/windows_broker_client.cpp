/**
 * @file src/platform/windows/windows_broker_client.cpp
 * @brief Internal Windows broker client helper definitions.
 */

// local includes
#include "platform/windows/windows_broker_client.hpp"

// standard includes
#include <array>
#include <cstddef>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
#include <Windows.h>

namespace lvh::detail::windows_broker {
  namespace {

    using UniqueHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;

    // GENERIC_READ is required when switching the client end to message-read mode.
    constexpr auto pipe_client_access = GENERIC_READ | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;
    constexpr auto pipe_client_granted_access = FILE_GENERIC_READ | FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES;
    constexpr auto pipe_wait_timeout = 5000U;

    static_assert(pipe_client_access == 0x80000102U);
    static_assert(pipe_client_granted_access == 0x0012018BU);
    static_assert((pipe_client_granted_access & FILE_CREATE_PIPE_INSTANCE) == 0U);

    UniqueHandle make_unique_handle(HANDLE handle) {
      if (handle == INVALID_HANDLE_VALUE) {
        handle = nullptr;
      }
      return {handle, &::CloseHandle};
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
        return std::format("Windows error {}", error_code);
      }

      std::string message {message_buffer.data(), message_size};
      while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
      }
      return message;
    }

  }  // namespace

  LvhWindowsBrokerRequestHeader make_request_header(LvhWindowsBrokerRequestType type, std::uint32_t size) {
    return {
      .version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION,
      .size = size,
      .type = std::to_underlying(type),
      .reserved0 = 0,
    };
  }

  OperationStatus response_status(std::uint32_t status, std::string_view message) {
    using enum ErrorCode;

    const auto text = message.empty() ? "Windows broker request failed" : std::string {message};
    switch (static_cast<LvhWindowsBrokerStatusCode>(status)) {
      case LvhWindowsBrokerStatusCode::success:
        return OperationStatus::success();
      case LvhWindowsBrokerStatusCode::invalid_argument:
        return OperationStatus::failure(invalid_argument, text);
      case LvhWindowsBrokerStatusCode::unsupported_profile:
        return OperationStatus::failure(unsupported_profile, text);
      case LvhWindowsBrokerStatusCode::device_not_found:
        return OperationStatus::failure(device_closed, text);
      case LvhWindowsBrokerStatusCode::backend_unavailable:
        return OperationStatus::failure(backend_unavailable, text);
      case LvhWindowsBrokerStatusCode::license_required:
        return OperationStatus::failure(license_required, text);
      case LvhWindowsBrokerStatusCode::license_invalid:
        return OperationStatus::failure(license_invalid, text);
      case LvhWindowsBrokerStatusCode::activation_limit_reached:
        return OperationStatus::failure(activation_limit_reached, text);
      case LvhWindowsBrokerStatusCode::network_unavailable:
        return OperationStatus::failure(network_unavailable, text);
      case LvhWindowsBrokerStatusCode::backend_failure:
      default:
        return OperationStatus::failure(backend_failure, text);
    }
  }

  OperationStatus call_bytes(
    std::span<const std::byte> request,
    std::span<std::byte> response,
    std::string_view operation
  ) {
    auto pipe = make_unique_handle(::CreateFileA(LVH_WINDOWS_BROKER_PIPE_PATH, pipe_client_access, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!pipe && ::GetLastError() == ERROR_PIPE_BUSY && ::WaitNamedPipeA(LVH_WINDOWS_BROKER_PIPE_PATH, pipe_wait_timeout) != FALSE) {
      pipe = make_unique_handle(::CreateFileA(LVH_WINDOWS_BROKER_PIPE_PATH, pipe_client_access, 0, nullptr, OPEN_EXISTING, 0, nullptr));
    }
    if (!pipe) {
      return OperationStatus::failure(
        ErrorCode::backend_unavailable,
        std::format("{}: {}", operation, windows_error_message(::GetLastError()))
      );
    }

    if (DWORD read_mode = PIPE_READMODE_MESSAGE; ::SetNamedPipeHandleState(pipe.get(), &read_mode, nullptr, nullptr) == FALSE) {
      return OperationStatus::failure(
        ErrorCode::backend_unavailable,
        std::format("{}: {}", operation, windows_error_message(::GetLastError()))
      );
    }

    auto request_copy = std::vector<std::byte> {request.begin(), request.end()};
    DWORD bytes_read = 0;
    if (::TransactNamedPipe(pipe.get(), request_copy.data(), static_cast<DWORD>(request_copy.size()), response.data(), static_cast<DWORD>(response.size()), &bytes_read, nullptr) == FALSE) {
      return OperationStatus::failure(
        ErrorCode::backend_unavailable,
        std::format("{}: {}", operation, windows_error_message(::GetLastError()))
      );
    }

    if (bytes_read != response.size()) {
      return OperationStatus::failure(ErrorCode::backend_failure, "Windows broker returned a truncated or invalid response");
    }

    return OperationStatus::success();
  }

}  // namespace lvh::detail::windows_broker
