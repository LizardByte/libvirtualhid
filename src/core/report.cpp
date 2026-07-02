/**
 * @file src/core/report.cpp
 * @brief Gamepad report normalization and packing definitions.
 */

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

// local includes
#include <libvirtualhid/report.hpp>

namespace lvh::reports {
  namespace {

    constexpr std::uint8_t neutral_hat = 8;

    using ByteReport = std::vector<std::byte>;

    constexpr auto zero_byte = std::byte {0x00};

    constexpr auto dualshock4_usb_output_report_id = std::byte {0x05};

    constexpr auto dualshock4_bt_input_report_id = std::byte {0x11};

    constexpr auto dualshock4_bt_output_report_id = std::byte {0x11};

    constexpr auto dualshock4_output_hwctl_crc32 = std::byte {0x40};

    constexpr auto dualshock4_flag0_rumble = std::byte {0x01};

    constexpr auto dualshock4_flag0_lightbar = std::byte {0x02};

    constexpr auto dualsense_usb_output_report_id = std::byte {0x02};

    constexpr auto dualsense_bt_input_report_id = std::byte {0x31};

    constexpr auto dualsense_bt_output_report_id = std::byte {0x31};

    constexpr auto dualsense_bt_input_report_reserved = std::byte {0x00};

    constexpr auto playstation_input_crc_seed = std::byte {0xA1};

    constexpr auto playstation_output_crc_seed = std::byte {0xA2};

    constexpr auto dualsense_flag0_rumble = std::byte {0x01};

    constexpr auto dualsense_flag0_right_trigger = std::byte {0x04};

    constexpr auto dualsense_flag0_left_trigger = std::byte {0x08};

    constexpr auto dualsense_flag1_lightbar = std::byte {0x04};

    constexpr auto dualsense_flag2_compatible_vibration = std::byte {0x04};

    constexpr std::byte to_byte(std::uint8_t value) {
      return static_cast<std::byte>(value);
    }

    constexpr std::byte to_low_byte(std::uint32_t value) {
      return static_cast<std::byte>(value & 0xFFU);
    }

    constexpr std::uint8_t to_uint8(std::byte value) {
      return std::to_integer<std::uint8_t>(value);
    }

    bool has_flag(std::byte value, std::byte flag) {
      return (value & flag) != zero_byte;
    }

    void add_flag(ByteReport &report, std::size_t offset, std::byte flag) {
      report[offset] |= flag;
    }

    std::vector<std::uint8_t> to_uint8_report(const ByteReport &report) {
      std::vector<std::uint8_t> bytes;
      bytes.reserve(report.size());
      for (const auto value : report) {
        bytes.push_back(to_uint8(value));
      }
      return bytes;
    }

    ByteReport to_byte_report(const std::vector<std::uint8_t> &report) {
      ByteReport bytes;
      bytes.reserve(report.size());
      for (const auto value : report) {
        bytes.push_back(to_byte(value));
      }
      return bytes;
    }

    void write_u16(ByteReport &report, std::size_t offset, std::uint16_t value) {
      report[offset] = to_low_byte(value);
      report[offset + 1U] = to_low_byte(value >> 8U);
    }

    void write_u32(ByteReport &report, std::size_t offset, std::uint32_t value) {
      report[offset] = to_low_byte(value);
      report[offset + 1U] = to_low_byte(value >> 8U);
      report[offset + 2U] = to_low_byte(value >> 16U);
      report[offset + 3U] = to_low_byte(value >> 24U);
    }

    void write_i16(ByteReport &report, std::size_t offset, std::int16_t value) {
      write_u16(report, offset, static_cast<std::uint16_t>(value));
    }

    std::uint16_t read_u16(const std::vector<std::uint8_t> &report, std::size_t offset) {
      const auto low = static_cast<std::uint16_t>(report[offset]);
      const auto high = static_cast<std::uint16_t>(report[offset + 1U]);
      return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
    }

    void append_u16(std::vector<std::uint8_t> &report, std::uint16_t value) {
      report.push_back(static_cast<std::uint8_t>(value & 0xFFU));
      report.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }

    std::uint32_t read_u32(const ByteReport &report, std::size_t offset) {
      return std::to_integer<std::uint32_t>(report[offset]) |
             (std::to_integer<std::uint32_t>(report[offset + 1U]) << 8U) |
             (std::to_integer<std::uint32_t>(report[offset + 2U]) << 16U) |
             (std::to_integer<std::uint32_t>(report[offset + 3U]) << 24U);
    }

