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
#ifndef DIRECTINPUT_VERSION
  #define DIRECTINPUT_VERSION 0x0800
#endif

// Windows base types must be declared before the HID headers.
#include <Windows.h>

// platform includes
#include <dinput.h>
#include <hidsdi.h>
#include <SetupAPI.h>

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// local includes
#include "fixtures/fixtures.hpp"
#include "platform/windows/control_protocol.hpp"

#include <libvirtualhid/libvirtualhid.hpp>

namespace {
  using namespace std::chrono_literals;
  using HidInterfacePaths = std::set<std::wstring, std::less<>>;

  class WindowsConsumerTest: public WindowsTest {};

  struct HidGamepadInterface {
    std::wstring path;
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    std::uint16_t usage = 0;
    std::uint16_t input_report_size = 0;
    std::uint16_t output_report_size = 0;
    std::uint16_t feature_report_size = 0;
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

  template<typename Interface>
  struct ComReleaser {
    void operator()(Interface *value) const {
      if (value != nullptr) {
        value->Release();
      }
    }
  };

  template<typename Interface>
  using ComInterface = std::unique_ptr<Interface, ComReleaser<Interface>>;

  struct DirectInputDeviceMatch {
    std::uint16_t vendor_id = 0;
    std::uint16_t product_id = 0;
    GUID instance_guid {};
    bool found = false;
  };

  BOOL CALLBACK find_directinput_device(const DIDEVICEINSTANCEW *instance, void *context) {
    auto &match = *static_cast<DirectInputDeviceMatch *>(context);
    const auto vendor_id = static_cast<std::uint16_t>(LOWORD(instance->guidProduct.Data1));
    if (const auto product_id = static_cast<std::uint16_t>(HIWORD(instance->guidProduct.Data1)); vendor_id != match.vendor_id || product_id != match.product_id) {
      return DIENUM_CONTINUE;
    }

    match.instance_guid = instance->guidInstance;
    match.found = true;
    return DIENUM_STOP;
  }

