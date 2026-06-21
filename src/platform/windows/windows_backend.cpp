/**
 * @file src/platform/windows/windows_backend.cpp
 * @brief Windows UMDF control-channel backend definitions.
 */

// local includes
#include "core/backend.hpp"
#include "platform/windows/control_protocol.hpp"

#include <libvirtualhid/report.hpp>

// standard includes
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
#include <windows.h>

namespace lvh::detail {
  namespace {

    class WindowsBackendContext;

    class UniqueHandle {
    public:
      UniqueHandle() = default;

      explicit UniqueHandle(HANDLE handle):
          handle_ {handle} {}

      UniqueHandle(const UniqueHandle &) = delete;
      UniqueHandle &operator=(const UniqueHandle &) = delete;

      UniqueHandle(UniqueHandle &&other) noexcept:
          handle_ {std::exchange(other.handle_, nullptr)} {}

      UniqueHandle &operator=(UniqueHandle &&other) noexcept {
        if (this != &other) {
          reset(std::exchange(other.handle_, nullptr));
        }

        return *this;
      }

      ~UniqueHandle() {
        reset();
      }

      void reset(HANDLE handle = nullptr) {
        if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
          static_cast<void>(::CloseHandle(handle_));
        }

        handle_ = handle;
      }

      HANDLE get() const {
        return handle_;
      }

      explicit operator bool() const {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
      }

    private:
      HANDLE handle_ {nullptr};
    };