    std::uint32_t crc32(std::span<const std::byte> buffer, std::uint32_t seed = 0) {
      auto crc = seed ^ 0xFFFFFFFFU;
      for (const auto value : buffer) {
        crc ^= std::to_integer<std::uint32_t>(value);
        for (auto bit = 0; bit < 8; ++bit) {
          const auto mask = 0U - (crc & 1U);
          crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
      }
      return crc ^ 0xFFFFFFFFU;
    }

    std::uint32_t playstation_crc_seed(std::byte seed) {
      const std::array seed_report {seed};
      return crc32(seed_report);
    }

    void write_playstation_crc(ByteReport &report, std::byte seed) {
      if (report.size() < 4U) {
        return;
      }

      const auto crc_offset = report.size() - 4U;
      write_u32(report, crc_offset, crc32({report.data(), crc_offset}, playstation_crc_seed(seed)));
    }

    std::int16_t scale_i16(float value, float multiplier) {
      const auto scaled = std::clamp(value * multiplier, -32768.0F, 32767.0F);
      return static_cast<std::int16_t>(std::lround(scaled));
    }

    std::uint16_t normalize_unsigned_axis(float value) {
      return static_cast<std::uint16_t>(std::lround((clamp_axis(value) + 1.0F) * 32767.5F));
    }

    std::uint8_t normalize_u8_axis(float value) {
      return static_cast<std::uint8_t>(std::lround((clamp_axis(value) + 1.0F) * 127.5F));
    }

    std::uint16_t scale_output_byte(std::uint8_t value) {
      return static_cast<std::uint16_t>(std::lround((static_cast<float>(value) / 255.0F) * 65535.0F));
    }

    std::uint16_t common_button_bits(const ButtonSet &buttons) {
      auto bits = std::uint16_t {};
      const auto set_bit = [&buttons, &bits](std::uint16_t bit, GamepadButton button) {
        if (buttons.test(button)) {
          bits |= static_cast<std::uint16_t>(1U << bit);
        }
      };

      set_bit(0U, GamepadButton::a);
      set_bit(1U, GamepadButton::b);
      set_bit(2U, GamepadButton::x);
      set_bit(3U, GamepadButton::y);
      set_bit(4U, GamepadButton::left_shoulder);
      set_bit(5U, GamepadButton::right_shoulder);
      set_bit(6U, GamepadButton::back);
      set_bit(7U, GamepadButton::start);
      set_bit(8U, GamepadButton::left_stick);
      set_bit(9U, GamepadButton::right_stick);
      set_bit(10U, GamepadButton::guide);
      set_bit(11U, GamepadButton::misc1);
      return bits;
    }

    std::uint16_t standard_gamepad_button_bits(const ButtonSet &buttons) {
      auto bits = common_button_bits(buttons);
      const auto set_bit = [&buttons, &bits](std::uint16_t bit, GamepadButton button) {
        if (buttons.test(button)) {
          bits |= static_cast<std::uint16_t>(1U << bit);
        }
      };

      set_bit(12U, GamepadButton::dpad_up);
      set_bit(13U, GamepadButton::dpad_down);
      set_bit(14U, GamepadButton::dpad_left);
      set_bit(15U, GamepadButton::dpad_right);
      return bits;
    }

    std::uint16_t xbox_gip_button_bits(const ButtonSet &buttons) {
      auto bits = std::uint16_t {};
      const auto set_bit = [&buttons, &bits](std::uint16_t bit, GamepadButton button) {
        if (buttons.test(button)) {
          bits |= static_cast<std::uint16_t>(1U << bit);
        }
      };

      set_bit(0U, GamepadButton::a);
      set_bit(1U, GamepadButton::b);
      set_bit(2U, GamepadButton::x);
      set_bit(3U, GamepadButton::y);
      set_bit(4U, GamepadButton::left_shoulder);
      set_bit(5U, GamepadButton::right_shoulder);
      set_bit(6U, GamepadButton::back);
      set_bit(7U, GamepadButton::start);
      set_bit(8U, GamepadButton::left_stick);
      set_bit(9U, GamepadButton::right_stick);
      set_bit(11U, GamepadButton::misc1);
      return bits;
    }

    std::uint16_t switch_pro_button_bits(const GamepadState &state) {
      auto bits = std::uint16_t {};
      const auto set_bit = [&state, &bits](std::uint16_t bit, GamepadButton button) {
        if (state.buttons.test(button)) {
          bits |= static_cast<std::uint16_t>(1U << bit);
        }
      };

      set_bit(0U, GamepadButton::a);
      set_bit(1U, GamepadButton::b);
      set_bit(2U, GamepadButton::x);
      set_bit(3U, GamepadButton::y);
      set_bit(4U, GamepadButton::left_shoulder);
      set_bit(5U, GamepadButton::right_shoulder);
      if (state.left_trigger > 0.0F) {
        bits |= static_cast<std::uint16_t>(1U << 6U);
      }
      if (state.right_trigger > 0.0F) {
        bits |= static_cast<std::uint16_t>(1U << 7U);
      }
      set_bit(8U, GamepadButton::back);
      set_bit(9U, GamepadButton::start);
      set_bit(10U, GamepadButton::left_stick);
      set_bit(11U, GamepadButton::right_stick);
      set_bit(12U, GamepadButton::guide);
      set_bit(13U, GamepadButton::misc1);
      return bits;
    }

    std::byte dualsense_battery_state(GamepadBatteryState state) {
      switch (state) {
        using enum GamepadBatteryState;

        case discharging:
          return std::byte {0x00};
        case charging:
          return std::byte {0x01};
        case full:
          return std::byte {0x02};
        case voltage_or_temperature_error:
          return std::byte {0x0A};
        case temperature_error:
          return std::byte {0x0B};
        case charging_error:
          return std::byte {0x0F};
        case unknown:
          break;
      }

      return std::byte {0x02};
    }

    void write_dualsense_touch_contact(
      ByteReport &report,
      std::size_t offset,
      const GamepadTouchContact &contact
    ) {
      const auto x = static_cast<std::uint16_t>(std::lround(std::clamp(contact.x, 0.0F, 1.0F) * 1919.0F));
      const auto y = static_cast<std::uint16_t>(std::lround(std::clamp(contact.y, 0.0F, 1.0F) * 1079.0F));
      report[offset] = (to_byte(contact.id) & std::byte {0x7F}) | (contact.active ? zero_byte : std::byte {0x80});
      report[offset + 1U] = to_low_byte(x);
      report[offset + 2U] = to_low_byte(((x >> 8U) & 0x0FU) | ((y & 0x0FU) << 4U));
      report[offset + 3U] = to_low_byte(y >> 4U);
    }

    void write_dualshock4_touch_contact(
      ByteReport &report,
      std::size_t offset,
      const GamepadTouchContact &contact
    ) {
      const auto x = static_cast<std::uint16_t>(std::lround(std::clamp(contact.x, 0.0F, 1.0F) * 1919.0F));
      const auto y = static_cast<std::uint16_t>(std::lround(std::clamp(contact.y, 0.0F, 1.0F) * 941.0F));
      report[offset] = (to_byte(contact.id) & std::byte {0x7F}) | (contact.active ? zero_byte : std::byte {0x80});
      report[offset + 1U] = to_low_byte(x);
      report[offset + 2U] = to_low_byte(((x >> 8U) & 0x0FU) | ((y & 0x0FU) << 4U));
      report[offset + 3U] = to_low_byte(y >> 4U);
    }

    std::uint8_t dualshock4_battery_status(const GamepadBattery &battery) {
      using enum GamepadBatteryState;

      if (battery.state == full) {
        return 0x1B;
      }
      if (
        battery.state == voltage_or_temperature_error ||
        battery.state == temperature_error ||
        battery.state == charging_error
      ) {
        return 0x0F;
      }

      const auto charge = std::min<std::uint8_t>(10U, static_cast<std::uint8_t>(std::lround(battery.percentage / 10.0F)));
      if (battery.state == discharging) {
        return charge;
      }

      return static_cast<std::uint8_t>(0x10U | charge);
    }

    std::uint8_t dualshock4_battery_level(const GamepadBattery &battery) {
      if (battery.state == GamepadBatteryState::full && battery.percentage >= 100U) {
        return 0xFF;
      }

      return static_cast<std::uint8_t>(std::lround((static_cast<float>(battery.percentage) / 100.0F) * 255.0F));
    }

    std::uint16_t dualshock4_sensor_timestamp() {
      static const auto start = std::chrono::steady_clock::now();
      const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
      return static_cast<std::uint16_t>((static_cast<std::uint64_t>(elapsed) * 3U) / 16U);
    }

    std::vector<std::uint8_t> pack_dualshock4_input_report(const DeviceProfile &profile, const GamepadState &state) {
      const auto is_bluetooth = profile.bus_type == BusType::bluetooth;
      const auto payload_offset = is_bluetooth ? 3U : 1U;
      if (const auto minimum_report_size = is_bluetooth ? 78U : 64U; profile.input_report_size < minimum_report_size) {
        return {};
      }

      const auto normalized = normalize_state(state);
      const auto acceleration = normalized.acceleration.value_or(Vector3 {.x = 0.0F, .y = 9.80665F, .z = 0.0F});
      const auto gyroscope = normalized.gyroscope.value_or(Vector3 {});
      const auto battery = normalized.battery.value_or(GamepadBattery {.state = GamepadBatteryState::full, .percentage = 100});

      ByteReport report(profile.input_report_size, zero_byte);
      report[0] = is_bluetooth ? dualshock4_bt_input_report_id : to_byte(profile.report_id);

      report[payload_offset + 0U] = to_byte(normalize_u8_axis(normalized.left_stick.x));
      report[payload_offset + 1U] = to_byte(normalize_u8_axis(-normalized.left_stick.y));
      report[payload_offset + 2U] = to_byte(normalize_u8_axis(normalized.right_stick.x));
      report[payload_offset + 3U] = to_byte(normalize_u8_axis(-normalized.right_stick.y));
      report[payload_offset + 4U] = to_byte(hat_from_buttons(normalized.buttons));

      if (normalized.buttons.test(GamepadButton::x)) {
        add_flag(report, payload_offset + 4U, std::byte {0x10});
      }
      if (normalized.buttons.test(GamepadButton::a)) {
        add_flag(report, payload_offset + 4U, std::byte {0x20});
      }
      if (normalized.buttons.test(GamepadButton::b)) {
        add_flag(report, payload_offset + 4U, std::byte {0x40});
      }
      if (normalized.buttons.test(GamepadButton::y)) {
        add_flag(report, payload_offset + 4U, std::byte {0x80});
      }

      if (normalized.buttons.test(GamepadButton::left_shoulder)) {
        add_flag(report, payload_offset + 5U, std::byte {0x01});
      }
      if (normalized.buttons.test(GamepadButton::right_shoulder)) {
        add_flag(report, payload_offset + 5U, std::byte {0x02});
      }
      if (normalized.left_trigger > 0.0F) {
        add_flag(report, payload_offset + 5U, std::byte {0x04});
      }
      if (normalized.right_trigger > 0.0F) {
        add_flag(report, payload_offset + 5U, std::byte {0x08});
      }
      if (normalized.buttons.test(GamepadButton::back)) {
        add_flag(report, payload_offset + 5U, std::byte {0x10});
      }
      if (normalized.buttons.test(GamepadButton::start)) {
        add_flag(report, payload_offset + 5U, std::byte {0x20});
      }
      if (normalized.buttons.test(GamepadButton::left_stick)) {
        add_flag(report, payload_offset + 5U, std::byte {0x40});
      }
      if (normalized.buttons.test(GamepadButton::right_stick)) {
        add_flag(report, payload_offset + 5U, std::byte {0x80});
      }

      if (normalized.buttons.test(GamepadButton::guide)) {
        add_flag(report, payload_offset + 6U, std::byte {0x01});
      }
      if (normalized.buttons.test(GamepadButton::touchpad)) {
        add_flag(report, payload_offset + 6U, std::byte {0x02});
      }

      report[payload_offset + 7U] = to_byte(normalize_trigger(normalized.left_trigger));
      report[payload_offset + 8U] = to_byte(normalize_trigger(normalized.right_trigger));
      write_u16(report, payload_offset + 9U, dualshock4_sensor_timestamp());
      report[payload_offset + 11U] = to_byte(dualshock4_battery_level(battery));
      write_i16(report, payload_offset + 12U, scale_i16(gyroscope.x, 20.0F));
      write_i16(report, payload_offset + 14U, scale_i16(gyroscope.y, 20.0F));
      write_i16(report, payload_offset + 16U, scale_i16(gyroscope.z, 20.0F));
      write_i16(report, payload_offset + 18U, scale_i16(acceleration.x, 10000.0F / 9.80665F));
      write_i16(report, payload_offset + 20U, scale_i16(acceleration.y, 10000.0F / 9.80665F));
      write_i16(report, payload_offset + 22U, scale_i16(acceleration.z, 10000.0F / 9.80665F));
      report[payload_offset + 29U] = to_byte(dualshock4_battery_status(battery));

      const auto touch_report_offset = payload_offset + 33U;
      report[payload_offset + 32U] = std::byte {0x01};
      write_dualshock4_touch_contact(report, touch_report_offset + 1U, normalized.touchpad_contacts[0]);
      write_dualshock4_touch_contact(report, touch_report_offset + 5U, normalized.touchpad_contacts[1]);

      if (is_bluetooth) {
        write_playstation_crc(report, playstation_input_crc_seed);
      }
      return to_uint8_report(report);
    }

    std::vector<std::uint8_t> pack_dualsense_input_report(const DeviceProfile &profile, const GamepadState &state) {
      const auto is_bluetooth = profile.bus_type == BusType::bluetooth;
      const auto payload_offset = is_bluetooth ? 2U : 1U;
      if (const auto minimum_report_size = is_bluetooth ? 78U : 64U; profile.input_report_size < minimum_report_size) {
        return {};
      }

      const auto normalized = normalize_state(state);
      ByteReport report(profile.input_report_size, zero_byte);
      report[0] = is_bluetooth ? dualsense_bt_input_report_id : to_byte(profile.report_id);
      if (is_bluetooth) {
        report[1] = dualsense_bt_input_report_reserved;
      }

      report[payload_offset + 0U] = to_byte(normalize_u8_axis(normalized.left_stick.x));
      report[payload_offset + 1U] = to_byte(normalize_u8_axis(-normalized.left_stick.y));
      report[payload_offset + 2U] = to_byte(normalize_u8_axis(normalized.right_stick.x));
      report[payload_offset + 3U] = to_byte(normalize_u8_axis(-normalized.right_stick.y));
      report[payload_offset + 4U] = to_byte(normalize_trigger(normalized.left_trigger));
      report[payload_offset + 5U] = to_byte(normalize_trigger(normalized.right_trigger));
      report[payload_offset + 7U] = to_byte(hat_from_buttons(normalized.buttons));

      if (normalized.buttons.test(GamepadButton::x)) {
        add_flag(report, payload_offset + 7U, std::byte {0x10});
      }
      if (normalized.buttons.test(GamepadButton::a)) {
        add_flag(report, payload_offset + 7U, std::byte {0x20});
      }
      if (normalized.buttons.test(GamepadButton::b)) {
        add_flag(report, payload_offset + 7U, std::byte {0x40});
      }
      if (normalized.buttons.test(GamepadButton::y)) {
        add_flag(report, payload_offset + 7U, std::byte {0x80});
      }

      if (normalized.buttons.test(GamepadButton::left_shoulder)) {
        add_flag(report, payload_offset + 8U, std::byte {0x01});
      }
      if (normalized.buttons.test(GamepadButton::right_shoulder)) {
        add_flag(report, payload_offset + 8U, std::byte {0x02});
      }
      if (normalized.left_trigger > 0.0F) {
        add_flag(report, payload_offset + 8U, std::byte {0x04});
      }
      if (normalized.right_trigger > 0.0F) {
        add_flag(report, payload_offset + 8U, std::byte {0x08});
      }
      if (normalized.buttons.test(GamepadButton::back)) {
        add_flag(report, payload_offset + 8U, std::byte {0x10});
      }
      if (normalized.buttons.test(GamepadButton::start)) {
        add_flag(report, payload_offset + 8U, std::byte {0x20});
      }
      if (normalized.buttons.test(GamepadButton::left_stick)) {
        add_flag(report, payload_offset + 8U, std::byte {0x40});
      }
      if (normalized.buttons.test(GamepadButton::right_stick)) {
        add_flag(report, payload_offset + 8U, std::byte {0x80});
      }

      if (normalized.buttons.test(GamepadButton::guide)) {
        add_flag(report, payload_offset + 9U, std::byte {0x01});
      }
      if (normalized.buttons.test(GamepadButton::touchpad)) {
        add_flag(report, payload_offset + 9U, std::byte {0x02});
      }
      if (normalized.buttons.test(GamepadButton::misc1)) {
        add_flag(report, payload_offset + 9U, std::byte {0x04});
      }

      if (normalized.gyroscope) {
        write_i16(report, payload_offset + 15U, scale_i16(normalized.gyroscope->x, 1145.0F));
        write_i16(report, payload_offset + 17U, scale_i16(normalized.gyroscope->y, 1145.0F));
        write_i16(report, payload_offset + 19U, scale_i16(normalized.gyroscope->z, 1145.0F));
      }
      if (normalized.acceleration) {
        write_i16(report, payload_offset + 21U, scale_i16(normalized.acceleration->x, 100.0F));
        write_i16(report, payload_offset + 23U, scale_i16(normalized.acceleration->y, 100.0F));
        write_i16(report, payload_offset + 25U, scale_i16(normalized.acceleration->z, 100.0F));
      }

      write_dualsense_touch_contact(report, payload_offset + 32U, normalized.touchpad_contacts[0]);
      write_dualsense_touch_contact(report, payload_offset + 36U, normalized.touchpad_contacts[1]);

      const auto battery = normalized.battery.value_or(GamepadBattery {.state = GamepadBatteryState::full, .percentage = 100});
      const auto battery_charge = std::min<std::uint8_t>(10U, static_cast<std::uint8_t>(std::lround(battery.percentage / 10.0F)));
      report[payload_offset + 52U] =
        to_byte(battery_charge) | (dualsense_battery_state(battery.state) << 4U);
      report[payload_offset + 53U] = std::byte {0x0C};

      if (is_bluetooth) {
        write_playstation_crc(report, playstation_input_crc_seed);
      }
      return to_uint8_report(report);
    }

    std::optional<std::size_t> dualsense_common_output_offset(const ByteReport &report) {
      if (report.size() >= 48U && report[0] == dualsense_usb_output_report_id) {
        return 1U;
      }
      if (report.size() >= 49U && report[0] == dualsense_bt_output_report_id) {
        if (report.size() >= 78U) {
          const auto expected_crc = crc32({report.data(), report.size() - 4U}, playstation_crc_seed(playstation_output_crc_seed));
          const auto actual_crc = read_u32(report, report.size() - 4U);
          if (actual_crc != expected_crc) {
            return std::nullopt;
          }
        }

        const auto enable_hid = has_flag(report[1], std::byte {0x02});
        if (!enable_hid && report.size() < 50U) {
          return std::nullopt;
        }
        return enable_hid ? 2U : 3U;
      }
      return std::nullopt;
    }

    std::optional<std::size_t> dualshock4_common_output_offset(const ByteReport &report) {
      if (report.size() >= 32U && report[0] == dualshock4_usb_output_report_id) {
        return 1U;
      }
      if (report.size() >= 78U && report[0] == dualshock4_bt_output_report_id) {
        if (has_flag(report[1], dualshock4_output_hwctl_crc32)) {
          const auto expected_crc = crc32({report.data(), report.size() - 4U}, playstation_crc_seed(playstation_output_crc_seed));
          const auto actual_crc = read_u32(report, report.size() - 4U);
          if (actual_crc != expected_crc) {
            return std::nullopt;
          }
        }
        return 3U;
      }
      return std::nullopt;
    }

    void append_dualshock4_outputs(
      const ByteReport &report,
      const std::vector<std::uint8_t> &raw_report,
      std::size_t offset,
      std::vector<GamepadOutput> &outputs
    ) {
      const auto valid_flag0 = report[offset];
      const auto valid_flag1 = report[offset + 1U];
      const auto motor_right = raw_report[offset + 3U];
      const auto motor_left = raw_report[offset + 4U];

      if (has_flag(valid_flag0, dualshock4_flag0_rumble)) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rumble;
        output.low_frequency_rumble = scale_output_byte(motor_left);
        output.high_frequency_rumble = scale_output_byte(motor_right);
        output.raw_report = raw_report;
        outputs.push_back(std::move(output));
      } else if (valid_flag0 == zero_byte && valid_flag1 == zero_byte) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rumble;
        output.raw_report = raw_report;
        outputs.push_back(std::move(output));
      }

