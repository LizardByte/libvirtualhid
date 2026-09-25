// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/macos/broker/libvirtualhid_macos_broker.cpp
 * @brief Root-owned licensed macOS virtual HID service.
 */

#include "io.hpp"
#include "license_manager.hpp"
#include "shared/playstation_feature_reports.hpp"
#include "shared/switch_pro_protocol.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <IOKit/hidsystem/IOHIDUserDevice.h>
#include <libvirtualhid/types.hpp>
#include <mach/mach_time.h>
#include <mutex>
#include <poll.h>
#include <span>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lvh::detail::macos_broker {
  namespace {

    namespace ps = playstation_feature_reports;

    template<std::size_t DestinationSize, std::size_t TextSize>
    void set_text(std::array<char, DestinationSize> &destination, const char (&text)[TextSize]) {
      static_assert(TextSize <= DestinationSize);
      std::ranges::copy(text, destination.begin());
    }

    template<std::size_t MessageSize>
    Message response_with_error(lvh::ErrorCode code, const char (&message)[MessageSize]) {
      Message response;
      response.type = MessageType::response;
      response.status = std::to_underlying(code);
      set_text(response.message, message);
      return response;
    }

    bool terminated(const std::array<char, max_text_size> &text) {
      return std::memchr(text.data(), '\0', text.size()) != nullptr;
    }

    void set_property(CFMutableDictionaryRef properties, CFStringRef key, const char *value) {
      CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, value, kCFStringEncodingUTF8);
      if (string) {
        CFDictionarySetValue(properties, key, string);
        CFRelease(string);
      }
    }

    void set_number(CFMutableDictionaryRef properties, CFStringRef key, std::uint32_t value) {
      CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &value);
      if (number) {
        CFDictionarySetValue(properties, key, number);
        CFRelease(number);
      }
    }

    std::uint32_t crc32(std::span<const std::uint8_t> bytes, std::uint8_t seed) {
      std::uint32_t crc = 0xFFFFFFFFU;
      auto advance = [&crc](std::uint8_t value) {
        crc ^= value;
        for (int bit = 0; bit < 8; ++bit) {
          crc = crc & 1U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
      };
      advance(seed);
      for (const auto byte : bytes) {
        advance(byte);
      }
      return ~crc;
    }

    std::array<std::uint8_t, 6> mac_address(const Message &request) {
      std::array<std::uint8_t, 6> mac {};
      if (std::array<unsigned, 6> bytes {}; std::sscanf(request.stable_id.data(), "%2x:%2x:%2x:%2x:%2x:%2x", &bytes[0], &bytes[1], &bytes[2], &bytes[3], &bytes[4], &bytes[5]) == 6) {
        for (std::size_t index = 0; index < mac.size(); ++index) {
          mac[index] = static_cast<std::uint8_t>(bytes[index]);
        }
        return mac;
      }
      std::uint32_t hash = 2166136261U;
      for (const char *value = request.stable_id.data(); *value; ++value) {
        hash = (hash ^ static_cast<std::uint8_t>(*value)) * 16777619U;
      }
      return {0x02, 0x00, static_cast<std::uint8_t>(hash >> 24U), static_cast<std::uint8_t>(hash >> 16U), static_cast<std::uint8_t>(hash >> 8U), static_cast<std::uint8_t>(hash)};
    }

    std::vector<std::uint8_t> feature_report(const Message &request, std::uint32_t report_id) {
      using lvh::GamepadProfileKind;
      const auto kind = static_cast<GamepadProfileKind>(request.kind);
      auto copy = [](auto payload) {
        return std::vector<std::uint8_t> {payload.begin(), payload.end()};
      };
      std::vector<std::uint8_t> result;
      if (kind == GamepadProfileKind::dualshock4) {
        switch (report_id) {
          case ps::dualshock4_usb_calibration_report:
            result = copy(ps::dualshock4_usb_calibration_info);
            break;
          case ps::dualshock4_bluetooth_calibration_report:
            if (request.bus == static_cast<std::uint32_t>(std::to_underlying(lvh::BusType::bluetooth))) {
              result = copy(ps::dualshock4_bluetooth_calibration_info);
            }
            break;
          case ps::dualshock4_pairing_report:
            result = copy(ps::dualshock4_pairing_info);
            break;
          case ps::dualshock4_firmware_report:
            result = copy(ps::dualshock4_firmware_info);
            break;
          default:
            break;
        }
      } else if (kind == GamepadProfileKind::dualsense) {
        switch (report_id) {
          case ps::dualsense_calibration_report:
            result = copy(ps::dualsense_calibration_info);
            break;
          case ps::dualsense_pairing_report:
            result = copy(ps::dualsense_pairing_info);
            break;
          case ps::dualsense_firmware_report:
            result = copy(ps::dualsense_firmware_info);
            break;
          default:
            break;
        }
      }
      if (!result.empty() && ((report_id == ps::dualshock4_pairing_report && kind == GamepadProfileKind::dualshock4) || (report_id == ps::dualsense_pairing_report && kind == GamepadProfileKind::dualsense))) {
        const auto mac = mac_address(request);
        for (std::size_t index = 0; index < mac.size(); ++index) {
          result[1U + index] = mac[mac.size() - 1U - index];
        }
      }
      if (!result.empty() && request.bus == static_cast<std::uint32_t>(std::to_underlying(lvh::BusType::bluetooth)) && result.size() >= 4U) {
        const auto value = crc32(std::span {result.data(), result.size() - 4U}, ps::playstation_feature_crc_seed);
        for (std::size_t index = 0; index < 4U; ++index) {
          result[result.size() - 4U + index] = static_cast<std::uint8_t>(value >> (8U * index));
        }
      }
      return result;
    }

    struct DeviceSession {
      int fd = -1;
      IOHIDUserDeviceRef device = nullptr;
      std::mutex writer;
      std::atomic<bool> open {true};

      bool send(const Message &message) {
        std::lock_guard lock {writer};
        if (!open.load()) {
          return false;
        }
        if (!send_message(fd, message)) {
          open = false;
          ::shutdown(fd, SHUT_RDWR);
          return false;
        }
        return true;
      }

      void output(const std::uint8_t *data, std::size_t size, std::uint32_t report_id, const Message &profile) {
        if (size == 0 || size > max_report_size) {
          return;
        }
        Message event;
        event.type = MessageType::output;
        if (report_id != 0 && data[0] != report_id) {
          event.data[0] = static_cast<std::uint8_t>(report_id);
          ++event.size;
        }
        if (event.size + size > max_report_size) {
          return;
        }
        std::copy_n(data, size, event.data.begin() + event.size);
        event.size += static_cast<std::uint32_t>(size);
        static_cast<void>(send(event));

        if (profile.kind == static_cast<std::uint32_t>(std::to_underlying(lvh::GamepadProfileKind::switch_pro))) {
          const auto reply = switch_pro_protocol::make_switch_pro_reply(std::span {event.data.data(), event.size});
          if (reply) {
            static_cast<void>(IOHIDUserDeviceHandleReportWithTimeStamp(device, mach_absolute_time(), reply->data(), reply->size()));
          }
        }
      }
    };

    IOHIDUserDeviceRef create_device(const Message &request, DeviceSession &session, dispatch_semaphore_t cancelled) {
      CFMutableDictionaryRef properties = CFDictionaryCreateMutable(
        kCFAllocatorDefault,
        0,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks
      );
      if (!properties) {
        return nullptr;
      }
      CFDataRef descriptor = CFDataCreate(kCFAllocatorDefault, request.data.data(), request.descriptor_size);
      if (!descriptor) {
        CFRelease(properties);
        return nullptr;
      }
      CFDictionarySetValue(properties, CFSTR(kIOHIDReportDescriptorKey), descriptor);
      CFRelease(descriptor);
      set_property(properties, CFSTR(kIOHIDProductKey), request.name.data());
      set_property(properties, CFSTR(kIOHIDManufacturerKey), request.manufacturer.data());
      set_property(properties, CFSTR(kIOHIDTransportKey), request.bus == static_cast<std::uint32_t>(std::to_underlying(lvh::BusType::bluetooth)) ? kIOHIDTransportBluetoothValue : kIOHIDTransportUSBValue);
      set_property(properties, CFSTR(kIOHIDSerialNumberKey), request.stable_id.data());
      set_number(properties, CFSTR(kIOHIDVendorIDKey), request.vendor_id);
      set_number(properties, CFSTR(kIOHIDProductIDKey), request.product_id);
      set_number(properties, CFSTR(kIOHIDVersionNumberKey), request.device_version);
      IOHIDUserDeviceRef device = IOHIDUserDeviceCreateWithProperties(
        kCFAllocatorDefault,
        properties,
        IOHIDUserDeviceOptionsCreateOnActivate
      );
      CFRelease(properties);
      if (!device) {
        return nullptr;
      }
      session.device = device;
      DeviceSession *session_ptr = &session;
      const Message profile = request;
      IOHIDUserDeviceRegisterSetReportBlock(device, ^IOReturn(IOHIDReportType type, uint32_t report_id, const uint8_t *data, CFIndex size) {
        if (type != kIOHIDReportTypeOutput || size < 0 || (size > 0 && !data)) {
          return kIOReturnUnsupported;
        }
        session_ptr->output(data, static_cast<std::size_t>(size), report_id, profile);
        return kIOReturnSuccess;
      });
      IOHIDUserDeviceRegisterGetReportBlock(device, ^IOReturn(IOHIDReportType type, uint32_t report_id, uint8_t *data, CFIndex *size) {
        if (type != kIOHIDReportTypeFeature || !data || !size || *size < 0) {
          return kIOReturnUnsupported;
        }
        const auto report = feature_report(profile, report_id);
        if (report.empty() || report.size() > static_cast<std::size_t>(*size)) {
          return kIOReturnBadArgument;
        }
        std::copy(report.begin(), report.end(), data);
        *size = static_cast<CFIndex>(report.size());
        return kIOReturnSuccess;
      });
      IOHIDUserDeviceSetDispatchQueue(device, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
      IOHIDUserDeviceSetCancelHandler(device, ^{
        dispatch_semaphore_signal(cancelled);
      });
      IOHIDUserDeviceActivate(device);
      return device;
    }

    bool valid_create_request(const Message &request) {
      return request.type == MessageType::create && request.descriptor_size != 0 && request.descriptor_size <= max_descriptor_size && request.input_report_size != 0 && request.input_report_size <= max_report_size && request.output_report_size <= max_report_size && terminated(request.name) && terminated(request.manufacturer) && terminated(request.stable_id) && request.kind <= static_cast<std::uint32_t>(std::to_underlying(lvh::GamepadProfileKind::dualshock4)) && request.bus <= static_cast<std::uint32_t>(std::to_underlying(lvh::BusType::bluetooth));
    }

    void receive_reports(DeviceSession &session, LicenseManager &licenses, bool evaluation, std::uint32_t expected_input_size) {
      Message request;
      for (;;) {
        pollfd descriptor {.fd = session.fd, .events = POLLIN, .revents = 0};
        const int polled = ::poll(&descriptor, 1, 1000);
        if (!licenses.device_is_authorized(evaluation) || (polled < 0 && errno != EINTR)) {
          return;
        }
        if (polled == 0 || polled < 0) {
          continue;
        }
        if ((descriptor.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0 || !receive_message(session.fd, request) || request.type == MessageType::close) {
          return;
        }
        if (request.type != MessageType::submit || request.size != expected_input_size || request.size > max_report_size) {
          auto failure = response_with_error(lvh::ErrorCode::invalid_argument, "Invalid gamepad input report");
          static_cast<void>(session.send(failure));
          continue;
        }
        const auto result = IOHIDUserDeviceHandleReportWithTimeStamp(session.device, mach_absolute_time(), request.data.data(), request.size);
        auto submit_response = Message {};
        submit_response.type = MessageType::response;
        if (result != kIOReturnSuccess) {
          submit_response = response_with_error(lvh::ErrorCode::backend_failure, "macOS rejected virtual HID input report");
        }
        if (!session.send(submit_response)) {
          return;
        }
      }
    }

    void serve_client(int fd, LicenseManager &licenses) {
      timeval receive_timeout {.tv_sec = 5, .tv_usec = 0};
      timeval send_timeout {.tv_sec = 5, .tv_usec = 0};
      static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &receive_timeout, sizeof(receive_timeout)));
      static_cast<void>(::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_timeout, sizeof(send_timeout)));
      Message request;
      if (!receive_message(fd, request)) {
        ::close(fd);
        return;
      }
      if (request.type == MessageType::status || request.type == MessageType::activate || request.type == MessageType::validate || request.type == MessageType::deactivate) {
        const auto response = licenses.handle(request);
        static_cast<void>(send_message(fd, response));
        ::close(fd);
        return;
      }
      if (!valid_create_request(request)) {
        static_cast<void>(send_message(fd, response_with_error(lvh::ErrorCode::invalid_argument, "Invalid macOS gamepad request")));
        ::close(fd);
        return;
      }
      Message response;
      bool evaluation = false;
      if (!licenses.authorize_create(response, evaluation)) {
        static_cast<void>(send_message(fd, response));
        ::close(fd);
        return;
      }
      DeviceSession session;
      session.fd = fd;
      dispatch_semaphore_t cancelled = dispatch_semaphore_create(0);
      const auto device = create_device(request, session, cancelled);
      if (!device) {
        static_cast<void>(send_message(fd, response_with_error(lvh::ErrorCode::backend_failure, "Virtual HID creation failed; check the broker's Apple virtual HID entitlement and provisioning profile")));
        ::close(fd);
        return;
      }
      licenses.add_device(evaluation);
      response.type = MessageType::response;
      response.status = 0;
      static_cast<void>(session.send(response));
      receive_reports(session, licenses, evaluation, request.input_report_size);
      session.open = false;
      IOHIDUserDeviceCancel(device);
      static_cast<void>(dispatch_semaphore_wait(cancelled, DISPATCH_TIME_FOREVER));
      CFRelease(device);
      licenses.remove_device(evaluation);
      ::close(fd);
    }

  }  // namespace
}  // namespace lvh::detail::macos_broker

