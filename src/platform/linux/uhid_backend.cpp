/**
 * @file src/platform/linux/uhid_backend.cpp
 * @brief Linux UHID backend definitions.
 */

// standard includes
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// platform includes
#include <fcntl.h>
#ifndef __user
  #define __user
#endif
#include <linux/uhid.h>
#include <poll.h>
#include <unistd.h>

// local includes
#include "core/backend.hpp"

#include <libvirtualhid/report.hpp>

namespace lvh::detail {
  namespace {

    constexpr auto uhid_path = "/dev/uhid";
    constexpr auto poll_timeout_ms = 100;

    std::string errno_message(int error) {
      return std::error_code(error, std::generic_category()).message();
    }

    Status system_error_status(ErrorCode code, const std::string &operation, int error) {
      return Status::failure(code, operation + ": " + errno_message(error));
    }

    bool can_access_uhid() {
      return ::access(uhid_path, R_OK | W_OK) == 0;
    }

    std::uint16_t to_uhid_bus(BusType bus_type) {
      if (bus_type == BusType::bluetooth) {
        return BUS_BLUETOOTH;
      }
      return BUS_USB;
    }

    template<std::size_t Size>
    void copy_string(__u8 (&destination)[Size], const std::string &source) {
      const auto length = std::min(source.size(), Size - 1);
      std::memcpy(destination, source.data(), length);
      destination[length] = 0;
    }

    /**
     * @brief Backend gamepad backed by one Linux UHID file descriptor.
     */
    class UhidGamepad final: public BackendGamepad {
    public:
      explicit UhidGamepad(int file_descriptor):
          fd_ {file_descriptor} {}

      UhidGamepad(const UhidGamepad &) = delete;
      UhidGamepad &operator=(const UhidGamepad &) = delete;
      UhidGamepad(UhidGamepad &&) noexcept = delete;
      UhidGamepad &operator=(UhidGamepad &&) noexcept = delete;

      ~UhidGamepad() override {
        static_cast<void>(close());
      }

      Status create(DeviceId id, const CreateGamepadOptions &options) {
        uhid_event event {};
        auto &request = event.u.create2;

        if (options.profile.report_descriptor.size() > sizeof(request.rd_data)) {
          return Status::failure(ErrorCode::unsupported_profile, "HID report descriptor is too large for UHID");
        }

        event.type = UHID_CREATE2;
        copy_string(request.name, options.profile.name);
        copy_string(request.phys, "libvirtualhid/uhid/" + std::to_string(id));
        copy_string(request.uniq, options.metadata.stable_id.empty() ? std::to_string(id) : options.metadata.stable_id);
        request.rd_size = static_cast<std::uint16_t>(options.profile.report_descriptor.size());
        request.bus = to_uhid_bus(options.profile.bus_type);
        request.vendor = options.profile.vendor_id;
        request.product = options.profile.product_id;
        request.version = options.profile.version;
        std::memcpy(request.rd_data, options.profile.report_descriptor.data(), options.profile.report_descriptor.size());
        profile_ = options.profile;

        if (const auto status = write_event(event); !status.ok()) {
          return status;
        }

        running_ = true;
        reader_ = std::thread {[this]() {
          read_loop();
        }};
        return Status::success();
      }

      Status submit(const std::vector<std::uint8_t> &report) override {
        if (!open_) {
          return Status::failure(ErrorCode::device_closed, "UHID gamepad is closed");
        }

        uhid_event event {};
        if (report.size() > sizeof(event.u.input2.data)) {
          return Status::failure(ErrorCode::invalid_argument, "HID input report is too large for UHID");
        }

        event.type = UHID_INPUT2;
        event.u.input2.size = static_cast<std::uint16_t>(report.size());
        std::memcpy(event.u.input2.data, report.data(), report.size());
        return write_event(event);
      }

      void set_output_callback(OutputCallback callback) override {
        std::lock_guard lock {callback_mutex_};
        output_callback_ = std::move(callback);
      }

      Status close() override {
        if (!open_.exchange(false)) {
          return Status::success();
        }

        running_ = false;

        auto status = Status::success();
        if (fd_ >= 0) {
          uhid_event event {};
          event.type = UHID_DESTROY;
          status = write_event(event);
        }

        if (reader_.joinable()) {
          reader_.join();
        }

        if (fd_ >= 0) {
          if (::close(fd_) != 0 && status.ok()) {
            status = system_error_status(ErrorCode::backend_failure, "failed to close /dev/uhid", errno);
          }
          fd_ = -1;
        }

        return status;
      }

