/**
 * @file tests/fixtures/include/fixtures/linux_backend_test_hooks.hpp
 * @brief Test-only hooks for Linux UHID backend internals.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail::test {

  /**
   * @brief Linux input event captured by a pipe-backed backend test.
   */
  struct LinuxInputEventRecord {
    /**
     * @brief Event type.
     */
    std::uint16_t type = 0;

    /**
     * @brief Event code.
     */
    std::uint16_t code = 0;

    /**
     * @brief Event value.
     */
    std::int32_t value = 0;
  };

  /**
   * @brief Result from a pipe-backed uinput submission.
   */
  struct LinuxInputSubmissionResult {
    /**
     * @brief Submit operation status.
     */
    OperationStatus status;

    /**
     * @brief Events written to the pipe.
     */
    std::vector<LinuxInputEventRecord> events;
  };

  /**
   * @brief Result from a fake uinput Xbox force-feedback exchange.
   */
  struct LinuxUinputRumbleResult {
    /**
     * @brief Create operation status.
     */
    OperationStatus create_status;

    /**
     * @brief Close operation status.
     */
    OperationStatus close_status;

    /**
     * @brief Number of normalized rumble callbacks received.
     */
    std::size_t callback_count = 0;

    /**
     * @brief Last normalized rumble callback.
     */
    GamepadOutput last_output;
  };

  /**
   * @brief Result from a socketpair-backed UHID lifecycle test.
   */
  struct LinuxUhidRoundTripResult {
    /**
     * @brief Create operation status.
     */
    OperationStatus create_status;

    /**
     * @brief Submit operation status.
     */
    OperationStatus submit_status;

    /**
     * @brief Close operation status.
     */
    OperationStatus close_status;

    /**
     * @brief Whether the peer observed a create event.
     */
    bool saw_create = false;

    /**
     * @brief Whether the peer observed an input report event.
     */
    bool saw_input = false;

    /**
     * @brief Whether the peer observed a get-report reply.
     */
    bool saw_get_report_reply = false;

    /**
     * @brief Whether the peer observed a DualSense calibration reply.
     */
    bool saw_dualsense_calibration = false;

    /**
     * @brief Whether the peer observed a DualShock 4 calibration reply.
     */
    bool saw_dualshock4_calibration = false;

    /**
     * @brief Whether the peer observed a DualSense pairing reply.
     */
    bool saw_dualsense_pairing = false;

    /**
     * @brief Whether the peer observed a DualShock 4 pairing reply.
     */
    bool saw_dualshock4_pairing = false;

    /**
     * @brief Whether the peer observed a DualSense firmware reply.
     */
    bool saw_dualsense_firmware = false;

    /**
     * @brief Whether the peer observed a DualShock 4 firmware reply.
     */
    bool saw_dualshock4_firmware = false;

    /**
     * @brief Whether the peer observed a signed Bluetooth DualSense feature reply.
     */
    bool saw_dualsense_feature_crc = false;

    /**
     * @brief Whether the peer observed a signed Bluetooth DualShock 4 feature reply.
     */
    bool saw_dualshock4_feature_crc = false;

    /**
     * @brief Whether the peer observed a Bluetooth-framed DualSense input report.
     */
    bool saw_dualsense_bluetooth_input = false;

    /**
     * @brief Whether the peer observed a Bluetooth-framed DualShock 4 input report.
     */
    bool saw_dualshock4_bluetooth_input = false;

    /**
     * @brief Whether the peer observed a set-report reply.
     */
    bool saw_set_report_reply = false;

    /**
     * @brief Whether the peer observed a destroy event.
     */
    bool saw_destroy = false;

    /**
     * @brief Number of output callbacks received.
     */
    std::size_t output_callback_count = 0;

    /**
     * @brief Last output callback payload.
     */
    GamepadOutput last_output;
  };

  /**
   * @brief Result from creating each Linux backend device through fake syscalls.
   */
  struct LinuxBackendFakeCreationResult {
    /**
     * @brief Backend capabilities reported while fake device nodes are accessible.
     */
    BackendCapabilities capabilities;

    /**
     * @brief Gamepad creation status.
     */
    OperationStatus gamepad_status;

    /**
     * @brief Gamepad close status.
     */
    OperationStatus gamepad_close_status;

    /**
     * @brief Keyboard creation status.
     */
    OperationStatus keyboard_status;

    /**
     * @brief Keyboard close status.
     */
    OperationStatus keyboard_close_status;

    /**
     * @brief Mouse creation status.
     */
    OperationStatus mouse_status;

    /**
     * @brief Mouse close status.
     */
    OperationStatus mouse_close_status;

    /**
     * @brief Touchscreen creation status.
     */
    OperationStatus touchscreen_status;

    /**
     * @brief Touchscreen close status.
     */
    OperationStatus touchscreen_close_status;

    /**
     * @brief Trackpad creation status.
     */
    OperationStatus trackpad_status;

    /**
     * @brief Trackpad close status.
     */
    OperationStatus trackpad_close_status;

    /**
     * @brief Pen tablet creation status.
     */
    OperationStatus pen_tablet_status;

    /**
     * @brief Pen tablet close status.
     */
    OperationStatus pen_tablet_close_status;
  };

  /**
   * @brief Event code recorded from a fake libevdev device definition.
   */
  struct LinuxLibevdevEventCode {
    /**
     * @brief Linux input event type.
     */
    std::uint32_t type = 0;

    /**
     * @brief Linux input event code.
     */
    std::uint32_t code = 0;

    /**
     * @brief Whether absolute axis metadata was supplied.
     */
    bool has_absinfo = false;

    /**
     * @brief Absolute axis minimum.
     */
    std::int32_t minimum = 0;

    /**
     * @brief Absolute axis maximum.
     */
    std::int32_t maximum = 0;

    /**
     * @brief Absolute axis fuzz.
     */
    std::int32_t fuzz = 0;

    /**
     * @brief Absolute axis flat.
     */
    std::int32_t flat = 0;

    /**
     * @brief Absolute axis resolution.
     */
    std::int32_t resolution = 0;
  };

  /**
   * @brief Result from a fake libevdev uinput construction.
   */
  struct LinuxLibevdevCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Close status when creation succeeded.
     */
    OperationStatus close_status;

    /**
     * @brief Configured device name.
     */
    std::string name;

    /**
     * @brief Configured bus type.
     */
    std::uint16_t bustype = 0;

    /**
     * @brief Configured vendor id.
     */
    std::uint16_t vendor = 0;

    /**
     * @brief Configured product id.
     */
    std::uint16_t product = 0;

    /**
     * @brief Configured version.
     */
    std::uint16_t version = 0;

    /**
     * @brief Enabled Linux input event types.
     */
    std::vector<std::uint32_t> event_types;

    /**
     * @brief Enabled Linux input event codes.
     */
    std::vector<LinuxLibevdevEventCode> event_codes;

    /**
     * @brief Enabled Linux input properties.
     */
    std::vector<std::uint32_t> properties;

    /**
     * @brief Number of fake libevdev uinput handles destroyed.
     */
    std::size_t destroy_count = 0;
  };

  /**
   * @brief Copy into a fixed-size Linux char buffer using the backend string helper.
   *
   * @param source Source string.
   * @return Copied, null-terminated string.
   */
  std::string linux_copy_string_char_buffer(const std::string &source);

  /**
   * @brief Translate a portable keyboard key code to a Linux input key code.
   *
   * @param key_code Portable keyboard key code.
   * @return Linux key code, or `-1` when unsupported.
   */
  int linux_key_code(KeyboardKeyCode key_code);

  /**
   * @brief Translate a mouse button to a Linux input button code.
   *
   * @param button Mouse button.
   * @return Linux button code.
   */
  int linux_mouse_button(MouseButton button);

  /**
   * @brief Translate a bus type to a Linux UHID bus code.
   *
   * @param bus_type Bus type.
   * @return Linux UHID bus code.
   */
  std::uint16_t linux_uhid_bus(BusType bus_type);

  /**
   * @brief Select the Linux UHID bus code for a built-in gamepad profile.
   *
   * @param kind Built-in gamepad profile kind.
   * @return Linux UHID bus code.
   */
  std::uint16_t linux_gamepad_uhid_bus(GamepadProfileKind kind);

  /**
   * @brief Translate a bus type to a Linux uinput bus code.
   *
   * @param bus_type Bus type.
   * @return Linux uinput bus code.
   */
  std::uint16_t linux_uinput_bus(BusType bus_type);

  /**
   * @brief Scale an absolute pointer coordinate into the Linux absolute axis range.
   *
   * @param value Coordinate value.
   * @param limit Coordinate space limit.
   * @return Linux absolute axis value.
   */
  int linux_absolute_axis(std::int32_t value, std::int32_t limit);

  /**
   * @brief Decode UTF-8 into Unicode code points using the Linux backend decoder.
   *
   * @param text UTF-8 text.
   * @return Decoded code points.
   */
  std::vector<std::uint32_t> linux_decode_utf8(const std::string &text);

  /**
   * @brief Format a code point as upper-case hexadecimal.
   *
   * @param codepoint Unicode code point.
   * @return Upper-case hexadecimal representation.
   */
  std::string linux_uppercase_hex(std::uint32_t codepoint);

  /**
   * @brief Convert a hexadecimal digit into the portable keyboard key code used by text input.
   *
   * @param digit Upper-case hexadecimal digit.
   * @return Portable keyboard key code.
   */
  KeyboardKeyCode linux_hex_digit_key_code(char digit);

  /**
   * @brief Convert a high-resolution scroll distance into legacy wheel steps.
   *
   * @param distance High-resolution scroll distance.
   * @return Legacy wheel steps.
   */
  int linux_legacy_scroll_steps(std::int32_t distance);

  /**
   * @brief Translate a pen tool to a Linux input tool code.
   *
   * @param tool Pen tool.
   * @return Linux tool code, or `-1` when unchanged or unsupported.
   */
  int linux_pen_tool(PenToolType tool);

  /**
   * @brief Translate a pen button to a Linux input button code.
   *
   * @param button Pen button.
   * @return Linux button code.
   */
  int linux_pen_button(PenButton button);

  /**
   * @brief Format a parsed or generated Linux DualSense MAC address.
   *
   * @param stable_id Optional stable id to parse as a MAC address.
   * @param id Device id used when the stable id is not a valid MAC address.
   * @return Formatted MAC address.
   */
  std::string linux_dualsense_mac_address(const std::string &stable_id, DeviceId id);

  /**
   * @brief Check whether the first line in a file matches an expected value.
   *
   * @param path File path to read.
   * @param expected Expected first line.
   * @return `true` when the first line exists and matches.
   */
  bool linux_first_line_matches(std::string_view path, std::string_view expected);

  /**
   * @brief Check whether reading the first line of a file fails.
   *
   * @param path File path to read.
   * @return `true` when no first line could be read.
   */
  bool linux_first_line_missing(std::string_view path);

  /**
   * @brief Check whether a hidraw uevent file advertises the expected HID name.
   *
   * @param path Uevent file path.
   * @param name Expected HID name.
   * @return `true` when the HID name matches.
   */
  bool linux_hidraw_name_matches(std::string_view path, std::string_view name);

  /**
   * @brief Discover Linux input nodes for a device name.
   *
   * @param name Device name to discover.
   * @return Matching device nodes.
   */
  std::vector<DeviceNode> linux_discover_nodes_by_name(const std::string &name);

  /**
   * @brief Discover Linux input nodes for a device name with explicit sysfs roots.
   *
   * @param name Device name to discover.
   * @param input_root Test input sysfs root.
   * @param hidraw_root Test hidraw sysfs root.
   * @return Matching device nodes.
   */
  std::vector<DeviceNode> linux_discover_nodes_by_name(
    const std::string &name,
    const std::string &input_root,
    const std::string &hidraw_root
  );

  /**
   * @brief Call device-node discovery wrappers on unopened backend devices.
   *
   * @return Total node count.
   */
  std::size_t linux_empty_device_nodes_count();

  /**
   * @brief Get the maximum UHID report descriptor size accepted by the backend.
   *
   * @return Maximum descriptor size.
   */
  std::size_t linux_uhid_descriptor_limit();

  /**
   * @brief Get the maximum UHID input report size accepted by the backend.
   *
   * @return Maximum input report size.
   */
  std::size_t linux_uhid_input_limit();

  /**
   * @brief Try creating a UHID gamepad on an invalid file descriptor.
   *
   * @param descriptor_size Descriptor size to use.
   * @return Creation status.
   */
  OperationStatus linux_uhid_create_with_descriptor_size(std::size_t descriptor_size);

  /**
   * @brief Try submitting a UHID input report on an invalid file descriptor.
   *
   * @param report_size Input report size to use.
   * @return Submit status.
   */
  OperationStatus linux_uhid_submit_report_size(std::size_t report_size);

  /**
   * @brief Try submitting a UHID input report after closing the backend device.
   *
   * @return Submit status.
   */
  OperationStatus linux_uhid_submit_after_close();

  /**
   * @brief Try creating a uinput keyboard on an invalid file descriptor.
   *
   * @return Creation status.
   */
  OperationStatus linux_uinput_keyboard_create_invalid_fd();

  /**
   * @brief Submit a keyboard event to a uinput keyboard on an invalid file descriptor.
   *
   * @param event Keyboard event.
   * @return Submit status.
   */
  OperationStatus linux_uinput_keyboard_submit_invalid_fd(const KeyboardEvent &event);

  /**
   * @brief Submit text to a uinput keyboard on an invalid file descriptor.
   *
   * @param text UTF-8 text.
   * @return Submit status.
   */
  OperationStatus linux_uinput_keyboard_type_text_invalid_fd(const std::string &text);

  /**
   * @brief Try submitting a keyboard event after closing the backend device.
   *
   * @return Submit status.
   */
  OperationStatus linux_uinput_keyboard_submit_after_close();

  /**
   * @brief Submit a keyboard event to a pipe-backed uinput keyboard.
   *
   * @param event Keyboard event.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_keyboard_submit_pipe(const KeyboardEvent &event);

  /**
   * @brief Submit normalized state through a pipe-backed uinput gamepad.
   *
   * @param kind Gamepad profile kind.
   * @param state Gamepad state to submit.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_gamepad_submit_pipe(GamepadProfileKind kind, const GamepadState &state);

  /**
   * @brief Exercise uinput gamepad force-feedback upload and playback.
   *
   * @param kind Gamepad profile kind.
   * @param effect_type Linux force-feedback effect type.
   * @return Creation, callback, and close results.
   */
  LinuxUinputRumbleResult linux_uinput_gamepad_fake_rumble(
    GamepadProfileKind kind,
    std::uint16_t effect_type
  );

  /**
   * @brief Try creating a libevdev uinput mouse on an invalid file descriptor.
   *
   * @return Creation status.
   */
  OperationStatus linux_uinput_user_device_invalid_fd();

  /**
   * @brief Try creating a libevdev uinput mouse on a pipe.
   *
   * @return Creation status.
   */
  OperationStatus linux_uinput_user_device_pipe();

  /**
   * @brief Try creating a uinput mouse on an invalid file descriptor.
   *
   * @return Creation status.
   */
  OperationStatus linux_uinput_mouse_create_invalid_fd();

  /**
   * @brief Submit a mouse event to a uinput mouse on an invalid file descriptor.
   *
   * @param event Mouse event.
   * @return Submit status.
   */
  OperationStatus linux_uinput_mouse_submit_invalid_fd(const MouseEvent &event);

  /**
   * @brief Try submitting a mouse event after closing the backend device.
   *
   * @return Submit status.
   */
  OperationStatus linux_uinput_mouse_submit_after_close();

  /**
   * @brief Submit a mouse event to a pipe-backed uinput mouse.
   *
   * @param event Mouse event.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_mouse_submit_pipe(const MouseEvent &event);

  /**
   * @brief Place and release a contact through a pipe-backed uinput touchscreen.
   *
   * @param contact Touch contact to place.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_touchscreen_contact_pipe(const TouchContact &contact);

  /**
   * @brief Place, click, and release a contact through a pipe-backed uinput trackpad.
   *
   * @param contact Touch contact to place.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_trackpad_contact_pipe(const TouchContact &contact);

  /**
   * @brief Submit a tool and button through a pipe-backed uinput pen tablet.
   *
   * @param state Pen tool state to place.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_pen_tablet_tool_pipe(const PenToolState &state);

  /**
   * @brief Submit multiple contacts through a pipe-backed uinput trackpad.
   *
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_trackpad_multi_contact_pipe();

  /**
   * @brief Submit invalid touchscreen contacts through pipe-backed devices.
   *
   * @return Final invalid-contact status.
   */
  OperationStatus linux_uinput_touchscreen_invalid_contacts();

  /**
   * @brief Submit pen tablet tool transitions through a pipe-backed device.
   *
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_pen_tablet_transition_pipe();

  /**
   * @brief Submit pen tablet events after close.
   *
   * @return Final closed-device status.
   */
  OperationStatus linux_uinput_pen_tablet_closed_status();

  /**
   * @brief Exercise a UHID gamepad lifecycle over a socketpair.
   *
   * @return Round-trip result.
   */
  LinuxUhidRoundTripResult linux_uhid_socketpair_roundtrip();

  /**
   * @brief Exercise DualSense UHID feature-report replies over a socketpair.
   *
   * @return Round-trip result with feature-report observations.
   */
  LinuxUhidRoundTripResult linux_dualsense_uhid_socketpair_reports();

  /**
   * @brief Exercise Bluetooth DualSense UHID framing and signed feature replies over a socketpair.
   *
   * @return Round-trip result with Bluetooth framing observations.
   */
  LinuxUhidRoundTripResult linux_dualsense_bluetooth_uhid_socketpair_reports();

  /**
   * @brief Exercise DualShock 4 UHID feature-report replies over a socketpair.
   *
   * @return Round-trip result with feature-report observations.
   */
  LinuxUhidRoundTripResult linux_dualshock4_uhid_socketpair_reports();

  /**
   * @brief Exercise Bluetooth DualShock 4 UHID framing and signed feature replies over a socketpair.
   *
   * @return Round-trip result with Bluetooth framing observations.
   */
  LinuxUhidRoundTripResult linux_dualshock4_bluetooth_uhid_socketpair_reports();

  /**
   * @brief Create all Linux backend device types using fake successful syscalls.
   *
   * @return Creation and close statuses for each backend device.
   */
  LinuxBackendFakeCreationResult linux_backend_create_all_fake_success();

  /**
   * @brief Get Linux backend capabilities while fake device-node access fails.
   *
   * @return Backend capabilities.
   */
  BackendCapabilities linux_backend_fake_unavailable_capabilities();

  /**
   * @brief Try creating a Linux backend gamepad while fake open fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_gamepad_fake_open_failure();

  /**
   * @brief Try creating a Linux backend gamepad while fake UHID creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_gamepad_fake_create_failure();

  /**
   * @brief Try creating a Linux backend keyboard while fake open fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_keyboard_fake_open_failure();

  /**
   * @brief Try creating a Linux backend keyboard while fake uinput creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_keyboard_fake_create_failure();

  /**
   * @brief Create a Linux backend keyboard through a fake successful fallback after uinput creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_keyboard_fake_fallback_success();

  /**
   * @brief Try creating a Linux backend mouse while fake open fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_mouse_fake_open_failure();

  /**
   * @brief Try creating a Linux backend mouse while fake uinput creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_mouse_fake_create_failure();

  /**
   * @brief Create a Linux backend mouse through a fake successful fallback after uinput creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_mouse_fake_fallback_success();

  /**
   * @brief Try creating a Linux backend keyboard while uinput and XTest fallback both fail.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_keyboard_fake_create_failure_without_fallback();

  /**
   * @brief Try creating a Linux backend mouse while uinput and XTest fallback both fail.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_mouse_fake_create_failure_without_fallback();

  /**
   * @brief Try creating a Linux backend touchscreen while fake open fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_touchscreen_fake_open_failure();

  /**
   * @brief Try creating a Linux backend touchscreen while fake uinput creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_touchscreen_fake_create_failure();

  /**
   * @brief Try creating a Linux backend trackpad while fake open fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_trackpad_fake_open_failure();

  /**
   * @brief Try creating a Linux backend trackpad while fake uinput creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_trackpad_fake_create_failure();

  /**
   * @brief Try creating a Linux backend pen tablet while fake open fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_pen_tablet_fake_open_failure();

  /**
   * @brief Try creating a Linux backend pen tablet while fake uinput creation fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_backend_pen_tablet_fake_create_failure();

  /**
   * @brief Try submitting a UHID input report while fake write fails.
   *
   * @return Submit status.
   */
  OperationStatus linux_uhid_submit_fake_write_failure();

  /**
   * @brief Try submitting a UHID input report while fake write is short.
   *
   * @return Submit status.
   */
  OperationStatus linux_uhid_submit_fake_short_write();

  /**
   * @brief Try closing a UHID gamepad while fake destroy write fails.
   *
   * @return Close status.
   */
  OperationStatus linux_uhid_close_fake_write_failure();

  /**
   * @brief Try closing a UHID gamepad while fake close fails.
   *
   * @return Close status.
   */
  OperationStatus linux_uhid_close_fake_close_failure();

  /**
   * @brief Exercise UHID read-loop timeout and retry branches using fake poll/read syscalls.
   *
   * @return Close status after the scripted read loop exits.
   */
  OperationStatus linux_uhid_read_loop_fake_retry_branches();

  /**
   * @brief Exercise UHID read-loop poll error branches using fake poll syscalls.
   *
   * @return Close status after the scripted read loop exits.
   */
  OperationStatus linux_uhid_read_loop_fake_poll_errors();

  /**
   * @brief Exercise UHID read-loop read error branches using fake read syscalls.
   *
   * @return Close status after the scripted read loop exits.
   */
  OperationStatus linux_uhid_read_loop_fake_read_error();

  /**
   * @brief Exercise UHID read-loop output handling when no callback is registered.
   *
   * @return Close status after the scripted read loop exits.
   */
  OperationStatus linux_uhid_read_loop_fake_output_without_callback();

  /**
   * @brief Create a uinput device through the fake libevdev recorder.
   *
   * @param device_type Device type to create.
   * @return Recorded fake libevdev construction result.
   */
  LinuxLibevdevCreationResult linux_uinput_create_fake_libevdev_device(DeviceType device_type);

  /**
   * @brief Create a uinput gamepad through the fake libevdev recorder.
   *
   * @param kind Gamepad profile kind to create.
   * @return Recorded fake libevdev construction result.
   */
  LinuxLibevdevCreationResult linux_uinput_create_fake_gamepad(GamepadProfileKind kind);

  /**
   * @brief Try creating a uinput device while fake libevdev allocation fails.
   *
   * @param device_type Device type to create.
   * @return Creation status.
   */
  OperationStatus linux_uinput_create_fake_libevdev_allocation_failure(DeviceType device_type);

  /**
   * @brief Try creating a uinput device while fake libevdev event-type enablement fails.
   *
   * @param device_type Device type to create.
   * @return Creation status.
   */
  OperationStatus linux_uinput_create_fake_libevdev_event_type_failure(DeviceType device_type);

  /**
   * @brief Try creating a uinput device while fake libevdev event-code enablement fails.
   *
   * @param device_type Device type to create.
   * @return Creation status.
   */
  OperationStatus linux_uinput_create_fake_libevdev_event_code_failure(DeviceType device_type);

  /**
   * @brief Try creating a uinput device while fake libevdev property enablement fails.
   *
   * @param device_type Device type to create.
   * @return Creation status.
   */
  OperationStatus linux_uinput_create_fake_libevdev_property_failure(DeviceType device_type);

  /**
   * @brief Try creating a uinput device while fake libevdev uinput creation fails.
   *
   * @param device_type Device type to create.
   * @return Creation status.
   */
  OperationStatus linux_uinput_create_fake_libevdev_create_failure(DeviceType device_type);

  /**
   * @brief Submit a keyboard event while fake write fails.
   *
   * @return Submit status.
   */
  OperationStatus linux_uinput_keyboard_submit_fake_write_failure();

  /**
   * @brief Submit a keyboard event while fake write is short.
   *
   * @return Submit status.
   */
  OperationStatus linux_uinput_keyboard_submit_fake_short_write();

  /**
   * @brief Submit text through a fake successful uinput keyboard.
   *
   * @return Submit status.
   */
  OperationStatus linux_uinput_keyboard_type_text_fake_success();

  /**
   * @brief Submit text while a fake keyboard write call fails.
   *
   * @param fail_write_call One-based write call to fail.
   * @return Submit status.
   */
  OperationStatus linux_uinput_keyboard_type_text_fake_write_failure(int fail_write_call);

  /**
   * @brief Close a uinput keyboard while fake close fails.
   *
   * @return Close status.
   */
  OperationStatus linux_uinput_keyboard_close_fake_close_failure();

  /**
   * @brief Create a fake uinput keyboard with auto repeat enabled.
   *
   * @return Close status after the repeat thread runs.
   */
  OperationStatus linux_uinput_keyboard_auto_repeat_fake_success();

  /**
   * @brief Submit a mouse event while fake write fails.
   *
   * @param event Mouse event to submit.
   * @return Submit status.
   */
  OperationStatus linux_uinput_mouse_submit_fake_write_failure(const MouseEvent &event);

  /**
   * @brief Submit a mouse event while fake write is short.
   *
   * @param event Mouse event to submit.
   * @return Submit status.
   */
  OperationStatus linux_uinput_mouse_submit_fake_short_write(const MouseEvent &event);

  /**
   * @brief Submit keyboard input through the XTest fallback.
   *
   * @return Submit status.
   */
  OperationStatus linux_xtest_keyboard_submit_success();

  /**
   * @brief Submit unsupported keyboard input through the XTest fallback.
   *
   * @return Submit status.
   */
  OperationStatus linux_xtest_keyboard_submit_invalid();

  /**
   * @brief Submit keyboard input after closing the XTest fallback.
   *
   * @return Submit status.
   */
  OperationStatus linux_xtest_keyboard_submit_closed();

  /**
   * @brief Submit text through the XTest fallback.
   *
   * @return Submit status.
   */
  OperationStatus linux_xtest_keyboard_type_text_success();

  /**
   * @brief Submit text through XTest while a fake keycode lookup fails.
   *
   * @param fail_keycode_call One-based keycode lookup call to fail.
   * @return Submit status.
   */
  OperationStatus linux_xtest_keyboard_type_text_fake_keycode_failure(int fail_keycode_call);

  /**
   * @brief Try creating the XTest keyboard fallback while the extension query fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_xtest_keyboard_create_query_failure();

  /**
   * @brief Submit mouse input through the XTest fallback.
   *
   * @return Submit status.
   */
  OperationStatus linux_xtest_mouse_submit_success();

  /**
   * @brief Submit mouse input after closing the XTest fallback.
   *
   * @return Submit status.
   */
  OperationStatus linux_xtest_mouse_submit_closed();

  /**
   * @brief Try creating the XTest mouse fallback while the extension query fails.
   *
   * @return Creation status.
   */
  OperationStatus linux_xtest_mouse_create_query_failure();

  /**
   * @brief Translate a portable key code to an XTest keysym.
   *
   * @param key_code Portable keyboard key code.
   * @return X11 keysym, or `0` when unsupported or XTest is disabled.
   */
  unsigned long linux_xtest_keysym(KeyboardKeyCode key_code);

  /**
   * @brief Translate a mouse button to an XTest button code.
   *
   * @param button Mouse button.
   * @return XTest button code.
   */
  int linux_xtest_mouse_button(MouseButton button);

}  // namespace lvh::detail::test
