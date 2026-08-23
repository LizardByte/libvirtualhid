// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

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
#include <bcrypt.h>

// standard includes
#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

// local includes
#include "generic_pid_protocol.hpp"
#include "lvh_windows_protocol.h"
#include "mouse_protocol.hpp"
#include "playstation_feature_protocol.hpp"
#include "rotating_trace_log.hpp"
#include "switch_pro_protocol.hpp"
#include "unique_win32_handle.hpp"
#include "vhf_input_report_queue.hpp"
#include "windows_device_identity.hpp"

using VhfContext = PVOID;  // NOSONAR(cpp:S5008): VHF callback ABI requires PVOID; client context narrows to DeviceRecord.

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD LvhEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE LvhEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE LvhEvtDeviceReleaseHardware;
EVT_WDF_FILE_CLEANUP LvhEvtFileCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LvhEvtIoDeviceControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP LvhEvtDeviceCleanup;
EVT_WDF_REQUEST_CANCEL LvhEvtOutputReadCanceled;
EVT_VHF_ASYNC_OPERATION LvhEvtVhfGetFeature;
EVT_VHF_READY_FOR_NEXT_READ_REPORT LvhEvtVhfReadyForNextReadReport;
EVT_VHF_ASYNC_OPERATION LvhEvtVhfSetFeature;
EVT_VHF_ASYNC_OPERATION LvhEvtVhfWriteReport;

namespace {

  constexpr auto symbolic_link_name = L"\\DosDevices\\LibVirtualHid";
  constexpr auto global_symbolic_link_name = L"\\DosDevices\\Global\\LibVirtualHid";
  constexpr auto broker_service_name = L"libvirtualhid_broker";
  constexpr auto broker_service_account_name = L"NT SERVICE\\libvirtualhid_broker";
  constexpr auto trace_file_name = std::wstring_view {L"libvirtualhid-umdf-driver.log"};
  constexpr auto known_gamepad_flags =
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY |
    LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS;

  using UniqueServiceHandle = std::unique_ptr<
    std::remove_pointer_t<SC_HANDLE>,
    decltype(&::CloseServiceHandle)>;

  struct DeviceRecord {
    explicit DeviceRecord(const LvhWindowsCreateDeviceRequest &create_request):
        request {create_request},
        pending_input_reports {
          create_request.device_type,
          create_request.gamepad_kind,
          create_request.bus_type,
          create_request.hardware_ids.report_id,
        } {
    }

    std::mutex mutex;
    std::condition_variable submissions_drained;
    std::uint64_t driver_device_id {};
    WDFDEVICE owner_device {};
    WDFFILEOBJECT owner_file {};
    WDFIOTARGET vhf_io_target {};
    LvhWindowsCreateDeviceRequest request {};
    LvhWindowsSessionToken session_token {};
    VHFHANDLE vhf_handle {};
    std::vector<UCHAR> report_descriptor;
    std::wstring hardware_ids;
    lvh::detail::windows::GenericPidFeatureState generic_pid_feature_state;
    lvh::detail::windows::VhfInputReportQueue pending_input_reports;
    std::shared_ptr<std::vector<std::uint8_t>> in_flight_input_report;
    std::size_t active_input_submissions {};
    bool vhf_ready_for_input_report {};
    bool shutting_down {};
  };

  struct PendingOutputRequest {
    WDFREQUEST request {};
    WDFFILEOBJECT owner_file {};
  };

  struct BufferedOutputEvent {
    LvhWindowsOutputReportEvent event {};
    WDFFILEOBJECT owner_file {};
  };

  struct DriverState {
    std::atomic<std::uint64_t> next_driver_device_id {1};
    std::mutex devices_mutex;
    std::map<std::uint64_t, std::shared_ptr<DeviceRecord>> devices;
    std::mutex output_requests_mutex;
    std::vector<PendingOutputRequest> pending_output_requests;
    std::vector<BufferedOutputEvent> buffered_output_events;
  };

  DriverState &driver_state() {
    static DriverState state;
    return state;
  }

