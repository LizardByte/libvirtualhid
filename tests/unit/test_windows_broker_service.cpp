/**
 * @file tests/unit/test_windows_broker_service.cpp
 * @brief Direct tests for the installed Windows broker service boundary.
 */

// local includes
#include "fixtures/fixtures.hpp"
#include "fixtures/windows_broker_service_test_hooks.hpp"
#include "lvh_windows_broker_protocol.h"

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
#include <Windows.h>
#include <winsvc.h>

// standard includes
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

  using UniqueServiceHandle = std::unique_ptr<SC_HANDLE__, decltype(&::CloseServiceHandle)>;

  constexpr auto pipe_retry_interval = 10U;
  constexpr auto pipe_wait_timeout = 5000U;

  struct BrokerResponsePrefix {
    std::uint32_t version = 0;
    std::uint32_t size = 0;
    std::uint32_t status = 0;
    std::uint32_t reserved = 0;
  };

  struct RawBrokerResponse {
    DWORD error = ERROR_SUCCESS;
    std::vector<std::byte> bytes;
  };

  HANDLE open_broker_pipe() {
    return ::CreateFileA(
      LVH_WINDOWS_BROKER_PIPE_PATH,
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );
  }

  bool wait_to_retry_broker_pipe(DWORD &last_error) {
    if (last_error == ERROR_FILE_NOT_FOUND) {
      ::Sleep(pipe_retry_interval);
      return true;
    }
    if (last_error != ERROR_PIPE_BUSY) {
      return false;
    }
    if (::WaitNamedPipeA(LVH_WINDOWS_BROKER_PIPE_PATH, pipe_retry_interval) != FALSE) {
      return true;
    }

    last_error = ::GetLastError();
    return last_error == ERROR_SEM_TIMEOUT || last_error == ERROR_FILE_NOT_FOUND;
  }

  HANDLE connect_to_broker() {
    DWORD last_error = ERROR_FILE_NOT_FOUND;
    for (auto attempt = 0U; attempt < pipe_wait_timeout / pipe_retry_interval; ++attempt) {
      if (HANDLE pipe = open_broker_pipe(); pipe != INVALID_HANDLE_VALUE) {
        if (DWORD read_mode = PIPE_READMODE_MESSAGE; ::SetNamedPipeHandleState(pipe, &read_mode, nullptr, nullptr) == FALSE) {
          const auto error = ::GetLastError();
          static_cast<void>(::CloseHandle(pipe));
          ::SetLastError(error);
          return INVALID_HANDLE_VALUE;
        }
        return pipe;
      }

      last_error = ::GetLastError();
      if (!wait_to_retry_broker_pipe(last_error)) {
        ::SetLastError(last_error);
        return INVALID_HANDLE_VALUE;
      }
    }
    ::SetLastError(last_error);
    return INVALID_HANDLE_VALUE;
  }

  RawBrokerResponse transact_raw(std::span<const std::byte> request) {
    const auto pipe = connect_to_broker();
    if (pipe == INVALID_HANDLE_VALUE) {
      return {.error = ::GetLastError()};
    }

    if (DWORD bytes_written = 0; ::WriteFile(pipe, request.data(), static_cast<DWORD>(request.size()), &bytes_written, nullptr) == FALSE || bytes_written != request.size()) {
      const auto error = ::GetLastError();
      static_cast<void>(::CloseHandle(pipe));
      return {.error = error};
    }

    std::array<std::byte, 2048> response_buffer {};
    DWORD bytes_read = 0;
    if (::ReadFile(pipe, response_buffer.data(), static_cast<DWORD>(response_buffer.size()), &bytes_read, nullptr) == FALSE) {
      const auto error = ::GetLastError();
      static_cast<void>(::CloseHandle(pipe));
      return {.error = error};
    }
    static_cast<void>(::CloseHandle(pipe));

    return {
      .bytes = std::vector<std::byte> {
        response_buffer.begin(),
        response_buffer.begin() + bytes_read,
      },
    };
  }

  template<typename Request>
  RawBrokerResponse transact_raw(const Request &request) {
    return transact_raw(std::as_bytes(std::span<const Request> {&request, 1}));
  }

  BrokerResponsePrefix response_prefix(const RawBrokerResponse &response) {
    BrokerResponsePrefix prefix {};
    if (response.bytes.size() >= sizeof(prefix)) {
      std::memcpy(&prefix, response.bytes.data(), sizeof(prefix));
    }
    return prefix;
  }

  LvhWindowsBrokerStatusRequest status_request() {
    LvhWindowsBrokerStatusRequest request {};
    request.header.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
    request.header.size = sizeof(request);
    request.header.type = std::to_underlying(LvhWindowsBrokerRequestType::status);
    return request;
  }

  bool broker_is_available() {
    const auto response = transact_raw(status_request());
    return response.error == ERROR_SUCCESS &&
           response_prefix(response).status ==
             std::to_underlying(LvhWindowsBrokerStatusCode::success);
  }

  void expect_status(
    const RawBrokerResponse &response,
    LvhWindowsBrokerStatusCode expected
  ) {
    ASSERT_EQ(response.error, ERROR_SUCCESS);
    ASSERT_GE(response.bytes.size(), sizeof(BrokerResponsePrefix));
    const auto prefix = response_prefix(response);
    EXPECT_EQ(prefix.version, LVH_WINDOWS_BROKER_PROTOCOL_VERSION);
    EXPECT_EQ(prefix.size, response.bytes.size());
    EXPECT_EQ(prefix.status, std::to_underlying(expected));
  }

  bool wait_for_service_state(SC_HANDLE service, DWORD expected_state) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds {15};
    while (std::chrono::steady_clock::now() < deadline) {
      SERVICE_STATUS_PROCESS status {};
      if (DWORD bytes_needed = 0; ::QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, std::bit_cast<LPBYTE>(std::as_writable_bytes(std::span {&status, 1}).data()), sizeof(status), &bytes_needed) == FALSE) {
        return false;
      }
      if (status.dwCurrentState == expected_state) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {100});
    }
    return false;
  }

  class ServiceStartGuard {
  public:
    explicit ServiceStartGuard(SC_HANDLE service):
        service_ {service} {}

    ~ServiceStartGuard() {
      SERVICE_STATUS_PROCESS status {};
      if (DWORD bytes_needed = 0; ::QueryServiceStatusEx(service_, SC_STATUS_PROCESS_INFO, std::bit_cast<LPBYTE>(std::as_writable_bytes(std::span {&status, 1}).data()), sizeof(status), &bytes_needed) != FALSE && status.dwCurrentState == SERVICE_RUNNING) {
        return;
      }
      if (status.dwCurrentState == SERVICE_START_PENDING) {
        static_cast<void>(wait_for_service_state(service_, SERVICE_RUNNING));
        return;
      }
      if (status.dwCurrentState == SERVICE_STOP_PENDING && !wait_for_service_state(service_, SERVICE_STOPPED)) {
        return;
      }

      if (::StartServiceW(service_, 0, nullptr) == FALSE && ::GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) {
        return;
      }
      static_cast<void>(wait_for_service_state(service_, SERVICE_RUNNING));
    }

    ServiceStartGuard(const ServiceStartGuard &) = delete;
    ServiceStartGuard &operator=(const ServiceStartGuard &) = delete;

  private:
    SC_HANDLE service_;
  };

  class WindowsBrokerServiceTest: public WindowsTest {
  protected:
    void SetUp() override {
      if (!broker_is_available()) {
        GTEST_SKIP() << "The installed libvirtualhid broker service is unavailable.";
      }
    }
  };

}  // namespace

