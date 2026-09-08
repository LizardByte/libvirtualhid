// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/driver/libvirtualhid_driver_setup.cpp
 * @brief Native SetupAPI helper for installing the libvirtualhid root device.
 */

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
// clang-format off
#include <Windows.h>
#include <newdev.h>
#include <SetupAPI.h>
// clang-format on

// standard includes
#include <array>
#include <bit>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
  constexpr std::wstring_view root_prefix = L"ROOT\\";

  class device_info_set {
  public:
    explicit device_info_set(HDEVINFO value):
        value_ {value} {}

    ~device_info_set() {
      if (value_ != INVALID_HANDLE_VALUE) {
        static_cast<void>(SetupDiDestroyDeviceInfoList(value_));
      }
    }

    device_info_set(const device_info_set &) = delete;
    device_info_set &operator=(const device_info_set &) = delete;
    device_info_set(device_info_set &&) = delete;
    device_info_set &operator=(device_info_set &&) = delete;

    [[nodiscard]] HDEVINFO get() const {
      return value_;
    }

  private:
    HDEVINFO value_;
  };

  [[nodiscard]] std::wstring windows_error_message(DWORD error) {
    std::array<wchar_t, 1024> buffer {};
    const auto length = FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      error,
      0,
      buffer.data(),
      static_cast<DWORD>(buffer.size()),
      nullptr
    );

    std::wstring message;
    if (length != 0) {
      message.assign(buffer.data(), length);
      while (!message.empty() && (message.back() == L'\r' || message.back() == L'\n')) {
        message.pop_back();
      }
    }
    return message;
  }

  int report_last_error(std::wstring_view action) {
    auto error = GetLastError();
    if (error == ERROR_SUCCESS) {
      error = ERROR_GEN_FAILURE;
    }

    std::wcerr << action << L" failed with Win32 error " << error;
    if (const auto message = windows_error_message(error); !message.empty()) {
      std::wcerr << L": " << message;
    }
    std::wcerr << L'\n';
    return EXIT_FAILURE;
  }

  [[nodiscard]] std::optional<std::wstring> root_device_name(std::wstring_view hardware_id) {
    if (hardware_id.size() <= root_prefix.size() || CompareStringOrdinal(hardware_id.data(), static_cast<int>(root_prefix.size()), root_prefix.data(), static_cast<int>(root_prefix.size()), TRUE) != CSTR_EQUAL) {
      return std::nullopt;
    }

    const auto name = hardware_id.substr(root_prefix.size());
    if (name.contains(L'\\')) {
      return std::nullopt;
    }
    return std::wstring {name};
  }

  int install_root_device(const std::wstring &inf_path, const std::wstring &hardware_id) {
    const auto device_name = root_device_name(hardware_id);
    if (!device_name) {
      std::wcerr << L"Hardware ID must be a root-enumerated device ID without an instance suffix: " << hardware_id << L'\n';
      return EXIT_FAILURE;
    }

    GUID class_guid {};
    if (std::array<wchar_t, 256> class_name {}; SetupDiGetINFClassW(inf_path.c_str(), &class_guid, class_name.data(), static_cast<DWORD>(class_name.size()), nullptr) == FALSE) {
      return report_last_error(L"SetupDiGetINFClass");
    }

    device_info_set devices {SetupDiCreateDeviceInfoList(&class_guid, nullptr)};
    if (devices.get() == INVALID_HANDLE_VALUE) {
      return report_last_error(L"SetupDiCreateDeviceInfoList");
    }

    SP_DEVINFO_DATA device_info {};
    device_info.cbSize = sizeof(device_info);
    if (SetupDiCreateDeviceInfoW(devices.get(), device_name->c_str(), &class_guid, nullptr, nullptr, DICD_GENERATE_ID, &device_info) == FALSE) {
      return report_last_error(L"SetupDiCreateDeviceInfo");
    }

    std::vector<wchar_t> hardware_ids {hardware_id.begin(), hardware_id.end()};
    hardware_ids.push_back(L'\0');
    hardware_ids.push_back(L'\0');
    if (const auto hardware_id_bytes = std::as_bytes(std::span {hardware_ids}); SetupDiSetDeviceRegistryPropertyW(devices.get(), &device_info, SPDRP_HARDWAREID, std::bit_cast<const BYTE *>(hardware_id_bytes.data()), static_cast<DWORD>(hardware_id_bytes.size())) == FALSE) {
      return report_last_error(L"SetupDiSetDeviceRegistryProperty");
    }

    if (SetupDiCallClassInstaller(DIF_REGISTERDEVICE, devices.get(), &device_info) == FALSE) {
      return report_last_error(L"SetupDiCallClassInstaller");
    }

    return EXIT_SUCCESS;
  }

  int update_root_device(const std::wstring &inf_path, const std::wstring &hardware_id) {
    BOOL reboot_required = FALSE;
    if (UpdateDriverForPlugAndPlayDevicesW(nullptr, hardware_id.c_str(), inf_path.c_str(), INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &reboot_required) == FALSE) {
      return report_last_error(L"UpdateDriverForPlugAndPlayDevices");
    }

    if (reboot_required != FALSE) {
      return ERROR_SUCCESS_REBOOT_REQUIRED;
    }
    return EXIT_SUCCESS;
  }

  void print_usage() {
    std::wcout << L"Usage: libvirtualhid_driver_setup.exe <install|update> <inf-path> <hardware-id>\n";
  }
}  // namespace

int wmain(int argc, wchar_t **argv) {
  if (argc == 2 && std::wstring_view {argv[1]} == L"--help") {
    print_usage();
    return EXIT_SUCCESS;
  }
  if (argc != 4) {
    print_usage();
    return EXIT_FAILURE;
  }

  const std::wstring_view action {argv[1]};
  if (action == L"install") {
    return install_root_device(argv[2], argv[3]);
  }
  if (action == L"update") {
    return update_root_device(argv[2], argv[3]);
  }

  std::wcerr << L"Unknown action: " << action << L'\n';
  print_usage();
  return EXIT_FAILURE;
}
