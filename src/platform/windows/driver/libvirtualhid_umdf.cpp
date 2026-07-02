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
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// local includes
#include "lvh_windows_protocol.h"

using VhfContext = PVOID;  // NOSONAR(cpp:S5008): VHF callback ABI requires PVOID; client context narrows to DeviceRecord.

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD LvhEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE LvhEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE LvhEvtDeviceReleaseHardware;
EVT_WDF_FILE_CLEANUP LvhEvtFileCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LvhEvtIoDeviceControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP LvhEvtDeviceCleanup;
EVT_WDF_REQUEST_CANCEL LvhEvtOutputReadCanceled;
EVT_VHF_ASYNC_OPERATION LvhEvtVhfWriteReport;

namespace {

  constexpr auto symbolic_link_name = L"\\DosDevices\\LibVirtualHid";
  constexpr auto global_symbolic_link_name = L"\\DosDevices\\Global\\LibVirtualHid";
  constexpr auto trace_file_name = L"libvirtualhid-umdf-driver.log";

  struct DeviceRecord {
    std::mutex mutex;
    std::uint64_t driver_device_id {};
    WDFFILEOBJECT owner_file {};
    LvhWindowsCreateGamepadRequest request {};
    VHFHANDLE vhf_handle {};
    std::vector<UCHAR> report_descriptor;
    std::wstring hardware_ids;
  };

  struct DriverState {
    std::atomic<std::uint64_t> next_driver_device_id {1};
    std::mutex vhf_target_mutex;
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

  void trace_status(const char *step, NTSTATUS status = STATUS_SUCCESS) {
    static std::atomic<unsigned long> sequence {0};

    wchar_t trace_file_path[MAX_PATH] {};
    constexpr auto trace_file_path_length = static_cast<DWORD>(MAX_PATH);
    auto trace_path_size = GetTempPathW(trace_file_path_length, trace_file_path);
    if (trace_path_size == 0U || trace_path_size >= trace_file_path_length) {
      return;
    }

    const auto file_name_size = wcslen(trace_file_name);
    if (trace_path_size + file_name_size >= trace_file_path_length) {
      return;
    }
    std::memcpy(
      trace_file_path + trace_path_size,
      trace_file_name,
      (file_name_size + 1U) * sizeof(wchar_t)
    );

    const auto file = CreateFileW(
      trace_file_path,
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    );
    if (file == INVALID_HANDLE_VALUE) {
      return;
    }

    SYSTEMTIME time {};
    GetSystemTime(&time);

    char line[320] {};
    const auto written_chars = std::snprintf(
      line,
      sizeof(line),
      "%04hu-%02hu-%02huT%02hu:%02hu:%02hu.%03huZ [%lu] %s status=0x%08lX\r\n",
      time.wYear,
      time.wMonth,
      time.wDay,
      time.wHour,
      time.wMinute,
      time.wSecond,
      time.wMilliseconds,
      sequence.fetch_add(1U) + 1U,
      step,
      static_cast<unsigned long>(status)
    );
    if (written_chars > 0) {
      DWORD bytes_written {};
      const auto bytes_to_write = static_cast<DWORD>(std::min<int>(written_chars, sizeof(line) - 1U));
      static_cast<void>(WriteFile(file, line, bytes_to_write, &bytes_written, nullptr));
    }

    static_cast<void>(CloseHandle(file));
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

  void delete_vhf_devices_for_file(WDFFILEOBJECT file_object) {
    if (file_object == nullptr) {
      return;
    }

    std::vector<std::shared_ptr<DeviceRecord>> devices;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.devices_mutex};
      for (auto iter = state.devices.begin(); iter != state.devices.end();) {
        if (iter->second->owner_file == file_object) {
          devices.push_back(iter->second);
          iter = state.devices.erase(iter);
        } else {
          ++iter;
        }
      }
    }

    for (const auto &record : devices) {
      delete_vhf_device(record);
    }
  }

  NTSTATUS initialize_vhf_target(WDFDEVICE device) {
    auto &state = driver_state();
    std::lock_guard lock {state.vhf_target_mutex};
    if (state.vhf_io_target != nullptr) {
      return STATUS_SUCCESS;
    }

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

    state.vhf_io_target = vhf_io_target;
    return STATUS_SUCCESS;
  }

  void reset_vhf_target(bool delete_target) {
    auto &state = driver_state();
    const auto vhf_io_target = state.vhf_io_target;
    state.vhf_io_target = nullptr;

    delete_all_vhf_devices();

    if (delete_target && vhf_io_target != nullptr) {
      WdfObjectDelete(vhf_io_target);
    }
  }

  wchar_t hex_digit(unsigned value) {
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    return digits[value & 0x0FU];
  }