TEST_F(WindowsBrokerServiceTest, RejectsTruncatedOversizedAndUnknownMessages) {
  std::array<std::byte, sizeof(LvhWindowsBrokerRequestHeader) - 1U> truncated {};
  expect_status(
    transact_raw(truncated),
    LvhWindowsBrokerStatusCode::invalid_argument
  );

  auto unknown = status_request();
  unknown.header.type = 999U;
  expect_status(
    transact_raw(unknown),
    LvhWindowsBrokerStatusCode::invalid_argument
  );

  std::vector<std::byte> oversized(1024U);
  auto oversized_header = status_request().header;
  oversized_header.size = static_cast<std::uint32_t>(oversized.size());
  std::memcpy(oversized.data(), &oversized_header, sizeof(oversized_header));
  expect_status(
    transact_raw(oversized),
    LvhWindowsBrokerStatusCode::invalid_argument
  );
}

TEST_F(WindowsBrokerServiceTest, RejectsMalformedAndUnterminatedRequests) {
  auto malformed_status = status_request();
  malformed_status.header.reserved0 = 1U;
  expect_status(
    transact_raw(malformed_status),
    LvhWindowsBrokerStatusCode::invalid_argument
  );

  LvhWindowsBrokerLicenseRequest unterminated {};
  unterminated.header.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
  unterminated.header.size = sizeof(unterminated);
  unterminated.header.type =
    std::to_underlying(LvhWindowsBrokerRequestType::activate_license);
  std::ranges::fill(unterminated.license_key, 'x');
  std::ranges::fill(unterminated.instance_name, 'y');
  expect_status(
    transact_raw(unterminated),
    LvhWindowsBrokerStatusCode::invalid_argument
  );
}

TEST_F(WindowsBrokerServiceTest, RecoversFromClientDisconnectsAndConcurrentCalls) {
  {
    const auto pipe = connect_to_broker();
    ASSERT_NE(pipe, INVALID_HANDLE_VALUE);
    std::array<std::byte, 3> partial {};
    DWORD bytes_written = 0;
    const auto wrote_partial_request =
      ::WriteFile(
        pipe,
        partial.data(),
        static_cast<DWORD>(partial.size()),
        &bytes_written,
        nullptr
      );
    static_cast<void>(::CloseHandle(pipe));
    ASSERT_NE(wrote_partial_request, FALSE);
  }

  expect_status(
    transact_raw(status_request()),
    LvhWindowsBrokerStatusCode::success
  );

  std::array<RawBrokerResponse, 8> responses;
  {
    std::array<std::jthread, responses.size()> calls;
    for (std::size_t index = 0; index < calls.size(); ++index) {
      calls[index] = std::jthread {[&responses, index]() {
        responses[index] = transact_raw(status_request());
      }};
    }
  }
  for (const auto &response : responses) {
    expect_status(response, LvhWindowsBrokerStatusCode::success);
  }
}

