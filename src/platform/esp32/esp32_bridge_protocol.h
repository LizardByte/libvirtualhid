/**
 * @file src/platform/esp32/esp32_bridge_protocol.h
 * @brief Wire protocol shared by the ESP32-S3 USB bridge backend and firmware.
 *
 * The host talks to the bridge over the CDC-ACM interface of the bridge's
 * native USB port. Every frame is `type, sequence, payload..., crc16_le`,
 * COBS-encoded and terminated by a single zero byte. The CRC is
 * CRC-16/CCITT-FALSE over `type`, `sequence`, and the payload.
 *
 * This header is plain C so the firmware can include it unchanged.
 *
 * Pairing: unless the user turned it off, the bridge only accepts layout and
 * input frames from a host that proved knowledge of a paired secret. HELLO_ACK
 * carries a fresh nonce; the host answers with #LVHB_MSG_AUTH, where
 * `mac = HMAC-SHA256(secret, "LVHB-AUTH" || nonce || key_id || device_id)`.
 * New pairings and every configuration change wait for a press of the
 * bridge's BOOT button (#LVHB_CONFIRM_WINDOW_MS) and finish with
 * #LVHB_MSG_CONFIRM_RESULT.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVHB_PROTOCOL_VERSION 2U  ///< Bumped on incompatible wire changes.
#define LVHB_MAX_SLOTS 2U  ///< Device slots that fit next to CDC in the S3 endpoint budget.
#define LVHB_MAX_PAYLOAD 600U  ///< Largest decoded frame payload.
#define LVHB_MAX_FRAME (LVHB_MAX_PAYLOAD + 4U)  ///< Largest decoded frame including header and CRC.
#define LVHB_MAX_ENCODED_FRAME (LVHB_MAX_FRAME + (LVHB_MAX_FRAME / 254U) + 2U)  ///< COBS worst case plus delimiter.
#define LVHB_MAX_REPORT_DESCRIPTOR 512U  ///< Largest HID report descriptor accepted for a slot.
#define LVHB_MAX_REPORT 64U  ///< Largest input, output, or feature report.
#define LVHB_MAX_FEATURE_REPORTS 4U  ///< Cached feature reports per slot.
#define LVHB_MAX_STRING 48U  ///< Largest manufacturer or product string.
#define LVHB_SERIAL_PREFIX "LVHB-"  ///< USB serial number prefix used for host discovery.
#define LVHB_XINPUT_REPORT_SIZE 20U  ///< Xbox 360 wired input report size.
#define LVHB_HOST_IDLE_NEUTRAL_MS 3000U  ///< Host silence after which slots fall back to their neutral report.
#define LVHB_HOST_IDLE_RELEASE_MS 15000U  ///< Host silence after which the bridge drops all slots and re-enumerates.
#define LVHB_KEEPALIVE_MS 1000U  ///< Interval at which the host sends #LVHB_MSG_PING while slots are active.
#define LVHB_NONCE_SIZE 16U  ///< Authentication challenge size.
#define LVHB_DEVICE_ID_SIZE 6U  ///< Bridge identity (its MAC address) carried in HELLO_ACK.
#define LVHB_KEY_ID_SIZE 8U  ///< Public identifier of one pairing.
#define LVHB_SECRET_SIZE 32U  ///< Pairing secret size.
#define LVHB_MAC_SIZE 32U  ///< HMAC-SHA256 size.
#define LVHB_MAX_PAIRINGS 4U  ///< Pairings the bridge stores.
#define LVHB_MAX_PAIRING_NAME 32U  ///< Longest host label of a pairing.
#define LVHB_CONFIRM_WINDOW_MS 30000U  ///< Time the user has to press BOOT after a pairing or configuration request.
#define LVHB_AUTH_CONTEXT "LVHB-AUTH"  ///< Prefix of the authenticated message.

  /**
   * @brief Frame types. Values below 0x80 travel host to device.
   */
  enum lvhb_message_type {
    LVHB_MSG_HELLO = 0x01,  ///< u16 protocol_version.
    LVHB_MSG_AUTH = 0x06,  ///< u8 key_id[8], u8 mac[32]. Acknowledged with OK or UNAUTHORIZED.
    LVHB_MSG_PAIR = 0x07,  ///< u8 len, host label. ACK OK arms a BOOT confirmation; the result follows as CONFIRM_RESULT.
    LVHB_MSG_CONFIG_GET = 0x08,  ///< Empty. Answered with #LVHB_MSG_CONFIG.
    LVHB_MSG_CONFIG_SET = 0x09,  ///< u8 #lvhb_config_op, arguments. ACK OK arms a BOOT confirmation.
    LVHB_MSG_LAYOUT_BEGIN = 0x02,  ///< u16 vid, u16 pid, u16 bcd_device, u8 len, manufacturer, u8 len, product.
    LVHB_MSG_LAYOUT_SLOT = 0x03,  ///< u8 slot, u8 kind, u8 interval_ms, u16 descriptor_len, descriptor, neutral reports (see #lvhb_slot_kind).
    LVHB_MSG_LAYOUT_FEATURE = 0x04,  ///< u8 slot, u8 report_id, report bytes (including the report id).
    LVHB_MSG_LAYOUT_COMMIT = 0x05,  ///< u32 generation. The bridge re-enumerates after acknowledging.
    LVHB_MSG_INPUT_REPORT = 0x10,  ///< u8 slot, report bytes. Not acknowledged.
    LVHB_MSG_PING = 0x11,  ///< u32 token.

    LVHB_MSG_HELLO_ACK = 0x81,  ///< u16 protocol_version, u8 max_slots, u8 usb_mounted, u32 generation, u16 max_descriptor, u8 #lvhb_auth_flags, u8 nonce[16], u8 device_id[6], firmware string.
    LVHB_MSG_ACK = 0x82,  ///< u8 acknowledged_type, u8 acknowledged_sequence, u8 status.
    LVHB_MSG_OUTPUT_REPORT = 0x90,  ///< u8 slot, u8 report_kind, report bytes.
    LVHB_MSG_STATUS = 0x91,  ///< u8 usb_mounted, u8 usb_suspended, u32 generation.
    LVHB_MSG_PONG = 0x92,  ///< u32 token.
    LVHB_MSG_LOG = 0x93,  ///< UTF-8 text.
    LVHB_MSG_CONFIRM_RESULT = 0x94,  ///< u8 request_type, u8 status; a confirmed PAIR adds u8 key_id[8], u8 secret[32].
    LVHB_MSG_CONFIG = 0x95,  ///< u8 #lvhb_auth_flags, u8 count, count x (u8 key_id[8], u8 len, label).
  };

  /**
   * @brief Authentication flags in HELLO_ACK and CONFIG.
   */
  enum lvhb_auth_flags {
    LVHB_AUTH_PAIRING_REQUIRED = 0x01,  ///< Layout and input frames need an authenticated session.
    LVHB_AUTH_AUTHORIZED = 0x02,  ///< This session may send layout and input frames.
  };

  /**
   * @brief Operations of #LVHB_MSG_CONFIG_SET.
   */
  enum lvhb_config_op {
    LVHB_CONFIG_REQUIRE_PAIRING = 1,  ///< u8 required (0 or 1).
    LVHB_CONFIG_REVOKE = 2,  ///< u8 key_id[8].
    LVHB_CONFIG_REVOKE_ALL = 3,  ///< No arguments.
  };

  /**
   * @brief Personality of one device slot.
   */
  enum lvhb_slot_kind {
    LVHB_SLOT_NONE = 0,  ///< Slot is not enumerated.
    LVHB_SLOT_XINPUT = 1,  ///< Xbox 360 wired XUSB interface bound by the inbox xusb22 driver.
    LVHB_SLOT_HID = 2,  ///< HID gamepad; the latest input report streams continuously. Neutral: one report.
    LVHB_SLOT_KEYBOARD_MOUSE = 3,  ///< HID keyboard and mouse; reports queue in order and never repeat. Neutral: u8-length-prefixed reports.
  };

  /**
   * @brief Report layout of the #LVHB_SLOT_KEYBOARD_MOUSE interface.
   *
   * The host owns the report descriptor; the firmware only relies on these
   * layouts to merge queued pointer reports.
   */
  enum lvhb_keyboard_mouse_report {
    LVHB_KM_KEYBOARD_REPORT_ID = 1,  ///< id, modifiers, reserved, 6 key usages. Output: id, LED bits.
    LVHB_KM_KEYBOARD_REPORT_SIZE = 9,  ///< Keyboard input report size including the id.
    LVHB_KM_RELATIVE_REPORT_ID = 2,  ///< id, buttons, i16 x, i16 y, i8 wheel, i8 pan.
    LVHB_KM_RELATIVE_REPORT_SIZE = 8,  ///< Relative pointer report size including the id.
    LVHB_KM_ABSOLUTE_REPORT_ID = 3,  ///< id, buttons, u16 x, u16 y in 0..32767 across the virtual desktop.
    LVHB_KM_ABSOLUTE_REPORT_SIZE = 6,  ///< Absolute pointer report size including the id.
  };

  /**
   * @brief Origin of a report forwarded from the USB host to the bridge host.
   */
  enum lvhb_report_kind {
    LVHB_REPORT_OUTPUT = 0,  ///< Interrupt OUT data or a SET_REPORT(Output) request.
    LVHB_REPORT_FEATURE = 1,  ///< SET_REPORT(Feature) request.
  };

  /**
   * @brief Status codes carried by #LVHB_MSG_ACK.
   */
  enum lvhb_status {
    LVHB_STATUS_OK = 0,  ///< Request applied.
    LVHB_STATUS_BAD_REQUEST = 1,  ///< Payload was malformed.
    LVHB_STATUS_BAD_STATE = 2,  ///< Request arrived out of order.
    LVHB_STATUS_INVALID_LAYOUT = 3,  ///< Layout exceeds the endpoint budget or is inconsistent.
    LVHB_STATUS_TOO_LARGE = 4,  ///< Descriptor, report, or string exceeds a protocol limit.
    LVHB_STATUS_VERSION_MISMATCH = 5,  ///< Host protocol version is not supported.
    LVHB_STATUS_UNAUTHORIZED = 6,  ///< Pairing is required and this session is not authenticated, or AUTH failed.
    LVHB_STATUS_BUSY = 7,  ///< Another request is waiting for the BOOT button.
    LVHB_STATUS_FULL = 8,  ///< All pairing slots are in use.
    LVHB_STATUS_TIMEOUT = 9,  ///< BOOT was not pressed in time.
    LVHB_STATUS_NOT_FOUND = 10,  ///< No pairing has that key id.
  };

  /**
   * @brief Endpoints a slot kind consumes besides EP0.
   *
   * XInput uses interrupt IN and OUT. HID and keyboard/mouse use interrupt IN
   * only; output reports arrive as SET_REPORT on EP0.
   *
   * @param kind Slot kind.
   * @return Endpoint count.
   */
  static inline unsigned lvhb_slot_endpoint_count(uint8_t kind) {
    return kind == LVHB_SLOT_XINPUT ? 2U : ((kind == LVHB_SLOT_HID || kind == LVHB_SLOT_KEYBOARD_MOUSE) ? 1U : 0U);
  }

  /**
   * @brief Check a slot combination against the ESP32-S3 endpoint budget.
   *
   * The S3 OTG core offers six endpoints besides EP0, at most four of them IN.
   * CDC-ACM uses three (notification IN, data IN, data OUT). Every slot kind
   * uses exactly one IN endpoint; XInput adds an OUT endpoint.
   *
   * @param kinds Slot kinds, #LVHB_MAX_SLOTS entries.
   * @return Non-zero when the layout fits.
   */
  static inline int lvhb_layout_fits(const uint8_t *kinds) {
    unsigned endpoints = 3U;
    unsigned in_endpoints = 2U;
    for (unsigned index = 0; index < LVHB_MAX_SLOTS; ++index) {
      endpoints += lvhb_slot_endpoint_count(kinds[index]);
      in_endpoints += kinds[index] != LVHB_SLOT_NONE ? 1U : 0U;
    }
    return endpoints <= 6U && in_endpoints <= 4U;
  }

  /**
   * @brief Compute CRC-16/CCITT-FALSE.
   *
   * @param data Input bytes.
   * @param size Number of bytes.
   * @return CRC value.
   */
  static inline uint16_t lvhb_crc16(const uint8_t *data, size_t size) {
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0; index < size; ++index) {
      crc ^= (uint16_t) ((uint16_t) data[index] << 8U);
      for (int bit = 0; bit < 8; ++bit) {
        crc = (crc & 0x8000U) ? (uint16_t) ((crc << 1U) ^ 0x1021U) : (uint16_t) (crc << 1U);
      }
    }
    return crc;
  }

  /**
   * @brief COBS-encode a buffer and append the zero delimiter.
   *
   * @param input Decoded bytes.
   * @param size Number of decoded bytes.
   * @param output Destination, at least `size + size / 254 + 2` bytes.
   * @return Encoded size including the delimiter.
   */
  static inline size_t lvhb_cobs_encode(const uint8_t *input, size_t size, uint8_t *output) {
    size_t write = 1;
    size_t code_index = 0;
    uint8_t code = 1;
    for (size_t read = 0; read < size; ++read) {
      if (input[read] == 0U) {
        output[code_index] = code;
        code_index = write++;
        code = 1;
        continue;
      }
      output[write++] = input[read];
      if (++code == 0xFFU) {
        output[code_index] = code;
        code_index = write++;
        code = 1;
      }
    }
    output[code_index] = code;
    output[write++] = 0U;
    return write;
  }

  /**
   * @brief Decode one COBS frame without its delimiter.
   *
   * @param input Encoded bytes, excluding the trailing zero.
   * @param size Number of encoded bytes.
   * @param output Destination buffer.
   * @param capacity Destination capacity.
   * @return Decoded size, or 0 when the frame is malformed or too large.
   */
  static inline size_t lvhb_cobs_decode(const uint8_t *input, size_t size, uint8_t *output, size_t capacity) {
    size_t read = 0;
    size_t write = 0;
    while (read < size) {
      const uint8_t code = input[read++];
      if (code == 0U || read + code - 1U > size) {
        return 0;
      }
      for (uint8_t index = 1; index < code; ++index) {
        if (write >= capacity) {
          return 0;
        }
        output[write++] = input[read++];
      }
      if (code != 0xFFU && read < size) {
        if (write >= capacity) {
          return 0;
        }
        output[write++] = 0U;
      }
    }
    return write;
  }

  /**
   * @brief Build an encoded frame ready to write to the CDC port.
   *
   * @param type Message type.
   * @param sequence Sender sequence number.
   * @param payload Payload bytes, may be NULL when @p payload_size is 0.
   * @param payload_size Payload size, at most #LVHB_MAX_PAYLOAD.
   * @param output Destination of at least #LVHB_MAX_ENCODED_FRAME bytes.
   * @return Encoded size, or 0 when the payload is too large.
   */
  static inline size_t lvhb_encode_frame(
    uint8_t type,
    uint8_t sequence,
    const uint8_t *payload,
    size_t payload_size,
    uint8_t *output
  ) {
    uint8_t frame[LVHB_MAX_FRAME];
    if (payload_size > LVHB_MAX_PAYLOAD) {
      return 0;
    }
    frame[0] = type;
    frame[1] = sequence;
    for (size_t index = 0; index < payload_size; ++index) {
      frame[2U + index] = payload[index];
    }
    const uint16_t crc = lvhb_crc16(frame, payload_size + 2U);
    frame[payload_size + 2U] = (uint8_t) (crc & 0xFFU);
    frame[payload_size + 3U] = (uint8_t) (crc >> 8U);
    return lvhb_cobs_encode(frame, payload_size + 4U, output);
  }

  /**
   * @brief Decode and validate an encoded frame.
   *
   * @param encoded Encoded bytes, excluding the trailing zero.
   * @param encoded_size Number of encoded bytes.
   * @param frame Destination of at least #LVHB_MAX_FRAME bytes.
   * @return Payload size, or -1 when the frame is malformed or fails its CRC.
   *         On success `frame[0]` is the type, `frame[1]` the sequence, and the
   *         payload starts at `frame + 2`.
   */
  static inline int lvhb_decode_frame(const uint8_t *encoded, size_t encoded_size, uint8_t *frame) {
    const size_t size = lvhb_cobs_decode(encoded, encoded_size, frame, LVHB_MAX_FRAME);
    if (size < 4U) {
      return -1;
    }
    const uint16_t expected = (uint16_t) (frame[size - 2U] | (frame[size - 1U] << 8U));
    if (lvhb_crc16(frame, size - 2U) != expected) {
      return -1;
    }
    return (int) (size - 4U);
  }

#ifdef __cplusplus
}
#endif