  void append_hex4(std::wstring &text, std::uint16_t value) {
    text.push_back(hex_digit(value >> 12U));
    text.push_back(hex_digit(value >> 8U));
    text.push_back(hex_digit(value >> 4U));
    text.push_back(hex_digit(value));
  }

  void append_hid_vid_pid(std::wstring &hardware_ids, const LvhWindowsGamepadHardwareIds &ids) {
    hardware_ids.append(L"HID\\VID_");
    append_hex4(hardware_ids, ids.vendor_id);
    hardware_ids.append(L"&PID_");
    append_hex4(hardware_ids, ids.product_id);
  }

  std::wstring make_hardware_ids(const LvhWindowsGamepadHardwareIds &ids) {
    std::wstring hardware_ids;
    append_hid_vid_pid(hardware_ids, ids);
    hardware_ids.append(L"&REV_");
    append_hex4(hardware_ids, ids.device_version);
    hardware_ids.push_back(L'\0');

    append_hid_vid_pid(hardware_ids, ids);
    hardware_ids.push_back(L'\0');
    hardware_ids.push_back(L'\0');
    return hardware_ids;
  }

  NTSTATUS create_vhf_device(WDFDEVICE device, const std::shared_ptr<DeviceRecord> &record) {
    auto &state = driver_state();
    auto status = initialize_vhf_target(device);
    if (!NT_SUCCESS(status)) {
      return status;
    }

    const auto descriptor_size = record->request.report_sizes.report_descriptor_size;
    record->report_descriptor.assign(
      record->request.report_descriptor,
      record->request.report_descriptor + descriptor_size
    );
    record->hardware_ids = make_hardware_ids(record->request.hardware_ids);

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
    vhf_config.HardwareIDsLength = static_cast<USHORT>(record->hardware_ids.size() * sizeof(wchar_t));
    vhf_config.HardwareIDs = record->hardware_ids.data();
    vhf_config.EvtVhfAsyncOperationWriteReport = LvhEvtVhfWriteReport;

    status = VhfCreate(&vhf_config, &record->vhf_handle);
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

  bool symbolic_link_already_exists(NTSTATUS status) {
    const auto value = static_cast<std::uint32_t>(status);
    return value == 0xC0000035U || value == 0x800700B7U || value == 0x900700B7U;
  }

  constexpr GUID control_device_interface_guid {
    0x3890af65,
    0x2da0,
    0x443c,
    {0x84, 0xff, 0x6e, 0x70, 0xe8, 0x41, 0xba, 0x1e}
  };

  NTSTATUS create_control_symbolic_link(WDFDEVICE device, const wchar_t *link_name, const char *trace_step) {
    UNICODE_STRING symbolic_link;
    RtlInitUnicodeString(&symbolic_link, link_name);

    const auto status = WdfDeviceCreateSymbolicLink(device, &symbolic_link);
    trace_status(trace_step, status);
    if (NT_SUCCESS(status) || symbolic_link_already_exists(status)) {
      return STATUS_SUCCESS;
    }

    return status;
  }

  std::vector<UCHAR> make_vhf_input_payload(
    const DeviceRecord &record,
    const LvhWindowsSubmitInputReportRequest &request
  ) {
    const auto report_id = record.request.hardware_ids.report_id;
    const auto report_begin = request.report;
    const auto report_end = request.report + request.report_size;
    if (report_id == 0U) {
      return {report_begin, report_end};
    }

    if (request.report_size <= 1U || request.report[0] != report_id) {
      return {};
    }

    return {report_begin + 1U, report_end};
  }

  void copy_vhf_output_payload(
    LvhWindowsOutputReportEvent &event,
    const HID_XFER_PACKET &packet
  ) {
    const auto report_id = packet.reportId;
    const auto report_id_size = report_id == 0U ? 0U : 1U;
    const auto payload_capacity = LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE - report_id_size;
    const auto payload_size = std::min(packet.reportBufferLen, static_cast<ULONG>(payload_capacity));

    if (report_id != 0U) {
      event.report[0] = report_id;
    }

    if (payload_size > 0U) {
      std::memcpy(event.report + report_id_size, packet.reportBuffer, payload_size);
    }

    event.report_size = static_cast<std::uint32_t>(report_id_size + payload_size);
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

  void handle_create_gamepad_request(WDFDEVICE device, WDFREQUEST request) {
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
    record->owner_file = WdfRequestGetFileObject(request);
    record->request = *create_request;

    status = create_vhf_device(device, record);
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

    std::lock_guard lock {record->mutex};
    if (record->vhf_handle == nullptr) {
      complete_request(request, STATUS_OBJECT_NAME_NOT_FOUND);
      return;
    }

    auto report = make_vhf_input_payload(*record, *submit_request);
    if (report.empty()) {
      complete_request(request, STATUS_INVALID_PARAMETER);
      return;
    }

    HID_XFER_PACKET packet {};
    packet.reportBuffer = report.data();
    packet.reportBufferLen = static_cast<ULONG>(report.size());
    packet.reportId = record->request.hardware_ids.report_id;

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
  trace_status("DriverEntry begin");

  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, LvhEvtDeviceAdd);

  const auto status = WdfDriverCreate(driver_object, registry_path, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
  trace_status("DriverEntry WdfDriverCreate", status);
  return status;
}

NTSTATUS LvhEvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT device_init) {
  UNREFERENCED_PARAMETER(driver);

  trace_status("EvtDeviceAdd begin");

  WDF_PNPPOWER_EVENT_CALLBACKS pnp_callbacks;
  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp_callbacks);
  pnp_callbacks.EvtDevicePrepareHardware = LvhEvtDevicePrepareHardware;
  pnp_callbacks.EvtDeviceReleaseHardware = LvhEvtDeviceReleaseHardware;
  WdfDeviceInitSetPnpPowerEventCallbacks(device_init, &pnp_callbacks);

  WDF_FILEOBJECT_CONFIG file_config;
  WDF_FILEOBJECT_CONFIG_INIT(&file_config, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, LvhEvtFileCleanup);
  WdfDeviceInitSetFileObjectConfig(device_init, &file_config, WDF_NO_OBJECT_ATTRIBUTES);

  WDFDEVICE device = nullptr;
  WDF_OBJECT_ATTRIBUTES device_attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&device_attributes);
  device_attributes.EvtCleanupCallback = LvhEvtDeviceCleanup;
  auto status = WdfDeviceCreate(&device_init, &device_attributes, &device);
  trace_status("EvtDeviceAdd WdfDeviceCreate", status);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  status = WdfDeviceCreateDeviceInterface(device, &control_device_interface_guid, nullptr);
  trace_status("EvtDeviceAdd WdfDeviceCreateDeviceInterface", status);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  status =
    create_control_symbolic_link(device, global_symbolic_link_name, "EvtDeviceAdd WdfDeviceCreateSymbolicLink global");
  if (!NT_SUCCESS(status)) {
    return status;
  }