      if (has_flag(valid_flag0, dualshock4_flag0_lightbar)) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rgb_led;
        output.red = raw_report[offset + 5U];
        output.green = raw_report[offset + 6U];
        output.blue = raw_report[offset + 7U];
        output.raw_report = raw_report;
        outputs.push_back(std::move(output));
      }
    }

    void append_dualsense_outputs(
      const ByteReport &report,
      const std::vector<std::uint8_t> &raw_report,
      std::size_t offset,
      std::vector<GamepadOutput> &outputs
    ) {
      const auto valid_flag0 = report[offset];
      const auto valid_flag1 = report[offset + 1U];
      const auto motor_right = raw_report[offset + 2U];
      const auto motor_left = raw_report[offset + 3U];
      const auto right_trigger_effect_type = raw_report[offset + 10U];
      const auto left_trigger_effect_type = raw_report[offset + 21U];

      if (const auto valid_flag2 = report[offset + 38U]; has_flag(valid_flag0, dualsense_flag0_rumble) || has_flag(valid_flag2, dualsense_flag2_compatible_vibration)) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rumble;
        output.low_frequency_rumble = scale_output_byte(motor_left);
        output.high_frequency_rumble = scale_output_byte(motor_right);
        output.raw_report = raw_report;
        outputs.push_back(std::move(output));
      } else if (valid_flag0 == zero_byte && valid_flag1 == zero_byte && valid_flag2 == zero_byte) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rumble;
        output.raw_report = raw_report;
        outputs.push_back(std::move(output));
      }

      if (has_flag(valid_flag1, dualsense_flag1_lightbar)) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rgb_led;
        output.red = raw_report[offset + 44U];
        output.green = raw_report[offset + 45U];
        output.blue = raw_report[offset + 46U];
        output.raw_report = raw_report;
        outputs.push_back(std::move(output));
      }

      const auto trigger_flags = valid_flag0 & (dualsense_flag0_left_trigger | dualsense_flag0_right_trigger);
      if (trigger_flags != zero_byte) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::adaptive_triggers;
        output.adaptive_trigger_flags = to_uint8(trigger_flags);
        output.left_trigger_effect_type = left_trigger_effect_type;
        output.right_trigger_effect_type = right_trigger_effect_type;
        std::copy_n(raw_report.begin() + static_cast<std::ptrdiff_t>(offset + 11U), output.right_trigger_effect.size(), output.right_trigger_effect.begin());
        std::copy_n(raw_report.begin() + static_cast<std::ptrdiff_t>(offset + 22U), output.left_trigger_effect.size(), output.left_trigger_effect.begin());
        output.raw_report = raw_report;
        outputs.push_back(std::move(output));
      }
    }

  }  // namespace

  float clamp_axis(float value) {
    if (std::isnan(value)) {
      return 0.0F;
    }

    return std::clamp(value, -1.0F, 1.0F);
  }

  float clamp_trigger(float value) {
    if (std::isnan(value)) {
      return 0.0F;
    }

    return std::clamp(value, 0.0F, 1.0F);
  }

  std::int16_t normalize_axis(float value) {
    const auto clamped = clamp_axis(value);
    if (clamped <= -1.0F) {
      return -32768;
    }
    if (clamped >= 1.0F) {
      return 32767;
    }
    if (clamped < 0.0F) {
      return static_cast<std::int16_t>(std::lround(clamped * 32768.0F));
    }
    return static_cast<std::int16_t>(std::lround(clamped * 32767.0F));
  }

  std::uint8_t normalize_trigger(float value) {
    return static_cast<std::uint8_t>(std::lround(clamp_trigger(value) * 255.0F));
  }

  GamepadState normalize_state(const GamepadState &state) {
    auto normalized = state;
    normalized.left_stick.x = clamp_axis(state.left_stick.x);
    normalized.left_stick.y = clamp_axis(state.left_stick.y);
    normalized.right_stick.x = clamp_axis(state.right_stick.x);
    normalized.right_stick.y = clamp_axis(state.right_stick.y);
    normalized.left_trigger = clamp_trigger(state.left_trigger);
    normalized.right_trigger = clamp_trigger(state.right_trigger);
    for (auto &contact : normalized.touchpad_contacts) {
      contact.x = std::clamp(contact.x, 0.0F, 1.0F);
      contact.y = std::clamp(contact.y, 0.0F, 1.0F);
    }
    if (normalized.battery) {
      normalized.battery->percentage = std::min<std::uint8_t>(100U, normalized.battery->percentage);
    }
    return normalized;
  }

  std::uint8_t hat_from_buttons(const ButtonSet &buttons) {
    auto up = buttons.test(GamepadButton::dpad_up);
    auto down = buttons.test(GamepadButton::dpad_down);
    auto left = buttons.test(GamepadButton::dpad_left);
    auto right = buttons.test(GamepadButton::dpad_right);

    if (up && down) {
      up = false;
      down = false;
    }
    if (left && right) {
      left = false;
      right = false;
    }

    if (up && right) {
      return 1;
    }
    if (down && right) {
      return 3;
    }
    if (down && left) {
      return 5;
    }
    if (up && left) {
      return 7;
    }
    if (up) {
      return 0;
    }
    if (right) {
      return 2;
    }
    if (down) {
      return 4;
    }
    if (left) {
      return 6;
    }

    return neutral_hat;
  }

  std::uint16_t normalize_u10_trigger(float value) {
    return static_cast<std::uint16_t>(std::lround(clamp_trigger(value) * 1023.0F));
  }

  std::uint8_t xbox_gip_hat_from_buttons(const ButtonSet &buttons) {
    const auto hat = hat_from_buttons(buttons);
    return hat == neutral_hat ? 0 : static_cast<std::uint8_t>(hat + 1U);
  }

  std::uint8_t battery_strength(const std::optional<GamepadBattery> &battery) {
    if (!battery) {
      return 0xFF;
    }

    return static_cast<std::uint8_t>(std::lround((static_cast<float>(battery->percentage) / 100.0F) * 255.0F));
  }

  std::vector<std::uint8_t> pack_xbox_gip_input_report(const DeviceProfile &profile, const GamepadState &state) {
    constexpr std::size_t xbox_gip_input_report_size = 17;
    if (profile.input_report_size < xbox_gip_input_report_size) {
      return {};
    }

    const auto normalized = normalize_state(state);

    ByteReport report(profile.input_report_size, zero_byte);
    write_u16(report, 0U, normalize_unsigned_axis(normalized.left_stick.x));
    write_u16(report, 2U, normalize_unsigned_axis(-normalized.left_stick.y));
    write_u16(report, 4U, normalize_unsigned_axis(normalized.right_stick.x));
    write_u16(report, 6U, normalize_unsigned_axis(-normalized.right_stick.y));
    write_u16(report, 8U, normalize_u10_trigger(normalized.left_trigger));
    write_u16(report, 10U, normalize_u10_trigger(normalized.right_trigger));
    write_u16(report, 12U, xbox_gip_button_bits(normalized.buttons));
    report[14] = to_byte(xbox_gip_hat_from_buttons(normalized.buttons));
    if (normalized.buttons.test(GamepadButton::guide)) {
      report[15] = std::byte {0x01};
    }
    report[16] = to_byte(battery_strength(normalized.battery));
    return to_uint8_report(report);
  }

  std::vector<std::uint8_t> pack_standard_gamepad_input_report(
    const DeviceProfile &profile,
    const GamepadState &state
  ) {
    constexpr std::size_t standard_report_size = 9;
    if (profile.input_report_size < standard_report_size) {
      return {};
    }

    const auto normalized = normalize_state(state);

    std::vector<std::uint8_t> report;
    report.reserve(standard_report_size);
    report.push_back(profile.report_id);
    append_u16(report, standard_gamepad_button_bits(normalized.buttons));
    report.push_back(normalize_u8_axis(normalized.left_stick.x));
    report.push_back(normalize_u8_axis(-normalized.left_stick.y));
    report.push_back(normalize_u8_axis(normalized.right_stick.x));
    report.push_back(normalize_u8_axis(-normalized.right_stick.y));
    report.push_back(normalize_trigger(normalized.left_trigger));
    report.push_back(normalize_trigger(normalized.right_trigger));

    report.resize(profile.input_report_size, 0);
    return report;
  }

  std::vector<std::uint8_t> pack_switch_pro_input_report(const DeviceProfile &profile, const GamepadState &state) {
    constexpr std::size_t switch_pro_input_report_size = 64;
    if (profile.input_report_size < switch_pro_input_report_size) {
      return {};
    }

    const auto normalized = normalize_state(state);

    ByteReport report(profile.input_report_size, zero_byte);
    report[0] = to_byte(profile.report_id);
    write_u16(report, 1U, switch_pro_button_bits(normalized));
    write_u16(report, 3U, normalize_unsigned_axis(normalized.left_stick.x));
    write_u16(report, 5U, normalize_unsigned_axis(-normalized.left_stick.y));
    write_u16(report, 7U, normalize_unsigned_axis(normalized.right_stick.x));
    write_u16(report, 9U, normalize_unsigned_axis(-normalized.right_stick.y));
    report[11] = to_byte(hat_from_buttons(normalized.buttons));
    return to_uint8_report(report);
  }

  std::vector<std::uint8_t> pack_input_report(const DeviceProfile &profile, const GamepadState &state) {
    if (profile.device_type == DeviceType::gamepad) {
      if (profile.gamepad_kind == GamepadProfileKind::xbox_one || profile.gamepad_kind == GamepadProfileKind::xbox_series) {
        return pack_xbox_gip_input_report(profile, state);
      }
      if (profile.gamepad_kind == GamepadProfileKind::dualshock4) {
        return pack_dualshock4_input_report(profile, state);
      }
      if (profile.gamepad_kind == GamepadProfileKind::dualsense) {
        return pack_dualsense_input_report(profile, state);
      }
      if (profile.gamepad_kind == GamepadProfileKind::switch_pro) {
        return pack_switch_pro_input_report(profile, state);
      }
      if (profile.gamepad_kind == GamepadProfileKind::generic) {
        return pack_standard_gamepad_input_report(profile, state);
      }
    }

    constexpr std::size_t common_report_size = 9;
    if (profile.device_type != DeviceType::gamepad || profile.input_report_size < common_report_size) {
      return {};
    }

    const auto normalized = normalize_state(state);

    std::vector<std::uint8_t> report;
    report.reserve(common_report_size);
    report.push_back(profile.report_id);
    append_u16(
      report,
      static_cast<std::uint16_t>(
        common_button_bits(normalized.buttons) | static_cast<std::uint16_t>(hat_from_buttons(normalized.buttons) << 12U)
      )
    );
    report.push_back(normalize_u8_axis(normalized.left_stick.x));
    report.push_back(normalize_u8_axis(-normalized.left_stick.y));
    report.push_back(normalize_trigger(normalized.left_trigger));
    report.push_back(normalize_u8_axis(normalized.right_stick.x));
    report.push_back(normalize_u8_axis(-normalized.right_stick.y));
    report.push_back(normalize_trigger(normalized.right_trigger));

    report.resize(profile.input_report_size, 0);
    return report;
  }

  GamepadOutput parse_output_report(const DeviceProfile &profile, const std::vector<std::uint8_t> &report) {
    if (const auto outputs = parse_output_reports(profile, report); !outputs.empty()) {
      return outputs.front();
    }

    GamepadOutput output;
    output.raw_report = report;
    return output;
  }

  std::vector<GamepadOutput> parse_output_reports(const DeviceProfile &profile, const std::vector<std::uint8_t> &report) {
    std::vector<GamepadOutput> outputs;

    if (profile.gamepad_kind == GamepadProfileKind::dualshock4) {
      const auto byte_report = to_byte_report(report);
      if (const auto offset = dualshock4_common_output_offset(byte_report); offset.has_value()) {
        append_dualshock4_outputs(byte_report, report, *offset, outputs);
      }
      if (!outputs.empty()) {
        return outputs;
      }
    }

    if (profile.gamepad_kind == GamepadProfileKind::dualsense) {
      const auto byte_report = to_byte_report(report);
      if (const auto offset = dualsense_common_output_offset(byte_report); offset.has_value()) {
        append_dualsense_outputs(byte_report, report, *offset, outputs);
      }
      if (!outputs.empty()) {
        return outputs;
      }
    }

    if (
      profile.capabilities.supports_rumble && profile.output_report_size >= 5U &&
      report.size() >= profile.output_report_size && report[0] == profile.report_id
    ) {
      GamepadOutput output;
      output.raw_report = report;
      output.kind = GamepadOutputKind::rumble;
      output.low_frequency_rumble = read_u16(report, 1U);
      output.high_frequency_rumble = read_u16(report, 3U);
      outputs.push_back(std::move(output));
      return outputs;
    }

    GamepadOutput output;
    output.raw_report = report;
    outputs.push_back(std::move(output));
    return outputs;
  }

}  // namespace lvh::reports