    private:
      Status write_event(const uhid_event &event) {
        std::lock_guard lock {write_mutex_};
        if (fd_ < 0) {
          return Status::failure(ErrorCode::device_closed, "UHID file descriptor is closed");
        }

        const auto result = ::write(fd_, &event, sizeof(event));
        if (result < 0) {
          return system_error_status(ErrorCode::backend_failure, "failed to write UHID event", errno);
        }
        if (static_cast<std::size_t>(result) != sizeof(event)) {
          return Status::failure(ErrorCode::backend_failure, "short write while sending UHID event");
        }

        return Status::success();
      }

      void read_loop() {
        while (running_) {
          pollfd descriptor {};
          descriptor.fd = fd_;
          descriptor.events = POLLIN;

          const auto result = ::poll(&descriptor, 1, poll_timeout_ms);
          if (result < 0) {
            if (errno == EINTR) {
              continue;
            }
            break;
          }
          if (result == 0) {
            continue;
          }
          if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            break;
          }
          if ((descriptor.revents & POLLIN) == 0) {
            continue;
          }

          uhid_event event {};
          const auto read_result = ::read(fd_, &event, sizeof(event));
          if (read_result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
              continue;
            }
            break;
          }
          if (read_result == 0) {
            break;
          }

          handle_event(event);
        }
      }

      void handle_event(const uhid_event &event) {
        switch (event.type) {
          case UHID_OUTPUT:
            dispatch_output_report(event.u.output.data, event.u.output.size);
            break;
          case UHID_GET_REPORT:
            send_get_report_reply(event.u.get_report.id);
            break;
          case UHID_SET_REPORT:
            dispatch_output_report(event.u.set_report.data, event.u.set_report.size);
            send_set_report_reply(event.u.set_report.id, 0);
            break;
          default:
            break;
        }
      }

      void dispatch_output_report(const __u8 *data, std::size_t report_size) {
        OutputCallback callback;
        {
          std::lock_guard lock {callback_mutex_};
          callback = output_callback_;
        }

        if (!callback) {
          return;
        }

        const auto size = std::min<std::size_t>(report_size, UHID_DATA_MAX);
        std::vector<std::uint8_t> report(data, data + size);
        auto output = reports::parse_output_report(profile_, report);
        callback(output);
      }

      void send_get_report_reply(std::uint32_t id) {
        uhid_event event {};
        event.type = UHID_GET_REPORT_REPLY;
        event.u.get_report_reply.id = id;
        event.u.get_report_reply.err = EIO;
        static_cast<void>(write_event(event));
      }

      void send_set_report_reply(std::uint32_t id, std::uint16_t error) {
        uhid_event event {};
        event.type = UHID_SET_REPORT_REPLY;
        event.u.set_report_reply.id = id;
        event.u.set_report_reply.err = error;
        static_cast<void>(write_event(event));
      }

      int fd_ = -1;
      DeviceProfile profile_;
      std::atomic_bool open_ = true;
      std::atomic_bool running_ = false;
      std::thread reader_;
      std::mutex write_mutex_;
      std::mutex callback_mutex_;
      OutputCallback output_callback_;
    };

    /**
     * @brief Linux platform backend backed by UHID.
     */
    class LinuxUhidBackend final: public Backend {
    public:
      LinuxUhidBackend() {
        const auto uhid_accessible = can_access_uhid();
        capabilities_.backend_name = "linux-uhid";
        capabilities_.supports_virtual_hid = uhid_accessible;
        capabilities_.supports_gamepad = uhid_accessible;
        capabilities_.supports_output_reports = uhid_accessible;
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) override {
        const auto fd = ::open(uhid_path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) {
          return {system_error_status(ErrorCode::backend_unavailable, "failed to open /dev/uhid", errno), nullptr};
        }

        auto gamepad = std::make_unique<UhidGamepad>(fd);
        if (const auto status = gamepad->create(id, options); !status.ok()) {
          static_cast<void>(gamepad->close());
          return {status, nullptr};
        }

        return {Status::success(), std::move(gamepad)};
      }

    private:
      BackendCapabilities capabilities_;
    };

  }  // namespace

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<LinuxUhidBackend>();
  }

}  // namespace lvh::detail