  std::optional<GUID> wait_for_directinput_device(
    IDirectInput8W &direct_input,
    std::uint16_t vendor_id,
    std::uint16_t product_id
  ) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
      DirectInputDeviceMatch match {.vendor_id = vendor_id, .product_id = product_id};
      if (const auto status = direct_input.EnumDevices(DI8DEVCLASS_GAMECTRL, find_directinput_device, &match, DIEDFL_ATTACHEDONLY); SUCCEEDED(status) && match.found) {
        return match.instance_guid;
      }
      std::this_thread::sleep_for(100ms);
    }
    return std::nullopt;
  }

  BOOL CALLBACK find_constant_force_effect(const DIEFFECTINFOW *effect, void *context) {
    auto &found = *static_cast<bool *>(context);
    if (IsEqualGUID(effect->guid, GUID_ConstantForce) != FALSE) {
      found = true;
      return DIENUM_STOP;
    }
    return DIENUM_CONTINUE;
  }

  BOOL CALLBACK collect_force_feedback_axes(const DIDEVICEOBJECTINSTANCEW *object, void *context) {
    if ((object->dwType & DIDFT_FFACTUATOR) != 0U) {
      static_cast<std::vector<DWORD> *>(context)->push_back(object->dwOfs);
    }
    return DIENUM_CONTINUE;
  }

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

      const auto detail_count =
        (required_size + sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) - 1U) / sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
      std::vector<SP_DEVICE_INTERFACE_DETAIL_DATA_W> detail_storage(detail_count);
      auto *detail = detail_storage.data();
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
      if (
        caps_status != HIDP_STATUS_SUCCESS || capabilities.UsagePage != 0x01U ||
        (capabilities.Usage != 0x04U && capabilities.Usage != 0x05U)
      ) {
        continue;
      }

      interfaces.push_back({
        .path = detail->DevicePath,
        .vendor_id = attributes.VendorID,
        .product_id = attributes.ProductID,
        .usage = capabilities.Usage,
        .input_report_size = capabilities.InputReportByteLength,
        .output_report_size = capabilities.OutputReportByteLength,
        .feature_report_size = capabilities.FeatureReportByteLength,
      });
    }
    return interfaces;
  }

  std::optional<HidGamepadInterface> wait_for_new_interface(
    const HidInterfacePaths &previous_paths,
    std::uint16_t vendor_id,
    std::uint16_t product_id
  ) {
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < deadline) {
      const auto interfaces = enumerate_gamepad_interfaces();
      if (const auto position = std::ranges::find_if(interfaces, [vendor_id, product_id, &previous_paths](const auto &hid_interface) {
            return hid_interface.vendor_id == vendor_id && hid_interface.product_id == product_id &&
                   !previous_paths.contains(hid_interface.path);
          });
          position != interfaces.end()) {
        return *position;
      }
      std::this_thread::sleep_for(100ms);
    }
    return std::nullopt;
  }

  std::optional<std::vector<std::uint8_t>> read_hid_report_with_timeout(
    HANDLE hid,
    std::size_t report_size,
    std::chrono::milliseconds timeout
  ) {
    Handle event {CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!event) {
      return std::nullopt;
    }

    std::vector<std::uint8_t> report(report_size, 0);
    OVERLAPPED overlapped {};
    overlapped.hEvent = event.get();
    DWORD bytes_read = 0;
    if (const auto started = ReadFile(hid, report.data(), static_cast<DWORD>(report.size()), &bytes_read, &overlapped); started == FALSE && GetLastError() != ERROR_IO_PENDING) {
      return std::nullopt;
    }

    if (const auto wait_result = WaitForSingleObject(event.get(), static_cast<DWORD>(timeout.count())); wait_result != WAIT_OBJECT_0) {
      static_cast<void>(CancelIoEx(hid, &overlapped));
      static_cast<void>(WaitForSingleObject(event.get(), INFINITE));
      static_cast<void>(GetOverlappedResult(hid, &overlapped, &bytes_read, FALSE));
      return std::nullopt;
    }
    if (GetOverlappedResult(hid, &overlapped, &bytes_read, FALSE) == FALSE) {
      return std::nullopt;
    }

    report.resize(bytes_read);
    return report;
  }

  std::optional<lvh::GamepadOutput> wait_for_rumble_output(
    std::mutex &output_mutex,
    std::condition_variable &output_ready,
    const std::vector<lvh::GamepadOutput> &outputs,
    std::size_t &next_output,
    bool expect_nonzero
  ) {
    const auto matches = [expect_nonzero](const lvh::GamepadOutput &output) {
      const auto has_strength = output.low_frequency_rumble > 0U || output.high_frequency_rumble > 0U;
      return output.kind == lvh::GamepadOutputKind::rumble && has_strength == expect_nonzero;
    };
    const auto find_next_match = [&outputs, &next_output, &matches] {
      return std::find_if(
        outputs.begin() + static_cast<std::ptrdiff_t>(next_output),
        outputs.end(),
        matches
      );
    };

    if (std::unique_lock lock {output_mutex}; output_ready.wait_for(lock, 5s, [&outputs, &find_next_match] {
          return find_next_match() != outputs.end();
        })) {
      const auto match = find_next_match();
      next_output = static_cast<std::size_t>(std::distance(outputs.begin(), match)) + 1U;
      return *match;
    }
    return std::nullopt;
  }

  HidInterfacePaths current_gamepad_interface_paths() {
    HidInterfacePaths paths;
    for (const auto &hid_interface : enumerate_gamepad_interfaces()) {
      paths.insert(hid_interface.path);
    }
    return paths;
  }

  class GamepadOutputCapture {
  public:
    void attach(lvh::GamepadStateAdapter &adapter) {
      adapter.set_output_callback([this](const lvh::GamepadOutput &output) {
        {
          std::lock_guard lock {mutex_};
          outputs_.push_back(output);
        }
        ready_.notify_all();
      });
    }

    std::optional<lvh::GamepadOutput> wait_for_rumble(bool expect_nonzero) {
      return wait_for_rumble_output(mutex_, ready_, outputs_, next_output_, expect_nonzero);
    }

  private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::vector<lvh::GamepadOutput> outputs_;
    std::size_t next_output_ = 0;
  };

}  // namespace

