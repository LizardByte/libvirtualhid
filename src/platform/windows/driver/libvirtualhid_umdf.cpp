/**
 * @file src/platform/windows/driver/libvirtualhid_umdf.cpp
 * @brief UMDF2 control driver entry points for the Windows libvirtualhid backend.
 */

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
#define WIN32_NO_STATUS
#include <windows.h>
#undef WIN32_NO_STATUS

#if defined(_MSC_VER)
  #pragma warning(push)
  #pragma warning(disable : 4324 4471)
#endif
#include <wdf.h>
#if defined(_MSC_VER)
  #pragma warning(pop)
#endif

// standard includes
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

// local includes
#include "lvh_windows_protocol.h"

namespace {

  constexpr auto symbolic_link_name = L"\\DosDevices\\LibVirtualHid";

  struct DeviceRecord {
    LvhWindowsCreateGamepadRequest request {};
  };

  std::atomic<std::uint64_t> next_driver_device_id {1};
  std::mutex devices_mutex;
  std::map<std::uint64_t, DeviceRecord> devices;

  std::mutex output_requests_mutex;
  std::vector<WDFREQUEST> pending_output_requests;

  bool valid_header(std::uint32_t version, std::uint32_t size, std::uint32_t expected_size) {
    return version == LVH_WINDOWS_CONTROL_PROTOCOL_VERSION && size == expected_size;
  }

  void complete_request(WDFREQUEST request, NTSTATUS status, ULONG_PTR information = 0) {
    WdfRequestCompleteWithInformation(request, status, information);
  }

  bool remove_pending_output_request(WDFREQUEST request) {
    std::lock_guard lock {output_requests_mutex};
    const auto iter = std::find(pending_output_requests.begin(), pending_output_requests.end(), request);
    if (iter == pending_output_requests.end()) {
      return false;
    }

    pending_output_requests.erase(iter);
    return true;
  }

}  // namespace

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD LvhEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LvhEvtIoDeviceControl;
EVT_WDF_REQUEST_CANCEL LvhEvtOutputReadCanceled;

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, LvhEvtDeviceAdd);

  return WdfDriverCreate(driver_object, registry_path, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS LvhEvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT device_init) {
  UNREFERENCED_PARAMETER(driver);

  WDFDEVICE device = nullptr;
  WDF_OBJECT_ATTRIBUTES device_attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&device_attributes);
  auto status = WdfDeviceCreate(&device_init, &device_attributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  UNICODE_STRING symbolic_link;
  RtlInitUnicodeString(&symbolic_link, symbolic_link_name);
  status = WdfDeviceCreateSymbolicLink(device, &symbolic_link);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_IO_QUEUE_CONFIG queue_config;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue_config, WdfIoQueueDispatchParallel);
  queue_config.EvtIoDeviceControl = LvhEvtIoDeviceControl;

  return WdfIoQueueCreate(device, &queue_config, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

void LvhEvtOutputReadCanceled(WDFREQUEST request) {
  if (remove_pending_output_request(request)) {
    complete_request(request, STATUS_CANCELLED);
  }
}

void LvhEvtIoDeviceControl(
  WDFQUEUE queue,
  WDFREQUEST request,
  size_t output_buffer_length,
  size_t input_buffer_length,
  ULONG io_control_code
) {
  UNREFERENCED_PARAMETER(queue);
  UNREFERENCED_PARAMETER(output_buffer_length);
  UNREFERENCED_PARAMETER(input_buffer_length);

  switch (io_control_code) {
    case LVH_WINDOWS_IOCTL_CREATE_GAMEPAD:
      {
        auto *create_request = static_cast<LvhWindowsCreateGamepadRequest *>(nullptr);
        auto status = WdfRequestRetrieveInputBuffer(request, sizeof(LvhWindowsCreateGamepadRequest), reinterpret_cast<PVOID *>(&create_request), nullptr);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
          return;
        }

        auto *create_response = static_cast<LvhWindowsCreateGamepadResponse *>(nullptr);
        status = WdfRequestRetrieveOutputBuffer(request, sizeof(LvhWindowsCreateGamepadResponse), reinterpret_cast<PVOID *>(&create_response), nullptr);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
          return;
        }

        std::memset(create_response, 0, sizeof(*create_response));
        create_response->version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
        create_response->size = sizeof(*create_response);

        if (!valid_header(create_request->version, create_request->size, sizeof(*create_request)) || create_request->report_descriptor_size == 0U || create_request->report_descriptor_size > LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE || create_request->input_report_size == 0U || create_request->input_report_size > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE || create_request->output_report_size > LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE) {
          create_response->status = LVH_WINDOWS_STATUS_INVALID_ARGUMENT;
          complete_request(request, STATUS_SUCCESS, sizeof(*create_response));
          return;
        }

        const auto driver_device_id = next_driver_device_id.fetch_add(1);
        {
          std::lock_guard lock {devices_mutex};
          devices.emplace(driver_device_id, DeviceRecord {.request = *create_request});
        }

        create_response->status = LVH_WINDOWS_STATUS_SUCCESS;
        create_response->driver_device_id = driver_device_id;
        std::snprintf(create_response->device_path, sizeof(create_response->device_path), "%s#%llu", LVH_WINDOWS_CONTROL_DEVICE_PATH, static_cast<unsigned long long>(driver_device_id));
        complete_request(request, STATUS_SUCCESS, sizeof(*create_response));
        return;
      }

    case LVH_WINDOWS_IOCTL_DESTROY_DEVICE:
      {
        auto *destroy_request = static_cast<LvhWindowsDestroyDeviceRequest *>(nullptr);
        const auto status = WdfRequestRetrieveInputBuffer(request, sizeof(LvhWindowsDestroyDeviceRequest), reinterpret_cast<PVOID *>(&destroy_request), nullptr);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
          return;
        }

        if (!valid_header(destroy_request->version, destroy_request->size, sizeof(*destroy_request))) {
          complete_request(request, STATUS_INVALID_PARAMETER);
          return;
        }

        std::lock_guard lock {devices_mutex};
        devices.erase(destroy_request->driver_device_id);
        complete_request(request, STATUS_SUCCESS);
        return;
      }

    case LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT:
      {
        auto *submit_request = static_cast<LvhWindowsSubmitInputReportRequest *>(nullptr);
        const auto status = WdfRequestRetrieveInputBuffer(request, sizeof(LvhWindowsSubmitInputReportRequest), reinterpret_cast<PVOID *>(&submit_request), nullptr);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
          return;
        }

        if (!valid_header(submit_request->version, submit_request->size, sizeof(*submit_request)) || submit_request->report_size == 0U || submit_request->report_size > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
          complete_request(request, STATUS_INVALID_PARAMETER);
          return;
        }

        std::lock_guard lock {devices_mutex};
        if (devices.find(submit_request->driver_device_id) == devices.end()) {
          complete_request(request, STATUS_OBJECT_NAME_NOT_FOUND);
          return;
        }

        complete_request(request, STATUS_SUCCESS);
        return;
      }

    case LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT:
      {
        std::lock_guard lock {output_requests_mutex};
        const auto status = WdfRequestMarkCancelableEx(request, LvhEvtOutputReadCanceled);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
          return;
        }

        pending_output_requests.push_back(request);
        return;
      }

    default:
      complete_request(request, STATUS_INVALID_DEVICE_REQUEST);
      return;
  }
}
