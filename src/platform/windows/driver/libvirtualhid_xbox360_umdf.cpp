// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/driver/libvirtualhid_xbox360_umdf.cpp
 * @brief Per-controller UMDF2 XUSB personality for Xbox 360 emulation.
 */

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

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

#include "windows_device_identity.hpp"
#include "xbox_360_protocol.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

using VhfContext = PVOID;  // NOSONAR(cpp:S5008): required by the VHF callback ABI.

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD LvhXbox360EvtDeviceAdd;
EVT_WDF_FILE_CLEANUP LvhXbox360EvtFileCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL LvhXbox360EvtIoDeviceControl;
EVT_WDF_OBJECT_CONTEXT_CLEANUP LvhXbox360EvtDeviceCleanup;
EVT_WDF_TIMER LvhXbox360EvtInputPump;
EVT_VHF_ASYNC_OPERATION LvhXbox360EvtVhfGetInputReport;
EVT_VHF_READY_FOR_NEXT_READ_REPORT LvhXbox360EvtVhfReadyForNextReadReport;

namespace {

  constexpr auto xbox_version = std::uint16_t {0x0103U};
  constexpr auto xbox_input_pump_period_ms = 8U;

  struct VhfSubmission {
    VHFHANDLE handle {};
    std::shared_ptr<std::vector<std::uint8_t>> report;
    std::uint8_t report_id {};
  };

  struct VirtualHidState {
    WDFIOTARGET target {};
    VHFHANDLE handle {};
    std::vector<std::uint8_t> report_descriptor;
    std::wstring hardware_ids;
    std::vector<std::uint8_t> latest_report;
    std::optional<std::vector<std::uint8_t>> pending_report;
    std::shared_ptr<std::vector<std::uint8_t>> in_flight_report;
    std::size_t active_submissions {};
    bool ready {};
  };

  struct Xbox360DriverState {
    std::mutex mutex;
    std::condition_variable submissions_drained;
    WDFDEVICE device {};
    WDFQUEUE wait_input_queue {};
    WDFQUEUE output_queue {};
    WDFTIMER input_pump {};
    LvhWindowsCreateDeviceRequest create_request {};
    LvhWindowsSessionToken session_token {};
    LvhWindowsXbox360InputState input {};
    VirtualHidState virtual_hid;
    std::optional<LvhWindowsOutputReportEvent> buffered_output;
    std::uint64_t driver_device_id {};
    std::uint32_t packet_number {};
    std::uint8_t low_frequency_rumble {};
    std::uint8_t high_frequency_rumble {};
    bool initialized {};
    bool shutting_down {};
  };

  Xbox360DriverState &driver_state() {
    static Xbox360DriverState state;
    return state;
  }

