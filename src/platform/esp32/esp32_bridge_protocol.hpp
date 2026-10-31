/**
 * @file src/platform/esp32/esp32_bridge_protocol.hpp
 * @brief Wire protocol between the ESP32-S3 USB bridge backend and its firmware.
 *
 * The host talks to the bridge over the CDC-ACM interface of the bridge's
 * native USB port. Every frame is `type, sequence, payload..., crc16_le`,
 * COBS-encoded and terminated by a single zero byte. The CRC is
 * CRC-16/CCITT-FALSE over `type`, `sequence`, and the payload.
 *
 * The firmware carries a C copy of these definitions; both sides must change
 * together and bump protocol::version on incompatible changes.
 *
 * Pairing: unless the user turned it off, the bridge only accepts layout and
 * input frames from a host that proved knowledge of a paired secret. HELLO_ACK
 * carries a fresh nonce; the host answers with protocol::message::auth, where
 * `mac = HMAC-SHA256(secret, "LVHB-AUTH" || nonce || key_id || device_id)`.
 * New pairings and every configuration change wait for a press of the
 * bridge's BOOT button (protocol::confirm_window) and finish with
 * protocol::message::confirm_result.
 */
#pragma once

// standard includes
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace lvh::detail::esp32::protocol {

  inline constexpr std::uint16_t version = 2;  ///< Bumped on incompatible wire changes.
  inline constexpr std::size_t max_slots = 2;  ///< Device slots that fit next to CDC in the S3 endpoint budget.
  inline constexpr std::size_t max_payload = 600;  ///< Largest decoded frame payload.
  inline constexpr std::size_t max_frame = max_payload + 4;  ///< Largest decoded frame including header and CRC.
  inline constexpr std::size_t max_encoded_frame = max_frame + (max_frame / 254) + 2;  ///< COBS worst case plus delimiter.
  inline constexpr std::size_t max_report_descriptor = 512;  ///< Largest HID report descriptor accepted for a slot.
  inline constexpr std::size_t max_report = 64;  ///< Largest input, output, or feature report.
  inline constexpr std::size_t max_feature_reports = 4;  ///< Cached feature reports per slot.
  inline constexpr std::size_t max_string = 48;  ///< Largest manufacturer or product string.
  inline constexpr std::string_view serial_prefix = "LVHB-";  ///< USB serial number prefix used for host discovery.
  inline constexpr std::size_t xinput_report_size = 20;  ///< Xbox 360 wired input report size.
  inline constexpr std::chrono::milliseconds keepalive_interval {1000};  ///< Ping period while slots are active.
  inline constexpr std::size_t nonce_size = 16;  ///< Authentication challenge size.
  inline constexpr std::size_t device_id_size = 6;  ///< Bridge identity (its MAC address) carried in HELLO_ACK.
  inline constexpr std::size_t key_id_size = 8;  ///< Public identifier of one pairing.
  inline constexpr std::size_t secret_size = 32;  ///< Pairing secret size.
  inline constexpr std::size_t mac_size = 32;  ///< HMAC-SHA256 size.
  inline constexpr std::size_t max_pairings = 4;  ///< Pairings the bridge stores.
  inline constexpr std::size_t max_pairing_name = 32;  ///< Longest host label of a pairing.
  inline constexpr std::chrono::milliseconds confirm_window {30000};  ///< Time the user has to press BOOT after a pairing or configuration request.
  inline constexpr std::string_view auth_context = "LVHB-AUTH";  ///< Prefix of the authenticated message.

  /**
   * @brief Frame types. Values below 0x80 travel host to device.
   */
  namespace message {
    inline constexpr std::uint8_t hello = 0x01;  ///< u16 protocol_version.
    inline constexpr std::uint8_t layout_begin = 0x02;  ///< u16 vid, u16 pid, u16 bcd_device, u8 len, manufacturer, u8 len, product.
    inline constexpr std::uint8_t layout_slot = 0x03;  ///< u8 slot, u8 kind, u8 interval_ms, u16 descriptor_len, descriptor, neutral reports (see protocol::slot).
    inline constexpr std::uint8_t layout_feature = 0x04;  ///< u8 slot, u8 report_id, report bytes (including the report id).
    inline constexpr std::uint8_t layout_commit = 0x05;  ///< u32 generation. The bridge re-enumerates after acknowledging.
    inline constexpr std::uint8_t auth = 0x06;  ///< u8 key_id[8], u8 mac[32]. Acknowledged with OK or UNAUTHORIZED.
    inline constexpr std::uint8_t pair = 0x07;  ///< u8 len, host label. ACK OK arms a BOOT confirmation; the result follows as confirm_result.
    inline constexpr std::uint8_t config_get = 0x08;  ///< Empty. Answered with message::config.
    inline constexpr std::uint8_t config_set = 0x09;  ///< u8 protocol::config_op, arguments. ACK OK arms a BOOT confirmation.
    inline constexpr std::uint8_t input_report = 0x10;  ///< u8 slot, report bytes. Not acknowledged.
    inline constexpr std::uint8_t ping = 0x11;  ///< u32 token.

    inline constexpr std::uint8_t hello_ack = 0x81;  ///< u16 protocol_version, u8 max_slots, u8 usb_mounted, u32 generation, u16 max_descriptor, u8 protocol::auth_flag, u8 nonce[16], u8 device_id[6], firmware string.
    inline constexpr std::uint8_t ack = 0x82;  ///< u8 acknowledged_type, u8 acknowledged_sequence, u8 status.
    inline constexpr std::uint8_t output_report = 0x90;  ///< u8 slot, u8 report_kind, report bytes.
    inline constexpr std::uint8_t status = 0x91;  ///< u8 usb_mounted, u8 usb_suspended, u32 generation.
    inline constexpr std::uint8_t pong = 0x92;  ///< u32 token.
    inline constexpr std::uint8_t log = 0x93;  ///< UTF-8 text.
    inline constexpr std::uint8_t confirm_result = 0x94;  ///< u8 request_type, u8 status; a confirmed PAIR adds u8 key_id[8], u8 secret[32].
    inline constexpr std::uint8_t config = 0x95;  ///< u8 protocol::auth_flag, u8 count, count x (u8 key_id[8], u8 len, label).
  }  // namespace message

  /**
   * @brief Authentication flags in HELLO_ACK and CONFIG.
   */
  namespace auth_flag {
    inline constexpr std::uint8_t pairing_required = 0x01;  ///< Layout and input frames need an authenticated session.
    inline constexpr std::uint8_t authorized = 0x02;  ///< This session may send layout and input frames.
  }  // namespace auth_flag

  /**
   * @brief Operations of message::config_set.
   */
  namespace config_op {
    inline constexpr std::uint8_t require_pairing = 1;  ///< u8 required (0 or 1).
    inline constexpr std::uint8_t revoke = 2;  ///< u8 key_id[8].
    inline constexpr std::uint8_t revoke_all = 3;  ///< No arguments.
  }  // namespace config_op

  /**
   * @brief Personality of one device slot.
   */
  namespace slot {
    inline constexpr std::uint8_t none = 0;  ///< Slot is not enumerated.
    inline constexpr std::uint8_t xinput = 1;  ///< Xbox 360 wired XUSB interface bound by the inbox xusb22 driver.
    inline constexpr std::uint8_t hid = 2;  ///< HID gamepad; the latest input report streams continuously. Neutral: one report.
    inline constexpr std::uint8_t keyboard_mouse = 3;  ///< HID keyboard and mouse; reports queue in order and never repeat. Neutral: u8-length-prefixed reports.
  }  // namespace slot

  /**
   * @brief Report layout of the slot::keyboard_mouse interface.
   *
   * The host owns the report descriptor; the firmware only relies on these
   * layouts to merge queued pointer reports.
   */
  namespace km {
    inline constexpr std::uint8_t keyboard_report_id = 1;  ///< id, modifiers, reserved, 6 key usages. Output: id, LED bits.
    inline constexpr std::size_t keyboard_report_size = 9;  ///< Keyboard input report size including the id.
    inline constexpr std::uint8_t relative_report_id = 2;  ///< id, buttons, i16 x, i16 y, i8 wheel, i8 pan.
    inline constexpr std::size_t relative_report_size = 8;  ///< Relative pointer report size including the id.
    inline constexpr std::uint8_t absolute_report_id = 3;  ///< id, buttons, u16 x, u16 y in 0..32767 across the virtual desktop.
    inline constexpr std::size_t absolute_report_size = 6;  ///< Absolute pointer report size including the id.
  }  // namespace km

  /**
   * @brief Origin of a report forwarded from the USB host to the bridge host.
   */
  namespace report_kind {
    inline constexpr std::uint8_t output = 0;  ///< Interrupt OUT data or a SET_REPORT(Output) request.
    inline constexpr std::uint8_t feature = 1;  ///< SET_REPORT(Feature) request.
  }  // namespace report_kind

  /**
   * @brief Status codes carried by message::ack.
   */
  namespace status {
    inline constexpr std::uint8_t ok = 0;  ///< Request applied.
    inline constexpr std::uint8_t bad_request = 1;  ///< Payload was malformed.
    inline constexpr std::uint8_t bad_state = 2;  ///< Request arrived out of order.
    inline constexpr std::uint8_t invalid_layout = 3;  ///< Layout exceeds the endpoint budget or is inconsistent.
    inline constexpr std::uint8_t too_large = 4;  ///< Descriptor, report, or string exceeds a protocol limit.
    inline constexpr std::uint8_t version_mismatch = 5;  ///< Host protocol version is not supported.
    inline constexpr std::uint8_t unauthorized = 6;  ///< Pairing is required and this session is not authenticated, or AUTH failed.
    inline constexpr std::uint8_t busy = 7;  ///< Another request is waiting for the BOOT button.
    inline constexpr std::uint8_t full = 8;  ///< All pairing slots are in use.
    inline constexpr std::uint8_t timeout = 9;  ///< BOOT was not pressed in time.
    inline constexpr std::uint8_t not_found = 10;  ///< No pairing has that key id.
  }  // namespace status

  /**
   * @brief Endpoints a slot kind consumes besides EP0.
   *
   * XInput uses interrupt IN and OUT. HID and keyboard/mouse use interrupt IN
   * only; output reports arrive as SET_REPORT on EP0.
   *
   * @param kind Slot kind.
   * @return Endpoint count.
   */
  constexpr unsigned slot_endpoint_count(std::uint8_t kind) {
    if (kind == slot::xinput) {
      return 2;
    }
    return kind == slot::hid || kind == slot::keyboard_mouse ? 1 : 0;
  }

  /**
   * @brief Check a slot combination against the ESP32-S3 endpoint budget.
   *
   * The S3 OTG core offers six endpoints besides EP0, at most four of them IN.
   * CDC-ACM uses three (notification IN, data IN, data OUT). Every slot kind
   * uses exactly one IN endpoint; XInput adds an OUT endpoint.
   *
   * @param kinds Slot kinds.
   * @return True when the layout fits.
   */
  constexpr bool layout_fits(std::span<const std::uint8_t, max_slots> kinds) {
    unsigned endpoints = 3;
    unsigned in_endpoints = 2;
    for (const auto kind : kinds) {
      endpoints += slot_endpoint_count(kind);
      in_endpoints += kind != slot::none ? 1 : 0;
    }
    return endpoints <= 6 && in_endpoints <= 4;
  }

  /**
   * @brief Compute CRC-16/CCITT-FALSE.
   *
   * @param data Input bytes.
   * @return CRC value.
   */
  constexpr std::uint16_t crc16(std::span<const std::uint8_t> data) {
    std::uint32_t crc = 0xFFFF;
    for (const std::uint32_t value : data) {
      crc ^= value << 8;
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000) != 0 ? (crc << 1) ^ 0x1021 : crc << 1;
        crc &= 0xFFFF;
      }
    }
    return static_cast<std::uint16_t>(crc);
  }

  /**
   * @brief COBS-encode a buffer and append the zero delimiter.
   *
   * @param input Decoded bytes.
   * @param output Destination, at least `input.size() + input.size() / 254 + 2` bytes.
   * @return Encoded size including the delimiter.
   */
  constexpr std::size_t cobs_encode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    std::size_t write = 1;
    std::size_t code_index = 0;
    std::uint8_t code = 1;
    for (const auto value : input) {
      if (value == 0) {
        output[code_index] = code;
        code_index = write++;
        code = 1;
        continue;
      }
      output[write++] = value;
      if (++code == 0xFF) {
        output[code_index] = code;
        code_index = write++;
        code = 1;
      }
    }
    output[code_index] = code;
    output[write++] = 0;
    return write;
  }

  /**
   * @brief Decode one COBS frame without its delimiter.
   *
   * @param input Encoded bytes, excluding the trailing zero.
   * @param output Destination buffer.
   * @return Decoded size, or 0 when the frame is malformed or too large.
   */
  constexpr std::size_t cobs_decode(std::span<const std::uint8_t> input, std::span<std::uint8_t> output) {
    std::size_t read = 0;
    std::size_t write = 0;
    while (read < input.size()) {
      const std::size_t code = input[read++];
      if (code == 0 || read + code - 1 > input.size() || write + code - 1 > output.size()) {
        return 0;
      }
      for (std::size_t index = 1; index < code; ++index) {
        output[write++] = input[read++];
      }
      if (code == 0xFF || read >= input.size()) {
        continue;
      }
      if (write >= output.size()) {
        return 0;
      }
      output[write++] = 0;
    }
    return write;
  }

  /**
   * @brief Build an encoded frame ready to write to the CDC port.
   *
   * @param type Message type.
   * @param sequence Sender sequence number.
   * @param payload Payload bytes, at most #max_payload.
   * @param output Destination.
   * @return Encoded size, or 0 when the payload is too large.
   */
  constexpr std::size_t encode_frame(
    std::uint8_t type,
    std::uint8_t sequence,
    std::span<const std::uint8_t> payload,
    std::span<std::uint8_t, max_encoded_frame> output
  ) {
    if (payload.size() > max_payload) {
      return 0;
    }
    std::array<std::uint8_t, max_frame> frame {};
    frame[0] = type;
    frame[1] = sequence;
    for (std::size_t index = 0; index < payload.size(); ++index) {
      frame[2 + index] = payload[index];
    }
    const std::uint16_t crc = crc16(std::span {frame}.first(payload.size() + 2));
    frame[payload.size() + 2] = static_cast<std::uint8_t>(crc % 0x100);
    frame[payload.size() + 3] = static_cast<std::uint8_t>(crc / 0x100);
    return cobs_encode(std::span {frame}.first(payload.size() + 4), output);
  }

  /**
   * @brief Decode and validate an encoded frame.
   *
   * On success `frame[0]` is the type, `frame[1]` the sequence, and the
   * payload starts at `frame[2]`.
   *
   * @param encoded Encoded bytes, excluding the trailing zero.
   * @param frame Destination.
   * @return Payload size, or empty when the frame is malformed or fails its CRC.
   */
  constexpr std::optional<std::size_t> decode_frame(std::span<const std::uint8_t> encoded, std::span<std::uint8_t, max_frame> frame) {
    const std::size_t size = cobs_decode(encoded, frame);
    if (size < 4) {
      return std::nullopt;
    }
    if (const auto expected = static_cast<std::uint16_t>(frame[size - 2] + (frame[size - 1] * 0x100U)); crc16(std::span {frame}.first(size - 2)) != expected) {
      return std::nullopt;
    }
    return size - 4;
  }

}  // namespace lvh::detail::esp32::protocol
