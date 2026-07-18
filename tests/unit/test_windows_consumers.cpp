/**
 * @file tests/unit/test_windows_consumers.cpp
 * @brief Windows consumer integration tests for installed VHF gamepads.
 */

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// Windows base types must be declared before the HID headers.
#include <Windows.h>

// platform includes
#include <hidsdi.h>
#include <SetupAPI.h>

// standard includes
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/libvirtualhid.hpp>

namespace {
  using namespace std::chrono_literals;

  class WindowsConsumerTest: public WindowsTest {};

  struct HidGamepadInterface {
    std::wstring path;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::uint16_t output_report_size = 0;
  };

  class DeviceInfoSet {
  public:
    explicit DeviceInfoSet(HDEVINFO value):
        value_ {value} {}

    DeviceInfoSet(const DeviceInfoSet &) = delete;
    DeviceInfoSet &operator=(const DeviceInfoSet &) = delete;

    ~DeviceInfoSet() {
      if (value_ != INVALID_HANDLE_VALUE) {
        static_cast<void>(SetupDiDestroyDeviceInfoList(value_));
      }
    }

    HDEVINFO get() const {
      return value_;
    }

  private:
    HDEVINFO value_;
  };

  class Handle {
  public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE):
        value_ {value} {}

    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;

    ~Handle() {
      if (value_ != INVALID_HANDLE_VALUE) {
        static_cast<void>(CloseHandle(value_));
      }
    }

    HANDLE get() const {
      return value_;
    }

    explicit operator bool() const {
      return value_ != INVALID_HANDLE_VALUE;
    }

  private:
    HANDLE value_;
  };

  std::vector<HidGamepadInterface> enumerate_gamepad_interfaces() {
    GUID hid_guid {};
    HidD_GetHidGuid(&hid_guid);
    DeviceInfoSet devices {SetupDiGetClassDevsW(
      &hid_guid,
      nullptr,
      nullptr,
      DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    )};
    if (devices.get() == INVALID_HANDLE_VALUE) {
      return {};
    }

    std::vector<HidGamepadInterface> interfaces;
    for (DWORD index = 0;; ++index) {
      SP_DEVICE_INTERFACE_DATA interface_data {};
      interface_data.cbSize = sizeof(interface_data);
      if (SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &hid_guid, index, &interface_data) == FALSE) {
        if (GetLastError() == ERROR_NO_MORE_ITEMS) {
          break;
        }
        continue;
      }

      DWORD required_size = 0;
      static_cast<void>(SetupDiGetDeviceInterfaceDetailW(
        devices.get(),
        &interface_data,
        nullptr,
        0,
        &required_size,
        nullptr
      ));
      if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        continue;
      }

      std::vector<std::byte> detail_storage(required_size);
      auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detail_storage.data());
      detail->cbSize = sizeof(*detail);
      if (SetupDiGetDeviceInterfaceDetailW(devices.get(), &interface_data, detail, required_size, nullptr, nullptr) == FALSE) {
        continue;
      }

      Handle handle {CreateFileW(
        detail->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
      )};
      if (!handle) {
        continue;
      }

      HIDD_ATTRIBUTES attributes {};
      attributes.Size = sizeof(attributes);
      PHIDP_PREPARSED_DATA preparsed_data = nullptr;
      HIDP_CAPS capabilities {};
      if (HidD_GetAttributes(handle.get(), &attributes) == FALSE || HidD_GetPreparsedData(handle.get(), &preparsed_data) == FALSE) {
        continue;
      }

      const auto caps_status = HidP_GetCaps(preparsed_data, &capabilities);
      static_cast<void>(HidD_FreePreparsedData(preparsed_data));
      if (caps_status != HIDP_STATUS_SUCCESS || capabilities.UsagePage != 0x01U || capabilities.Usage != 0x05U) {
        continue;
      }

      interfaces.push_back({
        .path = detail->DevicePath,
        .vendor_id = attributes.VendorID,
        .product_id = attributes.ProductID,
        .output_report_size = capabilities.OutputReportByteLength,
      });
    }
    return interfaces;
  }

  std::optional<HidGamepadInterface> wait_for_new_interface(
    const std::set<std::wstring> &previous_paths,
    std::uint16_t vendor_id,
    std::uint16_t product_id
  ) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
      const auto interfaces = enumerate_gamepad_interfaces();
      const auto position = std::ranges::find_if(interfaces, [&](const auto &interface) {
        return interface.vendor_id == vendor_id && interface.product_id == product_id &&
               !previous_paths.contains(interface.path);
      });
      if (position != interfaces.end()) {
        return *position;
      }
      std::this_thread::sleep_for(100ms);
    }
    return std::nullopt;
  }

}  // namespace