  void trace_status(const char *step, NTSTATUS status = STATUS_SUCCESS) {
    static std::atomic<unsigned long> sequence {0};

    constexpr auto trace_file_path_length = static_cast<DWORD>(MAX_PATH);
    std::wstring trace_file_path(trace_file_path_length, L'\0');
    auto trace_path_size = GetWindowsDirectoryW(trace_file_path.data(), trace_file_path_length);
    if (trace_path_size == 0U || trace_path_size >= trace_file_path_length) {
      return;
    }

    constexpr auto trace_directory = std::wstring_view {L"\\Temp\\"};
    const auto required_path_size =
      static_cast<std::size_t>(trace_path_size) + trace_directory.size() + trace_file_name.size();
    if (required_path_size >= trace_file_path_length) {
      return;
    }
    trace_file_path.resize(trace_path_size);
    trace_file_path.append(trace_directory);
    trace_file_path.append(trace_file_name);

    SYSTEMTIME time {};
    GetSystemTime(&time);

    const auto line = std::format(
      "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z [{}] {} status=0x{:08X}\r\n",
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
    static_cast<void>(lvh::detail::windows::append_rotating_trace_log(trace_file_path, line));
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
    const auto iter = std::ranges::find(state.pending_output_requests, request, &PendingOutputRequest::request);
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

  struct VhfInputSubmission {
    VHFHANDLE vhf_handle {};
    std::shared_ptr<std::vector<std::uint8_t>> report;
    std::uint8_t report_id {};
  };

  std::optional<VhfInputSubmission> prepare_vhf_input_submission(DeviceRecord &record) {
    std::lock_guard lock {record.mutex};
    if (record.shutting_down || record.vhf_handle == nullptr || !record.vhf_ready_for_input_report) {
      return std::nullopt;
    }

    auto pending = record.pending_input_reports.pop();
    if (!pending.has_value()) {
      return std::nullopt;
    }

    auto report = std::make_shared<std::vector<std::uint8_t>>(std::move(*pending));
    const auto configured_report_id = record.request.hardware_ids.report_id;
    const auto report_id = configured_report_id == 0U || report->empty() ? configured_report_id : report->front();

    record.vhf_ready_for_input_report = false;
    record.in_flight_input_report = report;
    ++record.active_input_submissions;
    return VhfInputSubmission {
      .vhf_handle = record.vhf_handle,
      .report = std::move(report),
      .report_id = report_id,
    };
  }

  std::optional<NTSTATUS> submit_next_vhf_input_report(DeviceRecord &record) {
    auto submission = prepare_vhf_input_submission(record);
    if (!submission.has_value()) {
      return std::nullopt;
    }

    HID_XFER_PACKET packet {};
    packet.reportBuffer = submission->report->data();
    packet.reportBufferLen = static_cast<ULONG>(submission->report->size());
    packet.reportId = submission->report_id;
    const auto status = VhfReadReportSubmit(submission->vhf_handle, &packet);

    {
      std::lock_guard lock {record.mutex};
      --record.active_input_submissions;
      if (!NT_SUCCESS(status) && record.in_flight_input_report == submission->report) {
        record.in_flight_input_report.reset();
      }
    }
    record.submissions_drained.notify_all();

    if (!NT_SUCCESS(status)) {
      trace_status("submit_next_vhf_input_report VhfReadReportSubmit", status);
    }
    return status;
  }

  NTSTATUS queue_vhf_input_report(DeviceRecord &record, std::vector<std::uint8_t> report) {
    {
      std::lock_guard lock {record.mutex};
      if (record.shutting_down || record.vhf_handle == nullptr) {
        return STATUS_OBJECT_NAME_NOT_FOUND;
      }
      record.pending_input_reports.push(std::move(report));
    }

    if (const auto status = submit_next_vhf_input_report(record); status.has_value()) {
      return *status;
    }

    std::lock_guard lock {record.mutex};
    return record.shutting_down || record.vhf_handle == nullptr ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_SUCCESS;
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

  void queue_output_event(WDFFILEOBJECT owner_file, const LvhWindowsOutputReportEvent &event) {
    WDFREQUEST request = nullptr;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.output_requests_mutex};
      const auto pending = std::ranges::find(
        state.pending_output_requests,
        owner_file,
        &PendingOutputRequest::owner_file
      );
      if (pending == state.pending_output_requests.end()) {
        state.buffered_output_events.push_back({event, owner_file});
        return;
      }

      request = pending->request;
      state.pending_output_requests.erase(pending);
    }

    if (!complete_output_request(request, event, true)) {
      auto &state = driver_state();
      std::lock_guard lock {state.output_requests_mutex};
      state.buffered_output_events.push_back({event, owner_file});
    }
  }

  void complete_pending_output_requests(NTSTATUS status) {
    std::vector<PendingOutputRequest> requests;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.output_requests_mutex};
      requests.swap(state.pending_output_requests);
      state.buffered_output_events.clear();
    }

