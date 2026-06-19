/**
 * @file tests/fixtures/include/fixtures/linux_backend_test_hooks.hpp
 * @brief Test-only hooks for Linux UHID backend internals.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <string>
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
    Status status;

    /**
     * @brief Events written to the pipe.
     */
    std::vector<LinuxInputEventRecord> events;
  };

  /**
   * @brief Result from a socketpair-backed UHID lifecycle test.
   */
  struct LinuxUhidRoundTripResult {
    /**
     * @brief Create operation status.
     */
    Status create_status;

    /**
     * @brief Submit operation status.
     */
    Status submit_status;

    /**
     * @brief Close operation status.
     */
    Status close_status;

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
    Status gamepad_status;

    /**
     * @brief Gamepad close status.
     */
    Status gamepad_close_status;

    /**
     * @brief Keyboard creation status.
     */
    Status keyboard_status;

    /**
     * @brief Keyboard close status.
     */
    Status keyboard_close_status;

    /**
     * @brief Mouse creation status.
     */
    Status mouse_status;

    /**
     * @brief Mouse close status.
     */
    Status mouse_close_status;
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
  Status linux_uhid_create_with_descriptor_size(std::size_t descriptor_size);

  /**
   * @brief Try submitting a UHID input report on an invalid file descriptor.
   *
   * @param report_size Input report size to use.
   * @return Submit status.
   */
  Status linux_uhid_submit_report_size(std::size_t report_size);

  /**
   * @brief Try submitting a UHID input report after closing the backend device.
   *
   * @return Submit status.
   */
  Status linux_uhid_submit_after_close();

  /**
   * @brief Try creating a uinput keyboard on an invalid file descriptor.
   *
   * @return Creation status.
   */
  Status linux_uinput_keyboard_create_invalid_fd();

  /**
   * @brief Submit a keyboard event to a uinput keyboard on an invalid file descriptor.
   *
   * @param event Keyboard event.
   * @return Submit status.
   */
  Status linux_uinput_keyboard_submit_invalid_fd(const KeyboardEvent &event);

  /**
   * @brief Submit text to a uinput keyboard on an invalid file descriptor.
   *
   * @param text UTF-8 text.
   * @return Submit status.
   */
  Status linux_uinput_keyboard_type_text_invalid_fd(const std::string &text);

  /**
   * @brief Try submitting a keyboard event after closing the backend device.
   *
   * @return Submit status.
   */
  Status linux_uinput_keyboard_submit_after_close();

  /**
   * @brief Submit a keyboard event to a pipe-backed uinput keyboard.
   *
   * @param event Keyboard event.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_keyboard_submit_pipe(const KeyboardEvent &event);

  /**
   * @brief Try writing a uinput device definition to an invalid file descriptor.
   *
   * @return Write status.
   */
  Status linux_uinput_user_device_invalid_fd();

  /**
   * @brief Try writing a uinput device definition to a pipe.
   *
   * @return Write status.
   */
  Status linux_uinput_user_device_pipe();

  /**
   * @brief Try creating a uinput mouse on an invalid file descriptor.
   *
   * @return Creation status.
   */
  Status linux_uinput_mouse_create_invalid_fd();

  /**
   * @brief Submit a mouse event to a uinput mouse on an invalid file descriptor.
   *
   * @param event Mouse event.
   * @return Submit status.
   */
  Status linux_uinput_mouse_submit_invalid_fd(const MouseEvent &event);

  /**
   * @brief Try submitting a mouse event after closing the backend device.
   *
   * @return Submit status.
   */
  Status linux_uinput_mouse_submit_after_close();

  /**
   * @brief Submit a mouse event to a pipe-backed uinput mouse.
   *
   * @param event Mouse event.
   * @return Submission status and captured input events.
   */
  LinuxInputSubmissionResult linux_uinput_mouse_submit_pipe(const MouseEvent &event);

  /**
   * @brief Exercise a UHID gamepad lifecycle over a socketpair.
   *
   * @return Round-trip result.
   */
  LinuxUhidRoundTripResult linux_uhid_socketpair_roundtrip();

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
  Status linux_backend_gamepad_fake_open_failure();

  /**
   * @brief Try creating a Linux backend gamepad while fake UHID creation fails.
   *
   * @return Creation status.
   */
  Status linux_backend_gamepad_fake_create_failure();

  /**
   * @brief Try creating a Linux backend keyboard while fake open fails.
   *
   * @return Creation status.
   */
  Status linux_backend_keyboard_fake_open_failure();

  /**
   * @brief Try creating a Linux backend keyboard while fake uinput creation fails.
   *
   * @return Creation status.
   */
  Status linux_backend_keyboard_fake_create_failure();

  /**
   * @brief Create a Linux backend keyboard through a fake successful fallback after uinput creation fails.
   *
   * @return Creation status.
   */
  Status linux_backend_keyboard_fake_fallback_success();

  /**
   * @brief Try creating a Linux backend mouse while fake open fails.
   *
   * @return Creation status.
   */
  Status linux_backend_mouse_fake_open_failure();

  /**
   * @brief Try creating a Linux backend mouse while fake uinput creation fails.
   *
   * @return Creation status.
   */
  Status linux_backend_mouse_fake_create_failure();

  /**
   * @brief Create a Linux backend mouse through a fake successful fallback after uinput creation fails.
   *
   * @return Creation status.
   */
  Status linux_backend_mouse_fake_fallback_success();

  /**
   * @brief Try submitting a UHID input report while fake write fails.
   *
   * @return Submit status.
   */
  Status linux_uhid_submit_fake_write_failure();

  /**
   * @brief Try submitting a UHID input report while fake write is short.
   *
   * @return Submit status.
   */
  Status linux_uhid_submit_fake_short_write();

  /**
   * @brief Try closing a UHID gamepad while fake destroy write fails.
   *
   * @return Close status.
   */
  Status linux_uhid_close_fake_write_failure();

  /**
   * @brief Try closing a UHID gamepad while fake close fails.
   *
   * @return Close status.
   */
  Status linux_uhid_close_fake_close_failure();

  /**
   * @brief Exercise UHID read-loop timeout and retry branches using fake poll/read syscalls.
   *
   * @return Close status after the scripted read loop exits.
   */
  Status linux_uhid_read_loop_fake_retry_branches();

  /**
   * @brief Exercise UHID read-loop poll error branches using fake poll syscalls.
   *
   * @return Close status after the scripted read loop exits.
   */
  Status linux_uhid_read_loop_fake_poll_errors();

  /**
   * @brief Exercise UHID read-loop read error branches using fake read syscalls.
   *
   * @return Close status after the scripted read loop exits.
   */
  Status linux_uhid_read_loop_fake_read_error();

  /**
   * @brief Exercise UHID read-loop output handling when no callback is registered.
   *
   * @return Close status after the scripted read loop exits.
   */
  Status linux_uhid_read_loop_fake_output_without_callback();

  /**
   * @brief Try creating a uinput keyboard while a fake ioctl call fails.
   *
   * @param fail_ioctl_call One-based ioctl call to fail.
   * @return Creation status.
   */
  Status linux_uinput_keyboard_create_fake_ioctl_failure(int fail_ioctl_call);

  /**
   * @brief Try writing a uinput device definition while fake write is short.
   *
   * @return Write status.
   */
  Status linux_uinput_user_device_fake_short_write();

  /**
   * @brief Try writing a uinput device definition while fake device creation ioctl fails.
   *
   * @return Write status.
   */
  Status linux_uinput_user_device_fake_create_failure();

  /**
   * @brief Submit a keyboard event while fake write fails.
   *
   * @return Submit status.
   */
  Status linux_uinput_keyboard_submit_fake_write_failure();

  /**
   * @brief Submit a keyboard event while fake write is short.
   *
   * @return Submit status.
   */
  Status linux_uinput_keyboard_submit_fake_short_write();

  /**
   * @brief Submit text through a fake successful uinput keyboard.
   *
   * @return Submit status.
   */
  Status linux_uinput_keyboard_type_text_fake_success();

  /**
   * @brief Close a uinput keyboard while fake close fails.
   *
   * @return Close status.
   */
  Status linux_uinput_keyboard_close_fake_close_failure();

  /**
   * @brief Try creating a uinput mouse while a fake ioctl call fails.
   *
   * @param fail_ioctl_call One-based ioctl call to fail.
   * @return Creation status.
   */
  Status linux_uinput_mouse_create_fake_ioctl_failure(int fail_ioctl_call);

  /**
   * @brief Submit a mouse event while fake write fails.
   *
   * @param event Mouse event to submit.
   * @return Submit status.
   */
  Status linux_uinput_mouse_submit_fake_write_failure(const MouseEvent &event);

  /**
   * @brief Submit a mouse event while fake write is short.
   *
   * @param event Mouse event to submit.
   * @return Submit status.
   */
  Status linux_uinput_mouse_submit_fake_short_write(const MouseEvent &event);

}  // namespace lvh::detail::test
