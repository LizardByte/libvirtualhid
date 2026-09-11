// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/broker/xbox_360_software_device.cpp
 * @brief Xbox 360 software-device creation and private-channel handoff.
 */

#include "xbox_360_software_device.hpp"

#include "xbox_360_protocol.hpp"

#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <bit>
#include <cfgmgr32.h>
#include <combaseapi.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <memory>
#include <mutex>
#include <SetupAPI.h>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace lvh::windows {
  namespace {

    using UniqueHandle = std::unique_ptr<void, decltype(&::CloseHandle)>;
    using UniqueDeviceInfo = std::unique_ptr<
      std::remove_pointer_t<HDEVINFO>,
      decltype(&::SetupDiDestroyDeviceInfoList)>;
    using UniqueModule = std::unique_ptr<std::remove_pointer_t<HMODULE>, decltype(&::FreeLibrary)>;

    struct SoftwareDeviceHandleTag;
    using SoftwareDeviceHandle = SoftwareDeviceHandleTag *;

    constexpr auto software_device_capability_silent_install = 0x00000002U;
    constexpr auto software_device_capability_no_display_in_ui = 0x00000004U;
    constexpr auto software_device_capability_driver_required = 0x00000008U;
    constexpr auto interface_poll_interval_ms = 25U;
    constexpr auto interface_start_timeout_ms = 30000U;

    struct SoftwareDeviceCreateInfo {
      ULONG size;
      PCWSTR instance_id;
      PCWSTR hardware_ids;
      PCWSTR compatible_ids;
      const GUID *container_id;
      ULONG capability_flags;
      PCWSTR device_description;
      PCWSTR device_location;
      const SECURITY_DESCRIPTOR *security_descriptor;
    };

    struct CreationCallbackContext {
      std::mutex mutex;
      UniqueHandle event {nullptr, &::CloseHandle};
      HRESULT result = E_PENDING;
      std::wstring instance_id;
      bool completed {};
      bool abandoned {};
    };

    using SoftwareDeviceCreateCallback = void(WINAPI *)(
      SoftwareDeviceHandle,
      HRESULT,
      CreationCallbackContext *,
      PCWSTR
    );
    using SoftwareDeviceCreateFunction = HRESULT(WINAPI *)(
      PCWSTR,
      PCWSTR,
      const SoftwareDeviceCreateInfo *,
      ULONG,
      const void *,
      SoftwareDeviceCreateCallback,
      CreationCallbackContext *,
      SoftwareDeviceHandle *
    );
    using SoftwareDeviceCloseFunction = void(WINAPI *)(SoftwareDeviceHandle);

    struct SoftwareDeviceApi {
      UniqueModule library {nullptr, &::FreeLibrary};
      SoftwareDeviceCreateFunction create {};
      SoftwareDeviceCloseFunction close {};

      bool load() {
        library.reset(::LoadLibraryW(L"cfgmgr32.dll"));
        if (!library) {
          return false;
        }
        create = std::bit_cast<SoftwareDeviceCreateFunction>(::GetProcAddress(library.get(), "SwDeviceCreate"));
        close = std::bit_cast<SoftwareDeviceCloseFunction>(::GetProcAddress(library.get(), "SwDeviceClose"));
        return create != nullptr && close != nullptr;
      }
    };

    void WINAPI software_device_created(
      SoftwareDeviceHandle,
      HRESULT result,
      CreationCallbackContext *context,
      PCWSTR instance_id
    ) {
      std::unique_ptr<CreationCallbackContext> abandoned_context;
      {
        std::lock_guard lock {context->mutex};
        context->result = result;
        context->instance_id = instance_id == nullptr ? L"" : instance_id;
        context->completed = true;
        if (context->abandoned) {
          abandoned_context.reset(context);
        } else {
          static_cast<void>(::SetEvent(context->event.get()));
        }
      }
    }

    std::string windows_error_message(DWORD error_code) {
      std::array<char, 1024> buffer {};
      const auto size = ::FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        nullptr
      );
      if (size == 0U) {
        return std::format("Windows error {}", error_code);
      }
      std::string message {buffer.data(), size};
      while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) {
        message.pop_back();
      }
      return message;
    }

    bool random_guid(GUID &guid) {
      std::array<std::byte, sizeof(GUID)> random_bytes {};
      if (const auto status = ::BCryptGenRandom(nullptr, std::bit_cast<PUCHAR>(random_bytes.data()), static_cast<ULONG>(random_bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG); status != 0) {
        return false;
      }

      constexpr auto variant_byte_offset = offsetof(GUID, Data4);
      random_bytes[variant_byte_offset] =
        (random_bytes[variant_byte_offset] & std::byte {0x3FU}) | std::byte {0x80U};
      guid = std::bit_cast<GUID>(random_bytes);
      guid.Data3 = static_cast<unsigned short>((guid.Data3 & 0x0FFFU) | 0x4000U);
      return true;
    }

    std::wstring guid_string(const GUID &guid) {
      std::array<wchar_t, 40> buffer {};
      if (::StringFromGUID2(guid, buffer.data(), static_cast<int>(buffer.size())) == 0) {
        return {};
      }
      return buffer.data();
    }

    std::wstring make_multi_string(std::span<const std::wstring_view> values) {
      std::wstring result;
      for (const auto value : values) {
        result.append(value);
        result.push_back(L'\0');
      }
      result.push_back(L'\0');
      return result;
    }

    std::wstring hardware_id(const LvhWindowsCreateDeviceRequest &request) {
      return std::format(
        L"USB\\VID_{:04X}&PID_{:04X}&XI_00",
        request.hardware_ids.vendor_id,
        request.hardware_ids.product_id
      );
    }

    UniqueDeviceInfo make_unique_device_info(HDEVINFO value) {
      if (value == INVALID_HANDLE_VALUE) {
        value = nullptr;
      }
      return {value, &::SetupDiDestroyDeviceInfoList};
    }

    std::wstring find_interface_path(const GUID &interface_guid, std::wstring_view instance_id) {
      auto device_info = make_unique_device_info(
        ::SetupDiGetClassDevsW(&interface_guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE)
      );
      if (!device_info) {
        return {};
      }

      for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interface_data {};
        interface_data.cbSize = sizeof(interface_data);
        if (::SetupDiEnumDeviceInterfaces(device_info.get(), nullptr, &interface_guid, index, &interface_data) == FALSE) {
          break;
        }

        DWORD required_size = 0;
        static_cast<void>(::SetupDiGetDeviceInterfaceDetailW(
          device_info.get(),
          &interface_data,
          nullptr,
          0,
          &required_size,
          nullptr
        ));
        if (required_size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
          continue;
        }

        const auto element_count =
          (required_size + sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) - 1U) /
          sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        std::vector<SP_DEVICE_INTERFACE_DETAIL_DATA_W> storage(element_count);
        auto *detail = storage.data();
        detail->cbSize = sizeof(*detail);
        SP_DEVINFO_DATA device_data {};
        device_data.cbSize = sizeof(device_data);
        if (::SetupDiGetDeviceInterfaceDetailW(device_info.get(), &interface_data, detail, required_size, nullptr, &device_data) == FALSE) {
          continue;
        }

        std::array<wchar_t, MAX_DEVICE_ID_LEN> candidate_instance {};
        if (::SetupDiGetDeviceInstanceIdW(device_info.get(), &device_data, candidate_instance.data(), static_cast<DWORD>(candidate_instance.size()), nullptr) == FALSE) {
          continue;
        }
        if (_wcsicmp(candidate_instance.data(), std::wstring {instance_id}.c_str()) == 0) {
          return detail->DevicePath;
        }
      }
      return {};
    }

    std::wstring wait_for_interface_path(const GUID &interface_guid, std::wstring_view instance_id) {
      for (auto elapsed_ms = 0U; elapsed_ms < interface_start_timeout_ms; elapsed_ms += interface_poll_interval_ms) {
        if (auto path = find_interface_path(interface_guid, instance_id); !path.empty()) {
          return path;
        }
        ::Sleep(interface_poll_interval_ms);
      }
      return {};
    }

    std::string narrow_string(std::wstring_view value) {
      if (value.empty()) {
        return {};
      }
      const auto size = ::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr
      );
      if (size <= 0) {
        return {};
      }
      std::string result(static_cast<std::size_t>(size), '\0');
      static_cast<void>(::WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        result.data(),
        size,
        nullptr,
        nullptr
      ));
      return result;
    }

    template<std::size_t Size>
    void copy_string(std::array<char, Size> &target, std::string_view source) {
      std::ranges::fill(target, '\0');
      const auto count = std::min(source.size(), Size - 1U);
      std::memcpy(target.data(), source.data(), count);
    }

    bool send_initialize(
      HANDLE handle,
      const LvhWindowsCreateDeviceRequest &request,
      std::uint64_t driver_device_id,
      const LvhWindowsSessionToken &session_token,
      std::string &message
    ) {
      LvhWindowsXbox360InitializeRequest initialize {};
      initialize.version = LVH_WINDOWS_XBOX360_PROTOCOL_VERSION;
      initialize.size = sizeof(initialize);
      initialize.driver_device_id = driver_device_id;
      initialize.session_token = session_token;
      initialize.device = request;

      auto event = UniqueHandle {::CreateEventW(nullptr, TRUE, FALSE, nullptr), &::CloseHandle};
      if (!event) {
        message = "Unable to create the Xbox 360 initialization event: " +
                  windows_error_message(::GetLastError());
        return false;
      }
      OVERLAPPED overlapped {};
      overlapped.hEvent = event.get();
      DWORD bytes_returned = 0;
      if (::DeviceIoControl(handle, LVH_WINDOWS_IOCTL_XBOX360_INITIALIZE, &initialize, sizeof(initialize), nullptr, 0, &bytes_returned, &overlapped) == FALSE && ::GetLastError() != ERROR_IO_PENDING) {
        message = "Initialize Xbox 360 UMDF device: " + windows_error_message(::GetLastError());
        return false;
      }
      if (::GetOverlappedResult(handle, &overlapped, &bytes_returned, TRUE) == FALSE) {
        message = "Initialize Xbox 360 UMDF device: " + windows_error_message(::GetLastError());
        return false;
      }
      return true;
    }

  }  // namespace

  struct Xbox360SoftwareDevice::Implementation {
    std::unique_ptr<SoftwareDeviceApi> api;
    SoftwareDeviceHandle handle {};

    ~Implementation() {
      if (handle != nullptr && api && api->close != nullptr) {
        api->close(handle);
      }
    }
  };

  Xbox360SoftwareDevice::Xbox360SoftwareDevice(std::unique_ptr<Implementation> implementation):
      implementation_ {std::move(implementation)} {}

  Xbox360SoftwareDevice::Xbox360SoftwareDevice(Xbox360SoftwareDevice &&) noexcept = default;
  Xbox360SoftwareDevice &Xbox360SoftwareDevice::operator=(Xbox360SoftwareDevice &&) noexcept = default;

  Xbox360SoftwareDevice::~Xbox360SoftwareDevice() {
    implementation_.reset();
  }

  std::unique_ptr<Xbox360SoftwareDevice> Xbox360SoftwareDevice::create(
    const LvhWindowsCreateDeviceRequest &request,
    std::uint64_t driver_device_id,
    const LvhWindowsSessionToken &session_token,
    HANDLE client_process,
    LvhWindowsCreateDeviceResponse &response,
    LvhWindowsBrokerStatusCode &status,
    std::string &message
  ) {
    status = LvhWindowsBrokerStatusCode::backend_failure;
    auto implementation = std::make_unique<Implementation>();
    implementation->api = std::make_unique<SoftwareDeviceApi>();
    if (!implementation->api->load()) {
      message = "Windows software-device API is unavailable: " + windows_error_message(::GetLastError());
      return nullptr;
    }

    GUID container_id {};
    GUID instance_guid {};
    if (!random_guid(container_id) || !random_guid(instance_guid)) {
      message = "Unable to generate Xbox 360 software-device identities.";
      return nullptr;
    }
    const auto instance_id = guid_string(instance_guid);
    if (instance_id.empty()) {
      message = "Unable to format the Xbox 360 software-device identity.";
      return nullptr;
    }

    const auto vendor_hardware_id = hardware_id(request);
    const std::array<std::wstring_view, 2> hardware_ids {
      LVH_WINDOWS_XBOX360_HARDWARE_ID,
      vendor_hardware_id,
    };
    const std::array<std::wstring_view, 4> compatible_ids {
      L"USB\\MS_COMP_XUSB10",
      L"USB\\Class_FF&SubClass_5D&Prot_01",
      L"USB\\Class_FF&SubClass_5D",
      L"USB\\Class_FF",
    };
    const auto hardware_multi_string = make_multi_string(hardware_ids);
    const auto compatible_multi_string = make_multi_string(compatible_ids);

    SoftwareDeviceCreateInfo create_info {};
    create_info.size = sizeof(create_info);
    create_info.instance_id = instance_id.c_str();
    create_info.hardware_ids = hardware_multi_string.c_str();
    create_info.compatible_ids = compatible_multi_string.c_str();
    create_info.container_id = &container_id;
    create_info.capability_flags = software_device_capability_silent_install |
                                   software_device_capability_no_display_in_ui |
                                   software_device_capability_driver_required;
    create_info.device_description = L"libvirtualhid Xbox 360 Controller";

    auto callback_context = std::make_unique<CreationCallbackContext>();
    callback_context->event.reset(::CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!callback_context->event) {
      message = "Unable to create the Xbox 360 software-device event: " +
                windows_error_message(::GetLastError());
      return nullptr;
    }

    const auto create_result = implementation->api->create(
      LVH_WINDOWS_XBOX360_ENUMERATOR,
      L"HTREE\\ROOT\\0",
      &create_info,
      0U,
      nullptr,
      software_device_created,
      callback_context.get(),
      &implementation->handle
    );
    if (FAILED(create_result)) {
      message = std::format("SwDeviceCreate failed with HRESULT 0x{:08X}.", static_cast<std::uint32_t>(create_result));
      return nullptr;
    }

    if (::WaitForSingleObject(callback_context->event.get(), 35000U) != WAIT_OBJECT_0) {
      auto callback_completed = false;
      {
        std::lock_guard lock {callback_context->mutex};
        callback_completed = callback_context->completed;
        if (!callback_completed) {
          callback_context->abandoned = true;
        }
      }
      if (!callback_completed) {
        static_cast<void>(callback_context.release());
      }
      message = "Timed out while Windows installed the Xbox 360 software device.";
      return nullptr;
    }

    auto callback_result = E_FAIL;
    std::wstring full_instance_id;
    {
      std::lock_guard lock {callback_context->mutex};
      callback_result = callback_context->result;
      full_instance_id = callback_context->instance_id;
    }
    if (FAILED(callback_result) || full_instance_id.empty()) {
      message = std::format(
        "Windows failed to start the Xbox 360 software device (HRESULT 0x{:08X}).",
        static_cast<std::uint32_t>(callback_result)
      );
      return nullptr;
    }

    const auto xusb_path = wait_for_interface_path(LVH_WINDOWS_XUSB_INTERFACE_GUID, full_instance_id);
    if (xusb_path.empty()) {
      message = "The Xbox 360 UMDF driver started without publishing its XUSB interface.";
      return nullptr;
    }

    auto transport_handle = UniqueHandle {
      ::CreateFileW(
        xusb_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        nullptr
      ),
      &::CloseHandle,
    };
    if (transport_handle.get() == INVALID_HANDLE_VALUE) {
      transport_handle.release();
      message = "Unable to open the Xbox 360 broker transport: " +
                windows_error_message(::GetLastError());
      return nullptr;
    }
    if (!send_initialize(
          transport_handle.get(),
          request,
          driver_device_id,
          session_token,
          message
        )) {
      return nullptr;
    }

    HANDLE client_handle = nullptr;
    if (::DuplicateHandle(::GetCurrentProcess(), transport_handle.get(), client_process, &client_handle, 0U, FALSE, DUPLICATE_SAME_ACCESS) == FALSE) {
      message = "Unable to duplicate the Xbox 360 transport into the requesting client: " +
                windows_error_message(::GetLastError());
      return nullptr;
    }

    response.status = LVH_WINDOWS_STATUS_SUCCESS;
    response.driver_device_id = driver_device_id;
    response.transport_handle = std::bit_cast<std::uintptr_t>(client_handle);
    response.session_token = session_token;
    copy_string(response.device_path, narrow_string(xusb_path));
    status = LvhWindowsBrokerStatusCode::success;
    message.clear();
    return std::make_unique<Xbox360SoftwareDevice>(std::move(implementation));
  }

}  // namespace lvh::windows