TEST_F(WindowsConsumerTest, NativePlayStationFeatureAndOutputReportsReachOwningRuntime) {
  struct NativePlayStationCase {
    lvh::DeviceProfile profile;
    std::uint8_t calibration_report_id;
    std::uint8_t pairing_report_id;
    std::uint8_t firmware_report_id;
    std::array<std::uint8_t, 3> firmware_prefix;
    std::uint8_t output_report_id;
    std::size_t valid_flags_offset;
    std::size_t right_motor_offset;
    std::size_t left_motor_offset;
    std::size_t minimum_feature_report_size;
  };

  const std::array test_cases {
    NativePlayStationCase {
      .profile = lvh::profiles::dualshock4_usb(),
      .calibration_report_id = 0x02,
      .pairing_report_id = 0x12,
      .firmware_report_id = 0xA3,
      .firmware_prefix = {'A', 'u', 'g'},
      .output_report_id = 0x05,
      .valid_flags_offset = 1U,
      .right_motor_offset = 4U,
      .left_motor_offset = 5U,
      .minimum_feature_report_size = 49U,
    },
    NativePlayStationCase {
      .profile = lvh::profiles::dualsense_usb(),
      .calibration_report_id = 0x05,
      .pairing_report_id = 0x09,
      .firmware_report_id = 0x20,
      .firmware_prefix = {'J', 'u', 'n'},
      .output_report_id = 0x02,
      .valid_flags_offset = 1U,
      .right_motor_offset = 3U,
      .left_motor_offset = 4U,
      .minimum_feature_report_size = 64U,
    },
  };

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

  for (const auto &test_case : test_cases) {
    const auto &profile = test_case.profile;
    const auto previous_paths = current_gamepad_interface_paths();

    lvh::CreateGamepadOptions options;
    options.profile = profile;
    options.metadata.stable_id = "02:11:22:33:44:55";
    auto created = lvh::GamepadStateAdapter::create(*runtime, options);
    ASSERT_TRUE(created) << profile.name << ": " << created.status.message();
    GamepadOutputCapture output_capture;
    output_capture.attach(*created.adapter);

    const auto hid_interface = wait_for_new_interface(previous_paths, profile.vendor_id, profile.product_id);
    ASSERT_TRUE(hid_interface.has_value()) << "The VHF " << profile.name << " HID interface was not enumerated";
    ASSERT_EQ(hid_interface->output_report_size, profile.output_report_size);

    Handle hid {CreateFileW(
      hid_interface->path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    )};
    ASSERT_TRUE(hid) << "Unable to open the VHF " << profile.name << " HID interface: " << GetLastError();

    ASSERT_GE(hid_interface->feature_report_size, test_case.minimum_feature_report_size);
    std::vector<std::uint8_t> calibration(hid_interface->feature_report_size, 0);
    calibration[0] = test_case.calibration_report_id;
    ASSERT_TRUE(HidD_GetFeature(hid.get(), calibration.data(), static_cast<ULONG>(calibration.size())))
      << profile.name << " calibration GetFeature failed: " << GetLastError();
    EXPECT_EQ(calibration[0], test_case.calibration_report_id);
    EXPECT_EQ(calibration[7], 0x10U);
    EXPECT_EQ(calibration[8], 0x27U);

    std::vector<std::uint8_t> pairing(hid_interface->feature_report_size, 0);
    pairing[0] = test_case.pairing_report_id;
    ASSERT_TRUE(HidD_GetFeature(hid.get(), pairing.data(), static_cast<ULONG>(pairing.size())))
      << profile.name << " pairing GetFeature failed: " << GetLastError();
    EXPECT_EQ(pairing[0], test_case.pairing_report_id);
    EXPECT_EQ(pairing[1], 0x55U);
    EXPECT_EQ(pairing[2], 0x44U);
    EXPECT_EQ(pairing[3], 0x33U);
    EXPECT_EQ(pairing[4], 0x22U);
    EXPECT_EQ(pairing[5], 0x11U);
    EXPECT_EQ(pairing[6], 0x02U);

    std::vector<std::uint8_t> firmware(hid_interface->feature_report_size, 0);
    firmware[0] = test_case.firmware_report_id;
    ASSERT_TRUE(HidD_GetFeature(hid.get(), firmware.data(), static_cast<ULONG>(firmware.size())))
      << profile.name << " firmware GetFeature failed: " << GetLastError();
    EXPECT_EQ(firmware[0], test_case.firmware_report_id);
    EXPECT_TRUE(std::ranges::equal(test_case.firmware_prefix, std::span {firmware}.subspan(1U, 3U)));

    std::vector<std::uint8_t> report(hid_interface->output_report_size, 0);
    report[0] = test_case.output_report_id;
    report[test_case.valid_flags_offset] = 0x01;
    report[test_case.right_motor_offset] = 0x80;
    report[test_case.left_motor_offset] = 0x40;
    DWORD bytes_written = 0;
    ASSERT_TRUE(WriteFile(
      hid.get(),
      report.data(),
      static_cast<DWORD>(report.size()),
      &bytes_written,
      nullptr
    )) << profile.name
       << " WriteFile failed: "
       << GetLastError();
    ASSERT_EQ(bytes_written, report.size());

    const auto rumble = output_capture.wait_for_rumble(true);
    ASSERT_TRUE(rumble.has_value())
      << "No normalized rumble callback followed the native " << profile.name << " HID output write";

    EXPECT_EQ(rumble->low_frequency_rumble, 16448U);
    EXPECT_EQ(rumble->high_frequency_rumble, 32896U);
    EXPECT_EQ(rumble->raw_report, report);
    ASSERT_TRUE(created.adapter->close().ok());
  }
}