    std::string windows_error_message(DWORD error_code) {
      LPSTR message_buffer = nullptr;
      const auto message_size = ::FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message_buffer),
        0,
        nullptr
      );

      std::string message;
      if (message_size > 0U && message_buffer != nullptr) {
        message.assign(message_buffer, message_size);
        while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
          message.pop_back();
        }
      } else {
        message = "Windows error " + std::to_string(error_code);
      }

      if (message_buffer != nullptr) {
        ::LocalFree(message_buffer);
      }

      return message;
    }

    OperationStatus windows_failure(ErrorCode code, std::string_view operation, DWORD error_code) {
      std::string message {operation};
      message += ": ";
      message += windows_error_message(error_code);
      return OperationStatus::failure(code, std::move(message));
    }

    std::string resolve_control_device_path() {
      constexpr auto environment_name = "LIBVIRTUALHID_WINDOWS_CONTROL_DEVICE";
      const auto required_size = ::GetEnvironmentVariableA(environment_name, nullptr, 0);
      if (required_size > 1U) {
        std::string path(required_size - 1U, '\0');
        const auto copied_size = ::GetEnvironmentVariableA(environment_name, path.data(), required_size);
        if (copied_size > 0U && copied_size < required_size) {
          return path;
        }
      }

      return std::string {windows::default_control_device_path};
    }

    OperationStatus protocol_status(std::uint32_t status, std::string_view operation) {
      switch (status) {
        case LVH_WINDOWS_STATUS_SUCCESS:
          return OperationStatus::success();
        case LVH_WINDOWS_STATUS_INVALID_ARGUMENT:
          return OperationStatus::failure(ErrorCode::invalid_argument, std::string {operation});
        case LVH_WINDOWS_STATUS_UNSUPPORTED_PROFILE:
          return OperationStatus::failure(ErrorCode::unsupported_profile, std::string {operation});
        case LVH_WINDOWS_STATUS_DEVICE_NOT_FOUND:
          return OperationStatus::failure(ErrorCode::device_closed, std::string {operation});
        case LVH_WINDOWS_STATUS_BACKEND_FAILURE:
        default:
          return OperationStatus::failure(ErrorCode::backend_failure, std::string {operation});
      }
    }

    OperationStatus validate_windows_gamepad_profile(const DeviceProfile &profile) {
      if (profile.report_descriptor.size() > LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "Windows gamepad HID descriptor exceeds control protocol limit");
      }
      if (profile.input_report_size > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "Windows gamepad input report exceeds control protocol limit");
      }
      if (profile.output_report_size > LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "Windows gamepad output report exceeds control protocol limit");
      }

      return OperationStatus::success();
    }

    class WindowsControlChannel {
    public:
      static std::unique_ptr<WindowsControlChannel> open(const std::string &path) {
        UniqueHandle handle {
          ::CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            nullptr
          )
        };

        if (!handle) {
          return nullptr;
        }

        return std::unique_ptr<WindowsControlChannel> {new WindowsControlChannel(path, std::move(handle))};
      }

      const std::string &path() const {
        return path_;
      }

      OperationStatus create_gamepad(
        const LvhWindowsCreateGamepadRequest &request,
        LvhWindowsCreateGamepadResponse &response
      ) const {
        DWORD bytes_returned = 0;
        const auto status = device_io_control(
          LVH_WINDOWS_IOCTL_CREATE_GAMEPAD,
          &request,
          sizeof(request),
          &response,
          sizeof(response),
          &bytes_returned,
          "create Windows gamepad"
        );
        if (!status.ok()) {
          return status;
        }

        if (bytes_returned < sizeof(response)) {
          return OperationStatus::failure(ErrorCode::backend_failure, "Windows driver returned a truncated gamepad response");
        }

        return protocol_status(response.status, "Windows driver rejected gamepad creation");
      }

      OperationStatus destroy_device(std::uint64_t driver_device_id) const {
        const auto request = windows::make_destroy_device_request(driver_device_id);
        DWORD bytes_returned = 0;
        return device_io_control(
          LVH_WINDOWS_IOCTL_DESTROY_DEVICE,
          &request,
          sizeof(request),
          nullptr,
          0,
          &bytes_returned,
          "destroy Windows virtual HID device"
        );
      }

      OperationStatus submit_input_report(std::uint64_t driver_device_id, const std::vector<std::uint8_t> &report) const {
        if (report.size() > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "input report exceeds Windows control protocol limit");
        }

        const auto request = windows::make_submit_input_report_request(driver_device_id, report);
        DWORD bytes_returned = 0;
        return device_io_control(
          LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT,
          &request,
          sizeof(request),
          nullptr,
          0,
          &bytes_returned,
          "submit Windows input report"
        );
      }

      std::optional<LvhWindowsOutputReportEvent> read_output_report(HANDLE stop_event) const {
        LvhWindowsOutputReportEvent event {};
        event.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
        event.size = sizeof(event);

        UniqueHandle operation_event {::CreateEventA(nullptr, TRUE, FALSE, nullptr)};
        if (!operation_event) {
          return std::nullopt;
        }

        OVERLAPPED overlapped {};
        overlapped.hEvent = operation_event.get();
        DWORD bytes_returned = 0;

        const auto started = ::DeviceIoControl(
          handle_.get(),
          LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT,
          nullptr,
          0,
          &event,
          sizeof(event),
          &bytes_returned,
          &overlapped
        );

        if (started == FALSE) {
          const auto error_code = ::GetLastError();
          if (error_code != ERROR_IO_PENDING) {
            return std::nullopt;
          }

          HANDLE wait_handles[] {
            operation_event.get(),
            stop_event,
          };
          const auto wait_result = ::WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
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

        if (bytes_returned < sizeof(event.version) + sizeof(event.size) + sizeof(event.driver_device_id) + sizeof(event.report_size)) {
          return std::nullopt;
        }

        event.report_size = std::min(event.report_size, static_cast<std::uint32_t>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE));
        return event;
      }

    private:
      WindowsControlChannel(std::string path, UniqueHandle handle):
          path_ {std::move(path)},
          handle_ {std::move(handle)} {}

      OperationStatus device_io_control(
        DWORD control_code,
        const void *input,
        DWORD input_size,
        void *output,
        DWORD output_size,
        DWORD *bytes_returned,
        std::string_view operation
      ) const {
        if (::DeviceIoControl(handle_.get(), control_code, const_cast<void *>(input), input_size, output, output_size, bytes_returned, nullptr) == FALSE) {
          return windows_failure(ErrorCode::backend_failure, operation, ::GetLastError());
        }

        return OperationStatus::success();
      }

      std::string path_;
      UniqueHandle handle_;
    };

    struct WindowsGamepadState {
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

      mutable std::mutex mutex;
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
          event_channel_ {std::move(event_channel)},
          stop_event_ {::CreateEventA(nullptr, TRUE, FALSE, nullptr)} {}

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

        const auto status = command_channel_->create_gamepad(request, response);
        if (!status.ok()) {
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

      OperationStatus submit_gamepad_report(const std::shared_ptr<WindowsGamepadState> &state, const std::vector<std::uint8_t> &report) {
        const auto driver_id = state->driver_id;
        return command_channel_->submit_input_report(driver_id, report);
      }

      OperationStatus close_gamepad(const std::shared_ptr<WindowsGamepadState> &state) {
        std::uint64_t driver_id = 0;
        {
          std::lock_guard lock {state->mutex};
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
          std::lock_guard lock {state->mutex};
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
      UniqueHandle stop_event_;
      std::jthread output_thread_;
      std::mutex devices_mutex_;
      std::map<std::uint64_t, std::weak_ptr<WindowsGamepadState>> gamepads_;
    };

    OperationStatus WindowsGamepad::submit(const std::vector<std::uint8_t> &report) {
      {
        std::lock_guard lock {state_->mutex};
        if (!state_->open) {
          return OperationStatus::failure(ErrorCode::device_closed, "Windows gamepad is closed");
        }
      }

      return context_->submit_gamepad_report(state_, report);
    }

    void WindowsGamepad::set_output_callback(OutputCallback callback) {
      std::lock_guard lock {state_->mutex};
      state_->output_callback = std::move(callback);
    }

    std::vector<DeviceNode> WindowsGamepad::device_nodes() const {
      std::lock_guard lock {state_->mutex};
      if (state_->path.empty()) {
        return {};
      }

      return {{.kind = DeviceNodeKind::other, .path = state_->path}};
    }

    OperationStatus WindowsGamepad::close() {
      return context_->close_gamepad(state_);
    }

    class WindowsBackend final: public Backend {
    public:
      WindowsBackend() {
        capabilities_.backend_name = "windows-umdf";
        capabilities_.requires_installed_driver = true;

        auto command_channel = WindowsControlChannel::open(resolve_control_device_path());
        auto event_channel = WindowsControlChannel::open(resolve_control_device_path());
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
        if (const auto validation = validate_windows_gamepad_profile(options.profile); !validation.ok()) {
          return {validation, nullptr};
        }

        if (!context_) {
          return {
            OperationStatus::failure(
              ErrorCode::backend_unavailable,
              "Windows UMDF control device is unavailable; install the libvirtualhid driver package"
            ),
            nullptr,
          };
        }

        return context_->create_gamepad(id, options);
      }

      BackendKeyboardCreationResult create_keyboard(DeviceId /*id*/, const CreateKeyboardOptions & /*options*/) override {
        return {unsupported_device_status(), nullptr};
      }

      BackendMouseCreationResult create_mouse(DeviceId /*id*/, const CreateMouseOptions & /*options*/) override {
        return {unsupported_device_status(), nullptr};
      }

      BackendTouchscreenCreationResult create_touchscreen(
        DeviceId /*id*/,
        const CreateTouchscreenOptions & /*options*/
      ) override {
        return {unsupported_device_status(), nullptr};
      }

      BackendTrackpadCreationResult create_trackpad(DeviceId /*id*/, const CreateTrackpadOptions & /*options*/) override {
        return {unsupported_device_status(), nullptr};
      }

      BackendPenTabletCreationResult create_pen_tablet(
        DeviceId /*id*/,
        const CreatePenTabletOptions & /*options*/
      ) override {
        return {unsupported_device_status(), nullptr};
      }

    private:
      static OperationStatus unsupported_device_status() {
        return OperationStatus::failure(
          ErrorCode::unsupported_profile,
          "Windows MVP backend currently supports gamepad devices only"
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
