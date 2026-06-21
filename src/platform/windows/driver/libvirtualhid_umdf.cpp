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
#include <Windows.h>
#undef WIN32_NO_STATUS

#if defined(_MSC_VER)
  #pragma warning(push)
  #pragma warning(disable : 4324 4471)
#endif
#include <wdf.h>
// VHF uses UMDF WDM types declared by wdf.h.
#include <vhf.h>
#if defined(_MSC_VER)
  #pragma warning(pop)
#endif

// standard includes
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

// local includes
#include "lvh_windows_protocol.h"

using VhfContext = PVOID;  // NOSONAR(cpp:S5008): VHF callback ABI requires PVOID; client context narrows to DeviceRecord.

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD LvhEvtDeviceAdd;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LvhEvtIoDeviceControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP LvhEvtDeviceCleanup;
EVT_WDF_REQUEST_CANCEL LvhEvtOutputReadCanceled;
EVT_VHF_ASYNC_OPERATION LvhEvtVhfWriteReport;

namespace {

  constexpr auto symbolic_link_name = L"\\DosDevices\\LibVirtualHid";

  struct DeviceRecord {
    std::mutex mutex;
    std::uint64_t driver_device_id {};
    LvhWindowsCreateGamepadRequest request {};
    VHFHANDLE vhf_handle {};
    std::vector<UCHAR> report_descriptor;
  };

  struct DriverState {
    std::atomic<std::uint64_t> next_driver_device_id {1};
    WDFIOTARGET vhf_io_target {};
    std::mutex devices_mutex;
    std::map<std::uint64_t, std::shared_ptr<DeviceRecord>> devices;
    std::mutex output_requests_mutex;
    std::vector<WDFREQUEST> pending_output_requests;
    std::vector<LvhWindowsOutputReportEvent> buffered_output_events;
  };

  DriverState &driver_state() {
    static DriverState state;
    return state;
  }

  bool valid_header(std::uint32_t version, std::uint32_t size, std::uint32_t expected_size) {
    return version == LVH_WINDOWS_CONTROL_PROTOCOL_VERSION && size == expected_size;
  }

  void complete_request(WDFREQUEST request, NTSTATUS status, ULONG_PTR information = 0) {
    WdfRequestCompleteWithInformation(request, status, information);
  }

  bool remove_pending_output_request(WDFREQUEST request) {
    auto &state = driver_state();
    std::lock_guard lock {state.output_requests_mutex};
    const auto iter = std::ranges::find(state.pending_output_requests, request);
    if (iter == state.pending_output_requests.end()) {
      return false;
    }

    state.pending_output_requests.erase(iter);
    return true;
  }

  template<typename ProtocolBuffer>
  NTSTATUS retrieve_input_buffer(WDFREQUEST request, ProtocolBuffer *&buffer) {
    PVOID raw_buffer = nullptr;
    const auto status = WdfRequestRetrieveInputBuffer(request, sizeof(ProtocolBuffer), &raw_buffer, nullptr);
    buffer = static_cast<ProtocolBuffer *>(raw_buffer);
    return status;
  }

  template<typename ProtocolBuffer>
  NTSTATUS retrieve_output_buffer(WDFREQUEST request, ProtocolBuffer *&buffer) {
    PVOID raw_buffer = nullptr;
    const auto status = WdfRequestRetrieveOutputBuffer(request, sizeof(ProtocolBuffer), &raw_buffer, nullptr);
    buffer = static_cast<ProtocolBuffer *>(raw_buffer);
    return status;
  }

  std::shared_ptr<DeviceRecord> find_device(std::uint64_t driver_device_id) {
    auto &state = driver_state();
    std::lock_guard lock {state.devices_mutex};
    const auto iter = state.devices.find(driver_device_id);
    if (iter == state.devices.end()) {
      return nullptr;
    }

    return iter->second;
  }

  bool complete_output_request(
    WDFREQUEST request,
    const LvhWindowsOutputReportEvent &event,
    bool request_is_cancelable
  ) {
    if (request_is_cancelable && !NT_SUCCESS(WdfRequestUnmarkCancelable(request))) {
      return false;
    }

    auto *output_event = static_cast<LvhWindowsOutputReportEvent *>(nullptr);
    const auto status = retrieve_output_buffer(request, output_event);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return true;
    }