TEST_F(WindowsConsumerTest, NativeSwitchHandshakeAndInputReportReachHidClient) {
  const auto profile = lvh::profiles::switch_pro();
  const auto previous_paths = current_gamepad_interface_paths();

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_NE(runtime, nullptr);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad)
    << "The installed libvirtualhid Windows driver is required for this integration test";

  lvh::CreateGamepadOptions options;
  options.profile = profile;
  auto created = lvh::GamepadStateAdapter::create(*runtime, options);
  ASSERT_TRUE(created) << created.status.message();
  GamepadOutputCapture output_capture;
  output_capture.attach(*created.adapter);
  const auto hid_interface = wait_for_new_interface(previous_paths, profile.vendor_id, profile.product_id);
  ASSERT_TRUE(hid_interface.has_value()) << "The VHF Switch Pro HID interface was not enumerated";
  ASSERT_EQ(hid_interface->input_report_size, profile.input_report_size);
  ASSERT_EQ(hid_interface->output_report_size, profile.output_report_size);

  Handle writer {CreateFileW(
    hid_interface->path.c_str(),
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    nullptr
  )};
  ASSERT_TRUE(writer) << "Unable to open the VHF Switch Pro HID interface for writes: " << GetLastError();

  Handle reader {CreateFileW(
    hid_interface->path.c_str(),
    GENERIC_READ | GENERIC_WRITE,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
    nullptr
  )};
  ASSERT_TRUE(reader) << "Unable to open the VHF Switch Pro HID interface for reads: " << GetLastError();

  const auto send_proprietary_command = [&hid_interface, &reader, &writer](std::uint8_t command) {
    std::vector<std::uint8_t> output(hid_interface->output_report_size, 0);
    output[0] = 0x80;
    output[1] = command;
    DWORD bytes_written = 0;
    EXPECT_TRUE(WriteFile(
      writer.get(),
      output.data(),
      static_cast<DWORD>(output.size()),
      &bytes_written,
      nullptr
    )) << "Switch proprietary WriteFile failed: "
       << GetLastError();
    EXPECT_EQ(bytes_written, output.size());
    return read_hid_report_with_timeout(reader.get(), hid_interface->input_report_size, 5s);
  };

  const auto status_reply = send_proprietary_command(0x01);
  ASSERT_TRUE(status_reply.has_value()) << "No 0x81 Switch status reply reached the HID client";
  ASSERT_GE(status_reply->size(), 10U);
  EXPECT_EQ(status_reply->at(0), 0x81U);
  EXPECT_EQ(status_reply->at(1), 0x01U);
  EXPECT_EQ(status_reply->at(3), 0x03U);  // Pro Controller.

  const auto handshake_reply = send_proprietary_command(0x02);
  ASSERT_TRUE(handshake_reply.has_value()) << "No 0x81 Switch handshake reply reached the HID client";
  ASSERT_GE(handshake_reply->size(), 2U);
  EXPECT_EQ(handshake_reply->at(0), 0x81U);
  EXPECT_EQ(handshake_reply->at(1), 0x02U);

  std::vector<std::uint8_t> rumble_report(hid_interface->output_report_size, 0);
  constexpr std::array native_rumble {
    std::uint8_t {0x10},
    std::uint8_t {0x07},
    std::uint8_t {0x74},
    std::uint8_t {0x1A},
    std::uint8_t {0x3D},
    std::uint8_t {0x59},
    std::uint8_t {0x74},
    std::uint8_t {0x1A},
    std::uint8_t {0x3D},
    std::uint8_t {0x59},
  };
  std::ranges::copy(native_rumble, rumble_report.begin());
  DWORD bytes_written = 0;
  ASSERT_TRUE(WriteFile(
    writer.get(),
    rumble_report.data(),
    static_cast<DWORD>(rumble_report.size()),
    &bytes_written,
    nullptr
  )) << "Switch rumble WriteFile failed: "
     << GetLastError();
  ASSERT_EQ(bytes_written, rumble_report.size());

  const auto rumble = output_capture.wait_for_rumble(true);
  ASSERT_TRUE(rumble.has_value()) << "No normalized rumble callback followed the native Switch 0x10 output write";
  EXPECT_EQ(rumble->low_frequency_rumble, 22251U);
  EXPECT_EQ(rumble->high_frequency_rumble, 5213U);
  EXPECT_EQ(rumble->raw_report, rumble_report);

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.buttons.set(lvh::GamepadButton::guide);
  state.buttons.set(lvh::GamepadButton::dpad_left);
  state.buttons.set(lvh::GamepadButton::right_stick);
  state.left_stick = {.x = 1.0F, .y = 0.0F};
  state.right_stick = {.x = 0.0F, .y = -1.0F};
  ASSERT_TRUE(created.adapter->set_state(state).ok());

  const auto input = read_hid_report_with_timeout(reader.get(), hid_interface->input_report_size, 5s);
  ASSERT_TRUE(input.has_value()) << "No native Switch 0x30 input report reached the HID client";
  ASSERT_EQ(input->size(), profile.input_report_size);
  EXPECT_EQ(input->at(0), 0x30U);
  EXPECT_EQ(input->at(3), 0x08U);  // Nintendo A in the native right-button byte.
  EXPECT_EQ(input->at(4), 0x14U);  // R3 and Home.
  EXPECT_EQ(input->at(5), 0x08U);  // D-pad left.
  EXPECT_EQ(input->at(6), 0xFFU);
  EXPECT_EQ(input->at(7), 0x0FU);
  EXPECT_EQ(input->at(8), 0x80U);
  EXPECT_EQ(input->at(9), 0x00U);
  EXPECT_EQ(input->at(10), 0x08U);
  EXPECT_EQ(input->at(11), 0x00U);
}