TEST_F(WindowsBrokerServiceTest, DriverRejectsDirectGamepadCreation) {
  const auto control = ::CreateFileA(
    LVH_WINDOWS_CONTROL_DEVICE_PATH,
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  );
  if (control == INVALID_HANDLE_VALUE) {
    GTEST_SKIP() << "The installed libvirtualhid control device is unavailable.";
  }

  LvhWindowsCreateGamepadRequest request {};
  request.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
  request.size = sizeof(request);
  LvhWindowsCreateGamepadResponse response {};
  DWORD bytes_returned = 0;
  const auto request_result =
    ::DeviceIoControl(
      control,
      LVH_WINDOWS_IOCTL_CREATE_GAMEPAD,
      &request,
      sizeof(request),
      &response,
      sizeof(response),
      &bytes_returned,
      nullptr
    );
  const auto request_error = ::GetLastError();
  static_cast<void>(::CloseHandle(control));
  EXPECT_EQ(request_result, FALSE);
  EXPECT_EQ(request_error, ERROR_ACCESS_DENIED);
}

TEST(WindowsBrokerImplementationTest, ReportsDpapiAndPolarFailures) {
  const auto dpapi_result = lvh::detail::test::broker_persistence_failure(
    lvh::detail::test::BrokerPersistenceFailure::dpapi
  );
  EXPECT_FALSE(dpapi_result.saved);
  EXPECT_TRUE(dpapi_result.message.starts_with("Unable to protect test state:"))
    << dpapi_result.message;

  using enum lvh::detail::test::BrokerPolarScenario;
  for (const auto scenario : {
         open_failure,
         connect_failure,
         request_failure,
         send_failure,
         receive_failure,
       }) {
    const auto result = lvh::detail::test::broker_polar_scenario(scenario);
    EXPECT_FALSE(result.transport_ok);
    EXPECT_FALSE(result.error.empty());
  }

  const auto structured =
    lvh::detail::test::broker_polar_scenario(structured_error);
  EXPECT_TRUE(structured.transport_ok);
  EXPECT_EQ(structured.http_status, 422U);
  EXPECT_EQ(structured.error, "License activation was rejected.");

  const auto malformed =
    lvh::detail::test::broker_polar_scenario(malformed_error);
  EXPECT_TRUE(malformed.transport_ok);
  EXPECT_EQ(malformed.http_status, 422U);
  EXPECT_EQ(malformed.error, "The license service returned an error.");

  const auto succeeded = lvh::detail::test::broker_polar_scenario(success);
  EXPECT_TRUE(succeeded.transport_ok);
  EXPECT_EQ(succeeded.http_status, 200U);
  EXPECT_TRUE(succeeded.error.empty());
}

TEST(WindowsBrokerImplementationTest, ReportsFilePersistenceFailures) {
  using enum lvh::detail::test::BrokerPersistenceFailure;
  for (const auto &[failure, expected_message] :
       std::array {
         std::pair {create_file, "Unable to write test state:"},
         std::pair {partial_write, "Unable to persist test state:"},
         std::pair {flush, "Unable to persist test state:"},
         std::pair {close, "Unable to close test state:"},
       }) {
    const auto result =
      lvh::detail::test::broker_persistence_failure(failure);
    EXPECT_FALSE(result.saved);
    EXPECT_TRUE(result.message.starts_with(expected_message)) << result.message;
  }
}

TEST_F(WindowsBrokerServiceTest, RestartsWithoutLosingTheServiceBoundary) {
  auto manager = UniqueServiceHandle {
    ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT),
    &::CloseServiceHandle,
  };
  ASSERT_TRUE(manager);

  auto service = UniqueServiceHandle {
    ::OpenServiceW(
      manager.get(),
      L"libvirtualhid_broker",
      SERVICE_QUERY_STATUS | SERVICE_START | SERVICE_STOP
    ),
    &::CloseServiceHandle,
  };
  if (!service && ::GetLastError() == ERROR_ACCESS_DENIED) {
    GTEST_SKIP() << "Restarting the broker service requires elevation.";
  }
  ASSERT_TRUE(service);
  const ServiceStartGuard service_start_guard {service.get()};

  if (SERVICE_STATUS status {}; ::ControlService(service.get(), SERVICE_CONTROL_STOP, &status) == FALSE) {
    const auto error = ::GetLastError();
    if (error != ERROR_SERVICE_NOT_ACTIVE) {
      FAIL() << "Unable to stop the broker service: " << error;
    }
  }
  ASSERT_TRUE(wait_for_service_state(service.get(), SERVICE_STOPPED));
  ASSERT_NE(::StartServiceW(service.get(), 0, nullptr), FALSE);
  ASSERT_TRUE(wait_for_service_state(service.get(), SERVICE_RUNNING));

  expect_status(
    transact_raw(status_request()),
    LvhWindowsBrokerStatusCode::success
  );
}
