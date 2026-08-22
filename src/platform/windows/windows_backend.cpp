/**
 * @file src/platform/windows/windows_backend.cpp
 * @brief Windows UMDF control-channel backend definitions.
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
#include "core/backend.hpp"
#include "lvh_windows_broker_protocol.h"
#include "platform/windows/control_protocol.hpp"
#include "platform/windows/keylayout.hpp"
#include "platform/windows/shared/generic_pid_rumble.hpp"
#include "platform/windows/shared/mouse_protocol.hpp"
#include "platform/windows/windows_broker_client.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

// lib includes
#include <lizardbyte/common/env.h>

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
// clang-format off
#include <Windows.h>
#include <SetupAPI.h>
// clang-format on

namespace lvh::detail {
  namespace {  // NOSONAR(cpp:S1000): Windows backend internals need internal linkage; tests include this file with the platform factory renamed.

    class WindowsBackendContext;

    using UniqueHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;
    using SendInputFunction = std::function<UINT(std::span<INPUT>)>;
    using SyncThreadDesktopFunction = std::function<HDESK()>;

    /**
     * @brief Thread-local desktop identity used for SendInput retry decisions.
     */
    struct LastKnownInputDesktop {
      HDESK value = nullptr;  ///< Last desktop returned by OpenInputDesktop.
    };

    /**
     * @brief Runtime-loaded Windows synthetic pointer API entry points.
     */
    struct SyntheticPointerApi {
      std::function<HSYNTHETICPOINTERDEVICE(POINTER_INPUT_TYPE, ULONG, POINTER_FEEDBACK_MODE)> create;  ///< Device creation entry point.
      std::function<BOOL(HSYNTHETICPOINTERDEVICE, const POINTER_TYPE_INFO *, UINT32)> inject;  ///< Pointer injection entry point.
      std::function<void(HSYNTHETICPOINTERDEVICE)> destroy;  ///< Device destroy entry point.
    };

    UINT send_input_with_win32(std::span<INPUT> inputs) {
      return ::SendInput(
        static_cast<UINT>(inputs.size()),
        inputs.data(),
        static_cast<int>(sizeof(INPUT))
      );
    }

    SendInputFunction &send_input_function() {
      static SendInputFunction function = send_input_with_win32;
      return function;
    }

    HDESK sync_thread_desktop_with_win32() {
      const auto desktop = ::OpenInputDesktop(DF_ALLOWOTHERACCOUNTHOOK, FALSE, GENERIC_ALL);
      if (!desktop) {
        return nullptr;
      }

      static_cast<void>(::SetThreadDesktop(desktop));
      ::CloseDesktop(desktop);
      return desktop;
    }

    SyncThreadDesktopFunction &sync_thread_desktop_function() {
      static SyncThreadDesktopFunction function = sync_thread_desktop_with_win32;
      return function;
    }

    LastKnownInputDesktop &last_known_input_desktop() {
      thread_local LastKnownInputDesktop desktop;
      return desktop;
    }

    template<typename Function>
    Function load_user32_function(HMODULE user32, const char *name) {
      const auto address = ::GetProcAddress(user32, name);
      Function function {};
      static_assert(sizeof(function) == sizeof(address));
      std::memcpy(&function, &address, sizeof(function));
      return function;
    }

    SyntheticPointerApi make_win32_synthetic_pointer_api() {
      const auto user32 = ::GetModuleHandleA("user32.dll");
      if (!user32) {
        return {};
      }

      const auto create = load_user32_function<decltype(&::CreateSyntheticPointerDevice)>(user32, "CreateSyntheticPointerDevice");
      const auto inject = load_user32_function<decltype(&::InjectSyntheticPointerInput)>(user32, "InjectSyntheticPointerInput");
      const auto destroy = load_user32_function<decltype(&::DestroySyntheticPointerDevice)>(user32, "DestroySyntheticPointerDevice");
      if (!create || !inject || !destroy) {
        return {};
      }

      return {
        .create = create,
        .inject = inject,
        .destroy = destroy,
      };
    }

    SyntheticPointerApi &synthetic_pointer_api() {
      static SyntheticPointerApi api = make_win32_synthetic_pointer_api();
      return api;
    }

    bool synthetic_pointer_available(const SyntheticPointerApi &api) {
      return api.create && api.inject && api.destroy;
    }

    OperationStatus unsupported_profile_status(std::string message) {
      return OperationStatus::failure(ErrorCode::unsupported_profile, std::move(message));
    }

    constexpr GUID control_device_interface_guid {
      0x3890af65,
      0x2da0,
      0x443c,
      {0x84, 0xff, 0x6e, 0x70, 0xe8, 0x41, 0xba, 0x1e}
    };

    UniqueHandle make_unique_handle(HANDLE handle) {
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

      std::string message;
      if (message_size > 0U) {
        message.assign(message_buffer.data(), message_size);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
          message.pop_back();
        }
      } else {
        std::ostringstream fallback;
        fallback << "Windows error " << error_code;
        message = fallback.str();
      }

      return message;
    }

    OperationStatus windows_failure(ErrorCode code, std::string_view operation, DWORD error_code) {
      std::ostringstream message;
      message << operation << ": " << windows_error_message(error_code);
      return OperationStatus::failure(code, message.str());
    }

    UniqueHandle &overlapped_device_io_event() {
      thread_local UniqueHandle operation_event {nullptr, &::CloseHandle};
      if (!operation_event) {
        operation_event = make_unique_handle(::CreateEventA(nullptr, TRUE, FALSE, nullptr));
      }

      return operation_event;
    }

    template<typename CancelOperation, typename FinishOperation>
    void cancel_and_drain_overlapped_io(
      OVERLAPPED &overlapped,
      DWORD *bytes_returned,
      CancelOperation &&cancel_operation,
      FinishOperation &&finish_operation
    ) {
      static_cast<void>(std::forward<CancelOperation>(cancel_operation)(overlapped));
      static_cast<void>(std::forward<FinishOperation>(finish_operation)(overlapped, bytes_returned, TRUE));
    }

    template<typename StartOperation, typename FinishOperation>
    OperationStatus run_overlapped_device_io(
      std::string_view operation,
      DWORD *bytes_returned,
      StartOperation &&start_operation,
      FinishOperation &&finish_operation
    ) {
      const auto &operation_event = overlapped_device_io_event();
      if (!operation_event) {
        return windows_failure(ErrorCode::backend_failure, operation, ::GetLastError());
      }
      if (::ResetEvent(operation_event.get()) == FALSE) {
        return windows_failure(ErrorCode::backend_failure, operation, ::GetLastError());
      }

      OVERLAPPED overlapped {};
      overlapped.hEvent = operation_event.get();
      if (std::forward<StartOperation>(start_operation)(overlapped, bytes_returned) != FALSE) {
        return OperationStatus::success();
      }

      if (const auto start_error = ::GetLastError(); start_error != ERROR_IO_PENDING) {
        return windows_failure(ErrorCode::backend_failure, operation, start_error);
      }

      if (std::forward<FinishOperation>(finish_operation)(overlapped, bytes_returned, TRUE) == FALSE) {
        return windows_failure(ErrorCode::backend_failure, operation, ::GetLastError());
      }

      return OperationStatus::success();
    }

    template<typename Submit>
    OperationStatus submit_with_desktop_retry(Submit submit, std::string_view operation) {
      using enum ErrorCode;

      if (submit()) {
        return OperationStatus::success();
      }

      auto error_code = ::GetLastError();
      auto &known_desktop = last_known_input_desktop();
      if (const auto desktop = sync_thread_desktop_function()(); known_desktop.value != desktop) {
        known_desktop.value = desktop;
        if (submit()) {
          return OperationStatus::success();
        }
        error_code = ::GetLastError();
      }

      return windows_failure(backend_failure, operation, error_code);
    }

    OperationStatus send_input(std::span<INPUT> inputs, std::string_view operation) {
      return submit_with_desktop_retry([&inputs] {
        return send_input_function()(inputs) == static_cast<UINT>(inputs.size());
      },
                                       operation);
    }

    OperationStatus send_input(const INPUT &input, std::string_view operation) {
      std::array inputs {input};
      return send_input(std::span<INPUT> {inputs}, operation);
    }

    OperationStatus inject_synthetic_pointer_input(
      const SyntheticPointerApi &api,
      HSYNTHETICPOINTERDEVICE device,
      const POINTER_TYPE_INFO *pointer_info,
      UINT32 count,
      std::string_view operation
    ) {
      using enum ErrorCode;

      if (!synthetic_pointer_available(api)) {
        return OperationStatus::failure(backend_unavailable, "Windows synthetic pointer APIs are unavailable");
      }
      return submit_with_desktop_retry([&api, device, pointer_info, count] {
        return api.inject(device, pointer_info, count) != FALSE;
      },
                                       operation);
    }

    std::vector<std::string> enumerate_control_device_interface_paths() {
      std::vector<std::string> paths;

      const auto device_info_set = ::SetupDiGetClassDevsA(
        &control_device_interface_guid,
        nullptr,
        nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
      );
      if (device_info_set == INVALID_HANDLE_VALUE) {
        return paths;
      }

      const auto cleanup = std::unique_ptr<void, decltype(&::SetupDiDestroyDeviceInfoList)> {
        device_info_set,
        &::SetupDiDestroyDeviceInfoList
      };

      for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data {};
        interface_data.cbSize = sizeof(interface_data);
        if (::SetupDiEnumDeviceInterfaces(device_info_set, nullptr, &control_device_interface_guid, index, &interface_data) == FALSE) {
          break;
        }

        DWORD required_size = 0;
        static_cast<void>(::SetupDiGetDeviceInterfaceDetailA(
          device_info_set,
          &interface_data,
          nullptr,
          0,
          &required_size,
          nullptr
        ));
        if (required_size == 0U || ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
          continue;
        }

        auto buffer = std::make_unique_for_overwrite<std::byte[]>(required_size);
        auto *detail_data = static_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A *>(static_cast<void *>(buffer.get()));
        detail_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (::SetupDiGetDeviceInterfaceDetailA(device_info_set, &interface_data, detail_data, required_size, nullptr, nullptr) != FALSE) {
          paths.emplace_back(detail_data->DevicePath);
        }
      }

      return paths;
    }

    DWORD mouse_button_flags(MouseButton button, bool pressed) {
      switch (button) {
        using enum MouseButton;

        case left:
          return pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        case middle:
          return pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        case right:
          return pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        case side:
        case extra:
          return pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
      }

      return 0;
    }

    DWORD mouse_button_data(MouseButton button) {
      switch (button) {
        using enum MouseButton;

        case side:
          return XBUTTON1;
        case extra:
          return XBUTTON2;
        case left:
        case middle:
        case right:
          return 0;
      }

      return 0;
    }

    LONG scale_absolute_axis(float value, std::int32_t dimension) {
      if (dimension <= 1) {
        return 0;
      }

      const auto clamped = std::clamp(value, 0.0F, static_cast<float>(dimension));
      const auto scaled = clamped * static_cast<float>(std::numeric_limits<std::uint16_t>::max()) / static_cast<float>(dimension);
      return static_cast<LONG>(std::lround(scaled));
    }

    PointerViewport resolve_pointer_viewport(PointerViewport viewport) {
      if (viewport.width > 0 && viewport.height > 0) {
        return viewport;
      }

      viewport.offset_x = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
      viewport.offset_y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
      viewport.width = std::max(1, ::GetSystemMetrics(SM_CXVIRTUALSCREEN));
      viewport.height = std::max(1, ::GetSystemMetrics(SM_CYVIRTUALSCREEN));
      return viewport;
    }

    POINT pointer_location(const PointerViewport &raw_viewport, float x, float y) {
      const auto viewport = resolve_pointer_viewport(raw_viewport);
      return {
        .x = viewport.offset_x + static_cast<LONG>(std::lround(std::clamp(x, 0.0F, 1.0F) * static_cast<float>(viewport.width))),
        .y = viewport.offset_y + static_cast<LONG>(std::lround(std::clamp(y, 0.0F, 1.0F) * static_cast<float>(viewport.height))),
      };
    }

    void update_pointer_location(POINTER_INFO &pointer_info, const PointerViewport &viewport, float x, float y) {
      pointer_info.ptPixelLocation = pointer_location(viewport, x, y);
    }

    bool extended_key(KeyboardKeyCode key_code) {
      switch (key_code) {
        case VK_LWIN:
        case VK_RWIN:
        case VK_RMENU:
        case VK_RCONTROL:
        case VK_INSERT:
        case VK_DELETE:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_UP:
        case VK_DOWN:
        case VK_LEFT:
        case VK_RIGHT:
        case VK_DIVIDE:
        case VK_APPS:
          return true;
        default:
          return false;
      }
    }

    bool can_map_virtual_key_to_scan_code(KeyboardKeyCode key_code) {
      return key_code != VK_LWIN && key_code != VK_RWIN && key_code != VK_PAUSE;
    }

    constexpr auto synthetic_pointer_repeat_interval = std::chrono::milliseconds {50};  ///< Active pointer refresh interval.
    constexpr auto pointer_edge_triggered_flags = POINTER_FLAG_DOWN | POINTER_FLAG_UP | POINTER_FLAG_CANCELED | POINTER_FLAG_UPDATE;  ///< One-frame pointer flags.

    std::vector<std::string> resolve_control_device_paths() {
      constexpr auto environment_name = "LIBVIRTUALHID_WINDOWS_CONTROL_DEVICE";
      if (std::string override_path; lizardbyte::common::get_env(environment_name, override_path) && !override_path.empty()) {
        return {override_path};
      }

      auto paths = enumerate_control_device_interface_paths();
      paths.emplace_back(windows::default_control_device_path);
      paths.emplace_back(windows::global_control_device_path);
      return paths;
    }

    OperationStatus protocol_status(std::uint32_t status, std::string_view operation) {
      using enum ErrorCode;

      switch (status) {
        case LVH_WINDOWS_STATUS_SUCCESS:
          return OperationStatus::success();
        case LVH_WINDOWS_STATUS_INVALID_ARGUMENT:
          return OperationStatus::failure(invalid_argument, std::string {operation});
        case LVH_WINDOWS_STATUS_UNSUPPORTED_PROFILE:
          return OperationStatus::failure(unsupported_profile, std::string {operation});
        case LVH_WINDOWS_STATUS_DEVICE_NOT_FOUND:
          return OperationStatus::failure(device_closed, std::string {operation});
        case LVH_WINDOWS_STATUS_BACKEND_FAILURE:
        default:
          return OperationStatus::failure(backend_failure, std::string {operation});
      }
    }

    OperationStatus validate_windows_gamepad_profile(const DeviceProfile &profile) {
      using enum ErrorCode;

      if (profile.report_descriptor.size() > LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE) {
        return OperationStatus::failure(
          invalid_argument,
          "Windows gamepad HID descriptor exceeds control protocol limit"
        );
      }
      if (profile.input_report_size > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
        return OperationStatus::failure(
          invalid_argument,
          "Windows gamepad input report exceeds control protocol limit"
        );
      }
      if (profile.output_report_size > LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE) {
        return OperationStatus::failure(
          invalid_argument,
          "Windows gamepad output report exceeds control protocol limit"
        );
      }

      return OperationStatus::success();
    }

    bool mouse_driver_fallback_allowed(ErrorCode code) {
      switch (code) {
        using enum ErrorCode;

        case backend_unavailable:
        case license_required:
        case license_invalid:
        case activation_limit_reached:
        case network_unavailable:
          return true;
        default:
          return false;
      }
    }

    class WindowsControlChannel {
    public:
      WindowsControlChannel(const WindowsControlChannel &) = delete;
      WindowsControlChannel &operator=(const WindowsControlChannel &) = delete;
      WindowsControlChannel(WindowsControlChannel &&) noexcept = delete;
      WindowsControlChannel &operator=(WindowsControlChannel &&) noexcept = delete;

      virtual ~WindowsControlChannel() = default;

      virtual const std::string &path() const = 0;

      virtual HANDLE native_handle() const {
        return nullptr;
      }

      virtual OperationStatus create_device(
        const LvhWindowsCreateDeviceRequest &request,
        LvhWindowsCreateDeviceResponse &response
      ) const = 0;

      virtual OperationStatus destroy_device(
        std::uint64_t driver_device_id,
        const LvhWindowsSessionToken &session_token
      ) const = 0;

      virtual OperationStatus submit_input_report(
        std::uint64_t driver_device_id,
        const LvhWindowsSessionToken &session_token,
        const std::vector<std::uint8_t> &report
      ) const = 0;

      virtual std::optional<LvhWindowsOutputReportEvent> read_output_report(HANDLE stop_event) const = 0;

    protected:
      WindowsControlChannel() = default;
    };

    class Win32WindowsControlChannel final: public WindowsControlChannel {
    public:
      struct SharedHandle {
        explicit SharedHandle(UniqueHandle value):
            value {std::move(value)} {}

        UniqueHandle value;
      };

      static std::pair<std::unique_ptr<WindowsControlChannel>, std::unique_ptr<WindowsControlChannel>> open_pair(
        const std::string &path
      ) {
        const auto handle = ::CreateFileA(
          path.c_str(),
          GENERIC_READ | GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE,
          nullptr,
          OPEN_EXISTING,
          FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
          nullptr
        );

        if (handle == INVALID_HANDLE_VALUE) {
          return {};
        }

        auto shared_handle = std::make_shared<SharedHandle>(make_unique_handle(handle));
        return {
          std::make_unique<Win32WindowsControlChannel>(path, shared_handle),
          std::make_unique<Win32WindowsControlChannel>(path, std::move(shared_handle)),
        };
      }

      const std::string &path() const override {
        return path_;
      }

      HANDLE native_handle() const override {
        return handle_->value.get();
      }

      OperationStatus create_device(
        const LvhWindowsCreateDeviceRequest &request,
        LvhWindowsCreateDeviceResponse &response
      ) const override {
        using enum ErrorCode;

        auto request_copy = request;
        DWORD bytes_returned = 0;
        if (const auto status = device_io_control(LVH_WINDOWS_IOCTL_CREATE_DEVICE, request_copy, response, &bytes_returned, "create Windows virtual HID device"); !status.ok()) {
          return status;
        }

        if (bytes_returned < sizeof(response)) {
          return OperationStatus::failure(backend_failure, "Windows driver returned a truncated device response");
        }

        return protocol_status(response.status, "Windows driver rejected virtual HID device creation");
      }

      OperationStatus destroy_device(
        std::uint64_t driver_device_id,
        const LvhWindowsSessionToken &session_token
      ) const override {
        auto request = windows::make_destroy_device_request(driver_device_id, session_token);
        DWORD bytes_returned = 0;
        return device_io_control(
          LVH_WINDOWS_IOCTL_DESTROY_DEVICE,
          request,
          &bytes_returned,
          "destroy Windows virtual HID device"
        );
      }

      OperationStatus submit_input_report(
        std::uint64_t driver_device_id,
        const LvhWindowsSessionToken &session_token,
        const std::vector<std::uint8_t> &report
      ) const override {
        using enum ErrorCode;

        if (report.size() > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
          return OperationStatus::failure(invalid_argument, "input report exceeds Windows control protocol limit");
        }

        auto request = windows::make_submit_input_report_request(driver_device_id, session_token, report);
        DWORD bytes_returned = 0;
        return device_io_control(
          LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT,
          request,
          &bytes_returned,
          "submit Windows input report"
        );
      }

      std::optional<LvhWindowsOutputReportEvent> read_output_report(HANDLE stop_event) const override {
        LvhWindowsOutputReportEvent event {};
        event.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
        event.size = sizeof(event);

        auto operation_event = make_unique_handle(::CreateEventA(nullptr, TRUE, FALSE, nullptr));
        if (!operation_event) {
          return std::nullopt;
        }

        OVERLAPPED overlapped {};
        overlapped.hEvent = operation_event.get();
        DWORD bytes_returned = 0;
        const auto cancel_and_drain = [this, &overlapped, &bytes_returned] {
          cancel_and_drain_overlapped_io(
            overlapped,
            &bytes_returned,
            [this](OVERLAPPED &pending) {
              return ::CancelIoEx(handle_->value.get(), &pending);
            },
            [this](OVERLAPPED &pending, DWORD *result_size, BOOL wait) {
              return ::GetOverlappedResult(handle_->value.get(), &pending, result_size, wait);
            }
          );
        };

        if (const auto started = ::DeviceIoControl(handle_->value.get(), LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT, nullptr, 0, &event, sizeof(event), &bytes_returned, &overlapped); started == FALSE) {
          if (const auto error_code = ::GetLastError(); error_code != ERROR_IO_PENDING) {
            return std::nullopt;
          }

          std::array<HANDLE, 2> wait_handles {
            operation_event.get(),
            stop_event,
          };
          const auto wait_result = ::WaitForMultipleObjects(
            static_cast<DWORD>(wait_handles.size()),
            wait_handles.data(),
            FALSE,
            INFINITE
          );
          if (wait_result == WAIT_OBJECT_0 + 1U) {
            cancel_and_drain();
            return std::nullopt;
          }
          if (wait_result != WAIT_OBJECT_0) {
            cancel_and_drain();
            return std::nullopt;
          }
        }

        if (::GetOverlappedResult(handle_->value.get(), &overlapped, &bytes_returned, FALSE) == FALSE) {
          return std::nullopt;
        }

        if (constexpr auto event_header_size = sizeof(event.version) + sizeof(event.size) + sizeof(event.driver_device_id) + sizeof(event.report_size); bytes_returned < event_header_size) {
          return std::nullopt;
        }

        event.report_size = std::min(event.report_size, static_cast<std::uint32_t>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE));
        return event;
      }

      Win32WindowsControlChannel(std::string path, std::shared_ptr<SharedHandle> handle):
          path_ {std::move(path)},
          handle_ {std::move(handle)} {}

    private:
      template<typename Input, typename Output>
      OperationStatus device_io_control(
        DWORD control_code,
        Input &input,
        Output &output,
        DWORD *bytes_returned,
        std::string_view operation
      ) const {
        return run_overlapped_device_io(
          operation,
          bytes_returned,
          [this, control_code, &input, &output](OVERLAPPED &overlapped, DWORD *result_size) {
            return ::DeviceIoControl(
              handle_->value.get(),
              control_code,
              &input,
              sizeof(input),
              &output,
              sizeof(output),
              result_size,
              &overlapped
            );
          },
          [this](OVERLAPPED &overlapped, DWORD *result_size, BOOL wait) {
            return ::GetOverlappedResult(handle_->value.get(), &overlapped, result_size, wait);
          }
        );
      }

      template<typename Input>
      OperationStatus device_io_control(
        DWORD control_code,
        Input &input,
        DWORD *bytes_returned,
        std::string_view operation
      ) const {
        return run_overlapped_device_io(
          operation,
          bytes_returned,
          [this, control_code, &input](OVERLAPPED &overlapped, DWORD *result_size) {
            return ::DeviceIoControl(
              handle_->value.get(),
              control_code,
              &input,
              sizeof(input),
              nullptr,
              0,
              result_size,
              &overlapped
            );
          },
          [this](OVERLAPPED &overlapped, DWORD *result_size, BOOL wait) {
            return ::GetOverlappedResult(handle_->value.get(), &overlapped, result_size, wait);
          }
        );
      }

      std::string path_;
      std::shared_ptr<SharedHandle> handle_;
    };

    struct WindowsControlChannels {
      std::unique_ptr<WindowsControlChannel> command;
      std::unique_ptr<WindowsControlChannel> event;
    };

    WindowsControlChannels open_control_channels() {
      for (const auto &path : resolve_control_device_paths()) {
        auto [command, event] = Win32WindowsControlChannel::open_pair(path);
        if (command && event) {
          return {std::move(command), std::move(event)};
        }
      }

      return {};
    }

    class BrokeredWindowsControlChannel final: public WindowsControlChannel {
    public:
      explicit BrokeredWindowsControlChannel(std::unique_ptr<WindowsControlChannel> direct_channel):
          direct_channel_ {std::move(direct_channel)} {}

      static std::unique_ptr<WindowsControlChannel> open(std::unique_ptr<WindowsControlChannel> direct_channel) {
        if (!direct_channel) {
          return nullptr;
        }

        auto brokered_channel = std::make_unique<BrokeredWindowsControlChannel>(std::move(direct_channel));
        if (!brokered_channel->broker_available()) {
          return nullptr;
        }

        return brokered_channel;
      }

      const std::string &path() const override {
        return direct_channel_->path();
      }

      HANDLE native_handle() const override {
        return direct_channel_->native_handle();
      }

      OperationStatus create_device(
        const LvhWindowsCreateDeviceRequest &request,
        LvhWindowsCreateDeviceResponse &response
      ) const override {
        LvhWindowsBrokerCreateDeviceRequest broker_request {};
        broker_request.header = windows_broker::make_request_header(
          LvhWindowsBrokerRequestType::create_device,
          sizeof(broker_request)
        );
        broker_request.client_control_handle = static_cast<std::uint64_t>(
          reinterpret_cast<std::uintptr_t>(direct_channel_->native_handle())
        );
        broker_request.device = request;

        LvhWindowsBrokerCreateDeviceResponse broker_response {};
        if (const auto status = windows_broker::call(broker_request, broker_response, "create Windows virtual HID device through broker"); !status.ok()) {
          return status;
        }

        response = broker_response.device;
        return protocol_status(response.status, "Windows driver rejected virtual HID device creation");
      }

      OperationStatus destroy_device(
        std::uint64_t driver_device_id,
        const LvhWindowsSessionToken &session_token
      ) const override {
        LvhWindowsBrokerDestroyDeviceRequest broker_request {};
        broker_request.header = windows_broker::make_request_header(
          LvhWindowsBrokerRequestType::destroy_device,
          sizeof(broker_request)
        );
        broker_request.device = windows::make_destroy_device_request(driver_device_id, session_token);

        LvhWindowsBrokerDestroyDeviceResponse broker_response {};
        return windows_broker::call(broker_request, broker_response, "destroy Windows virtual HID device through broker");
      }

      OperationStatus submit_input_report(
        std::uint64_t driver_device_id,
        const LvhWindowsSessionToken &session_token,
        const std::vector<std::uint8_t> &report
      ) const override {
        return direct_channel_->submit_input_report(driver_device_id, session_token, report);
      }

      std::optional<LvhWindowsOutputReportEvent> read_output_report(HANDLE stop_event) const override {
        return direct_channel_->read_output_report(stop_event);
      }

    private:
      bool broker_available() const {
        LvhWindowsBrokerStatusRequest request {};
        request.header = windows_broker::make_request_header(
          LvhWindowsBrokerRequestType::status,
          sizeof(request)
        );

        LvhWindowsBrokerStatusResponse response {};
        return windows_broker::call(request, response, "query Windows broker status").ok();
      }

      std::unique_ptr<WindowsControlChannel> direct_channel_;
    };

    WindowsControlChannels open_brokered_control_channels() {
      auto channels = open_control_channels();
      channels.command = BrokeredWindowsControlChannel::open(std::move(channels.command));
      return channels;
    }

    class WindowsVhfDeviceState {
    public:
      WindowsVhfDeviceState(
        DeviceId client_device_id,
        std::uint64_t driver_device_id,
        const LvhWindowsSessionToken &session_token,
        DeviceProfile device_profile,
        std::string device_path
      ):
          client_id {client_device_id},
          driver_id {driver_device_id},
          token {session_token},
          profile {std::move(device_profile)},
          path {std::move(device_path)} {
        if (profile.gamepad_kind == GamepadProfileKind::generic && profile.capabilities.supports_rumble) {
          uses_generic_pid = !windows::make_generic_pid_report_descriptor(profile.report_descriptor).empty();
        }
      }

    private:
      friend class WindowsBackendContext;
      friend class WindowsGamepad;
      friend class WindowsHidMouse;

      std::mutex output_dispatch_mutex_;
      mutable std::mutex mutex_;
      DeviceId client_id;
      std::uint64_t driver_id;
      LvhWindowsSessionToken token {};
      DeviceProfile profile;
      std::string path;
      bool open = true;
      OutputCallback output_callback;
      bool uses_generic_pid = false;
      windows::GenericPidRumbleState generic_pid_rumble;
    };

    class WindowsGamepad final: public BackendGamepad {
    public:
      WindowsGamepad(std::shared_ptr<WindowsBackendContext> context, std::shared_ptr<WindowsVhfDeviceState> state):
          context_ {std::move(context)},
          state_ {std::move(state)} {}

      OperationStatus submit(
        const GamepadState &state,
        const std::vector<std::uint8_t> &report
      ) override;
      void set_output_callback(OutputCallback callback) override;
      std::vector<DeviceNode> device_nodes() const override;
      OperationStatus close() override;

    private:
      std::shared_ptr<WindowsBackendContext> context_;
      std::shared_ptr<WindowsVhfDeviceState> state_;
    };

    class WindowsBackendContext: public std::enable_shared_from_this<WindowsBackendContext> {
    public:
      WindowsBackendContext(
        std::unique_ptr<WindowsControlChannel> command_channel,
        std::unique_ptr<WindowsControlChannel> event_channel
      ):
          command_channel_ {std::move(command_channel)},
          event_channel_ {std::move(event_channel)} {}

      WindowsBackendContext(const WindowsBackendContext &) = delete;
      WindowsBackendContext &operator=(const WindowsBackendContext &) = delete;

      ~WindowsBackendContext() {
        stop();
      }

      bool valid() const {
        return command_channel_ != nullptr && event_channel_ != nullptr && static_cast<bool>(stop_event_);
      }

      void start() {
        if (!valid() || output_thread_.joinable()) {
          return;
        }

        output_thread_ = std::jthread {[this](std::stop_token stop_token) {
          output_loop(stop_token);
        }};
        pid_timer_thread_ = std::jthread {[this](std::stop_token stop_token) {
          pid_timer_loop(stop_token);
        }};
      }

      void stop() {
        if (stop_event_) {
          static_cast<void>(::SetEvent(stop_event_.get()));
        }

        if (output_thread_.joinable()) {
          output_thread_.request_stop();
        }
        if (pid_timer_thread_.joinable()) {
          pid_timer_thread_.request_stop();
        }
        notify_pid_timer();

        if (output_thread_.joinable()) {
          output_thread_.join();
        }
        if (pid_timer_thread_.joinable()) {
          pid_timer_thread_.join();
        }
      }

      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) {
        auto request = windows::make_create_device_request(id, options);
        LvhWindowsCreateDeviceResponse response {};
        response.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
        response.size = sizeof(response);

        if (const auto status = command_channel_->create_device(request, response); !status.ok()) {
          return {status, nullptr};
        }

        auto state = std::make_shared<WindowsVhfDeviceState>(
          id,
          response.driver_device_id,
          response.session_token,
          options.profile,
          response.device_path[0] == '\0' ? command_channel_->path() : std::string {response.device_path.data()}
        );

        {
          std::lock_guard lock {devices_mutex_};
          devices_[state->driver_id] = state;
        }
        notify_pid_timer();

        auto gamepad = std::make_unique<WindowsGamepad>(shared_from_this(), std::move(state));
        return {OperationStatus::success(), std::move(gamepad)};
      }

      /**
       * @brief Create a driver-backed HID mouse.
       *
       * Defined out of line because it constructs `WindowsHidMouse`, which is
       * declared after this class.
       *
       * @param id Client device identity.
       * @param options Mouse creation options.
       * @return Creation result; the device is null when the driver declines.
       */
      BackendMouseCreationResult create_hid_mouse(DeviceId id, const CreateMouseOptions &options);

      OperationStatus submit_device_report(
        const std::shared_ptr<WindowsVhfDeviceState> &state,
        const std::vector<std::uint8_t> &report
      ) const {
        const auto driver_id = state->driver_id;
        const auto token = state->token;
        return command_channel_->submit_input_report(driver_id, token, report);
      }

      OperationStatus close_device(const std::shared_ptr<WindowsVhfDeviceState> &state) {
        std::uint64_t driver_id = 0;
        LvhWindowsSessionToken token {};
        {
          std::lock_guard lock {state->mutex_};
          if (!state->open) {
            return OperationStatus::success();
          }

          state->open = false;
          driver_id = state->driver_id;
          token = state->token;
        }

        {
          std::lock_guard lock {devices_mutex_};
          devices_.erase(driver_id);
        }
        notify_pid_timer();

        return command_channel_->destroy_device(driver_id, token);
      }

    private:
      using PidClock = windows::GenericPidRumbleState::Clock;
      using PidTimePoint = windows::GenericPidRumbleState::TimePoint;

      void output_loop(std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
          auto event = event_channel_->read_output_report(stop_event_.get());
          if (!event.has_value()) {
            continue;
          }

          dispatch_output_report(*event);
        }
      }

      void pid_timer_loop(std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
          const auto generation = pid_timer_generation_.load();
          const auto next_transition = dispatch_pid_transitions();
          std::unique_lock lock {pid_timer_mutex_};
          const auto interrupted = [this, generation, &stop_token] {
            return stop_token.stop_requested() || pid_timer_generation_.load() != generation;
          };
          if (next_transition.has_value()) {
            static_cast<void>(pid_timer_ready_.wait_until(lock, *next_transition, interrupted));
          } else {
            pid_timer_ready_.wait(lock, interrupted);
          }
        }
      }

      std::optional<PidTimePoint> dispatch_pid_transitions() {
        std::vector<std::shared_ptr<WindowsVhfDeviceState>> states;
        {
          std::lock_guard lock {devices_mutex_};
          states.reserve(devices_.size());
          for (const auto &[driver_id, weak_state] : devices_) {
            static_cast<void>(driver_id);
            if (auto state = weak_state.lock(); state) {
              states.push_back(std::move(state));
            }
          }
        }

        const auto now = PidClock::now();
        std::optional<PidTimePoint> next_transition;
        for (const auto &state : states) {
          std::unique_lock dispatch_lock {state->output_dispatch_mutex_};
          OutputCallback callback;
          windows::GenericPidRumbleUpdate update;
          {
            std::lock_guard lock {state->mutex_};
            if (!state->open || !state->uses_generic_pid) {
              continue;
            }

            update = state->generic_pid_rumble.advance(now);
            if (const auto candidate = state->generic_pid_rumble.next_transition(); candidate.has_value() && (!next_transition.has_value() || *candidate < *next_transition)) {
              next_transition = candidate;
            }
            callback = state->output_callback;
          }

          if (callback && update.rumble_changed) {
            GamepadOutput output;
            output.kind = GamepadOutputKind::rumble;
            output.low_frequency_rumble = update.strength;
            output.high_frequency_rumble = update.strength;
            callback(output);
          }
        }
        return next_transition;
      }

      void notify_pid_timer() {
        {
          std::lock_guard lock {pid_timer_mutex_};
          pid_timer_generation_.fetch_add(1U);
        }
        pid_timer_ready_.notify_one();
      }

      void dispatch_output_report(const LvhWindowsOutputReportEvent &event) {
        std::shared_ptr<WindowsVhfDeviceState> state;
        {
          std::lock_guard lock {devices_mutex_};
          if (const auto iter = devices_.find(event.driver_device_id); iter != devices_.end()) {
            state = iter->second.lock();
          }
        }

        if (!state) {
          return;
        }

        std::unique_lock dispatch_lock {state->output_dispatch_mutex_};
        std::vector<std::uint8_t> report(
          event.report.data(),
          event.report.data() + std::min(event.report_size, static_cast<std::uint32_t>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE))
        );

        DeviceProfile profile;
        OutputCallback callback;
        std::optional<windows::GenericPidRumbleUpdate> pid_update;
        {
          std::lock_guard lock {state->mutex_};
          if (!state->open || !state->output_callback) {
            return;
          }

          profile = state->profile;
          callback = state->output_callback;
          if (state->uses_generic_pid) {
            pid_update = state->generic_pid_rumble.handle_output_report(report);
          }
        }

        if (pid_update.has_value() && pid_update->recognized) {
          notify_pid_timer();
          GamepadOutput output;
          output.raw_report = report;
          if (pid_update->rumble_changed) {
            output.kind = GamepadOutputKind::rumble;
            output.low_frequency_rumble = pid_update->strength;
            output.high_frequency_rumble = pid_update->strength;
          }
          callback(output);
          return;
        }

        for (const auto &output : reports::parse_output_reports(profile, report)) {
          callback(output);
        }
      }

      std::unique_ptr<WindowsControlChannel> command_channel_;
      std::unique_ptr<WindowsControlChannel> event_channel_;
      UniqueHandle stop_event_ {make_unique_handle(::CreateEventA(nullptr, TRUE, FALSE, nullptr))};
      std::jthread output_thread_;
      std::jthread pid_timer_thread_;
      std::condition_variable pid_timer_ready_;
      std::mutex pid_timer_mutex_;
      std::atomic_uint64_t pid_timer_generation_ = 0;
      std::mutex devices_mutex_;
      std::map<std::uint64_t, std::weak_ptr<WindowsVhfDeviceState>> devices_;
    };

    OperationStatus WindowsGamepad::submit(
      const GamepadState & /*state*/,
      const std::vector<std::uint8_t> &report
    ) {
      using enum ErrorCode;

      {
        std::lock_guard lock {state_->mutex_};
        if (!state_->open) {
          return OperationStatus::failure(device_closed, "Windows gamepad is closed");
        }

        if (report.size() > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
          return OperationStatus::failure(invalid_argument, "Windows gamepad input report exceeds protocol limit");
        }

        if (state_->uses_generic_pid) {
          return context_->submit_device_report(state_, windows::make_generic_windows_input_report(report));
        }
      }

      return context_->submit_device_report(state_, report);
    }

    void WindowsGamepad::set_output_callback(OutputCallback callback) {
      std::lock_guard lock {state_->mutex_};
      state_->output_callback = std::move(callback);
    }

    std::vector<DeviceNode> WindowsGamepad::device_nodes() const {
      std::lock_guard lock {state_->mutex_};
      if (state_->path.empty()) {
        return {};
      }

      return {{.kind = DeviceNodeKind::other, .path = state_->path}};
    }

    OperationStatus WindowsGamepad::close() {
      return context_->close_device(state_);
    }

    class WindowsKeyboard final: public BackendKeyboard {
    public:
      OperationStatus submit(const KeyboardEvent &event) override {
        using enum ErrorCode;

        if (!open_) {
          return OperationStatus::failure(device_closed, "Windows keyboard is closed");
        }

        INPUT input {};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = event.key_code;
        if (event.scan_code != 0U) {
          input.ki.wVk = 0;
          input.ki.wScan = event.scan_code;
          input.ki.dwFlags |= KEYEVENTF_SCANCODE;
        } else {
          if (event.uses_normalized_key_code) {
            input.ki.wScan = windows_us_english_scan_code(event.key_code);
          } else if (event.prefer_native_scan_code && can_map_virtual_key_to_scan_code(event.key_code)) {
            input.ki.wScan = static_cast<WORD>(::MapVirtualKeyW(event.key_code, MAPVK_VK_TO_VSC));
          }

          if (input.ki.wScan != 0U) {
            input.ki.wVk = 0;
            input.ki.dwFlags |= KEYEVENTF_SCANCODE;
          }
        }
        if (extended_key(event.key_code)) {
          input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        }
        if (!event.pressed) {
          input.ki.dwFlags |= KEYEVENTF_KEYUP;
        }

        return send_input(input, "submit Windows keyboard input");
      }

      OperationStatus type_text(const KeyboardTextEvent &event) override {
        using enum ErrorCode;

        if (!open_) {
          return OperationStatus::failure(device_closed, "Windows keyboard is closed");
        }
        if (event.text.empty()) {
          return OperationStatus::success();
        }

        const auto required_size = ::MultiByteToWideChar(
          CP_UTF8,
          MB_ERR_INVALID_CHARS,
          event.text.data(),
          static_cast<int>(event.text.size()),
          nullptr,
          0
        );
        if (required_size <= 0) {
          return windows_failure(invalid_argument, "convert UTF-8 text for Windows keyboard input", ::GetLastError());
        }

        std::vector<WCHAR> wide_text(static_cast<std::size_t>(required_size));
        const auto converted_size = ::MultiByteToWideChar(
          CP_UTF8,
          MB_ERR_INVALID_CHARS,
          event.text.data(),
          static_cast<int>(event.text.size()),
          wide_text.data(),
          required_size
        );
        if (converted_size <= 0) {
          return windows_failure(invalid_argument, "convert UTF-8 text for Windows keyboard input", ::GetLastError());
        }

        std::vector<INPUT> inputs;
        inputs.reserve(static_cast<std::size_t>(converted_size) * 2U);
        for (const auto character : wide_text) {
          INPUT input {};
          input.type = INPUT_KEYBOARD;
          input.ki.wScan = character;
          input.ki.dwFlags = KEYEVENTF_UNICODE;
          inputs.push_back(input);
        }
        for (const auto character : wide_text) {
          INPUT input {};
          input.type = INPUT_KEYBOARD;
          input.ki.wScan = character;
          input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
          inputs.push_back(input);
        }

        return send_input(std::span<INPUT> {inputs}, "submit Windows keyboard text input");
      }

      OperationStatus close() override {
        open_ = false;
        return OperationStatus::success();
      }

    private:
      bool open_ = true;
    };

    class WindowsMouse final: public BackendMouse {
    public:
      OperationStatus submit(const MouseEvent &event) override {
        using enum ErrorCode;

        if (!open_) {
          return OperationStatus::failure(device_closed, "Windows mouse is closed");
        }

        INPUT input {};
        input.type = INPUT_MOUSE;
        auto &mouse = input.mi;

        switch (event.kind) {
          using enum MouseEventKind;

          case relative_motion:
            mouse.dwFlags = MOUSEEVENTF_MOVE;
            mouse.dx = event.x;
            mouse.dy = event.y;
            break;
          case absolute_motion:
            mouse.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
            mouse.dx = scale_absolute_axis(
              event.has_fractional_absolute_coordinates ? event.absolute_x : static_cast<float>(event.x),
              event.width
            );
            mouse.dy = scale_absolute_axis(
              event.has_fractional_absolute_coordinates ? event.absolute_y : static_cast<float>(event.y),
              event.height
            );
            break;
          case button:
            mouse.dwFlags = mouse_button_flags(event.button, event.pressed);
            mouse.mouseData = mouse_button_data(event.button);
            break;
          case vertical_scroll:
            mouse.dwFlags = MOUSEEVENTF_WHEEL;
            mouse.mouseData = static_cast<DWORD>(event.high_resolution_scroll);
            break;
          case horizontal_scroll:
            mouse.dwFlags = MOUSEEVENTF_HWHEEL;
            mouse.mouseData = static_cast<DWORD>(event.high_resolution_scroll);
            break;
        }

        return send_input(input, "submit Windows mouse input");
      }

      OperationStatus close() override {
        open_ = false;
        return OperationStatus::success();
      }

    private:
      bool open_ = true;
    };

    /**
     * @brief Report descriptor for the driver-backed relative mouse.
     *
     * Five buttons, 16-bit relative X/Y, an 8-bit wheel and an 8-bit AC Pan
     * axis. No report ID is declared, matching the seven byte input report
     * emitted by `WindowsHidMouse`.
     *
     * @return Report descriptor bytes.
     */
    std::vector<std::uint8_t> make_mouse_report_descriptor() {
      return {windows::mouse_report_descriptor.begin(), windows::mouse_report_descriptor.end()};
    }

    /**
     * @brief High-resolution scroll units that make up a single wheel detent.
     */
    constexpr std::int64_t mouse_scroll_units_per_detent = 120;

    /**
     * @brief Device profile describing the driver-backed mouse.
     *
     * @param requested Caller-supplied profile whose public identity is kept.
     * @return Effective mouse device profile.
     */
    DeviceProfile make_hid_mouse_profile(const DeviceProfile &requested) {
      auto profile = requested;
      profile.device_type = DeviceType::mouse;
      profile.gamepad_kind = GamepadProfileKind::generic;
      profile.report_id = 0;
      profile.report_descriptor = make_mouse_report_descriptor();
      profile.input_report_size = LVH_WINDOWS_MOUSE_INPUT_REPORT_SIZE;
      profile.output_report_size = 0;
      profile.capabilities = {};
      return profile;
    }

    /**
     * @brief Map a mouse button onto its HID button bit.
     *
     * HID orders the primary buttons left, right, middle, which differs from
     * the `MouseButton` declaration order.
     *
     * @param button Mouse button.
     * @return Bit index, or `std::nullopt` when the button is unmapped.
     */
    std::optional<unsigned> hid_mouse_button_bit(MouseButton button) {
      switch (button) {
        using enum MouseButton;

        case left:
          return 0U;
        case right:
          return 1U;
        case middle:
          return 2U;
        case side:
          return 3U;
        case extra:
          return 4U;
      }

      return std::nullopt;
    }

    /**
     * @brief Mouse backed by a real HID device created through the UMDF driver.
     *
     * Relative motion, buttons and scrolling are delivered as HID input
     * reports, so applications reading the Raw Input API observe them exactly
     * as they would a physical mouse. Absolute motion has no relative HID
     * equivalent and is delegated to the Win32 injection path.
     */
    class WindowsHidMouse final: public BackendMouse {
    public:
      WindowsHidMouse(
        std::shared_ptr<WindowsBackendContext> context,
        std::shared_ptr<WindowsVhfDeviceState> state
      ):
          context_ {std::move(context)},
          state_ {std::move(state)} {}

      OperationStatus submit(const MouseEvent &event) override {
        using enum ErrorCode;

        if (!open_) {
          return OperationStatus::failure(device_closed, "Windows HID mouse is closed");
        }

        switch (event.kind) {
          using enum MouseEventKind;

          case relative_motion:
            return submit_relative_motion(event.x, event.y);
          case absolute_motion:
            // Relative HID reports cannot express absolute positioning.
            return fallback_.submit(event);
          case button:
            return submit_button(event);
          case vertical_scroll:
            return submit_scroll(vertical_scroll_remainder_, event.high_resolution_scroll, true);
          case horizontal_scroll:
            return submit_scroll(horizontal_scroll_remainder_, event.high_resolution_scroll, false);
        }

        return OperationStatus::success();
      }

      std::vector<DeviceNode> device_nodes() const override {
        return {DeviceNode {.path = state_->path}};
      }

      OperationStatus close() override {
        if (!open_) {
          return OperationStatus::success();
        }

        open_ = false;
        return context_->close_device(state_);
      }

    private:
      /**
       * @brief Submit every part of a relative motion delta without clipping.
       *
       * @param x Incoming horizontal delta.
       * @param y Incoming vertical delta.
       * @return Submission status.
       */
      OperationStatus submit_relative_motion(std::int32_t x, std::int32_t y) {
        while (x != 0 || y != 0) {
          const auto report_x = static_cast<std::int16_t>(std::clamp(
            x,
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())
          ));
          const auto report_y = static_cast<std::int16_t>(std::clamp(
            y,
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::min()),
            static_cast<std::int32_t>(std::numeric_limits<std::int16_t>::max())
          ));
          if (const auto status = emit(report_x, report_y, 0, 0); !status.ok()) {
            return status;
          }
          x -= report_x;
          y -= report_y;
        }
        return OperationStatus::success();
      }

      /**
       * @brief Convert high-resolution scroll units into whole wheel detents.
       *
       * The descriptor exposes a detent-based wheel, so sub-detent movement is
       * carried in @p remainder until it accumulates into a full step.
       *
       * @param remainder Running sub-detent remainder for this axis.
       * @param high_resolution_scroll Incoming high-resolution distance.
       * @param vertical Whether to emit wheel rather than pan reports.
       * @return Submission status.
       */
      OperationStatus submit_scroll(
        std::int64_t &remainder,
        std::int32_t high_resolution_scroll,
        bool vertical
      ) {
        const auto total = remainder + high_resolution_scroll;
        auto detents = total / mouse_scroll_units_per_detent;
        remainder = total % mouse_scroll_units_per_detent;

        while (detents != 0) {
          const auto report_detents = static_cast<std::int8_t>(std::clamp<std::int64_t>(detents, -127, 127));
          const auto status = vertical ? emit(0, 0, report_detents, 0) : emit(0, 0, 0, report_detents);
          if (!status.ok()) {
            return status;
          }
          detents -= report_detents;
        }
        return OperationStatus::success();
      }

      /**
       * @brief Track a button transition and emit the updated button state.
       *
       * @param event Mouse event describing the transition.
       * @return Submission status.
       */
      OperationStatus submit_button(const MouseEvent &event) {
        const auto bit = hid_mouse_button_bit(event.button);
        if (!bit.has_value()) {
          return OperationStatus::success();
        }

        const auto mask = std::byte {1} << *bit;
        buttons_ = event.pressed ? (buttons_ | mask) : (buttons_ & ~mask);

        return emit(0, 0, 0, 0);
      }

      /**
       * @brief Pack and submit a single HID input report.
       *
       * @param x Relative X delta.
       * @param y Relative Y delta.
       * @param wheel Vertical wheel detents.
       * @param pan Horizontal wheel detents.
       * @return Submission status.
       */
      OperationStatus emit(std::int16_t x, std::int16_t y, std::int8_t wheel, std::int8_t pan) {
        std::vector<std::uint8_t> report(LVH_WINDOWS_MOUSE_INPUT_REPORT_SIZE, 0U);
        report[0] = std::to_integer<std::uint8_t>(buttons_);
        report[1] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(x) & 0xFFU);
        report[2] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(x) >> 8U) & 0xFFU);
        report[3] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(y) & 0xFFU);
        report[4] = static_cast<std::uint8_t>((static_cast<std::uint16_t>(y) >> 8U) & 0xFFU);
        report[5] = static_cast<std::uint8_t>(wheel);
        report[6] = static_cast<std::uint8_t>(pan);
        return context_->submit_device_report(state_, report);
      }

      std::shared_ptr<WindowsBackendContext> context_;
      std::shared_ptr<WindowsVhfDeviceState> state_;
      WindowsMouse fallback_;
      std::byte buttons_ {};
      std::int64_t vertical_scroll_remainder_ = 0;
      std::int64_t horizontal_scroll_remainder_ = 0;
      bool open_ = true;
    };

    BackendMouseCreationResult WindowsBackendContext::create_hid_mouse(
      DeviceId id,
      const CreateMouseOptions &options
    ) {
      auto effective_options = options;
      effective_options.profile = make_hid_mouse_profile(options.profile);

      auto request = windows::make_create_device_request(id, effective_options);
      LvhWindowsCreateDeviceResponse response {};
      response.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
      response.size = sizeof(response);

      if (const auto status = command_channel_->create_device(request, response); !status.ok()) {
        return {status, nullptr};
      }

      auto state = std::make_shared<WindowsVhfDeviceState>(
        id,
        response.driver_device_id,
        response.session_token,
        effective_options.profile,
        response.device_path[0] == '\0' ? command_channel_->path() : std::string {response.device_path.data()}
      );

      {
        std::lock_guard lock {devices_mutex_};
        devices_[state->driver_id] = state;
      }
      notify_pid_timer();

      return {OperationStatus::success(), std::make_unique<WindowsHidMouse>(shared_from_this(), std::move(state))};
    }

    /**
     * @brief Shared lifecycle and refresh support for Windows synthetic pointer devices.
     */
    class WindowsSyntheticPointerDevice {
    protected:
      WindowsSyntheticPointerDevice(SyntheticPointerApi api, HSYNTHETICPOINTERDEVICE device):
          api_ {std::move(api)},
          device_ {device} {}

      WindowsSyntheticPointerDevice(const WindowsSyntheticPointerDevice &) = delete;
      WindowsSyntheticPointerDevice &operator=(const WindowsSyntheticPointerDevice &) = delete;
      WindowsSyntheticPointerDevice(WindowsSyntheticPointerDevice &&) noexcept = delete;
      WindowsSyntheticPointerDevice &operator=(WindowsSyntheticPointerDevice &&) noexcept = delete;

      virtual ~WindowsSyntheticPointerDevice() = default;

      /**
       * @brief Start periodic synthetic pointer refreshes while input is active.
       *
       * @param operation Operation description used for failure reporting.
       */
      void start_repeat_thread(std::string_view operation) {
        repeat_thread_ = std::jthread {[this, operation = std::string {operation}](std::stop_token stop_token) {
          while (!stop_token.stop_requested()) {
            std::this_thread::sleep_for(synthetic_pointer_repeat_interval);
            if (stop_token.stop_requested()) {
              break;
            }

            std::lock_guard lock {mutex_};
            if (!open_ || !has_repeat_input_locked()) {
              continue;
            }
            static_cast<void>(inject_locked(operation));
          }
        }};
      }

      /**
       * @brief Close the device and release the synthetic pointer handle.
       *
       * @return Success status after the device is closed.
       */
      OperationStatus close_device() {
        stop_repeat_thread();

        std::lock_guard lock {mutex_};
        close_device_locked();
        return OperationStatus::success();
      }

      /**
       * @brief Close the device from the destructor without allowing exceptions to escape.
       */
      void close_device_noexcept() noexcept {
        stop_repeat_thread();

        std::lock_guard lock {mutex_};
        close_device_locked();
      }

      /**
       * @brief Lock the synthetic pointer device state.
       *
       * @return Lock guarding the device state.
       */
      [[nodiscard]] std::unique_lock<std::mutex> lock_device() const {
        return std::unique_lock {mutex_};
      }

      /**
       * @brief Check whether the synthetic pointer device is open while the caller holds the lock.
       *
       * @return true when the device is open, false otherwise.
       */
      bool is_open_locked() const {
        return open_;
      }

      /**
       * @brief Inject synthetic pointer input while the caller holds the lock.
       *
       * @param inputs Synthetic pointer packets to inject.
       * @param count Number of packets to inject.
       * @param operation Operation description used for failure reporting.
       * @return Operation status from the injection call.
       */
      OperationStatus inject_synthetic_pointer_input_locked(
        const POINTER_TYPE_INFO *inputs,
        UINT32 count,
        std::string_view operation
      ) {
        return inject_synthetic_pointer_input(api_, device_, inputs, count, operation);
      }

    private:
      virtual bool has_repeat_input_locked() const = 0;
      virtual OperationStatus inject_locked(std::string_view operation) = 0;

      SyntheticPointerApi api_;
      HSYNTHETICPOINTERDEVICE device_ {};
      mutable std::mutex mutex_;
      bool open_ = true;

      void stop_repeat_thread() {
        if (repeat_thread_.joinable()) {
          repeat_thread_.request_stop();
          repeat_thread_.join();
        }
      }

      void close_device_locked() {
        if (!open_) {
          return;
        }

        open_ = false;
        if (device_) {
          api_.destroy(device_);
          device_ = nullptr;
        }
      }

      std::jthread repeat_thread_;
    };

    /**
     * @brief Backend touchscreen implemented with Windows synthetic pointer injection.
     */
    class WindowsTouchscreen final: public WindowsSyntheticPointerDevice, public BackendTouchscreen {
    public:
      WindowsTouchscreen(SyntheticPointerApi api, HSYNTHETICPOINTERDEVICE device):
          WindowsSyntheticPointerDevice {std::move(api), device} {
        start_repeat_thread("refresh Windows touchscreen contacts");
      }

      WindowsTouchscreen(const WindowsTouchscreen &) = delete;
      WindowsTouchscreen &operator=(const WindowsTouchscreen &) = delete;
      WindowsTouchscreen(WindowsTouchscreen &&) noexcept = delete;
      WindowsTouchscreen &operator=(WindowsTouchscreen &&) noexcept = delete;

      ~WindowsTouchscreen() noexcept override {
        close_device_noexcept();
      }

      static BackendTouchscreenCreationResult create(const SyntheticPointerApi &api, const CreateTouchscreenOptions &options) {
        using enum ErrorCode;

        if (options.profile.device_type != DeviceType::touchscreen) {
          return {unsupported_profile_status("Windows touchscreen backend requires a touchscreen profile"), nullptr};
        }
        if (!synthetic_pointer_available(api)) {
          return {
            OperationStatus::failure(backend_unavailable, "Windows touchscreen backend requires Windows 10 1809 or later"),
            nullptr,
          };
        }

        const auto device = api.create(PT_TOUCH, max_contacts, POINTER_FEEDBACK_DEFAULT);
        if (!device) {
          return {windows_failure(backend_failure, "create Windows touchscreen device", ::GetLastError()), nullptr};
        }

        return {OperationStatus::success(), std::make_unique<WindowsTouchscreen>(api, device)};
      }

      OperationStatus place_contact(const TouchContact &contact) override {
        using enum ErrorCode;

        if (contact.id < 0) {
          return OperationStatus::failure(invalid_argument, "Windows touch contact id must not be negative");
        }

        const auto lock = lock_device();
        if (!is_open_locked()) {
          return OperationStatus::failure(device_closed, "Windows touchscreen is closed");
        }

        const auto slot = slot_for_contact(contact.id);
        if (!slot.has_value()) {
          return OperationStatus::failure(invalid_argument, "too many active Windows touch contacts");
        }

        auto &pointer = contacts_[*slot];
        pointer.type = PT_TOUCH;
        auto &touch_info = pointer.touchInfo;
        auto &pointer_info = touch_info.pointerInfo;
        const auto was_touching = contact_touching_[*slot];
        pointer_info.pointerType = PT_TOUCH;
        pointer_info.pointerId = static_cast<UINT32>(contact.id);
        pointer_info.pointerFlags |= POINTER_FLAG_INRANGE;
        if (contact.touching) {
          pointer_info.pointerFlags |= POINTER_FLAG_INCONTACT;
          pointer_info.pointerFlags |= was_touching ? POINTER_FLAG_UPDATE : POINTER_FLAG_DOWN;
        } else {
          pointer_info.pointerFlags &= ~POINTER_FLAG_INCONTACT;
          pointer_info.pointerFlags |= POINTER_FLAG_UPDATE;
        }
        update_pointer_location(pointer_info, contact.viewport, contact.x, contact.y);

        touch_info.touchMask = TOUCH_MASK_NONE;
        if (contact.touching && contact.pressure > 0.0F) {
          touch_info.touchMask |= TOUCH_MASK_PRESSURE;
          touch_info.pressure = static_cast<UINT32>(std::lround(std::clamp(contact.pressure, 0.0F, 1.0F) * 1024.0F));
        } else if (contact.touching) {
          touch_info.pressure = 512;
        } else {
          touch_info.pressure = 0;
          touch_info.rcContact = {};
        }
        if (contact.orientation != 0) {
          touch_info.touchMask |= TOUCH_MASK_ORIENTATION;
          touch_info.orientation = static_cast<UINT32>((contact.orientation % 360 + 360) % 360);
        } else {
          touch_info.orientation = 0;
        }
        if (contact.touching) {
          update_contact_area(touch_info, contact);
        }

        const auto status = inject_locked("submit Windows touchscreen contact");
        if (status.ok()) {
          pointer_info.pointerFlags &= ~pointer_edge_triggered_flags;
          new_slot_ = false;
          contact_touching_[*slot] = contact.touching;
        }
        return status;
      }

      OperationStatus release_contact(std::int32_t contact_id, PointerTransition transition) override {
        using enum ErrorCode;

        const auto lock = lock_device();
        if (!is_open_locked()) {
          return OperationStatus::failure(device_closed, "Windows touchscreen is closed");
        }

        const auto slot = find_contact_slot(contact_id);
        if (!slot.has_value()) {
          return OperationStatus::success();
        }

        auto &pointer_info = contacts_[*slot].touchInfo.pointerInfo;
        const auto was_touching = contact_touching_[*slot];
        pointer_info.pointerFlags &= ~(POINTER_FLAG_INCONTACT | POINTER_FLAG_INRANGE);
        switch (transition) {
          case PointerTransition::cancel:
            pointer_info.pointerFlags |= POINTER_FLAG_CANCELED;
            pointer_info.pointerFlags |= was_touching ? POINTER_FLAG_UP : POINTER_FLAG_UPDATE;
            break;
          case PointerTransition::leave:
            pointer_info.pointerFlags |= POINTER_FLAG_UPDATE;
            break;
          default:
            pointer_info.pointerFlags |= was_touching ? POINTER_FLAG_UP : POINTER_FLAG_UPDATE;
            break;
        }
        if (const auto status = inject_locked("release Windows touchscreen contact"); !status.ok()) {
          return status;
        }

        erase_slot(*slot);
        return OperationStatus::success();
      }

      OperationStatus close() override {
        return close_device();
      }

    private:
      static constexpr UINT32 max_contacts = 10;  ///< Windows synthetic touchscreen contact capacity.

      static void update_contact_area(POINTER_TOUCH_INFO &touch_info, const TouchContact &contact) {
        if (contact.contact_major_axis <= 0.0F || contact.contact_minor_axis <= 0.0F) {
          touch_info.rcContact = {};
          return;
        }

        const auto rotation = static_cast<float>(contact.orientation) * static_cast<float>(std::numbers::pi) / 180.0F;
        const auto minor_rotation = rotation + (static_cast<float>(std::numbers::pi) / 2.0F);
        const auto width =
          std::abs(std::cos(rotation) * contact.contact_major_axis) +
          std::abs(std::cos(minor_rotation) * contact.contact_minor_axis);
        const auto height =
          std::abs(std::sin(rotation) * contact.contact_major_axis) +
          std::abs(std::sin(minor_rotation) * contact.contact_minor_axis);
        const auto &location = touch_info.pointerInfo.ptPixelLocation;
        touch_info.rcContact.left = location.x - static_cast<LONG>(std::floor(width / 2.0F));
        touch_info.rcContact.right = location.x + static_cast<LONG>(std::ceil(width / 2.0F));
        touch_info.rcContact.top = location.y - static_cast<LONG>(std::floor(height / 2.0F));
        touch_info.rcContact.bottom = location.y + static_cast<LONG>(std::ceil(height / 2.0F));
        touch_info.touchMask |= TOUCH_MASK_CONTACTAREA;
      }

      std::optional<std::size_t> find_contact_slot(std::int32_t contact_id) const {
        for (std::size_t index = 0; index < active_contacts_; ++index) {
          if (contact_ids_[index] == contact_id) {
            return index;
          }
        }

        return std::nullopt;
      }

      std::optional<std::size_t> slot_for_contact(std::int32_t contact_id) {
        if (const auto slot = find_contact_slot(contact_id); slot.has_value()) {
          new_slot_ = false;
          return slot;
        }

        if (active_contacts_ >= contacts_.size()) {
          return std::nullopt;
        }

        const auto slot = active_contacts_++;
        contact_ids_[slot] = contact_id;
        new_slot_ = true;
        return slot;
      }

      void erase_slot(std::size_t slot) {
        for (std::size_t index = slot; index + 1U < active_contacts_; ++index) {
          contacts_[index] = contacts_[index + 1U];
          contact_ids_[index] = contact_ids_[index + 1U];
          contact_touching_[index] = contact_touching_[index + 1U];
        }
        contacts_[active_contacts_ - 1U] = {};
        contact_ids_[active_contacts_ - 1U].reset();
        contact_touching_[active_contacts_ - 1U] = false;
        --active_contacts_;
      }

      bool has_repeat_input_locked() const override {
        return active_contacts_ != 0U;
      }

      OperationStatus inject_locked(std::string_view operation) override {
        return inject_synthetic_pointer_input_locked(
          contacts_.data(),
          static_cast<UINT32>(active_contacts_),
          operation
        );
      }

      std::array<POINTER_TYPE_INFO, max_contacts> contacts_ {};
      std::array<std::optional<std::int32_t>, max_contacts> contact_ids_ {};
      std::array<bool, max_contacts> contact_touching_ {};
      std::size_t active_contacts_ = 0;
      bool new_slot_ = false;
    };

    /**
     * @brief Backend pen tablet implemented with Windows synthetic pointer injection.
     */
    class WindowsPenTablet final: public WindowsSyntheticPointerDevice, public BackendPenTablet {
    public:
      WindowsPenTablet(SyntheticPointerApi api, HSYNTHETICPOINTERDEVICE device):
          WindowsSyntheticPointerDevice {std::move(api), device} {
        pointer_.type = PT_PEN;
        pointer_.penInfo.pointerInfo.pointerType = PT_PEN;
        pointer_.penInfo.pointerInfo.pointerId = 0;
        start_repeat_thread("refresh Windows pen tablet tool");
      }

      WindowsPenTablet(const WindowsPenTablet &) = delete;
      WindowsPenTablet &operator=(const WindowsPenTablet &) = delete;
      WindowsPenTablet(WindowsPenTablet &&) noexcept = delete;
      WindowsPenTablet &operator=(WindowsPenTablet &&) noexcept = delete;

      ~WindowsPenTablet() noexcept override {
        close_device_noexcept();
      }

      static BackendPenTabletCreationResult create(const SyntheticPointerApi &api, const CreatePenTabletOptions &options) {
        using enum ErrorCode;

        if (options.profile.device_type != DeviceType::pen_tablet) {
          return {unsupported_profile_status("Windows pen tablet backend requires a pen tablet profile"), nullptr};
        }
        if (!synthetic_pointer_available(api)) {
          return {
            OperationStatus::failure(backend_unavailable, "Windows pen tablet backend requires Windows 10 1809 or later"),
            nullptr,
          };
        }

        const auto device = api.create(PT_PEN, 1, POINTER_FEEDBACK_DEFAULT);
        if (!device) {
          return {windows_failure(backend_failure, "create Windows pen tablet device", ::GetLastError()), nullptr};
        }

        return {OperationStatus::success(), std::make_unique<WindowsPenTablet>(api, device)};
      }

      OperationStatus place_tool(const PenToolState &state) override {
        using enum ErrorCode;

        const auto lock = lock_device();
        if (!is_open_locked()) {
          return OperationStatus::failure(device_closed, "Windows pen tablet is closed");
        }

        auto &pen_info = pointer_.penInfo;
        update_tool_flags(pen_info, state.tool);
        auto &pointer_info = pen_info.pointerInfo;
        pointer_info.pointerType = PT_PEN;
        pointer_info.pointerId = 0;
        if (state.transition == PointerTransition::update) {
          pointer_info.pointerFlags |= POINTER_FLAG_INRANGE | POINTER_FLAG_UPDATE;
          if (state.pressure >= 0.0F) {
            pointer_info.pointerFlags |= POINTER_FLAG_INCONTACT;
            if (!contacting_) {
              pointer_info.pointerFlags |= POINTER_FLAG_DOWN;
            }
          } else {
            pointer_info.pointerFlags &= ~POINTER_FLAG_INCONTACT;
          }
          active_ = true;
          contacting_ = state.pressure >= 0.0F;
          update_pointer_location(pointer_info, state.viewport, state.x, state.y);
        } else {
          pointer_info.pointerFlags &= ~(POINTER_FLAG_INCONTACT | POINTER_FLAG_INRANGE);
          switch (state.transition) {
            case PointerTransition::cancel:
              pointer_info.pointerFlags |= POINTER_FLAG_CANCELED;
              pointer_info.pointerFlags |= contacting_ ? POINTER_FLAG_UP : POINTER_FLAG_UPDATE;
              break;
            case PointerTransition::leave:
              pointer_info.pointerFlags |= POINTER_FLAG_UPDATE;
              break;
            default:
              pointer_info.pointerFlags |= contacting_ ? POINTER_FLAG_UP : POINTER_FLAG_UPDATE;
              break;
          }
          active_ = false;
          contacting_ = false;
        }

        pen_info.penMask = PEN_MASK_NONE;
        if (state.pressure > 0.0F) {
          pen_info.penMask |= PEN_MASK_PRESSURE;
          pen_info.pressure = static_cast<UINT32>(std::lround(std::clamp(state.pressure, 0.0F, 1.0F) * 1024.0F));
        } else {
          pen_info.pressure = 0;
        }
        if (state.tilt_x != 0.0F || state.tilt_y != 0.0F) {
          pen_info.penMask |= PEN_MASK_TILT_X | PEN_MASK_TILT_Y;
          pen_info.tiltX = static_cast<INT32>(std::lround(std::clamp(state.tilt_x, -90.0F, 90.0F)));
          pen_info.tiltY = static_cast<INT32>(std::lround(std::clamp(state.tilt_y, -90.0F, 90.0F)));
        } else {
          pen_info.tiltX = 0;
          pen_info.tiltY = 0;
        }
        if (barrel_pressed_) {
          pen_info.penFlags |= PEN_FLAG_BARREL;
        } else {
          pen_info.penFlags &= ~PEN_FLAG_BARREL;
        }

        const auto status = inject_locked("submit Windows pen tablet tool");
        if (status.ok()) {
          pointer_info.pointerFlags &= ~pointer_edge_triggered_flags;
        }
        return status;
      }

      OperationStatus button(PenButton /*button*/, bool pressed) override {
        using enum ErrorCode;

        const auto lock = lock_device();
        if (!is_open_locked()) {
          return OperationStatus::failure(device_closed, "Windows pen tablet is closed");
        }

        barrel_pressed_ = pressed;
        if (barrel_pressed_) {
          pointer_.penInfo.penFlags |= PEN_FLAG_BARREL;
        } else {
          pointer_.penInfo.penFlags &= ~PEN_FLAG_BARREL;
        }
        if (active_) {
          pointer_.penInfo.pointerInfo.pointerFlags |= POINTER_FLAG_UPDATE;
          const auto status = inject_locked("submit Windows pen tablet button");
          pointer_.penInfo.pointerInfo.pointerFlags &= ~pointer_edge_triggered_flags;
          return status;
        }

        return OperationStatus::success();
      }

      OperationStatus close() override {
        return close_device();
      }

    private:
      static void update_tool_flags(POINTER_PEN_INFO &pen_info, PenToolType tool) {
        switch (tool) {
          case PenToolType::eraser:
            pen_info.penFlags |= PEN_FLAG_ERASER;
            break;
          case PenToolType::unchanged:
            break;
          default:
            pen_info.penFlags &= ~PEN_FLAG_ERASER;
            break;
        }
      }

      bool has_repeat_input_locked() const override {
        return active_;
      }

      OperationStatus inject_locked(std::string_view operation) override {
        return inject_synthetic_pointer_input_locked(&pointer_, 1, operation);
      }

      POINTER_TYPE_INFO pointer_ {};
      bool barrel_pressed_ = false;
      bool active_ = false;
      bool contacting_ = false;
    };

    class WindowsBackend final: public Backend {
    public:
      WindowsBackend():
          WindowsBackend(open_brokered_control_channels()) {}

      explicit WindowsBackend(WindowsControlChannels channels):
          WindowsBackend(std::move(channels.command), std::move(channels.event)) {}

      WindowsBackend(
        std::unique_ptr<WindowsControlChannel> command_channel,
        std::unique_ptr<WindowsControlChannel> event_channel
      ) {
        capabilities_.backend_name = "windows-umdf";
        capabilities_.requires_installed_driver = true;
        capabilities_.supports_keyboard = true;
        capabilities_.supports_mouse = true;
        capabilities_.supports_touchscreen = synthetic_pointer_available(synthetic_pointer_api());
        capabilities_.supports_pen_tablet = synthetic_pointer_available(synthetic_pointer_api());

        if (!command_channel || !event_channel) {
          return;
        }

        context_ = std::make_shared<WindowsBackendContext>(std::move(command_channel), std::move(event_channel));
        if (!context_->valid()) {
          context_.reset();
          return;
        }

        context_->start();
        capabilities_.supports_virtual_hid = true;
        capabilities_.supports_gamepad = true;
        capabilities_.supports_output_reports = true;
      }

      ~WindowsBackend() override {
        if (context_) {
          context_->stop();
        }
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) override {
        using enum ErrorCode;

        if (const auto validation = validate_windows_gamepad_profile(options.profile); !validation.ok()) {
          return {validation, nullptr};
        }

        if (!context_) {
          return {
            OperationStatus::failure(
              backend_unavailable,
              "Windows UMDF control device is unavailable; install the libvirtualhid driver package"
            ),
            nullptr,
          };
        }

        if (options.profile.gamepad_kind == GamepadProfileKind::xbox_360) {
          return {
            unsupported_profile_status(
              "Windows UMDF/VHF backend cannot expose Xbox 360 XUSB gamepads; use an XUSB fallback for this profile"
            ),
            nullptr,
          };
        }

        auto effective_options = options;
        effective_options.profile = windows::effective_vhf_gamepad_profile(options.profile);
        auto result = context_->create_gamepad(id, effective_options);
        if (result) {
          result.effective_profile = std::move(effective_options.profile);
        }
        return result;
      }

      BackendKeyboardCreationResult create_keyboard(
        DeviceId /*id*/,
        const CreateKeyboardOptions &options
      ) override {
        if (options.profile.device_type != DeviceType::keyboard) {
          return {unsupported_profile_status("Windows keyboard backend requires a keyboard profile"), nullptr};
        }

        return {OperationStatus::success(), std::make_unique<WindowsKeyboard>()};
      }

      BackendMouseCreationResult create_mouse(DeviceId id, const CreateMouseOptions &options) override {
        if (options.profile.device_type != DeviceType::mouse) {
          return {unsupported_profile_status("Windows mouse backend requires a mouse profile"), nullptr};
        }

        // A driver-backed HID mouse is visible to Raw Input and requires the
        // same machine license as every broker-created driver device. Preserve
        // the SendInput behavior only when that licensed path is unavailable;
        // protocol and driver failures must remain visible to the caller.
        if (context_) {
          auto hid = context_->create_hid_mouse(id, options);
          if (hid.mouse) {
            return hid;
          }
          if (!mouse_driver_fallback_allowed(hid.status.code())) {
            return hid;
          }
        }

        return {OperationStatus::success(), std::make_unique<WindowsMouse>()};
      }

      BackendTouchscreenCreationResult create_touchscreen(
        DeviceId /*id*/,
        const CreateTouchscreenOptions &options
      ) override {
        return WindowsTouchscreen::create(synthetic_pointer_api(), options);
      }

      BackendTrackpadCreationResult create_trackpad(
        DeviceId /*id*/,
        const CreateTrackpadOptions & /*options*/
      ) override {
        return {unsupported_profile_status("Windows backend currently supports gamepad, keyboard, and mouse devices only"), nullptr};
      }

      BackendPenTabletCreationResult create_pen_tablet(
        DeviceId /*id*/,
        const CreatePenTabletOptions &options
      ) override {
        return WindowsPenTablet::create(synthetic_pointer_api(), options);
      }

    private:
      BackendCapabilities capabilities_;
      std::shared_ptr<WindowsBackendContext> context_;
    };

  }  // namespace

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<WindowsBackend>();
  }

}  // namespace lvh::detail