    *output_event = event;
    complete_request(request, STATUS_SUCCESS, sizeof(*output_event));
    return true;
  }

  void queue_output_event(const LvhWindowsOutputReportEvent &event) {
    WDFREQUEST request = nullptr;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.output_requests_mutex};
      if (state.pending_output_requests.empty()) {
        state.buffered_output_events.push_back(event);
        return;
      }

      request = state.pending_output_requests.front();
      state.pending_output_requests.erase(state.pending_output_requests.begin());
    }

    if (!complete_output_request(request, event, true)) {
      auto &state = driver_state();
      std::lock_guard lock {state.output_requests_mutex};
      state.buffered_output_events.push_back(event);
    }
  }

  void complete_pending_output_requests(NTSTATUS status) {
    std::vector<WDFREQUEST> requests;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.output_requests_mutex};
      requests.swap(state.pending_output_requests);
      state.buffered_output_events.clear();
    }

    for (const auto request : requests) {
      if (NT_SUCCESS(WdfRequestUnmarkCancelable(request))) {
        complete_request(request, status);
      }
    }
  }

  void delete_vhf_device(const std::shared_ptr<DeviceRecord> &record) {
    if (!record) {
      return;
    }

    VHFHANDLE vhf_handle = nullptr;
    {
      std::lock_guard lock {record->mutex};
      vhf_handle = record->vhf_handle;
      record->vhf_handle = nullptr;
    }

    if (vhf_handle != nullptr) {
      VhfDelete(vhf_handle, TRUE);
    }
  }

  void delete_all_vhf_devices() {
    std::vector<std::shared_ptr<DeviceRecord>> devices;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.devices_mutex};
      for (const auto &[driver_device_id, record] : state.devices) {
        static_cast<void>(driver_device_id);
        devices.push_back(record);
      }
      state.devices.clear();
    }

    for (const auto &record : devices) {
      delete_vhf_device(record);
    }
  }

  NTSTATUS initialize_vhf_target(WDFDEVICE device) {
    WDFIOTARGET vhf_io_target = nullptr;
    WDF_OBJECT_ATTRIBUTES target_attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&target_attributes);
    target_attributes.ParentObject = device;

    auto status = WdfIoTargetCreate(device, &target_attributes, &vhf_io_target);
    if (!NT_SUCCESS(status)) {
      return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS open_params;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_FILE(&open_params, nullptr);
    status = WdfIoTargetOpen(vhf_io_target, &open_params);
    if (!NT_SUCCESS(status)) {
      WdfObjectDelete(vhf_io_target);
      return status;
    }

    driver_state().vhf_io_target = vhf_io_target;
    return STATUS_SUCCESS;
  }

  NTSTATUS create_vhf_device(const std::shared_ptr<DeviceRecord> &record) {
    auto &state = driver_state();
    if (state.vhf_io_target == nullptr) {
      return STATUS_DEVICE_NOT_READY;
    }

    const auto descriptor_size = record->request.report_sizes.report_descriptor_size;
    record->report_descriptor.assign(
      record->request.report_descriptor,
      record->request.report_descriptor + descriptor_size
    );

    VHF_CONFIG vhf_config;
    VHF_CONFIG_INIT(
      &vhf_config,
      WdfIoTargetWdmGetTargetFileHandle(state.vhf_io_target),
      static_cast<USHORT>(record->report_descriptor.size()),
      record->report_descriptor.data()
    );
    vhf_config.VhfClientContext = record.get();
    vhf_config.VendorID = record->request.hardware_ids.vendor_id;
    vhf_config.ProductID = record->request.hardware_ids.product_id;
    vhf_config.VersionNumber = record->request.hardware_ids.device_version;
    vhf_config.EvtVhfAsyncOperationWriteReport = LvhEvtVhfWriteReport;

    auto status = VhfCreate(&vhf_config, &record->vhf_handle);
    if (!NT_SUCCESS(status)) {
      record->vhf_handle = nullptr;
      return status;
    }

    status = VhfStart(record->vhf_handle);
    if (!NT_SUCCESS(status)) {
      delete_vhf_device(record);
    }

    return status;
  }

  bool valid_create_request(const LvhWindowsCreateGamepadRequest &request) {
    const auto descriptor_size = request.report_sizes.report_descriptor_size;
    const auto input_report_size = request.report_sizes.input_report_size;
    const auto output_report_size = request.report_sizes.output_report_size;

    return valid_header(request.version, request.size, sizeof(request)) && descriptor_size > 0U &&
           descriptor_size <= LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE && input_report_size > 0U &&
           input_report_size <= LVH_WINDOWS_MAX_INPUT_REPORT_SIZE &&
           output_report_size <= LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE;
  }

  bool valid_submit_input_report_request(const LvhWindowsSubmitInputReportRequest &request) {
    return valid_header(request.version, request.size, sizeof(request)) && request.report_size > 0U &&
           request.report_size <= LVH_WINDOWS_MAX_INPUT_REPORT_SIZE;
  }

  void set_device_path(std::uint64_t driver_device_id, char (&device_path)[LVH_WINDOWS_MAX_DEVICE_PATH_SIZE]) {
    constexpr auto path_prefix_size = sizeof(LVH_WINDOWS_CONTROL_DEVICE_PATH) - 1U;
    constexpr auto separator_size = 1U;
    static_assert(path_prefix_size + separator_size < LVH_WINDOWS_MAX_DEVICE_PATH_SIZE);

    std::memcpy(device_path, LVH_WINDOWS_CONTROL_DEVICE_PATH, path_prefix_size);
    device_path[path_prefix_size] = '#';

    const auto output = std::to_chars(
      device_path + path_prefix_size + separator_size,
      device_path + sizeof(device_path) - 1U,
      driver_device_id
    );
    if (output.ec == std::errc {}) {
      *output.ptr = '\0';
    } else {
      device_path[path_prefix_size + separator_size] = '\0';
    }
  }

  void handle_create_gamepad_request(WDFREQUEST request) {
    auto *create_request = static_cast<LvhWindowsCreateGamepadRequest *>(nullptr);
    auto status = retrieve_input_buffer(request, create_request);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }

    auto *create_response = static_cast<LvhWindowsCreateGamepadResponse *>(nullptr);
    status = retrieve_output_buffer(request, create_response);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }

    std::memset(create_response, 0, sizeof(*create_response));
    create_response->version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
    create_response->size = sizeof(*create_response);

    if (!valid_create_request(*create_request)) {
      create_response->status = LVH_WINDOWS_STATUS_INVALID_ARGUMENT;
      complete_request(request, STATUS_SUCCESS, sizeof(*create_response));
      return;
    }

    auto &state = driver_state();
    const auto driver_device_id = state.next_driver_device_id.fetch_add(1);
    auto record = std::make_shared<DeviceRecord>();
    record->driver_device_id = driver_device_id;
    record->request = *create_request;

    status = create_vhf_device(record);
    if (!NT_SUCCESS(status)) {
      create_response->status = LVH_WINDOWS_STATUS_BACKEND_FAILURE;
      complete_request(request, STATUS_SUCCESS, sizeof(*create_response));
      return;
    }

    {
      std::lock_guard lock {state.devices_mutex};
      state.devices[driver_device_id] = record;
    }
    create_response->status = LVH_WINDOWS_STATUS_SUCCESS;
    create_response->driver_device_id = driver_device_id;
    set_device_path(driver_device_id, create_response->device_path);
    complete_request(request, STATUS_SUCCESS, sizeof(*create_response));
  }

  void handle_destroy_device_request(WDFREQUEST request) {
    auto *destroy_request = static_cast<LvhWindowsDestroyDeviceRequest *>(nullptr);
    const auto status = retrieve_input_buffer(request, destroy_request);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }

    if (!valid_header(destroy_request->version, destroy_request->size, sizeof(*destroy_request))) {
      complete_request(request, STATUS_INVALID_PARAMETER);
      return;
    }

    auto record = std::shared_ptr<DeviceRecord> {};
    {
      auto &state = driver_state();
      std::lock_guard lock {state.devices_mutex};
      const auto iter = state.devices.find(destroy_request->driver_device_id);
      if (iter != state.devices.end()) {
        record = iter->second;
        state.devices.erase(iter);
      }
    }
    delete_vhf_device(record);
    complete_request(request, STATUS_SUCCESS);
  }

  void handle_submit_input_report_request(WDFREQUEST request) {
    auto *submit_request = static_cast<LvhWindowsSubmitInputReportRequest *>(nullptr);
    const auto status = retrieve_input_buffer(request, submit_request);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }

    if (!valid_submit_input_report_request(*submit_request)) {
      complete_request(request, STATUS_INVALID_PARAMETER);
      return;
    }

    auto record = find_device(submit_request->driver_device_id);
    if (!record) {
      complete_request(request, STATUS_OBJECT_NAME_NOT_FOUND);
      return;
    }

    std::vector<UCHAR> report(
      submit_request->report,
      submit_request->report + submit_request->report_size
    );
    HID_XFER_PACKET packet {};
    packet.reportBuffer = report.data();
    packet.reportBufferLen = static_cast<ULONG>(report.size());
    packet.reportId = report.empty() ? 0 : report.front();

    std::lock_guard lock {record->mutex};
    if (record->vhf_handle == nullptr) {
      complete_request(request, STATUS_OBJECT_NAME_NOT_FOUND);
      return;
    }

    complete_request(request, VhfReadReportSubmit(record->vhf_handle, &packet));
  }

  void handle_read_output_report_request(WDFREQUEST request) {
    std::optional<LvhWindowsOutputReportEvent> buffered_event;
    auto &state = driver_state();
    {
      std::lock_guard lock {state.output_requests_mutex};
      if (!state.buffered_output_events.empty()) {
        buffered_event = state.buffered_output_events.front();
        state.buffered_output_events.erase(state.buffered_output_events.begin());
      } else {
        const auto status = WdfRequestMarkCancelableEx(request, LvhEvtOutputReadCanceled);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
          return;
        }

        state.pending_output_requests.push_back(request);
      }
    }

    if (buffered_event.has_value()) {
      static_cast<void>(complete_output_request(request, *buffered_event, false));
    }
  }

}  // namespace

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
  device_attributes.EvtCleanupCallback = LvhEvtDeviceCleanup;
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

  status = initialize_vhf_target(device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_IO_QUEUE_CONFIG queue_config;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue_config, WdfIoQueueDispatchParallel);
  queue_config.EvtIoDeviceControl = LvhEvtIoDeviceControl;

  return WdfIoQueueCreate(device, &queue_config, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
}

