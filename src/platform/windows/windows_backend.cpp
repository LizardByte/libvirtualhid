/**
 * @file src/platform/windows/windows_backend.cpp
 * @brief Windows UMDF control-channel backend definitions.
 */

// local includes
#include "core/backend.hpp"
#include "platform/windows/control_protocol.hpp"
#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)
  #include "platform/windows/windows_backend_test_hooks.hpp"
#endif

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
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
#include <Windows.h>

namespace lvh::detail {
  namespace {

    class WindowsBackendContext;

    using UniqueHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;

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

    std::string resolve_control_device_path() {
      constexpr auto environment_name = "LIBVIRTUALHID_WINDOWS_CONTROL_DEVICE";
      if (const auto required_size = ::GetEnvironmentVariableA(environment_name, nullptr, 0); required_size > 1U) {
        std::string path(required_size - 1U, '\0');
        const auto copied_size = ::GetEnvironmentVariableA(environment_name, path.data(), required_size);
        if (copied_size > 0U && copied_size < required_size) {
          return path;
        }
      }

      return std::string {windows::default_control_device_path};
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
        if (const auto status = device_io_control(
              LVH_WINDOWS_IOCTL_CREATE_GAMEPAD,
              request_copy,
              response,
              &bytes_returned,
              "create Windows gamepad"
            );
            !status.ok()) {
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

        if (const auto started = ::DeviceIoControl(
              handle_.get(),
              LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT,
              nullptr,
              0,
              &event,
              sizeof(event),
              &bytes_returned,
              &overlapped
            );
            started == FALSE) {
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

        if (constexpr auto event_header_size =
              sizeof(event.version) + sizeof(event.size) + sizeof(event.driver_device_id) + sizeof(event.report_size);
            bytes_returned < event_header_size) {
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

        if (::DeviceIoControl(
              handle_.get(),
              control_code,
              &input,
              sizeof(input),
              &output,
              sizeof(output),
              bytes_returned,
              nullptr
            ) == FALSE) {
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

        if (::DeviceIoControl(
              handle_.get(),
              control_code,
              &input,
              sizeof(input),
              nullptr,
              0,
              bytes_returned,
              nullptr
            ) == FALSE) {
          return windows_failure(backend_failure, operation, ::GetLastError());
        }

        return OperationStatus::success();
      }

      std::string path_;
      UniqueHandle handle_;
    };

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

    class WindowsBackend final: public Backend {
    public:
      WindowsBackend():
          WindowsBackend(
            Win32WindowsControlChannel::open(resolve_control_device_path()),
            Win32WindowsControlChannel::open(resolve_control_device_path())
          ) {}

      WindowsBackend(
        std::unique_ptr<WindowsControlChannel> command_channel,
        std::unique_ptr<WindowsControlChannel> event_channel
      ) {
        capabilities_.backend_name = "windows-umdf";
        capabilities_.requires_installed_driver = true;

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

        return context_->create_gamepad(id, options);
      }

      BackendKeyboardCreationResult create_keyboard(
        DeviceId /*id*/,
        const CreateKeyboardOptions & /*options*/
      ) override {
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

      BackendTrackpadCreationResult create_trackpad(
        DeviceId /*id*/,
        const CreateTrackpadOptions & /*options*/
      ) override {
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
        using enum ErrorCode;

        return OperationStatus::failure(
          unsupported_profile,
          "Windows MVP backend currently supports gamepad devices only"
        );
      }

      BackendCapabilities capabilities_;
      std::shared_ptr<WindowsBackendContext> context_;
    };

#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)

    class FakeWindowsControlChannelState {
    public:
      FakeWindowsControlChannelState() = default;

      FakeWindowsControlChannelState(std::string path, std::string response_device_path):
          path_ {std::move(path)},
          response_device_path_ {std::move(response_device_path)} {}

      const std::string &path() const {
        return path_;
      }

      void set_create_transport_status(OperationStatus status) {
        std::lock_guard lock {mutex_};
        create_transport_status_ = std::move(status);
      }

      void set_create_protocol_status(std::uint32_t status) {
        std::lock_guard lock {mutex_};
        create_protocol_status_ = status;
      }

      OperationStatus create_gamepad(
        const LvhWindowsCreateGamepadRequest &request,
        LvhWindowsCreateGamepadResponse &response
      ) {
        std::lock_guard lock {mutex_};
        create_requests_.push_back(request);
        if (!create_transport_status_.ok()) {
          return create_transport_status_;
        }

        response.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
        response.size = sizeof(response);
        response.status = create_protocol_status_;
        response.driver_device_id = next_driver_id_++;
        windows::copy_string(response.device_path, response_device_path_);
        return protocol_status(response.status, "Windows driver rejected gamepad creation");
      }

      OperationStatus destroy_device(std::uint64_t driver_device_id) {
        std::lock_guard lock {mutex_};
        destroyed_ids_.push_back(driver_device_id);
        return destroy_status_;
      }

      OperationStatus submit_input_report(const std::vector<std::uint8_t> &report) {
        using enum ErrorCode;

        if (report.size() > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
          return OperationStatus::failure(invalid_argument, "input report exceeds Windows control protocol limit");
        }

        std::lock_guard lock {mutex_};
        submit_reports_.push_back(report);
        return submit_status_;
      }

      std::optional<LvhWindowsOutputReportEvent> pop_output_event() {
        std::lock_guard lock {mutex_};
        if (output_events_.empty()) {
          return std::nullopt;
        }

        auto event = output_events_.front();
        output_events_.erase(output_events_.begin());
        return event;
      }

      bool output_events_empty() const {
        std::lock_guard lock {mutex_};
        return output_events_.empty();
      }

      std::uint64_t last_created_driver_id() const {
        std::lock_guard lock {mutex_};
        return next_driver_id_ - 1U;
      }

      void enqueue_output_event(const LvhWindowsOutputReportEvent &event) {
        std::lock_guard lock {mutex_};
        output_events_.push_back(event);
      }

      std::size_t create_request_count() const {
        std::lock_guard lock {mutex_};
        return create_requests_.size();
      }

      std::size_t submit_report_count() const {
        std::lock_guard lock {mutex_};
        return submit_reports_.size();
      }

      std::size_t destroy_request_count() const {
        std::lock_guard lock {mutex_};
        return destroyed_ids_.size();
      }

    private:
      mutable std::mutex mutex_;
      std::string path_ = R"(\\.\LibVirtualHid)";
      std::string response_device_path_ = R"(\\.\LibVirtualHid#100)";
      std::uint64_t next_driver_id_ = 100;
      OperationStatus create_transport_status_ = OperationStatus::success();
      std::uint32_t create_protocol_status_ = LVH_WINDOWS_STATUS_SUCCESS;
      OperationStatus submit_status_ = OperationStatus::success();
      OperationStatus destroy_status_ = OperationStatus::success();
      std::vector<LvhWindowsCreateGamepadRequest> create_requests_;
      std::vector<std::vector<std::uint8_t>> submit_reports_;
      std::vector<std::uint64_t> destroyed_ids_;
      std::vector<LvhWindowsOutputReportEvent> output_events_;
    };

    class FakeWindowsControlChannel final: public WindowsControlChannel {
    public:
      explicit FakeWindowsControlChannel(std::shared_ptr<FakeWindowsControlChannelState> state):
          state_ {std::move(state)} {}

      const std::string &path() const override {
        return state_->path();
      }

      OperationStatus create_gamepad(
        const LvhWindowsCreateGamepadRequest &request,
        LvhWindowsCreateGamepadResponse &response
      ) const override {
        return state_->create_gamepad(request, response);
      }

      OperationStatus destroy_device(std::uint64_t driver_device_id) const override {
        return state_->destroy_device(driver_device_id);
      }

      OperationStatus submit_input_report(
        std::uint64_t /*driver_device_id*/,
        const std::vector<std::uint8_t> &report
      ) const override {
        return state_->submit_input_report(report);
      }

      std::optional<LvhWindowsOutputReportEvent> read_output_report(HANDLE stop_event) const override {
        if (auto event = state_->pop_output_event(); event.has_value()) {
          return event;
        }

        static_cast<void>(::WaitForSingleObject(stop_event, 1U));
        return std::nullopt;
      }

    private:
      std::shared_ptr<FakeWindowsControlChannelState> state_;
    };

    std::unique_ptr<WindowsBackend> make_fake_windows_backend(
      std::shared_ptr<FakeWindowsControlChannelState> command_state,
      std::shared_ptr<FakeWindowsControlChannelState> event_state
    ) {
      return std::make_unique<WindowsBackend>(
        std::make_unique<FakeWindowsControlChannel>(std::move(command_state)),
        std::make_unique<FakeWindowsControlChannel>(std::move(event_state))
      );
    }

    template<typename Predicate>
    bool wait_until(Predicate predicate) {
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds {250};
      while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
          return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds {1});
      }

      return predicate();
    }

    void wait_for_output_events_to_drain(const std::shared_ptr<FakeWindowsControlChannelState> &state) {
      static_cast<void>(wait_until([&state] {
        return state->output_events_empty();
      }));
    }

    std::uint64_t last_created_driver_id(const std::shared_ptr<FakeWindowsControlChannelState> &state) {
      return state->last_created_driver_id();
    }

    void enqueue_output_report(
      const std::shared_ptr<FakeWindowsControlChannelState> &state,
      std::uint64_t driver_id,
      const DeviceProfile &profile
    ) {
      LvhWindowsOutputReportEvent event {};
      event.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
      event.size = sizeof(event);
      event.driver_device_id = driver_id;
      event.report_size = static_cast<std::uint32_t>(profile.output_report_size);
      event.report[0] = profile.report_id;
      event.report[1] = 0x78;
      event.report[2] = 0x56;
      event.report[3] = 0x34;
      event.report[4] = 0x12;

      state->enqueue_output_event(event);
    }

    OperationStatus create_gamepad_with_protocol_status(std::uint32_t protocol_status_code) {
      auto command_state = std::make_shared<FakeWindowsControlChannelState>();
      command_state->set_create_protocol_status(protocol_status_code);
      auto backend = make_fake_windows_backend(command_state, std::make_shared<FakeWindowsControlChannelState>());

      CreateGamepadOptions options;
      options.profile = profiles::xbox_360();
      return backend->create_gamepad(1, options).status;
    }

#endif

  }  // namespace

#if defined(LIBVIRTUALHID_ENABLE_TEST_HOOKS)

  namespace test {

    WindowsBackendLifecycleResult windows_backend_fake_channel_lifecycle() {
      WindowsBackendLifecycleResult result;
      auto command_state = std::make_shared<FakeWindowsControlChannelState>();
      auto event_state = std::make_shared<FakeWindowsControlChannelState>();
      auto backend = make_fake_windows_backend(command_state, event_state);
      result.capabilities = backend->capabilities();

      CreateGamepadOptions options;
      options.profile = profiles::xbox_360();
      auto created = backend->create_gamepad(7, options);
      result.create_status = created.status;
      if (created) {
        result.device_nodes = created.gamepad->device_nodes();
        std::vector<std::uint8_t> report(options.profile.input_report_size, 0x7A);
        result.submit_status = created.gamepad->submit(report);

        const auto driver_id = last_created_driver_id(command_state);
        enqueue_output_report(event_state, driver_id, options.profile);
        wait_for_output_events_to_drain(event_state);

        std::atomic_bool output_seen {false};
        created.gamepad->set_output_callback([&result, &output_seen](const GamepadOutput &output) {
          result.last_output = output;
          result.saw_output = true;
          output_seen.store(true);
        });
        enqueue_output_report(event_state, driver_id, options.profile);
        static_cast<void>(wait_until([&output_seen] {
          return output_seen.load();
        }));

        result.close_status = created.gamepad->close();
        enqueue_output_report(event_state, driver_id, options.profile);
        wait_for_output_events_to_drain(event_state);
        result.second_close_status = created.gamepad->close();
        result.submit_after_close_status = created.gamepad->submit(report);

        result.create_requests = command_state->create_request_count();
        result.submit_requests = command_state->submit_report_count();
        result.destroy_requests = command_state->destroy_request_count();
      }

      return result;
    }

    WindowsBackendFailureResult windows_backend_fake_channel_failures() {
      WindowsBackendFailureResult result;
      result.invalid_argument_status = create_gamepad_with_protocol_status(LVH_WINDOWS_STATUS_INVALID_ARGUMENT);
      result.unsupported_profile_status = create_gamepad_with_protocol_status(LVH_WINDOWS_STATUS_UNSUPPORTED_PROFILE);
      result.device_closed_status = create_gamepad_with_protocol_status(LVH_WINDOWS_STATUS_DEVICE_NOT_FOUND);
      result.backend_failure_status = create_gamepad_with_protocol_status(LVH_WINDOWS_STATUS_BACKEND_FAILURE);

      {
        auto command_state = std::make_shared<FakeWindowsControlChannelState>();
        command_state->set_create_transport_status(
          OperationStatus::failure(ErrorCode::backend_failure, "transport failed")
        );
        auto backend = make_fake_windows_backend(command_state, std::make_shared<FakeWindowsControlChannelState>());

        CreateGamepadOptions options;
        options.profile = profiles::xbox_360();
        result.transport_failure_status = backend->create_gamepad(2, options).status;
      }

      {
        WindowsBackend backend {nullptr, nullptr};

        CreateGamepadOptions options;
        options.profile = profiles::xbox_360();
        result.unavailable_status = backend.create_gamepad(3, options).status;
      }

      {
        auto command_state = std::make_shared<FakeWindowsControlChannelState>("", "");
        auto backend = make_fake_windows_backend(command_state, std::make_shared<FakeWindowsControlChannelState>());

        CreateGamepadOptions options;
        options.profile = profiles::xbox_360();
        auto created = backend->create_gamepad(4, options);
        result.empty_nodes_create_status = created.status;
        if (created) {
          result.empty_device_nodes = created.gamepad->device_nodes();
          result.oversized_submit_status =
            created.gamepad->submit(std::vector<std::uint8_t>(LVH_WINDOWS_MAX_INPUT_REPORT_SIZE + 1U, 0x7B));
        }
      }

      return result;
    }

    WindowsBackendUtilityResult windows_backend_fake_channel_utilities() {
      WindowsBackendUtilityResult result;
      result.default_device_path = resolve_control_device_path();

      constexpr auto environment_name = "LIBVIRTUALHID_WINDOWS_CONTROL_DEVICE";
      constexpr auto custom_path = R"(\\.\LibVirtualHid-Test)";
      std::array<char, 512> original_value {};
      const auto original_size = ::GetEnvironmentVariableA(
        environment_name,
        original_value.data(),
        static_cast<DWORD>(original_value.size())
      );
      static_cast<void>(::SetEnvironmentVariableA(environment_name, custom_path));
      result.custom_device_path = resolve_control_device_path();
      static_cast<void>(::SetEnvironmentVariableA(
        environment_name,
        original_size > 0U && original_size < original_value.size() ? original_value.data() : nullptr
      ));

      result.formatted_error_status =
        windows_failure(ErrorCode::backend_failure, "format known Windows error", ERROR_FILE_NOT_FOUND);
      result.fallback_error_status =
        windows_failure(ErrorCode::backend_failure, "format unknown Windows error", 0xE0000001U);

      auto invalid_context = std::make_shared<WindowsBackendContext>(
        std::unique_ptr<WindowsControlChannel> {},
        std::unique_ptr<WindowsControlChannel> {}
      );
      invalid_context->start();

      auto context = std::make_shared<WindowsBackendContext>(
        std::make_unique<FakeWindowsControlChannel>(std::make_shared<FakeWindowsControlChannelState>()),
        std::make_unique<FakeWindowsControlChannel>(std::make_shared<FakeWindowsControlChannelState>())
      );
      context->start();
      context->start();
      context->stop();

      result.timeout_result = wait_until([] {
        return false;
      });

      return result;
    }

  }  // namespace test

#endif

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<WindowsBackend>();
  }

}  // namespace lvh::detail