TEST_F(WindowsConsumerTest, NativeXboxPidRumbleWritesAreNormalized) {
  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_NE(runtime, nullptr);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad)
    << "The installed libvirtualhid Windows driver is required for this integration test";

  for (const auto &profile : {lvh::profiles::xbox_one(), lvh::profiles::xbox_series()}) {
    const auto previous_paths = current_gamepad_interface_paths();

    lvh::CreateGamepadOptions options;
    options.profile = profile;
    auto created = lvh::GamepadStateAdapter::create(*runtime, options);
    ASSERT_TRUE(created) << created.status.message();
    GamepadOutputCapture output_capture;
    output_capture.attach(*created.adapter);

    const auto product_id = profile.gamepad_kind == lvh::GamepadProfileKind::xbox_series ?
                              lvh::detail::windows::xbox_series_bluetooth_product_id :
                              profile.product_id;
    const auto hid_interface = wait_for_new_interface(previous_paths, profile.vendor_id, product_id);
    ASSERT_TRUE(hid_interface.has_value()) << "The VHF Xbox HID interface was not enumerated";
    ASSERT_EQ(hid_interface->output_report_size, profile.output_report_size + 1U);

    Handle hid {CreateFileW(
      hid_interface->path.c_str(),
      GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    )};
    ASSERT_TRUE(hid) << "Unable to open the VHF Xbox HID interface: " << GetLastError();

    const auto report_id =
      profile.gamepad_kind == lvh::GamepadProfileKind::xbox_series ? std::uint8_t {0x03U} : std::uint8_t {0x00U};
    const std::vector<std::uint8_t> report {report_id, 0x0F, 25, 50, 75, 100, 10, 0, 0};
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

    const auto rumble = output_capture.wait_for_rumble(true);
    ASSERT_TRUE(rumble.has_value()) << "No normalized Xbox rumble callback followed the native HID output write";
    EXPECT_EQ(rumble->low_frequency_rumble, 49151U);
    EXPECT_EQ(rumble->high_frequency_rumble, 65535U);
    ASSERT_TRUE(created.adapter->close().ok());
  }
}