    for (const auto &pending : requests) {
      if (NT_SUCCESS(WdfRequestUnmarkCancelable(pending.request))) {
        complete_request(pending.request, status);
      }
    }
  }

  void cancel_output_requests_for_file(WDFFILEOBJECT file_object) {
    std::vector<WDFREQUEST> requests;
    auto &state = driver_state();
    {
      std::lock_guard lock {state.output_requests_mutex};
      static_cast<void>(std::erase_if(state.pending_output_requests, [&](const auto &pending) {
        if (pending.owner_file != file_object) {
          return false;
        }
        requests.push_back(pending.request);
        return true;
      }));
      static_cast<void>(std::erase_if(state.buffered_output_events, [&](const auto &buffered) {
        return buffered.owner_file == file_object;
      }));
    }

    for (const auto request : requests) {
      if (NT_SUCCESS(WdfRequestUnmarkCancelable(request))) {
        complete_request(request, STATUS_CANCELLED);
      }
    }
  }

  void delete_vhf_device(const std::shared_ptr<DeviceRecord> &record) {
    if (!record) {
      return;
    }

    trace_status("delete_vhf_device begin");

    VHFHANDLE vhf_handle = nullptr;
    WDFIOTARGET vhf_io_target = nullptr;
    {
      std::unique_lock lock {record->mutex};
      record->shutting_down = true;
      vhf_handle = record->vhf_handle;
      record->vhf_handle = nullptr;
      record->vhf_ready_for_input_report = false;
      record->pending_input_reports.clear();
      record->submissions_drained.wait(lock, [record] {
        return record->active_input_submissions == 0U;
      });
      vhf_io_target = record->vhf_io_target;
      record->vhf_io_target = nullptr;
    }

    if (vhf_handle != nullptr) {
      trace_status("delete_vhf_device VhfDelete");
      VhfDelete(vhf_handle, TRUE);
    }

    {
      std::lock_guard lock {record->mutex};
      record->in_flight_input_report.reset();
    }

    if (vhf_io_target != nullptr) {
      trace_status("delete_vhf_device WdfObjectDelete target");
      WdfObjectDelete(vhf_io_target);
    }
  }

  void delete_vhf_devices_for_device(WDFDEVICE device) {
    if (device == nullptr) {
      return;
    }

    trace_status("delete_vhf_devices_for_device begin");

    std::vector<std::shared_ptr<DeviceRecord>> devices;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.devices_mutex};
      static_cast<void>(std::erase_if(
        state.devices,
        [&](const auto &entry) {
          if (entry.second->owner_device != device) {
            return false;
          }

          trace_status("delete_vhf_devices_for_device matched");
          devices.push_back(entry.second);
          return true;
        }
      ));
    }

    for (const auto &record : devices) {
      delete_vhf_device(record);
    }
  }

  void delete_vhf_devices_for_file(WDFFILEOBJECT file_object) {
    if (file_object == nullptr) {
      return;
    }

    trace_status("delete_vhf_devices_for_file begin");

    std::vector<std::shared_ptr<DeviceRecord>> devices;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.devices_mutex};
      static_cast<void>(std::erase_if(
        state.devices,
        [&](const auto &entry) {
          if (entry.second->owner_file != file_object) {
            return false;
          }

          trace_status("delete_vhf_devices_for_file matched");
          devices.push_back(entry.second);
          return true;
        }
      ));
    }

    for (const auto &record : devices) {
      delete_vhf_device(record);
    }
  }

  NTSTATUS initialize_vhf_target(WDFDEVICE device, const std::shared_ptr<DeviceRecord> &record) {
    WDF_OBJECT_ATTRIBUTES target_attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&target_attributes);
    target_attributes.ParentObject = record->owner_file != nullptr ? WDFOBJECT(record->owner_file) : WDFOBJECT(device);

    WDFIOTARGET vhf_io_target = nullptr;
    auto status = WdfIoTargetCreate(device, &target_attributes, &vhf_io_target);
    trace_status("initialize_vhf_target WdfIoTargetCreate", status);
    if (!NT_SUCCESS(status)) {
      return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS open_params;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_FILE(&open_params, nullptr);
    status = WdfIoTargetOpen(vhf_io_target, &open_params);
    trace_status("initialize_vhf_target WdfIoTargetOpen", status);
    if (!NT_SUCCESS(status)) {
      WdfObjectDelete(vhf_io_target);
      return status;
    }

    record->vhf_io_target = vhf_io_target;
    return STATUS_SUCCESS;
  }

  void reset_vhf_devices(WDFDEVICE device) {
    delete_vhf_devices_for_device(device);
  }

  NTSTATUS create_vhf_device(WDFDEVICE device, const std::shared_ptr<DeviceRecord> &record) {
    auto status = initialize_vhf_target(device, record);
    if (!NT_SUCCESS(status)) {
      return status;
    }

    const auto descriptor_size = record->request.report_sizes.report_descriptor_size;
    record->report_descriptor.assign(
      record->request.report_descriptor.data(),
      record->request.report_descriptor.data() + descriptor_size
    );
    record->hardware_ids = lvh::detail::windows::make_hardware_ids(record->request);

    VHF_CONFIG vhf_config;
    VHF_CONFIG_INIT(
      &vhf_config,
      WdfIoTargetWdmGetTargetFileHandle(record->vhf_io_target),
      static_cast<USHORT>(record->report_descriptor.size()),
      record->report_descriptor.data()
    );
    vhf_config.VhfClientContext = record.get();
    vhf_config.VendorID = record->request.hardware_ids.vendor_id;
    vhf_config.ProductID = record->request.hardware_ids.product_id;
    vhf_config.VersionNumber = record->request.hardware_ids.device_version;
    vhf_config.HardwareIDsLength = static_cast<USHORT>(record->hardware_ids.size() * sizeof(wchar_t));
    vhf_config.HardwareIDs = record->hardware_ids.data();
    vhf_config.EvtVhfReadyForNextReadReport = LvhEvtVhfReadyForNextReadReport;
    vhf_config.EvtVhfAsyncOperationGetFeature = LvhEvtVhfGetFeature;
    vhf_config.EvtVhfAsyncOperationSetFeature = LvhEvtVhfSetFeature;
    vhf_config.EvtVhfAsyncOperationWriteReport = LvhEvtVhfWriteReport;

    status = VhfCreate(&vhf_config, &record->vhf_handle);
    trace_status("create_vhf_device VhfCreate", status);
    if (!NT_SUCCESS(status)) {
      record->vhf_handle = nullptr;
      delete_vhf_device(record);
      return status;
    }

    status = VhfStart(record->vhf_handle);
    trace_status("create_vhf_device VhfStart", status);
    if (!NT_SUCCESS(status)) {
      delete_vhf_device(record);
    }

    return status;
  }

  bool valid_create_request(const LvhWindowsCreateDeviceRequest &request) {
    const auto descriptor_size = request.report_sizes.report_descriptor_size;
    const auto input_report_size = request.report_sizes.input_report_size;
    const auto output_report_size = request.report_sizes.output_report_size;

    const auto known_device = request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD ||
                              request.device_type == LVH_WINDOWS_DEVICE_MOUSE;
    const auto known_bus = request.bus_type == LVH_WINDOWS_BUS_UNKNOWN ||
                           request.bus_type == LVH_WINDOWS_BUS_USB ||
                           request.bus_type == LVH_WINDOWS_BUS_BLUETOOTH;
    const auto known_profile = request.gamepad_kind <= LVH_WINDOWS_GAMEPAD_DUALSHOCK4;
    const auto valid_mouse_descriptor =
      request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD ||
      (descriptor_size == lvh::detail::windows::mouse_report_descriptor.size() &&
       std::equal(
         lvh::detail::windows::mouse_report_descriptor.begin(),
         lvh::detail::windows::mouse_report_descriptor.end(),
         request.report_descriptor.begin()
       ));
    const auto valid_device_fields = request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD ||
                                     (request.gamepad_kind == LVH_WINDOWS_GAMEPAD_GENERIC &&
                                      request.flags == 0U &&
                                      request.hardware_ids.report_id == 0U &&
                                      input_report_size == LVH_WINDOWS_MOUSE_INPUT_REPORT_SIZE &&
                                      output_report_size == 0U);

    return valid_header(request.version, request.size, sizeof(request)) &&
           request.client_device_id != 0U && known_device && known_bus && known_profile && valid_device_fields &&
           valid_mouse_descriptor &&
           (request.flags & ~known_gamepad_flags) == 0U &&
           std::ranges::all_of(request.hardware_ids.reserved0, [](const auto value) {
             return value == 0U;
           }) &&
           descriptor_size > 0U && descriptor_size <= LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE && input_report_size > 0U && input_report_size <= LVH_WINDOWS_MAX_INPUT_REPORT_SIZE && output_report_size <= LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE;
  }

  bool valid_submit_input_report_request(const LvhWindowsSubmitInputReportRequest &request) {
    return valid_header(request.version, request.size, sizeof(request)) && request.report_size > 0U &&
           request.report_size <= LVH_WINDOWS_MAX_INPUT_REPORT_SIZE;
  }

  bool valid_destroy_device_request(const LvhWindowsDestroyDeviceRequest &request) {
    return valid_header(request.version, request.size, sizeof(request));
  }

  bool valid_reset_devices_request(const LvhWindowsResetDevicesRequest &request) {
    return valid_header(request.version, request.size, sizeof(request));
  }

  bool session_token_matches(const DeviceRecord &record, const LvhWindowsSessionToken &session_token) {
    return std::ranges::equal(record.session_token.bytes, session_token.bytes);
  }

  NTSTATUS generate_session_token(LvhWindowsSessionToken &session_token) {
    const auto status = BCryptGenRandom(
      nullptr,
      session_token.bytes.data(),
      static_cast<ULONG>(session_token.bytes.size()),
      BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );
    if (!NT_SUCCESS(status)) {
      return status;
    }

    const auto all_zero = std::ranges::all_of(session_token.bytes, [](const auto value) {
      return value == 0U;
    });
    return all_zero ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
  }

  std::optional<std::vector<std::uint8_t>> lookup_account_sid(const wchar_t *account_name) {
    auto sid_size = DWORD {};
    auto domain_size = DWORD {};
    auto sid_name_use = SID_NAME_USE {};
    static_cast<void>(LookupAccountNameW(
      nullptr,
      account_name,
      nullptr,
      &sid_size,
      nullptr,
      &domain_size,
      &sid_name_use
    ));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sid_size == 0U) {
      trace_status("lookup broker service sid size failed");
      return std::nullopt;
    }

    auto sid = std::vector<std::uint8_t>(sid_size);
    auto domain = std::wstring(domain_size, L'\0');
    if (LookupAccountNameW(nullptr, account_name, sid.data(), &sid_size, domain.data(), &domain_size, &sid_name_use) == FALSE) {
      trace_status("lookup broker service sid failed");
      return std::nullopt;
    }

    sid.resize(sid_size);
    return sid;
  }

  std::optional<std::vector<std::uint8_t>> broker_service_sid() {
    static std::mutex mutex;
    static auto sid = std::optional<std::vector<std::uint8_t>> {};

    std::lock_guard lock {mutex};
    if (!sid) {
      sid = lookup_account_sid(broker_service_account_name);
    }
    return sid;
  }

  bool token_has_sid(HANDLE token, const std::vector<std::uint8_t> &sid) {
    if (sid.empty()) {
      return false;
    }

    auto sid_to_check = sid;
    if (IsValidSid(sid_to_check.data()) == FALSE) {
      return false;
    }

    auto token_groups_size = DWORD {};
    static_cast<void>(GetTokenInformation(token, TokenGroups, nullptr, 0, &token_groups_size));
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || token_groups_size == 0U) {
      trace_status("broker service sid membership failed: token group size");
      return false;
    }

    auto token_groups_buffer = std::make_unique_for_overwrite<std::byte[]>(token_groups_size);
    auto *token_groups = static_cast<TOKEN_GROUPS *>(static_cast<void *>(token_groups_buffer.get()));
    if (GetTokenInformation(token, TokenGroups, token_groups, token_groups_size, &token_groups_size) == FALSE) {
      trace_status("broker service sid membership failed: token groups");
      return false;
    }

    for (auto index = DWORD {0}; index < token_groups->GroupCount; ++index) {
      const auto &group = token_groups->Groups[index];
      if ((group.Attributes & SE_GROUP_ENABLED) != 0U && EqualSid(group.Sid, sid_to_check.data()) != FALSE) {
        return true;
      }
    }
    return false;
  }

  bool requestor_is_running_broker_service(DWORD requestor_process_id) {
    auto service_manager = UniqueServiceHandle {
      ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT),
      &::CloseServiceHandle
    };
    if (!service_manager) {
      trace_status("broker service identity failed: open service manager");
      return false;
    }

    auto service = UniqueServiceHandle {
      ::OpenServiceW(service_manager.get(), broker_service_name, SERVICE_QUERY_STATUS),
      &::CloseServiceHandle
    };
    if (!service) {
      trace_status("broker service identity failed: open service");
      return false;
    }

    SERVICE_STATUS_PROCESS status {};
    const auto status_bytes = std::as_writable_bytes(std::span {&status, 1});
    auto bytes_needed = DWORD {};
    if (::QueryServiceStatusEx(service.get(), SC_STATUS_PROCESS_INFO, std::bit_cast<LPBYTE>(status_bytes.data()), static_cast<DWORD>(status_bytes.size()), &bytes_needed) == FALSE) {
      trace_status("broker service identity failed: query status");
      return false;
    }

    return status.dwCurrentState == SERVICE_RUNNING &&
           status.dwProcessId == requestor_process_id;
  }

  bool request_is_authorized_broker_service(WDFREQUEST request) {
    const auto requestor_process_id = WdfRequestGetRequestorProcessId(request);
    if (requestor_process_id == 0U) {
      trace_status("broker service access denied: missing requestor pid");
      return false;
    }

    auto process = lvh::detail::windows::make_unique_win32_handle(
      OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, requestor_process_id)
    );
    if (!process) {
      trace_status("broker service identity: open requestor process failed");
      return requestor_is_running_broker_service(requestor_process_id);
    }

    auto token_handle = HANDLE {};
    if (OpenProcessToken(process.get(), TOKEN_QUERY, &token_handle) == FALSE) {
      trace_status("broker service identity: open requestor token failed");
      return requestor_is_running_broker_service(requestor_process_id);
    }

    const auto token = lvh::detail::windows::make_unique_win32_handle(token_handle);
    const auto service_sid = broker_service_sid();
    if (service_sid && token_has_sid(token.get(), *service_sid)) {
      return true;
    }

    if (requestor_is_running_broker_service(requestor_process_id)) {
      trace_status("broker service authorized by running service identity");
      return true;
    }

    trace_status("broker service access denied: service identity not present");
    return false;
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
    if (request.report_size != record.request.report_sizes.input_report_size) {
      return {};
    }

    const auto report_id = record.request.hardware_ids.report_id;
    const auto report_begin = request.report.data();
    const auto report_end = request.report.data() + request.report_size;
    if (report_id == 0U) {
      return {report_begin, report_end};
    }

    if (request.report[0] != report_id) {
      return {};
    }

    return {report_begin, report_end};
  }

  void copy_vhf_output_payload(
    LvhWindowsOutputReportEvent &event,
    const HID_XFER_PACKET &packet
  ) {
    const auto report_id = packet.reportId;
    const auto packet_includes_report_id =
      report_id != 0U && packet.reportBufferLen > 0U && packet.reportBuffer[0] == report_id;
    const auto report_id_size = report_id == 0U || packet_includes_report_id ? 0U : 1U;
    const auto payload_capacity = LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE - report_id_size;
    const auto payload_size = std::min(packet.reportBufferLen, static_cast<ULONG>(payload_capacity));

    if (report_id_size != 0U) {
      event.report[0] = report_id;
    }

    if (payload_size > 0U) {
      std::memcpy(event.report.data() + report_id_size, packet.reportBuffer, payload_size);
    }

    event.report_size = static_cast<std::uint32_t>(report_id_size + payload_size);
  }

  void submit_switch_pro_reply(DeviceRecord &record, const LvhWindowsOutputReportEvent &event) {
    if (record.request.gamepad_kind != LVH_WINDOWS_GAMEPAD_SWITCH_PRO) {
      return;
    }

    auto reply = lvh::detail::windows::make_switch_pro_reply({event.report.data(), event.report_size});
    if (!reply.has_value()) {
      return;
    }

    static_cast<void>(queue_vhf_input_report(record, {reply->begin(), reply->end()}));
  }

  LvhWindowsOutputReportEvent make_output_event(DeviceRecord &record, const HID_XFER_PACKET &packet) {
    LvhWindowsOutputReportEvent event {};
    event.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
    event.size = sizeof(event);
    event.driver_device_id = record.driver_device_id;
    copy_vhf_output_payload(event, packet);
    return event;
  }

  NTSTATUS copy_vhf_feature_report(DeviceRecord &record, HID_XFER_PACKET &packet) {
    auto report_number = packet.reportId;
    if (report_number == 0U && packet.reportBufferLen > 0U) {
      report_number = packet.reportBuffer[0];
    }

    auto report = std::optional<std::vector<std::uint8_t>> {};
    if (
      record.request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD &&
      record.request.gamepad_kind == LVH_WINDOWS_GAMEPAD_GENERIC
    ) {
      std::lock_guard lock {record.mutex};
      report = record.generic_pid_feature_state.get_feature_report(report_number);
    } else {
      report = lvh::detail::windows::make_playstation_feature_report(record.request, report_number);
    }
    if (!report.has_value()) {
      return STATUS_NOT_SUPPORTED;
    }
    if (packet.reportBufferLen < report->size()) {
      return STATUS_BUFFER_TOO_SMALL;
    }

    std::fill_n(packet.reportBuffer, packet.reportBufferLen, UCHAR {});
    std::copy(report->begin(), report->end(), packet.reportBuffer);
    return STATUS_SUCCESS;
  }

  bool handle_vhf_set_feature(DeviceRecord &record, const HID_XFER_PACKET &packet) {
    if (
      record.request.device_type == LVH_WINDOWS_DEVICE_GAMEPAD &&
      record.request.gamepad_kind == LVH_WINDOWS_GAMEPAD_GENERIC
    ) {
      auto event = make_output_event(record, packet);
      const auto report_id = packet.reportId == 0U && event.report_size > 0U ? event.report[0] : packet.reportId;
      std::lock_guard lock {record.mutex};
      return record.generic_pid_feature_state.handle_set_feature(
        static_cast<std::uint8_t>(report_id),
        {event.report.data(), event.report_size}
      );
    }
    return lvh::detail::windows::is_playstation_gamepad(record.request.gamepad_kind);
  }

  void handle_vhf_output_report(DeviceRecord &record, const LvhWindowsOutputReportEvent &event) {
    if (
      record.request.device_type != LVH_WINDOWS_DEVICE_GAMEPAD ||
      record.request.gamepad_kind != LVH_WINDOWS_GAMEPAD_GENERIC
    ) {
      return;
    }

    std::lock_guard lock {record.mutex};
    static_cast<void>(record.generic_pid_feature_state.handle_output_report(
      static_cast<std::uint8_t>(event.report[0]),
      {event.report.data(), event.report_size}
    ));
  }

  void set_device_path(
    std::uint64_t driver_device_id,
    std::array<char, LVH_WINDOWS_MAX_DEVICE_PATH_SIZE> &device_path
  ) {
    constexpr auto path_prefix_size = sizeof(LVH_WINDOWS_CONTROL_DEVICE_PATH) - 1U;
    constexpr auto separator_size = 1U;
    static_assert(path_prefix_size + separator_size < LVH_WINDOWS_MAX_DEVICE_PATH_SIZE);

    std::memcpy(device_path.data(), LVH_WINDOWS_CONTROL_DEVICE_PATH, path_prefix_size);
    device_path[path_prefix_size] = '#';

    const auto output = std::to_chars(
      device_path.data() + path_prefix_size + separator_size,
      device_path.data() + device_path.size() - 1U,
      driver_device_id
    );
    if (output.ec == std::errc {}) {
      *output.ptr = '\0';
    } else {
      device_path[path_prefix_size + separator_size] = '\0';
    }
  }

  void handle_create_device_request(WDFDEVICE device, WDFREQUEST request) {
    if (!request_is_authorized_broker_service(request)) {
      complete_request(request, STATUS_ACCESS_DENIED);
      return;
    }

    auto *create_request = static_cast<LvhWindowsCreateDeviceRequest *>(nullptr);
    auto status = retrieve_input_buffer(request, create_request);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }

    auto *create_response = static_cast<LvhWindowsCreateDeviceResponse *>(nullptr);
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
    auto record = std::make_shared<DeviceRecord>(*create_request);
    record->driver_device_id = driver_device_id;
    record->owner_device = device;
    record->owner_file = WdfRequestGetFileObject(request);
    status = generate_session_token(record->session_token);
    if (!NT_SUCCESS(status)) {
      trace_status("create_device token failed", status);
      create_response->status = LVH_WINDOWS_STATUS_BACKEND_FAILURE;
      complete_request(request, STATUS_SUCCESS, sizeof(*create_response));
      return;
    }

    trace_status("create_device begin");

    status = create_vhf_device(device, record);
    if (!NT_SUCCESS(status)) {
      trace_status("create_device failed", status);
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
    create_response->session_token = record->session_token;
    set_device_path(driver_device_id, create_response->device_path);
    trace_status("create_device success");
    complete_request(request, STATUS_SUCCESS, sizeof(*create_response));
  }

  void handle_destroy_device_request(WDFREQUEST request) {
    if (!request_is_authorized_broker_service(request)) {
      complete_request(request, STATUS_ACCESS_DENIED);
      return;
    }

    auto *destroy_request = static_cast<LvhWindowsDestroyDeviceRequest *>(nullptr);
    const auto status = retrieve_input_buffer(request, destroy_request);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }

    if (!valid_destroy_device_request(*destroy_request)) {
      complete_request(request, STATUS_INVALID_PARAMETER);
      return;
    }

    auto record = std::shared_ptr<DeviceRecord> {};
    {
      auto &state = driver_state();
      std::lock_guard lock {state.devices_mutex};
      const auto iter = state.devices.find(destroy_request->driver_device_id);
      if (iter != state.devices.end()) {
        if (!session_token_matches(*iter->second, destroy_request->session_token)) {
          trace_status("destroy_device access denied");
          complete_request(request, STATUS_ACCESS_DENIED);
          return;
        }

        record = iter->second;
        state.devices.erase(iter);
        trace_status("destroy_device found");
      } else {
        trace_status("destroy_device missing");
      }
    }
    delete_vhf_device(record);
    complete_request(request, STATUS_SUCCESS);
  }

  void handle_reset_devices_request(WDFDEVICE device, WDFREQUEST request) {
    if (!request_is_authorized_broker_service(request)) {
      complete_request(request, STATUS_ACCESS_DENIED);
      return;
    }

    auto *reset_request = static_cast<LvhWindowsResetDevicesRequest *>(nullptr);
    const auto status = retrieve_input_buffer(request, reset_request);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }

    if (!valid_reset_devices_request(*reset_request)) {
      complete_request(request, STATUS_INVALID_PARAMETER);
      return;
    }

    delete_vhf_devices_for_device(device);
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
      trace_status("submit_input_report missing device");
      complete_request(request, STATUS_OBJECT_NAME_NOT_FOUND);
      return;
    }

    if (!session_token_matches(*record, submit_request->session_token)) {
      trace_status("submit_input_report access denied");
      complete_request(request, STATUS_ACCESS_DENIED);
      return;
    }

    auto report = make_vhf_input_payload(*record, *submit_request);
    if (report.empty()) {
      trace_status("submit_input_report invalid payload");
      complete_request(request, STATUS_INVALID_PARAMETER);
      return;
    }

    const auto queue_status = queue_vhf_input_report(*record, std::move(report));
    if (!NT_SUCCESS(queue_status)) {
      trace_status("submit_input_report queue failed", queue_status);
    }
    complete_request(request, queue_status);
  }

  void handle_read_output_report_request(WDFREQUEST request) {
    std::optional<LvhWindowsOutputReportEvent> buffered_event;
    const auto owner_file = WdfRequestGetFileObject(request);
    auto &state = driver_state();
    {
      std::lock_guard lock {state.output_requests_mutex};
      const auto buffered = std::ranges::find(
        state.buffered_output_events,
        owner_file,
        &BufferedOutputEvent::owner_file
      );
      if (buffered != state.buffered_output_events.end()) {
        buffered_event = buffered->event;
        state.buffered_output_events.erase(buffered);
      } else {
        const auto status = WdfRequestMarkCancelableEx(request, LvhEvtOutputReadCanceled);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
          return;
        }

        state.pending_output_requests.push_back({request, owner_file});
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
  reset_vhf_devices(device);
  return STATUS_SUCCESS;
}

void LvhEvtDeviceCleanup(WDFOBJECT device_object) {
  trace_status("EvtDeviceCleanup begin");
  complete_pending_output_requests(STATUS_CANCELLED);
  reset_vhf_devices(WDFDEVICE(device_object));
}

void LvhEvtFileCleanup(WDFFILEOBJECT file_object) {
  trace_status("EvtFileCleanup begin");
  delete_vhf_devices_for_file(file_object);
  cancel_output_requests_for_file(file_object);
}

void LvhEvtOutputReadCanceled(WDFREQUEST request) {
  if (remove_pending_output_request(request)) {
    complete_request(request, STATUS_CANCELLED);
  }
}

void LvhEvtVhfReadyForNextReadReport(VhfContext vhf_client_context) {
  auto *record = static_cast<DeviceRecord *>(vhf_client_context);
  if (record == nullptr) {
    return;
  }

  {
    std::lock_guard lock {record->mutex};
    if (record->shutting_down || record->vhf_handle == nullptr) {
      return;
    }

    // This callback confirms that VHF no longer references the previously
    // submitted buffer and grants permission for exactly one more submission.
    record->in_flight_input_report.reset();
    record->vhf_ready_for_input_report = true;
  }

  static_cast<void>(submit_next_vhf_input_report(*record));
}

void LvhEvtVhfGetFeature(
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

  const auto status = copy_vhf_feature_report(*record, *hid_transfer_packet);
  if (!NT_SUCCESS(status)) {
    trace_status("EvtVhfGetFeature complete", status);
  }
  static_cast<void>(VhfAsyncOperationComplete(vhf_operation_handle, status));
}

void LvhEvtVhfSetFeature(
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
  if (!handle_vhf_set_feature(*record, *hid_transfer_packet)) {
    static_cast<void>(VhfAsyncOperationComplete(vhf_operation_handle, STATUS_NOT_SUPPORTED));
    return;
  }

  queue_output_event(record->owner_file, make_output_event(*record, *hid_transfer_packet));
  static_cast<void>(VhfAsyncOperationComplete(vhf_operation_handle, STATUS_SUCCESS));
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

  auto event = make_output_event(*record, *hid_transfer_packet);

  submit_switch_pro_reply(*record, event);
  handle_vhf_output_report(*record, event);
  queue_output_event(record->owner_file, event);
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
    case LVH_WINDOWS_IOCTL_CREATE_DEVICE:
      handle_create_device_request(WdfIoQueueGetDevice(queue), request);
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

    case LVH_WINDOWS_IOCTL_RESET_DEVICES:
      handle_reset_devices_request(WdfIoQueueGetDevice(queue), request);
      return;

    default:
      complete_request(request, STATUS_INVALID_DEVICE_REQUEST);
      return;
  }
}