  template<typename Value>
  void write_little_endian(std::span<std::uint8_t> output, std::size_t offset, Value value) {
    static_assert(std::is_integral_v<Value>);
    using Unsigned = std::make_unsigned_t<Value>;
    const auto raw = static_cast<Unsigned>(value);
    for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
      output[offset + byte] = static_cast<std::uint8_t>(raw >> (byte * 8U));
    }
  }

  bool token_matches(const LvhWindowsSessionToken &left, const LvhWindowsSessionToken &right) {
    return std::ranges::equal(left.bytes, right.bytes);
  }

  bool valid_private_header(std::uint32_t version, std::uint32_t size, std::size_t expected_size) {
    return version == LVH_WINDOWS_XBOX360_PROTOCOL_VERSION && size == expected_size;
  }

  bool initialized_request(
    std::uint64_t driver_device_id,
    const LvhWindowsSessionToken &session_token
  ) {
    auto &state = driver_state();
    std::lock_guard lock {state.mutex};
    return state.initialized && !state.shutting_down && state.driver_device_id == driver_device_id &&
           token_matches(state.session_token, session_token);
  }

  void complete_request(WDFREQUEST request, NTSTATUS status, ULONG_PTR information = 0U) {
    WdfRequestCompleteWithInformation(request, status, information);
  }

  template<typename Value>
  NTSTATUS retrieve_input(WDFREQUEST request, Value *&value) {
    void *buffer = nullptr;
    size_t size = 0;
    const auto status = WdfRequestRetrieveInputBuffer(request, sizeof(Value), &buffer, &size);
    if (!NT_SUCCESS(status)) {
      return status;
    }
    if (size < sizeof(Value)) {
      return STATUS_BUFFER_TOO_SMALL;
    }
    value = static_cast<Value *>(buffer);
    return STATUS_SUCCESS;
  }

  NTSTATUS copy_to_request(WDFREQUEST request, std::span<const std::uint8_t> bytes) {
    void *buffer = nullptr;
    size_t size = 0;
    const auto status = WdfRequestRetrieveOutputBuffer(request, bytes.size(), &buffer, &size);
    if (!NT_SUCCESS(status) || size < bytes.size()) {
      complete_request(request, STATUS_BUFFER_TOO_SMALL);
      return STATUS_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, bytes.data(), bytes.size());
    complete_request(request, STATUS_SUCCESS, bytes.size());
    return STATUS_SUCCESS;
  }

  NTSTATUS copy_event_to_request(WDFREQUEST request, const LvhWindowsOutputReportEvent &event) {
    void *buffer = nullptr;
    size_t size = 0;
    const auto status = WdfRequestRetrieveOutputBuffer(request, sizeof(event), &buffer, &size);
    if (!NT_SUCCESS(status) || size < sizeof(event)) {
      complete_request(request, STATUS_BUFFER_TOO_SMALL);
      return STATUS_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, &event, sizeof(event));
    complete_request(request, STATUS_SUCCESS, sizeof(event));
    return STATUS_SUCCESS;
  }

  std::array<std::uint8_t, 29> make_xusb_state(bool wait_for_input) {
    auto &state = driver_state();
    std::array<std::uint8_t, 29> output {};
    LvhWindowsXbox360InputState input {};
    std::uint32_t packet_number = 0;
    {
      std::lock_guard lock {state.mutex};
      input = state.input;
      packet_number = state.packet_number;
    }

    write_little_endian<std::uint16_t>(output, 0U, xbox_version);
    output[2] = wait_for_input ? 0x03U : 0x01U;
    write_little_endian<std::uint32_t>(output, 5U, packet_number);
    if (wait_for_input) {
      output[9] = 0x00U;
      output[10] = 0x14U;
    }
    write_little_endian<std::uint16_t>(output, 11U, input.buttons);
    output[13] = input.left_trigger;
    output[14] = input.right_trigger;
    write_little_endian<std::int16_t>(output, 15U, input.left_thumb_x);
    write_little_endian<std::int16_t>(output, 17U, input.left_thumb_y);
    write_little_endian<std::int16_t>(output, 19U, input.right_thumb_x);
    write_little_endian<std::int16_t>(output, 21U, input.right_thumb_y);
    return output;
  }

  void complete_one_wait_for_input() {
    auto &state = driver_state();
    WDFQUEUE queue = nullptr;
    {
      std::lock_guard lock {state.mutex};
      if (state.shutting_down) {
        return;
      }
      queue = state.wait_input_queue;
    }
    if (queue == nullptr) {
      return;
    }

    WDFREQUEST request = nullptr;
    if (!NT_SUCCESS(WdfIoQueueRetrieveNextRequest(queue, &request))) {
      return;
    }
    const auto output = make_xusb_state(true);
    static_cast<void>(copy_to_request(request, output));
  }

  std::uint8_t signed_axis_to_hid(std::int16_t value, bool invert) {
    const auto adjusted = invert ? -static_cast<std::int32_t>(value) : static_cast<std::int32_t>(value);
    const auto clamped = std::clamp(adjusted, -32768, 32767);
    return static_cast<std::uint8_t>(
      (static_cast<std::uint32_t>(clamped + 32768) * 255U + 32767U) / 65535U
    );
  }

  std::uint16_t xinput_to_standard_buttons(std::uint16_t buttons) {
    auto result = std::uint16_t {};
    const auto add = [&](std::uint16_t source, std::uint16_t destination) {
      if ((buttons & source) != 0U) {
        result = static_cast<std::uint16_t>(result | destination);
      }
    };
    add(LVH_WINDOWS_XINPUT_A, 1U << 0U);
    add(LVH_WINDOWS_XINPUT_B, 1U << 1U);
    add(LVH_WINDOWS_XINPUT_X, 1U << 2U);
    add(LVH_WINDOWS_XINPUT_Y, 1U << 3U);
    add(LVH_WINDOWS_XINPUT_LEFT_SHOULDER, 1U << 4U);
    add(LVH_WINDOWS_XINPUT_RIGHT_SHOULDER, 1U << 5U);
    add(LVH_WINDOWS_XINPUT_BACK, 1U << 6U);
    add(LVH_WINDOWS_XINPUT_START, 1U << 7U);
    add(LVH_WINDOWS_XINPUT_LEFT_THUMB, 1U << 8U);
    add(LVH_WINDOWS_XINPUT_RIGHT_THUMB, 1U << 9U);
    add(LVH_WINDOWS_XINPUT_GUIDE, 1U << 10U);
    add(LVH_WINDOWS_XINPUT_DPAD_UP, 1U << 12U);
    add(LVH_WINDOWS_XINPUT_DPAD_DOWN, 1U << 13U);
    add(LVH_WINDOWS_XINPUT_DPAD_LEFT, 1U << 14U);
    add(LVH_WINDOWS_XINPUT_DPAD_RIGHT, 1U << 15U);
    return result;
  }

  std::vector<std::uint8_t> make_hid_report(
    const LvhWindowsCreateDeviceRequest &create_request,
    const LvhWindowsXbox360InputState &input
  ) {
    std::vector<std::uint8_t> report(create_request.report_sizes.input_report_size, 0U);
    if (report.size() < 9U) {
      return {};
    }
    report[0] = create_request.hardware_ids.report_id;
    const auto buttons = xinput_to_standard_buttons(input.buttons);
    report[1] = static_cast<std::uint8_t>(buttons & 0xFFU);
    report[2] = static_cast<std::uint8_t>(buttons >> 8U);
    report[3] = signed_axis_to_hid(input.left_thumb_x, false);
    report[4] = signed_axis_to_hid(input.left_thumb_y, true);
    report[5] = input.left_trigger;
    report[6] = signed_axis_to_hid(input.right_thumb_x, false);
    report[7] = signed_axis_to_hid(input.right_thumb_y, true);
    report[8] = input.right_trigger;
    return report;
  }

  std::optional<VhfSubmission> prepare_vhf_submission() {
    auto &state = driver_state();
    std::lock_guard lock {state.mutex};
    if (state.shutting_down || state.virtual_hid.handle == nullptr || !state.virtual_hid.ready || !state.virtual_hid.pending_report.has_value()) {
      return std::nullopt;
    }

    auto report = std::make_shared<std::vector<std::uint8_t>>(std::move(*state.virtual_hid.pending_report));
    state.virtual_hid.pending_report.reset();
    state.virtual_hid.in_flight_report = report;
    state.virtual_hid.ready = false;
    ++state.virtual_hid.active_submissions;
    return VhfSubmission {
      .handle = state.virtual_hid.handle,
      .report = std::move(report),
      .report_id = state.create_request.hardware_ids.report_id,
    };
  }

  NTSTATUS submit_pending_hid_report() {
    auto submission = prepare_vhf_submission();
    if (!submission.has_value()) {
      return STATUS_SUCCESS;
    }

    HID_XFER_PACKET packet {};
    packet.reportBuffer = submission->report->data();
    packet.reportBufferLen = static_cast<ULONG>(submission->report->size());
    packet.reportId = submission->report_id;
    const auto status = VhfReadReportSubmit(submission->handle, &packet);

    auto &state = driver_state();
    {
      std::lock_guard lock {state.mutex};
      --state.virtual_hid.active_submissions;
      if (!NT_SUCCESS(status) && state.virtual_hid.in_flight_report == submission->report) {
        state.virtual_hid.in_flight_report.reset();
      }
    }
    state.submissions_drained.notify_all();
    return status;
  }

  NTSTATUS queue_hid_report(std::vector<std::uint8_t> report) {
    auto &state = driver_state();
    {
      std::lock_guard lock {state.mutex};
      if (state.shutting_down || state.virtual_hid.handle == nullptr) {
        return STATUS_SUCCESS;
      }
      state.virtual_hid.latest_report = report;
      state.virtual_hid.pending_report = std::move(report);
    }
    return submit_pending_hid_report();
  }

  NTSTATUS initialize_vhf() {
    auto &state = driver_state();
    WDF_OBJECT_ATTRIBUTES target_attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&target_attributes);
    target_attributes.ParentObject = state.device;

    WDFIOTARGET target = nullptr;
    auto status = WdfIoTargetCreate(state.device, &target_attributes, &target);
    if (!NT_SUCCESS(status)) {
      return status;
    }

    WDF_IO_TARGET_OPEN_PARAMS open_params;
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_FILE(&open_params, nullptr);
    status = WdfIoTargetOpen(target, &open_params);
    if (!NT_SUCCESS(status)) {
      WdfObjectDelete(target);
      return status;
    }

    const auto descriptor_size = state.create_request.report_sizes.report_descriptor_size;
    state.virtual_hid.report_descriptor.assign(
      state.create_request.report_descriptor.begin(),
      state.create_request.report_descriptor.begin() + descriptor_size
    );
    state.virtual_hid.hardware_ids = lvh::detail::windows::make_hardware_ids(state.create_request);

    VHF_CONFIG config;
    VHF_CONFIG_INIT(
      &config,
      WdfIoTargetWdmGetTargetFileHandle(target),
      static_cast<USHORT>(state.virtual_hid.report_descriptor.size()),
      state.virtual_hid.report_descriptor.data()
    );
    config.VhfClientContext = &state;
    config.VendorID = state.create_request.hardware_ids.vendor_id;
    config.ProductID = state.create_request.hardware_ids.product_id;
    config.VersionNumber = state.create_request.hardware_ids.device_version;
    config.HardwareIDsLength = static_cast<USHORT>(state.virtual_hid.hardware_ids.size() * sizeof(wchar_t));
    config.HardwareIDs = state.virtual_hid.hardware_ids.data();
    config.EvtVhfReadyForNextReadReport = LvhXbox360EvtVhfReadyForNextReadReport;
    config.EvtVhfAsyncOperationGetInputReport = LvhXbox360EvtVhfGetInputReport;

    VHFHANDLE vhf_handle = nullptr;
    status = VhfCreate(&config, &vhf_handle);
    if (!NT_SUCCESS(status)) {
      WdfObjectDelete(target);
      return status;
    }
    status = VhfStart(vhf_handle);
    if (!NT_SUCCESS(status)) {
      VhfDelete(vhf_handle, TRUE);
      WdfObjectDelete(target);
      return status;
    }

    {
      std::lock_guard lock {state.mutex};
      state.virtual_hid.target = target;
      state.virtual_hid.handle = vhf_handle;
    }
    return STATUS_SUCCESS;
  }

  void cleanup_driver_state() {
    auto &state = driver_state();
    WDFQUEUE wait_queue = nullptr;
    WDFQUEUE output_queue = nullptr;
    WDFIOTARGET target = nullptr;
    VHFHANDLE vhf_handle = nullptr;
    {
      std::unique_lock lock {state.mutex};
      state.shutting_down = true;
      wait_queue = state.wait_input_queue;
      output_queue = state.output_queue;
      vhf_handle = state.virtual_hid.handle;
      state.virtual_hid.handle = nullptr;
      state.virtual_hid.ready = false;
      state.virtual_hid.pending_report.reset();
      state.submissions_drained.wait(lock, [&state] {
        return state.virtual_hid.active_submissions == 0U;
      });
      target = state.virtual_hid.target;
      state.virtual_hid.target = nullptr;
    }

    if (wait_queue != nullptr) {
      WdfIoQueuePurgeSynchronously(wait_queue);
    }
    if (output_queue != nullptr) {
      WdfIoQueuePurgeSynchronously(output_queue);
    }
    if (vhf_handle != nullptr) {
      VhfDelete(vhf_handle, TRUE);
    }
    if (target != nullptr) {
      WdfObjectDelete(target);
    }

    std::lock_guard lock {state.mutex};
    state.virtual_hid.in_flight_report.reset();
    state.buffered_output.reset();
  }

  bool request_is_broker(WDFREQUEST request) {
    const auto requestor_process_id = WdfRequestGetRequestorProcessId(request);
    if (requestor_process_id == 0U) {
      return false;
    }

    using UniqueServiceHandle = std::unique_ptr<
      std::remove_pointer_t<SC_HANDLE>,
      decltype(&::CloseServiceHandle)>;
    auto service_manager = UniqueServiceHandle {
      ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT),
      &::CloseServiceHandle,
    };
    if (!service_manager) {
      return false;
    }
    auto service = UniqueServiceHandle {
      ::OpenServiceW(service_manager.get(), L"libvirtualhid_broker", SERVICE_QUERY_STATUS),
      &::CloseServiceHandle,
    };
    if (!service) {
      return false;
    }

    SERVICE_STATUS_PROCESS service_status {};
    DWORD bytes_needed = 0;
    auto service_status_bytes = std::as_writable_bytes(std::span {&service_status, 1U});
    return ::QueryServiceStatusEx(
             service.get(),
             SC_STATUS_PROCESS_INFO,
             std::bit_cast<LPBYTE>(service_status_bytes.data()),
             static_cast<DWORD>(service_status_bytes.size()),
             &bytes_needed
           ) != FALSE &&
           service_status.dwCurrentState == SERVICE_RUNNING &&
           service_status.dwProcessId == requestor_process_id;
  }

  void handle_initialize(WDFREQUEST request) {
    if (!request_is_broker(request)) {
      complete_request(request, STATUS_ACCESS_DENIED);
      return;
    }

    auto *initialize = static_cast<LvhWindowsXbox360InitializeRequest *>(nullptr);
    const auto status = retrieve_input(request, initialize);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }
    const auto &device = initialize->device;
    if (!valid_private_header(initialize->version, initialize->size, sizeof(*initialize)) || device.version != LVH_WINDOWS_CONTROL_PROTOCOL_VERSION || device.size != sizeof(device) || device.device_type != LVH_WINDOWS_DEVICE_GAMEPAD || device.gamepad_kind != LVH_WINDOWS_GAMEPAD_XBOX_360 || initialize->driver_device_id == 0U || device.report_sizes.report_descriptor_size == 0U || device.report_sizes.report_descriptor_size > LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE || device.report_sizes.input_report_size < 9U || device.report_sizes.input_report_size > LVH_WINDOWS_MAX_INPUT_REPORT_SIZE) {
      complete_request(request, STATUS_INVALID_PARAMETER);
      return;
    }

    {
      auto &state = driver_state();
      std::lock_guard lock {state.mutex};
      if (state.initialized) {
        complete_request(request, STATUS_DEVICE_BUSY);
        return;
      }
      state.driver_device_id = initialize->driver_device_id;
      state.session_token = initialize->session_token;
      state.create_request = initialize->device;
    }

    const auto vhf_status = initialize_vhf();
    if (!NT_SUCCESS(vhf_status)) {
      complete_request(request, vhf_status);
      return;
    }

    auto initial_report = std::vector<std::uint8_t> {};
    {
      auto &state = driver_state();
      std::lock_guard lock {state.mutex};
      state.initialized = true;
      initial_report = make_hid_report(state.create_request, state.input);
    }
    static_cast<void>(queue_hid_report(std::move(initial_report)));
    complete_request(request, STATUS_SUCCESS);
  }

  bool same_input(
    const LvhWindowsXbox360InputState &left,
    const LvhWindowsXbox360InputState &right
  ) {
    return std::memcmp(&left, &right, sizeof(left)) == 0;
  }

  void handle_submit_input(WDFREQUEST request) {
    auto *submit = static_cast<LvhWindowsXbox360SubmitInputRequest *>(nullptr);
    const auto status = retrieve_input(request, submit);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }
    if (!valid_private_header(submit->version, submit->size, sizeof(*submit)) || !initialized_request(submit->driver_device_id, submit->session_token)) {
      complete_request(request, STATUS_ACCESS_DENIED);
      return;
    }

    LvhWindowsCreateDeviceRequest create_request {};
    bool changed = false;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.mutex};
      changed = !same_input(state.input, submit->state);
      if (changed) {
        state.input = submit->state;
        ++state.packet_number;
      }
      create_request = state.create_request;
    }
    if (changed) {
      const auto hid_status = queue_hid_report(make_hid_report(create_request, submit->state));
      if (!NT_SUCCESS(hid_status)) {
        complete_request(request, hid_status);
        return;
      }
      complete_one_wait_for_input();
    }
    complete_request(request, STATUS_SUCCESS);
  }

  void handle_read_output(WDFREQUEST request) {
    auto *read = static_cast<LvhWindowsXbox360ReadOutputRequest *>(nullptr);
    const auto status = retrieve_input(request, read);
    if (!NT_SUCCESS(status)) {
      complete_request(request, status);
      return;
    }
    if (!valid_private_header(read->version, read->size, sizeof(*read)) || !initialized_request(read->driver_device_id, read->session_token)) {
      complete_request(request, STATUS_ACCESS_DENIED);
      return;
    }

    auto &state = driver_state();
    std::optional<LvhWindowsOutputReportEvent> event;
    WDFQUEUE queue = nullptr;
    {
      std::lock_guard lock {state.mutex};
      event = std::move(state.buffered_output);
      state.buffered_output.reset();
      queue = state.output_queue;
    }
    if (event.has_value()) {
      static_cast<void>(copy_event_to_request(request, *event));
      return;
    }

    const auto forward_status = WdfRequestForwardToIoQueue(request, queue);
    if (!NT_SUCCESS(forward_status)) {
      complete_request(request, forward_status);
    }
  }

  void queue_rumble_output(std::uint8_t low_frequency, std::uint8_t high_frequency) {
    auto &state = driver_state();
    LvhWindowsOutputReportEvent event {};
    WDFQUEUE queue = nullptr;
    {
      std::lock_guard lock {state.mutex};
      if (!state.initialized || state.shutting_down || (state.low_frequency_rumble == low_frequency && state.high_frequency_rumble == high_frequency)) {
        return;
      }
      state.low_frequency_rumble = low_frequency;
      state.high_frequency_rumble = high_frequency;
      event.version = LVH_WINDOWS_CONTROL_PROTOCOL_VERSION;
      event.size = sizeof(event);
      event.driver_device_id = state.driver_device_id;
      event.report_size = 5U;
      event.report[0] = state.create_request.hardware_ids.report_id;
      const auto low = static_cast<std::uint16_t>(low_frequency) * 257U;
      const auto high = static_cast<std::uint16_t>(high_frequency) * 257U;
      event.report[1] = static_cast<std::uint8_t>(low & 0xFFU);
      event.report[2] = static_cast<std::uint8_t>(low >> 8U);
      event.report[3] = static_cast<std::uint8_t>(high & 0xFFU);
      event.report[4] = static_cast<std::uint8_t>(high >> 8U);
      queue = state.output_queue;
    }

    WDFREQUEST request = nullptr;
    if (queue != nullptr && NT_SUCCESS(WdfIoQueueRetrieveNextRequest(queue, &request))) {
      static_cast<void>(copy_event_to_request(request, event));
      return;
    }
    std::lock_guard lock {state.mutex};
    state.buffered_output = event;
  }

  void handle_xusb_information(WDFREQUEST request) {
    std::array<std::uint8_t, 12> information {};
    write_little_endian<std::uint16_t>(information, 0U, xbox_version);
    information[2] = 0x01U;
    std::uint16_t vendor_id = 0x045EU;
    std::uint16_t product_id = 0x028EU;
    {
      auto &state = driver_state();
      std::lock_guard lock {state.mutex};
      if (state.initialized) {
        vendor_id = state.create_request.hardware_ids.vendor_id;
        product_id = state.create_request.hardware_ids.product_id;
      }
    }
    write_little_endian<std::uint16_t>(information, 8U, vendor_id);
    write_little_endian<std::uint16_t>(information, 10U, product_id);
    static_cast<void>(copy_to_request(request, information));
  }

  void handle_xusb_capabilities(WDFREQUEST request, size_t output_buffer_length) {
    constexpr std::array<std::uint8_t, 24> capabilities_v1 {
      0x03U,
      0x01U,
      0x00U,
      0x01U,
      0xFFU,
      0xF7U,
      0xFFU,
      0xFFU,
      0xC0U,
      0xFFU,
      0xC0U,
      0xFFU,
      0xC0U,
      0xFFU,
      0xC0U,
      0xFFU,
      0xFFU,
      0xFFU,
      0xFFU,
      0xFFU,
      0x00U,
      0x00U,
      0xFFU,
      0xFFU,
    };
    if (output_buffer_length < 36U) {
      static_cast<void>(copy_to_request(request, capabilities_v1));
      return;
    }

    std::array<std::uint8_t, 36> capabilities_v2 {
      0x03U,
      0x01U,
      0x01U,
      0x01U,
      0x0CU,
      0x00U,
      0x5EU,
      0x04U,
      0x8EU,
      0x02U,
      0x10U,
      0x01U,
      0x00U,
      0xFAU,
      0x34U,
      0x22U,
      0xFFU,
      0xF7U,
      0xFFU,
      0xFFU,
      0xC0U,
      0xFFU,
      0xC0U,
      0xFFU,
      0xC0U,
      0xFFU,
      0xC0U,
      0xFFU,
      0xFFU,
      0xFFU,
      0xFFU,
      0xFFU,
      0x00U,
      0x00U,
      0xFFU,
      0xFFU,
    };
    {
      auto &state = driver_state();
      std::lock_guard lock {state.mutex};
      if (state.initialized) {
        capabilities_v2[6] = static_cast<std::uint8_t>(state.create_request.hardware_ids.vendor_id & 0xFFU);
        capabilities_v2[7] = static_cast<std::uint8_t>(state.create_request.hardware_ids.vendor_id >> 8U);
        capabilities_v2[8] = static_cast<std::uint8_t>(state.create_request.hardware_ids.product_id & 0xFFU);
        capabilities_v2[9] = static_cast<std::uint8_t>(state.create_request.hardware_ids.product_id >> 8U);
      }
    }
    static_cast<void>(copy_to_request(request, capabilities_v2));
  }

  void handle_xusb_set_state(WDFREQUEST request) {
    void *buffer = nullptr;
    size_t size = 0;
    if (NT_SUCCESS(WdfRequestRetrieveInputBuffer(request, 4U, &buffer, &size)) && size >= 4U) {
      const auto bytes = static_cast<const std::uint8_t *>(buffer);
      if (size >= 5U) {
        if (bytes[4] == 0x02U) {
          queue_rumble_output(bytes[2], bytes[3]);
        }
      } else {
        queue_rumble_output(bytes[1], bytes[3]);
      }
    }
    complete_request(request, STATUS_SUCCESS);
  }

}  // namespace

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver_object, PUNICODE_STRING registry_path) {
  WDF_DRIVER_CONFIG config;
  WDF_DRIVER_CONFIG_INIT(&config, LvhXbox360EvtDeviceAdd);
  return WdfDriverCreate(driver_object, registry_path, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS LvhXbox360EvtDeviceAdd(WDFDRIVER driver, PWDFDEVICE_INIT device_init) {
  UNREFERENCED_PARAMETER(driver);

  WDF_FILEOBJECT_CONFIG file_config;
  WDF_FILEOBJECT_CONFIG_INIT(
    &file_config,
    WDF_NO_EVENT_CALLBACK,
    WDF_NO_EVENT_CALLBACK,
    LvhXbox360EvtFileCleanup
  );
  WdfDeviceInitSetFileObjectConfig(device_init, &file_config, WDF_NO_OBJECT_ATTRIBUTES);

  WDF_OBJECT_ATTRIBUTES device_attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&device_attributes);
  device_attributes.EvtCleanupCallback = LvhXbox360EvtDeviceCleanup;
  WDFDEVICE device = nullptr;
  auto status = WdfDeviceCreate(&device_init, &device_attributes, &device);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  auto &state = driver_state();
  state.device = device;

  status = WdfDeviceCreateDeviceInterface(device, &LVH_WINDOWS_XUSB_INTERFACE_GUID, nullptr);
  if (!NT_SUCCESS(status)) {
    return status;
  }
  WDF_IO_QUEUE_CONFIG default_queue_config;
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&default_queue_config, WdfIoQueueDispatchParallel);
  default_queue_config.EvtIoDeviceControl = LvhXbox360EvtIoDeviceControl;
  status = WdfIoQueueCreate(device, &default_queue_config, WDF_NO_OBJECT_ATTRIBUTES, WDF_NO_HANDLE);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_IO_QUEUE_CONFIG manual_queue_config;
  WDF_IO_QUEUE_CONFIG_INIT(&manual_queue_config, WdfIoQueueDispatchManual);
  status = WdfIoQueueCreate(device, &manual_queue_config, WDF_NO_OBJECT_ATTRIBUTES, &state.wait_input_queue);
  if (!NT_SUCCESS(status)) {
    return status;
  }
  status = WdfIoQueueCreate(device, &manual_queue_config, WDF_NO_OBJECT_ATTRIBUTES, &state.output_queue);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  WDF_TIMER_CONFIG timer_config;
  WDF_TIMER_CONFIG_INIT_PERIODIC(&timer_config, LvhXbox360EvtInputPump, xbox_input_pump_period_ms);
  WDF_OBJECT_ATTRIBUTES timer_attributes;
  WDF_OBJECT_ATTRIBUTES_INIT(&timer_attributes);
  timer_attributes.ParentObject = device;
  status = WdfTimerCreate(&timer_config, &timer_attributes, &state.input_pump);
  if (!NT_SUCCESS(status)) {
    return status;
  }
  WdfTimerStart(state.input_pump, WDF_REL_TIMEOUT_IN_MS(xbox_input_pump_period_ms));
  return STATUS_SUCCESS;
}