TEST_F(WindowsConsumerTest, NativeGenericPidInterfaceIsAdvertisedToDirectInput) {
  const auto profile = lvh::profiles::generic_gamepad();
  const auto previous_paths = current_gamepad_interface_paths();

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_NE(runtime, nullptr);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad)
    << "The installed libvirtualhid Windows driver is required for this integration test";

  lvh::CreateGamepadOptions options;
  options.profile = profile;
  auto created = lvh::GamepadStateAdapter::create(*runtime, options);
  ASSERT_TRUE(created) << created.status.message();

  const auto hid_interface = wait_for_new_interface(previous_paths, profile.vendor_id, profile.product_id);
  ASSERT_TRUE(hid_interface.has_value()) << "The VHF Generic PID HID interface was not enumerated";
  EXPECT_EQ(hid_interface->usage, 0x04U);  // DirectInput PID requires a Joystick TLC.
  ASSERT_EQ(hid_interface->output_report_size, lvh::detail::windows::generic_pid_output_report_size);

  void *raw_direct_input = nullptr;
  ASSERT_TRUE(SUCCEEDED(DirectInput8Create(
    GetModuleHandleW(nullptr),
    DIRECTINPUT_VERSION,
    IID_IDirectInput8W,
    &raw_direct_input,
    nullptr
  )));
  ComInterface<IDirectInput8W> direct_input {static_cast<IDirectInput8W *>(raw_direct_input)};

  const auto direct_input_guid = wait_for_directinput_device(
    *direct_input,
    profile.vendor_id,
    profile.product_id
  );
  ASSERT_TRUE(direct_input_guid.has_value()) << "DirectInput did not enumerate the VHF Generic Controller";

  IDirectInputDevice8W *raw_direct_input_device = nullptr;
  ASSERT_TRUE(SUCCEEDED(direct_input->CreateDevice(
    *direct_input_guid,
    &raw_direct_input_device,
    nullptr
  )));
  ComInterface<IDirectInputDevice8W> direct_input_device {raw_direct_input_device};

  DIDEVCAPS direct_input_caps {};
  direct_input_caps.dwSize = sizeof(direct_input_caps);
  ASSERT_TRUE(SUCCEEDED(direct_input_device->GetCapabilities(&direct_input_caps)));
  EXPECT_NE(direct_input_caps.dwFlags & DIDC_FORCEFEEDBACK, 0U);

  bool has_constant_force = false;
  ASSERT_TRUE(SUCCEEDED(direct_input_device->EnumEffects(
    find_constant_force_effect,
    &has_constant_force,
    DIEFT_ALL
  )));
  ASSERT_TRUE(has_constant_force) << "DirectInput did not advertise GUID_ConstantForce";

  std::vector<DWORD> force_feedback_axes;
  ASSERT_TRUE(SUCCEEDED(direct_input_device->EnumObjects(
    collect_force_feedback_axes,
    &force_feedback_axes,
    DIDFT_AXIS
  )));
  ASSERT_FALSE(force_feedback_axes.empty()) << "DirectInput did not expose a force-feedback actuator axis";
}
