/**
 * @file src/platform/windows/windows_backend.cpp
 * @brief Windows UMDF control-channel backend definitions.
 */

// local includes
#include "core/backend.hpp"
#include "platform/windows/control_protocol.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
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
#include <setupapi.h>
// clang-format on

namespace lvh::detail {
  namespace {  // NOSONAR(cpp:S1000): Windows backend internals need internal linkage; tests include this file with the platform factory renamed.

    class WindowsBackendContext;

    using UniqueHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;
    using SendInputFunction = std::function<UINT(std::span<INPUT>)>;

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

    OperationStatus send_input(std::span<INPUT> inputs, std::string_view operation) {
      using enum ErrorCode;

      if (const auto sent = send_input_function()(inputs); sent != static_cast<UINT>(inputs.size())) {
        return windows_failure(backend_failure, operation, ::GetLastError());
      }

      return OperationStatus::success();
    }

    OperationStatus send_input(const INPUT &input, std::string_view operation) {
      std::array inputs {input};
      return send_input(std::span<INPUT> {inputs}, operation);
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

        std::vector<std::byte> buffer(required_size);
        auto *detail_data = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A *>(buffer.data());
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

    LONG scale_absolute_axis(std::int32_t value, std::int32_t dimension) {
      if (dimension <= 1) {
        return 0;
      }

      const auto clamped = std::clamp(value, 0, dimension);
      const auto numerator = static_cast<std::int64_t>(clamped) * std::numeric_limits<std::uint16_t>::max();
      return static_cast<LONG>(numerator / dimension);
    }

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
        if (!event.pressed) {
          input.ki.dwFlags = KEYEVENTF_KEYUP;
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
            mouse.dx = scale_absolute_axis(event.x, event.width);
            mouse.dy = scale_absolute_axis(event.y, event.height);
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
            unsupported_device_status(
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
          return {unsupported_device_status("Windows keyboard backend requires a keyboard profile"), nullptr};
        }

        return {OperationStatus::success(), std::make_unique<WindowsKeyboard>()};
      }

      BackendMouseCreationResult create_mouse(DeviceId /*id*/, const CreateMouseOptions &options) override {
        if (options.profile.device_type != DeviceType::mouse) {
          return {unsupported_device_status("Windows mouse backend requires a mouse profile"), nullptr};
        }

        return {OperationStatus::success(), std::make_unique<WindowsMouse>()};
      }

      BackendTouchscreenCreationResult create_touchscreen(
        DeviceId /*id*/,
        const CreateTouchscreenOptions & /*options*/
      ) override {
        return {unsupported_device_status("Windows backend currently supports gamepad, keyboard, and mouse devices only"), nullptr};
      }

      BackendTrackpadCreationResult create_trackpad(
        DeviceId /*id*/,
        const CreateTrackpadOptions & /*options*/
      ) override {
        return {unsupported_device_status("Windows backend currently supports gamepad, keyboard, and mouse devices only"), nullptr};
      }

      BackendPenTabletCreationResult create_pen_tablet(
        DeviceId /*id*/,
        const CreatePenTabletOptions & /*options*/
      ) override {
        return {unsupported_device_status("Windows backend currently supports gamepad, keyboard, and mouse devices only"), nullptr};
      }

    private:
      static OperationStatus unsupported_device_status(std::string message) {
        using enum ErrorCode;

        return OperationStatus::failure(
          unsupported_profile,
          std::move(message)
        );
      }

      BackendCapabilities capabilities_;
      std::shared_ptr<WindowsBackendContext> context_;
    };

  }  // namespace

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<WindowsBackend>();
  }

}  // namespace lvh::detail
