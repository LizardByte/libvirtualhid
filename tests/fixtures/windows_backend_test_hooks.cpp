/**
 * @file tests/fixtures/windows_backend_test_hooks.cpp
 * @brief Windows backend test hook definitions.
 */

// local includes
#include "fixtures/windows_backend_test_hooks.hpp"

#define create_platform_backend create_platform_backend_for_windows_backend_test_hooks
#include "../../src/platform/windows/windows_backend.cpp"
#undef create_platform_backend

namespace lvh::detail {
  namespace {

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

  }  // namespace

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

}  // namespace lvh::detail