void LvhXbox360EvtDeviceCleanup(WDFOBJECT device_object) {
  UNREFERENCED_PARAMETER(device_object);
  cleanup_driver_state();
}

void LvhXbox360EvtFileCleanup(WDFFILEOBJECT file_object) {
  UNREFERENCED_PARAMETER(file_object);
}

void LvhXbox360EvtInputPump(WDFTIMER timer) {
  UNREFERENCED_PARAMETER(timer);
  complete_one_wait_for_input();
}

void LvhXbox360EvtVhfReadyForNextReadReport(VhfContext vhf_client_context) {
  auto *state = static_cast<Xbox360DriverState *>(vhf_client_context);
  if (state == nullptr) {
    return;
  }
  {
    std::lock_guard lock {state->mutex};
    if (state->shutting_down || state->virtual_hid.handle == nullptr) {
      return;
    }
    state->virtual_hid.in_flight_report.reset();
    state->virtual_hid.ready = true;
  }
  static_cast<void>(submit_pending_hid_report());
}

void LvhXbox360EvtVhfGetInputReport(
  VhfContext vhf_client_context,
  VHFOPERATIONHANDLE operation_handle,
  VhfContext operation_context,
  PHID_XFER_PACKET packet
) {
  UNREFERENCED_PARAMETER(operation_context);
  auto *state = static_cast<Xbox360DriverState *>(vhf_client_context);
  if (state == nullptr || packet == nullptr || packet->reportBuffer == nullptr) {
    static_cast<void>(VhfAsyncOperationComplete(operation_handle, STATUS_INVALID_PARAMETER));
    return;
  }

  std::vector<std::uint8_t> report;
  {
    std::lock_guard lock {state->mutex};
    report = state->virtual_hid.latest_report;
  }
  const auto leading_report_id =
    packet->reportId != 0U && packet->reportBufferLen > report.size() ? 1U : 0U;
  if (report.empty() || packet->reportBufferLen < report.size() + leading_report_id) {
    static_cast<void>(VhfAsyncOperationComplete(operation_handle, STATUS_BUFFER_TOO_SMALL));
    return;
  }
  std::fill_n(packet->reportBuffer, packet->reportBufferLen, UCHAR {});
  std::copy(report.begin(), report.end(), packet->reportBuffer + leading_report_id);
  static_cast<void>(VhfAsyncOperationComplete(operation_handle, STATUS_SUCCESS));
}

