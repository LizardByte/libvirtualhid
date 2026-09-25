/**
 * @file src/platform/macos/macos_broker_client.cpp
 * @brief Client side of the licensed macOS virtual HID broker.
 */

#include "platform/macos/macos_broker_client.hpp"

#include "platform/macos/broker/io.hpp"
#include "platform/windows/shared/lvh_windows_broker_config.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <libvirtualhid/license.hpp>
#include <libvirtualhid/report.hpp>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace lvh::detail {
  namespace {

    template<std::size_t Size>
    bool copy_text(std::array<char, Size> &destination, std::string_view text) {
      if (text.size() >= destination.size()) {
        return false;
      }
      std::copy(text.begin(), text.end(), destination.begin());
      return true;
    }

    std::string message_text(const macos_broker::Message &message) {
      return {message.message.data(), strnlen(message.message.data(), message.message.size())};
    }

    OperationStatus response_status(const macos_broker::Message &response) {
      if (response.type != macos_broker::MessageType::response ||
          response.status < 0 || response.status > static_cast<int>(ErrorCode::backend_failure)) {
        return OperationStatus::failure(ErrorCode::backend_failure, "macOS broker returned an invalid response");
      }
      if (response.status == 0) {
        return OperationStatus::success();
      }
      return OperationStatus::failure(static_cast<ErrorCode>(response.status), message_text(response));
    }

    class MacosGamepad final: public BackendGamepad {
    public:
      MacosGamepad(int fd, DeviceProfile profile):
          fd_ {fd},
          profile_ {std::move(profile)},
          callback_state_ {std::make_shared<CallbackState>()},
          reader_ {[this, fd] {
            read_loop(fd);
          }},
          callback_thread_ {[state = callback_state_, profile = profile_] {
            callback_loop(state, profile);
          }} {}

      ~MacosGamepad() override {
        static_cast<void>(close());
      }

      OperationStatus submit(const GamepadState & /*state*/, const std::vector<std::uint8_t> &report) override {
        if (report.empty() || report.size() > macos_broker::max_report_size) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "macOS gamepad report exceeds broker limit");
        }
        macos_broker::Message request;
        request.type = macos_broker::MessageType::submit;
        request.size = static_cast<std::uint32_t>(report.size());
        std::copy(report.begin(), report.end(), request.data.begin());
        return call(request);
      }

      void set_output_callback(OutputCallback callback) override {
        std::lock_guard lock {callback_state_->mutex};
        callback_state_->callback = std::move(callback);
      }

      OperationStatus close() override {
        {
          std::lock_guard lock {call_mutex_};
          if (fd_ < 0) {
            return OperationStatus::success();
          }
          macos_broker::Message request;
          request.type = macos_broker::MessageType::close;
          static_cast<void>(macos_broker::send_message(fd_, request));
          ::shutdown(fd_, SHUT_RDWR);
          ::close(fd_);
          fd_ = -1;
        }
        if (reader_.joinable()) {
          reader_.join();
        }
        {
          std::lock_guard lock {callback_state_->mutex};
          callback_state_->stop = true;
        }
        callback_state_->condition.notify_all();
        if (callback_thread_.joinable()) {
          if (callback_thread_.get_id() == std::this_thread::get_id()) {
            callback_thread_.detach();
          } else {
            callback_thread_.join();
          }
        }
        return OperationStatus::success();
      }

    private:
      struct CallbackState {
        std::mutex mutex;
        std::condition_variable condition;
        std::deque<std::vector<std::uint8_t>> reports;
        OutputCallback callback;
        bool stop = false;
      };

      static void callback_loop(const std::shared_ptr<CallbackState> &state, const DeviceProfile &profile) {
        for (;;) {
          std::vector<std::uint8_t> report;
          OutputCallback callback;
          {
            std::unique_lock lock {state->mutex};
            state->condition.wait(lock, [&] {
              return state->stop || !state->reports.empty();
            });
            if (state->reports.empty()) {
              return;
            }
            report = std::move(state->reports.front());
            state->reports.pop_front();
            callback = state->callback;
          }
          if (callback) {
            for (const auto &output : reports::parse_output_reports(profile, report)) {
              callback(output);
            }
          }
        }
      }

      OperationStatus call(const macos_broker::Message &request) {
        std::lock_guard call_lock {call_mutex_};
        if (fd_ < 0) {
          return OperationStatus::failure(ErrorCode::device_closed, "macOS gamepad is closed");
        }
        {
          std::lock_guard lock {mutex_};
          response_ready_ = false;
        }
        if (!macos_broker::send_message(fd_, request)) {
          return OperationStatus::failure(ErrorCode::backend_unavailable, "macOS broker connection closed");
        }
        std::unique_lock lock {mutex_};
        if (!response_condition_.wait_for(lock, std::chrono::seconds {10}, [this] {
              return response_ready_ || disconnected_;
            })) {
          lock.unlock();
          ::shutdown(fd_, SHUT_RDWR);
          return OperationStatus::failure(ErrorCode::backend_unavailable, "macOS broker did not answer gamepad report");
        }
        if (!response_ready_) {
          return OperationStatus::failure(ErrorCode::backend_unavailable, "macOS broker connection closed");
        }
        return response_status(response_);
      }

      void read_loop(int fd) {
        macos_broker::Message event;
        while (macos_broker::receive_message(fd, event)) {
          if (event.type == macos_broker::MessageType::response) {
            {
              std::lock_guard lock {mutex_};
              response_ = event;
              response_ready_ = true;
            }
            response_condition_.notify_all();
          } else if (event.type == macos_broker::MessageType::output &&
                     event.size > 0 && event.size <= macos_broker::max_report_size) {
            {
              std::lock_guard lock {callback_state_->mutex};
              if (callback_state_->reports.size() < 64U) {
                callback_state_->reports.emplace_back(event.data.begin(), event.data.begin() + event.size);
              }
            }
            callback_state_->condition.notify_one();
          }
        }
        {
          std::lock_guard lock {mutex_};
          disconnected_ = true;
        }
        response_condition_.notify_all();
        {
          std::lock_guard lock {callback_state_->mutex};
          callback_state_->stop = true;
        }
        callback_state_->condition.notify_all();
      }

      int fd_;
      DeviceProfile profile_;
      std::shared_ptr<CallbackState> callback_state_;
      std::thread reader_;
      std::thread callback_thread_;
      std::mutex call_mutex_;
      std::mutex mutex_;
      std::condition_variable response_condition_;
      macos_broker::Message response_;
      bool response_ready_ = false;
      bool disconnected_ = false;
    };

    LicenseResult license_call(macos_broker::Message request) {
      LicenseResult result;
      result.license.purchase_url = windows::broker_config::buy_url;
      result.license.manage_account_url = windows::broker_config::manage_account_url;
      std::string error;
      const int fd = macos_broker::connect_to_broker(error);
      if (fd < 0) {
        result.status = OperationStatus::failure(ErrorCode::backend_unavailable, error);
        result.license.message = error;
        return result;
      }
      macos_broker::Message response;
      const bool exchanged = macos_broker::send_message(fd, request) && macos_broker::receive_message(fd, response);
      ::close(fd);
      if (!exchanged) {
        result.status = OperationStatus::failure(ErrorCode::backend_unavailable, "macOS broker did not answer");
        result.license.message = result.status.message();
        return result;
      }
      result.status = response_status(response);
      result.license.service_available = true;
      result.license.state = response.license_state <= static_cast<std::uint32_t>(LicenseState::invalid) ?
                               static_cast<LicenseState>(response.license_state) :
                               LicenseState::invalid;
      result.license.active_devices = response.active_devices;
      result.license.activation_limit = response.activation_limit;
      result.license.activation_usage = response.activation_usage;
      result.license.plan_name = response.plan_name.data();
      result.license.customer_email = response.customer_email.data();
      result.license.message = message_text(response);
      return result;
    }

  }  // namespace

  BackendGamepadCreationResult create_macos_brokered_gamepad(DeviceId id, const CreateGamepadOptions &options) {
    const auto &profile = options.profile;
    if (profile.device_type != DeviceType::gamepad || profile.report_descriptor.empty() ||
        profile.report_descriptor.size() > macos_broker::max_descriptor_size ||
        profile.input_report_size == 0 || profile.input_report_size > macos_broker::max_report_size ||
        profile.output_report_size > macos_broker::max_report_size) {
      return {OperationStatus::failure(ErrorCode::unsupported_profile, "macOS broker requires a valid gamepad HID descriptor and report sizes"), nullptr};
    }
    macos_broker::Message request;
    request.type = macos_broker::MessageType::create;
    request.kind = static_cast<std::uint32_t>(profile.gamepad_kind);
    request.bus = static_cast<std::uint32_t>(profile.bus_type);
    request.vendor_id = profile.vendor_id;
    request.product_id = profile.product_id;
    request.device_version = profile.version;
    request.report_id = profile.report_id;
    request.input_report_size = static_cast<std::uint32_t>(profile.input_report_size);
    request.output_report_size = static_cast<std::uint32_t>(profile.output_report_size);
    request.descriptor_size = static_cast<std::uint32_t>(profile.report_descriptor.size());
    if (options.metadata.stable_id.empty()) {
      std::snprintf(request.stable_id.data(), request.stable_id.size(), "02:00:%02x:%02x:%02x:%02x", static_cast<unsigned>((id >> 24U) & 0xFFU), static_cast<unsigned>((id >> 16U) & 0xFFU), static_cast<unsigned>((id >> 8U) & 0xFFU), static_cast<unsigned>(id & 0xFFU));
    }
    if (!copy_text(request.name, profile.name) || !copy_text(request.manufacturer, profile.manufacturer) ||
        (!options.metadata.stable_id.empty() && !copy_text(request.stable_id, options.metadata.stable_id))) {
      return {OperationStatus::failure(ErrorCode::invalid_argument, "macOS gamepad identity exceeds broker limit"), nullptr};
    }
    std::copy(profile.report_descriptor.begin(), profile.report_descriptor.end(), request.data.begin());

    std::string error;
    const int fd = macos_broker::connect_to_broker(error);
    if (fd < 0) {
      return {OperationStatus::failure(ErrorCode::backend_unavailable, error), nullptr};
    }
    macos_broker::Message response;
    const bool exchanged = macos_broker::send_message(fd, request) && macos_broker::receive_message(fd, response);
    if (!exchanged) {
      ::close(fd);
      return {OperationStatus::failure(ErrorCode::backend_unavailable, "macOS broker did not answer gamepad creation"), nullptr};
    }
    const auto status = response_status(response);
    if (!status.ok()) {
      ::close(fd);
      return {status, nullptr};
    }
    timeval no_receive_timeout {.tv_sec = 0, .tv_usec = 0};
    static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &no_receive_timeout, sizeof(no_receive_timeout)));
    return {OperationStatus::success(), std::make_unique<MacosGamepad>(fd, profile)};
  }

}  // namespace lvh::detail

namespace lvh {

  LicenseResult get_license_status() {
    return detail::license_call({.type = detail::macos_broker::MessageType::status});
  }

  LicenseResult activate_license(std::string_view license_key, std::string_view instance_name) {
    detail::macos_broker::Message request;
    request.type = detail::macos_broker::MessageType::activate;
    if (license_key.empty() || !detail::copy_text(request.license_key, license_key) ||
        !detail::copy_text(request.instance_name, instance_name)) {
      LicenseResult result;
      result.status = OperationStatus::failure(ErrorCode::invalid_argument, "invalid macOS license key or instance name");
      result.license.message = result.status.message();
      return result;
    }
    return detail::license_call(request);
  }

  LicenseResult validate_license() {
    return detail::license_call({.type = detail::macos_broker::MessageType::validate});
  }

  LicenseResult deactivate_license() {
    return detail::license_call({.type = detail::macos_broker::MessageType::deactivate});
  }

}  // namespace lvh