TEST_F(WindowsConsumerTest, NativeDualSenseOutputWriteReachesOwningRuntime) {
  const auto profile = lvh::profiles::dualsense_usb();
  std::set<std::wstring> previous_paths;
  for (const auto &interface : enumerate_gamepad_interfaces()) {
    previous_paths.insert(interface.path);
  }

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto decoy_runtime = lvh::Runtime::create(runtime_options);
  ASSERT_NE(decoy_runtime, nullptr);
  ASSERT_TRUE(decoy_runtime->capabilities().supports_gamepad);
  std::this_thread::sleep_for(100ms);

  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_NE(runtime, nullptr);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad)
    << "The installed libvirtualhid Windows driver is required for this integration test";

  lvh::CreateGamepadOptions options;
  options.profile = profile;
  std::mutex output_mutex;
  std::condition_variable output_ready;
  std::vector<lvh::GamepadOutput> outputs;

  auto created = lvh::GamepadStateAdapter::create(*runtime, options);
  ASSERT_TRUE(created) << created.status.message();
  created.adapter->set_output_callback([&](const lvh::GamepadOutput &output) {
    {
      std::lock_guard lock {output_mutex};
      outputs.push_back(output);
    }
    output_ready.notify_all();
  });

  const auto interface = wait_for_new_interface(previous_paths, profile.vendor_id, profile.product_id);
  ASSERT_TRUE(interface.has_value()) << "The VHF DualSense HID interface was not enumerated";
  ASSERT_EQ(interface->output_report_size, profile.output_report_size);

  Handle hid {CreateFileW(
    interface->path.c_str(),
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  )};
  ASSERT_TRUE(hid) << "Unable to open the VHF DualSense HID interface: " << GetLastError();

  std::vector<std::uint8_t> report(interface->output_report_size, 0);
  report[0] = 0x02;
  report[1] = 0x0D;
  report[2] = 0x04;
  report[3] = 0x80;
  report[4] = 0x40;
  DWORD bytes_written = 0;
  ASSERT_TRUE(WriteFile(
    hid.get(),
    report.data(),
    static_cast<DWORD>(report.size()),
    &bytes_written,
    nullptr
  )) << "WriteFile failed: "
     << GetLastError();
  ASSERT_EQ(bytes_written, report.size());

  {
    std::unique_lock lock {output_mutex};
    ASSERT_TRUE(output_ready.wait_for(lock, 5s, [&] {
      return std::ranges::any_of(outputs, [](const auto &output) {
        return output.kind == lvh::GamepadOutputKind::rumble;
      });
    }))
      << "No normalized rumble callback followed the native HID output writes";
  }

  const auto rumble = std::ranges::find_if(outputs, [](const auto &output) {
    return output.kind == lvh::GamepadOutputKind::rumble;
  });
  ASSERT_NE(rumble, outputs.end());
  EXPECT_GT(rumble->low_frequency_rumble, 0U);
  EXPECT_GT(rumble->high_frequency_rumble, 0U);
  EXPECT_EQ(rumble->raw_report, report);
}

TEST_F(WindowsConsumerTest, NativeXboxPidRumbleWritesAreNormalized) {
  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_NE(runtime, nullptr);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad)
    << "The installed libvirtualhid Windows driver is required for this integration test";

  for (const auto &profile : {lvh::profiles::xbox_one(), lvh::profiles::xbox_series()}) {
    std::set<std::wstring> previous_paths;
    for (const auto &interface : enumerate_gamepad_interfaces()) {
      previous_paths.insert(interface.path);
    }

    lvh::CreateGamepadOptions options;
    options.profile = profile;
    std::mutex output_mutex;
    std::condition_variable output_ready;
    std::vector<lvh::GamepadOutput> outputs;

    auto created = lvh::GamepadStateAdapter::create(*runtime, options);
    ASSERT_TRUE(created) << created.status.message();
    created.adapter->set_output_callback([&](const lvh::GamepadOutput &output) {
      {
        std::lock_guard lock {output_mutex};
        outputs.push_back(output);
      }
      output_ready.notify_all();
    });

    const auto interface = wait_for_new_interface(previous_paths, profile.vendor_id, profile.product_id);
    ASSERT_TRUE(interface.has_value()) << "The VHF Xbox HID interface was not enumerated";
    ASSERT_EQ(interface->output_report_size, profile.output_report_size + 1U);

    Handle hid {CreateFileW(
      interface->path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    )};
    ASSERT_TRUE(hid) << "Unable to open the VHF Xbox HID interface: " << GetLastError();

    const std::vector<std::uint8_t> report {0, 0x0F, 25, 50, 75, 100, 10, 0, 0};
    DWORD bytes_written = 0;
    ASSERT_TRUE(WriteFile(
      hid.get(),
      report.data(),
      static_cast<DWORD>(report.size()),
      &bytes_written,
      nullptr
    )) << "WriteFile failed: "
       << GetLastError();
    ASSERT_EQ(bytes_written, report.size());

    {
      std::unique_lock lock {output_mutex};
      ASSERT_TRUE(output_ready.wait_for(lock, 5s, [&] {
        return std::ranges::any_of(outputs, [](const auto &output) {
          return output.kind == lvh::GamepadOutputKind::rumble;
        });
      }))
        << "No normalized Xbox rumble callback followed the native HID output write";
    }

    const auto rumble = std::ranges::find_if(outputs, [](const auto &output) {
      return output.kind == lvh::GamepadOutputKind::rumble;
    });
    ASSERT_NE(rumble, outputs.end());
    EXPECT_EQ(rumble->low_frequency_rumble, 49151U);
    EXPECT_EQ(rumble->high_frequency_rumble, 65535U);
    ASSERT_TRUE(created.adapter->close().ok());
  }
}