int main() {
  using namespace lvh::detail::macos_broker;
  if (::geteuid() != 0) {
    return 1;
  }
  // New broker-owned files default to owner-only access.
  ::umask(0077);
  ::signal(SIGPIPE, SIG_IGN);
  if (::mkdir("/var/run/libvirtualhid", 0755) != 0 && errno != EEXIST) {
    return 1;
  }
  struct stat directory {};
  // Clients need traverse access, while root alone can change this directory.
  if (::lstat("/var/run/libvirtualhid", &directory) != 0 || !S_ISDIR(directory.st_mode) || directory.st_uid != 0 || ::chmod("/var/run/libvirtualhid", 0755) != 0) {
    return 1;
  }
  const int lock_fd = ::open("/var/run/libvirtualhid/broker.lock", O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
  if (lock_fd < 0 || ::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    return 1;
  }
  ::unlink(socket_path);
  const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0) {
    return 1;
  }
  sockaddr_un address {};
  address.sun_family = AF_UNIX;
  constexpr std::string_view path {socket_path};
  static_assert(path.size() < sizeof(address.sun_path));
  std::ranges::copy(path, address.sun_path);
  // Local unprivileged clients need to connect; licensing is enforced per request.
  if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 || ::chmod(socket_path, 0666) != 0 || ::listen(listener, 32) != 0) {
    return 1;
  }
  LicenseManager licenses;
  while (true) {
    const int client = ::accept(listener, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    auto uid = static_cast<uid_t>(-1);
    auto gid = static_cast<gid_t>(-1);
    if (::getpeereid(client, &uid, &gid) != 0) {
      ::close(client);
      continue;
    }
    // Serve clients concurrently; a session can remain open for a game's lifetime.
    std::jthread {[client, &licenses] {
      serve_client(client, licenses);
    }}.detach();
  }
  ::close(listener);
  return 1;
}
