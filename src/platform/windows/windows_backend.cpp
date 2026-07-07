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
#include "platform/windows/control_protocol.hpp"
#include "platform/windows/keylayout.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
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
      if (const auto required_size = ::GetEnvironmentVariableA(environment_name, nullptr, 0); required_size > 1U) {
        std::string path(required_size - 1U, '\0');
        const auto copied_size = ::GetEnvironmentVariableA(environment_name, path.data(), required_size);
        if (copied_size > 0U && copied_size < required_size) {
          return {path};
        }
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

    class WindowsControlChannel {
    public:
      WindowsControlChannel(const WindowsControlChannel &) = delete;
      WindowsControlChannel &operator=(const WindowsControlChannel &) = delete;
      WindowsControlChannel(WindowsControlChannel &&) noexcept = delete;
      WindowsControlChannel &operator=(WindowsControlChannel &&) noexcept = delete;

      virtual ~WindowsControlChannel() = default;

      virtual const std::string &path() const = 0;

      virtual OperationStatus create_gamepad(
        const LvhWindowsCreateGamepadRequest &request,
        LvhWindowsCreateGamepadResponse &response
      ) const = 0;

      virtual OperationStatus destroy_device(std::uint64_t driver_device_id) const = 0;

      virtual OperationStatus submit_input_report(
        std::uint64_t driver_device_id,
        const std::vector<std::uint8_t> &report
      ) const = 0;

      virtual std::optional<LvhWindowsOutputReportEvent> read_output_report(HANDLE stop_event) const = 0;

    protected:
      WindowsControlChannel() = default;
    };

    class Win32WindowsControlChannel final: public WindowsControlChannel {
    public:
      static std::unique_ptr<WindowsControlChannel> open(const std::string &path) {
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
          return nullptr;
        }

        return std::make_unique<Win32WindowsControlChannel>(path, make_unique_handle(handle));
      }

      const std::string &path() const override {
        return path_;
      }

      OperationStatus create_gamepad(
        const LvhWindowsCreateGamepadRequest &request,
        LvhWindowsCreateGamepadResponse &response
      ) const override {
        using enum ErrorCode;

        auto request_copy = request;
        DWORD bytes_returned = 0;
        if (const auto status = device_io_control(LVH_WINDOWS_IOCTL_CREATE_GAMEPAD, request_copy, response, &bytes_returned, "create Windows gamepad"); !status.ok()) {
          return status;
        }

        if (bytes_returned < sizeof(response)) {
          return OperationStatus::failure(backend_failure, "Windows driver returned a truncated gamepad response");
        }

        return protocol_status(response.status, "Windows driver rejected gamepad creation");
      }

      OperationStatus destroy_device(std::uint64_t driver_device_id) const override {
        auto request = windows::make_destroy_device_request(driver_device_id);
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
        const std::vector<std::uint8_t> &report
      ) const override {
        using enum ErrorCode;

        if (report.size() > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
          return OperationStatus::failure(invalid_argument, "input report exceeds Windows control protocol limit");
        }

        auto request = windows::make_submit_input_report_request(driver_device_id, report);
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

        if (const auto started = ::DeviceIoControl(handle_.get(), LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT, nullptr, 0, &event, sizeof(event), &bytes_returned, &overlapped); started == FALSE) {
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
            static_cast<void>(::CancelIoEx(handle_.get(), &overlapped));
            return std::nullopt;
          }
          if (wait_result != WAIT_OBJECT_0) {
            static_cast<void>(::CancelIoEx(handle_.get(), &overlapped));
            return std::nullopt;
          }
        }

        if (::GetOverlappedResult(handle_.get(), &overlapped, &bytes_returned, FALSE) == FALSE) {
          return std::nullopt;
        }

        if (constexpr auto event_header_size = sizeof(event.version) + sizeof(event.size) + sizeof(event.driver_device_id) + sizeof(event.report_size); bytes_returned < event_header_size) {
          return std::nullopt;
        }

        event.report_size = std::min(event.report_size, static_cast<std::uint32_t>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE));
        return event;
      }

      Win32WindowsControlChannel(std::string path, UniqueHandle handle):
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
        using enum ErrorCode;

        if (::DeviceIoControl(handle_.get(), control_code, &input, sizeof(input), &output, sizeof(output), bytes_returned, nullptr) == FALSE) {
          return windows_failure(backend_failure, operation, ::GetLastError());
        }

        return OperationStatus::success();
      }

      template<typename Input>
      OperationStatus device_io_control(
        DWORD control_code,
        Input &input,
        DWORD *bytes_returned,
        std::string_view operation
      ) const {
        using enum ErrorCode;

        if (::DeviceIoControl(handle_.get(), control_code, &input, sizeof(input), nullptr, 0, bytes_returned, nullptr) == FALSE) {
          return windows_failure(backend_failure, operation, ::GetLastError());
        }

        return OperationStatus::success();
      }

      std::string path_;
      UniqueHandle handle_;
    };

    std::unique_ptr<WindowsControlChannel> open_control_channel() {
      for (const auto &path : resolve_control_device_paths()) {
        if (auto channel = Win32WindowsControlChannel::open(path)) {
          return channel;
        }
      }

      return nullptr;
    }

    class WindowsGamepadState {
    public:
      WindowsGamepadState(
        DeviceId client_device_id,
        std::uint64_t driver_device_id,
        DeviceProfile device_profile,
        std::string device_path
      ):
          client_id {client_device_id},
          driver_id {driver_device_id},
          profile {std::move(device_profile)},
          path {std::move(device_path)} {}

    private:
      friend class WindowsBackendContext;
      friend class WindowsGamepad;

      mutable std::mutex mutex_;
      DeviceId client_id;
      std::uint64_t driver_id;
      DeviceProfile profile;
      std::string path;
      bool open = true;
      OutputCallback output_callback;
    };

    class WindowsGamepad final: public BackendGamepad {
    public:
      WindowsGamepad(std::shared_ptr<WindowsBackendContext> context, std::shared_ptr<WindowsGamepadState> state):
          context_ {std::move(context)},
          state_ {std::move(state)} {}

      OperationStatus submit(const std::vector<std::uint8_t> &report) override;
      void set_output_callback(OutputCallback callback) override;
      std::vector<DeviceNode> device_nodes() const override;
      OperationStatus close() override;

    private:
      std::shared_ptr<WindowsBackendContext> context_;
      std::shared_ptr<WindowsGamepadState> state_;
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
      }

      void stop() {
        if (stop_event_) {
          static_cast<void>(::SetEvent(stop_event_.get()));
        }

        if (output_thread_.joinable()) {
          output_thread_.request_stop();
          output_thread_.join();
        }
      }

      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) {
        auto request = windows::make_create_gamepad_request(id, options);
        LvhWindowsCreateGamepadResponse response {};
        response.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
        response.size = sizeof(response);

        if (const auto status = command_channel_->create_gamepad(request, response); !status.ok()) {
          return {status, nullptr};
        }

        auto state = std::make_shared<WindowsGamepadState>(
          id,
          response.driver_device_id,
          options.profile,
          response.device_path[0] == '\0' ? command_channel_->path() : std::string {response.device_path}
        );

        {
          std::lock_guard lock {devices_mutex_};
          gamepads_[state->driver_id] = state;
        }

        auto gamepad = std::make_unique<WindowsGamepad>(shared_from_this(), std::move(state));
        return {OperationStatus::success(), std::move(gamepad)};
      }

      OperationStatus submit_gamepad_report(
        const std::shared_ptr<WindowsGamepadState> &state,
        const std::vector<std::uint8_t> &report
      ) const {
        const auto driver_id = state->driver_id;
        return command_channel_->submit_input_report(driver_id, report);
      }

      OperationStatus close_gamepad(const std::shared_ptr<WindowsGamepadState> &state) {
        std::uint64_t driver_id = 0;
        {
          std::lock_guard lock {state->mutex_};
          if (!state->open) {
            return OperationStatus::success();
          }

          state->open = false;
          driver_id = state->driver_id;
        }

        {
          std::lock_guard lock {devices_mutex_};
          gamepads_.erase(driver_id);
        }

        return command_channel_->destroy_device(driver_id);
      }

    private:
      void output_loop(std::stop_token stop_token) {
        while (!stop_token.stop_requested()) {
          auto event = event_channel_->read_output_report(stop_event_.get());
          if (!event.has_value()) {
            continue;
          }

          dispatch_output_report(*event);
        }
      }

      void dispatch_output_report(const LvhWindowsOutputReportEvent &event) {
        std::shared_ptr<WindowsGamepadState> state;
        {
          std::lock_guard lock {devices_mutex_};
          if (const auto iter = gamepads_.find(event.driver_device_id); iter != gamepads_.end()) {
            state = iter->second.lock();
          }
        }

        if (!state) {
          return;
        }

        DeviceProfile profile;
        OutputCallback callback;
        {
          std::lock_guard lock {state->mutex_};
          if (!state->open || !state->output_callback) {
            return;
          }

          profile = state->profile;
          callback = state->output_callback;
        }

        std::vector<std::uint8_t> report(
          event.report,
          event.report + std::min(event.report_size, static_cast<std::uint32_t>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE))
        );
        for (const auto &output : reports::parse_output_reports(profile, report)) {
          callback(output);
        }
      }

      std::unique_ptr<WindowsControlChannel> command_channel_;
      std::unique_ptr<WindowsControlChannel> event_channel_;
      UniqueHandle stop_event_ {make_unique_handle(::CreateEventA(nullptr, TRUE, FALSE, nullptr))};
      std::jthread output_thread_;
      std::mutex devices_mutex_;
      std::map<std::uint64_t, std::weak_ptr<WindowsGamepadState>> gamepads_;
    };

    OperationStatus WindowsGamepad::submit(const std::vector<std::uint8_t> &report) {
      using enum ErrorCode;

      {
        std::lock_guard lock {state_->mutex_};
        if (!state_->open) {
          return OperationStatus::failure(device_closed, "Windows gamepad is closed");
        }
      }

      return context_->submit_gamepad_report(state_, report);
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
      return context_->close_gamepad(state_);
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

      SyntheticPointerApi api_;
      HSYNTHETICPOINTERDEVICE device_ {};
      mutable std::mutex mutex_;
      bool open_ = true;

    private:
      virtual bool has_repeat_input_locked() const = 0;
      virtual OperationStatus inject_locked(std::string_view operation) = 0;

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

        std::lock_guard lock {mutex_};
        if (!open_) {
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

        std::lock_guard lock {mutex_};
        if (!open_) {
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
        return inject_synthetic_pointer_input(
          api_,
          device_,
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

        std::lock_guard lock {mutex_};
        if (!open_) {
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

        std::lock_guard lock {mutex_};
        if (!open_) {
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
        return inject_synthetic_pointer_input(api_, device_, &pointer_, 1, operation);
      }

      POINTER_TYPE_INFO pointer_ {};
      bool barrel_pressed_ = false;
      bool active_ = false;
      bool contacting_ = false;
    };

    class WindowsBackend final: public Backend {
    public:
      WindowsBackend():
          WindowsBackend(
            open_control_channel(),
            open_control_channel()
          ) {}

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

        return context_->create_gamepad(id, options);
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

      BackendMouseCreationResult create_mouse(DeviceId /*id*/, const CreateMouseOptions &options) override {
        if (options.profile.device_type != DeviceType::mouse) {
          return {unsupported_profile_status("Windows mouse backend requires a mouse profile"), nullptr};
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