void LvhXbox360EvtIoDeviceControl(
  WDFQUEUE queue,
  WDFREQUEST request,
  size_t output_buffer_length,
  size_t input_buffer_length,
  ULONG io_control_code
) {
  UNREFERENCED_PARAMETER(queue);
  UNREFERENCED_PARAMETER(input_buffer_length);

  switch (io_control_code) {
    case LVH_WINDOWS_IOCTL_XBOX360_INITIALIZE:
      handle_initialize(request);
      return;
    case LVH_WINDOWS_IOCTL_XBOX360_SUBMIT_INPUT:
      handle_submit_input(request);
      return;
    case LVH_WINDOWS_IOCTL_XBOX360_READ_OUTPUT:
      handle_read_output(request);
      return;
    case LVH_WINDOWS_IOCTL_XUSB_GET_INFORMATION:
      handle_xusb_information(request);
      return;
    case LVH_WINDOWS_IOCTL_XUSB_GET_CAPABILITIES:
      handle_xusb_capabilities(request, output_buffer_length);
      return;
    case LVH_WINDOWS_IOCTL_XUSB_GET_STATE:
      {
        const auto state = make_xusb_state(false);
        static_cast<void>(copy_to_request(request, state));
        return;
      }
    case LVH_WINDOWS_IOCTL_XUSB_SET_STATE:
      handle_xusb_set_state(request);
      return;
    case LVH_WINDOWS_IOCTL_XUSB_GET_LED_STATE:
      {
        constexpr std::array<std::uint8_t, 3> led {0U, 0U, 0x06U};
        static_cast<void>(copy_to_request(request, led));
        return;
      }
    case LVH_WINDOWS_IOCTL_XUSB_GET_BATTERY_INFO:
      {
        constexpr std::array<std::uint8_t, 4> battery {0U, 0x01U, 0x03U, 0U};
        static_cast<void>(copy_to_request(request, battery));
        return;
      }
    case LVH_WINDOWS_IOCTL_XUSB_WAIT_FOR_INPUT:
      {
        auto &state = driver_state();
        const auto status = WdfRequestForwardToIoQueue(request, state.wait_input_queue);
        if (!NT_SUCCESS(status)) {
          complete_request(request, status);
        }
        return;
      }
    case LVH_WINDOWS_IOCTL_XUSB_GET_INFORMATION_EX:
      {
        std::array<std::uint8_t, 64> information {};
        write_little_endian<std::uint16_t>(information, 0U, xbox_version);
        information[2] = 0x01U;
        information[3] = 0x01U;
        std::uint16_t vendor_id = 0x045EU;
        std::uint16_t product_id = 0x028EU;
        {
          auto &state = driver_state();
          std::lock_guard lock {state.mutex};
          if (state.initialized) {
            vendor_id = state.create_request.hardware_ids.vendor_id;
            product_id = state.create_request.hardware_ids.product_id;
          }
        }
        write_little_endian<std::uint16_t>(information, 8U, vendor_id);
        write_little_endian<std::uint16_t>(information, 10U, product_id);
        const auto size = std::min(output_buffer_length, information.size());
        static_cast<void>(copy_to_request(request, std::span {information}.first(size)));
        return;
      }
    case LVH_WINDOWS_IOCTL_XUSB_POWER_DOWN:
    case LVH_WINDOWS_IOCTL_XUSB_POWER_INFO:
      complete_request(request, STATUS_SUCCESS);
      return;
    case LVH_WINDOWS_IOCTL_XUSB_WAIT_GUIDE:
    case LVH_WINDOWS_IOCTL_XUSB_WAIT_FOR_SYSTEM_BUTTONS:
    case LVH_WINDOWS_IOCTL_XUSB_GET_AUDIO_INFO:
    default:
      complete_request(request, STATUS_INVALID_DEVICE_REQUEST);
      return;
  }
}