void LvhEvtDeviceCleanup(WDFOBJECT device_object) {
  UNREFERENCED_PARAMETER(device_object);

  complete_pending_output_requests(STATUS_CANCELLED);
  delete_all_vhf_devices();
  driver_state().vhf_io_target = nullptr;
}

void LvhEvtOutputReadCanceled(WDFREQUEST request) {
  if (remove_pending_output_request(request)) {
    complete_request(request, STATUS_CANCELLED);
  }
}

void LvhEvtVhfWriteReport(
  VhfContext vhf_client_context,
  VHFOPERATIONHANDLE vhf_operation_handle,
  VhfContext vhf_operation_context,
  PHID_XFER_PACKET hid_transfer_packet
) {
  UNREFERENCED_PARAMETER(vhf_operation_context);

  auto *record = static_cast<DeviceRecord *>(vhf_client_context);
  if (record == nullptr || hid_transfer_packet == nullptr || hid_transfer_packet->reportBuffer == nullptr) {
    static_cast<void>(VhfAsyncOperationComplete(vhf_operation_handle, STATUS_INVALID_PARAMETER));
    return;
  }

  LvhWindowsOutputReportEvent event {};
  event.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
  event.size = sizeof(event);
  event.driver_device_id = record->driver_device_id;
  event.report_size =
    std::min(hid_transfer_packet->reportBufferLen, static_cast<ULONG>(LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE));

  if (event.report_size > 0U) {
    std::memcpy(event.report, hid_transfer_packet->reportBuffer, event.report_size);
  } else if (hid_transfer_packet->reportId != 0U) {
    event.report_size = 1U;
    event.report[0] = hid_transfer_packet->reportId;
  }

  queue_output_event(event);
  static_cast<void>(VhfAsyncOperationComplete(vhf_operation_handle, STATUS_SUCCESS));
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
      handle_create_gamepad_request(request);
      return;

    case LVH_WINDOWS_IOCTL_DESTROY_DEVICE:
      handle_destroy_device_request(request);
      return;

    case LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT:
      handle_submit_input_report_request(request);
      return;

    case LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT:
      handle_read_output_report_request(request);
      return;

    default:
      complete_request(request, STATUS_INVALID_DEVICE_REQUEST);
      return;
  }
}