  static_cast<void>(
    create_control_symbolic_link(device, symbolic_link_name, "EvtDeviceAdd WdfDeviceCreateSymbolicLink local")
  );

  WDF_IO_QUEUE_CONFIG queue_config;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue_config, WdfIoQueueDispatchParallel);
  queue_config.EvtIoDeviceControl = LvhEvtIoDeviceControl;

  status = WdfIoQueueCreate(device, &queue_config, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
  trace_status("EvtDeviceAdd WdfIoQueueCreate", status);
  return status;
}

NTSTATUS LvhEvtDevicePrepareHardware(
  WDFDEVICE device,
  WDFCMRESLIST resources_raw,
  WDFCMRESLIST resources_translated
) {
  UNREFERENCED_PARAMETER(device);
  UNREFERENCED_PARAMETER(resources_raw);
  UNREFERENCED_PARAMETER(resources_translated);

  trace_status("EvtDevicePrepareHardware begin");

  // The control device should still start if the local VHF target cannot be
  // opened yet. Gamepad creation will initialize VHF lazily and report the
  // backend failure through the IOCTL response if the target is unavailable.
  return STATUS_SUCCESS;
}

NTSTATUS LvhEvtDeviceReleaseHardware(WDFDEVICE device, WDFCMRESLIST resources_translated) {
  UNREFERENCED_PARAMETER(device);
  UNREFERENCED_PARAMETER(resources_translated);

  trace_status("EvtDeviceReleaseHardware begin");
  complete_pending_output_requests(STATUS_CANCELLED);
  reset_vhf_target(true);
  return STATUS_SUCCESS;
}

void LvhEvtDeviceCleanup(WDFOBJECT device_object) {
  UNREFERENCED_PARAMETER(device_object);

  trace_status("EvtDeviceCleanup begin");
  complete_pending_output_requests(STATUS_CANCELLED);
  reset_vhf_target(false);
}

void LvhEvtFileCleanup(WDFFILEOBJECT file_object) {
  trace_status("EvtFileCleanup begin");
  delete_vhf_devices_for_file(file_object);
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
  copy_vhf_output_payload(event, *hid_transfer_packet);

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
  UNREFERENCED_PARAMETER(output_buffer_length);
  UNREFERENCED_PARAMETER(input_buffer_length);

  switch (io_control_code) {
    case LVH_WINDOWS_IOCTL_CREATE_GAMEPAD:
      handle_create_gamepad_request(WdfIoQueueGetDevice(queue), request);
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
