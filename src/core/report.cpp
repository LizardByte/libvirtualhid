/**
 * @file src/core/report.cpp
 * @brief Gamepad report normalization and packing definitions.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

// local includes
#include <libvirtualhid/report.hpp>

namespace lvh::reports {
  namespace {

    constexpr std::uint8_t neutral_hat = 8;

    constexpr std::uint8_t dualsense_usb_output_report_id = 0x02;

    constexpr std::uint8_t dualsense_bt_input_report_id = 0x31;

    constexpr std::uint8_t dualsense_bt_output_report_id = 0x31;

    constexpr std::uint8_t dualsense_bt_input_report_reserved = 0x00;

    constexpr std::uint8_t dualsense_input_crc_seed = 0xA1;

    constexpr std::uint8_t dualsense_output_crc_seed = 0xA2;

    constexpr std::uint8_t dualsense_flag0_rumble = 0x01;

    constexpr std::uint8_t dualsense_flag0_right_trigger = 0x04;

    constexpr std::uint8_t dualsense_flag0_left_trigger = 0x08;

    constexpr std::uint8_t dualsense_flag1_lightbar = 0x04;

    constexpr std::uint8_t dualsense_flag2_compatible_vibration = 0x04;

    void append_u16(std::vector<std::uint8_t> &report, std::uint16_t value) {
      report.push_back(static_cast<std::uint8_t>(value & 0xFFU));
      report.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    }

    void append_i16(std::vector<std::uint8_t> &report, std::int16_t value) {
      append_u16(report, static_cast<std::uint16_t>(value));
    }

    void write_u16(std::vector<std::uint8_t> &report, std::size_t offset, std::uint16_t value) {
      report[offset] = static_cast<std::uint8_t>(value & 0xFFU);
      report[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    }

    void write_u32(std::vector<std::uint8_t> &report, std::size_t offset, std::uint32_t value) {
      report[offset] = static_cast<std::uint8_t>(value & 0xFFU);
      report[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
      report[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
      report[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
    }

    void write_i16(std::vector<std::uint8_t> &report, std::size_t offset, std::int16_t value) {
      write_u16(report, offset, static_cast<std::uint16_t>(value));
    }

    std::uint16_t read_u16(const std::vector<std::uint8_t> &report, std::size_t offset) {
      const auto low = static_cast<std::uint16_t>(report[offset]);
      const auto high = static_cast<std::uint16_t>(report[offset + 1U]);
      return static_cast<std::uint16_t>(low | static_cast<std::uint16_t>(high << 8U));
    }

    std::uint32_t crc32(const std::uint8_t *buffer, std::size_t length, std::uint32_t seed = 0) {
      auto crc = seed ^ 0xFFFFFFFFU;
      for (std::size_t index = 0; index < length; ++index) {
        crc ^= buffer[index];
        for (auto bit = 0; bit < 8; ++bit) {
          const auto mask = 0U - (crc & 1U);
          crc = (crc >> 1U) ^ (0xEDB88320U & mask);
        }
      }
      return crc ^ 0xFFFFFFFFU;
    }

    std::uint32_t dualsense_crc_seed(std::uint8_t seed) {
      return crc32(&seed, 1U);
    }

    void write_dualsense_crc(std::vector<std::uint8_t> &report, std::uint8_t seed) {
      if (report.size() < 4U) {
        return;
      }

      const auto crc_offset = report.size() - 4U;
      write_u32(report, crc_offset, crc32(report.data(), crc_offset, dualsense_crc_seed(seed)));
    }

    std::int16_t scale_i16(float value, float multiplier) {
      const auto scaled = std::clamp(value * multiplier, -32768.0F, 32767.0F);
      return static_cast<std::int16_t>(std::lround(scaled));
    }

    std::uint8_t normalize_dualsense_axis(float value) {
      return static_cast<std::uint8_t>(std::lround((clamp_axis(value) + 1.0F) * 127.5F));
    }

    std::uint16_t scale_output_byte(std::uint8_t value) {
      return static_cast<std::uint16_t>(std::lround((static_cast<float>(value) / 255.0F) * 65535.0F));
    }

    std::uint16_t report_button_bits(const ButtonSet &buttons) {
      std::uint16_t bits = 0;

      const auto add = [&bits, &buttons](GamepadButton button, std::uint16_t bit) {
        if (buttons.test(button)) {
          bits |= bit;
        }
      };

      add(GamepadButton::a, 1U << 0U);
      add(GamepadButton::b, 1U << 1U);
      add(GamepadButton::x, 1U << 2U);
      add(GamepadButton::y, 1U << 3U);
      add(GamepadButton::back, 1U << 4U);
      add(GamepadButton::start, 1U << 5U);
      add(GamepadButton::guide, 1U << 6U);
      add(GamepadButton::left_stick, 1U << 7U);
      add(GamepadButton::right_stick, 1U << 8U);
      add(GamepadButton::left_shoulder, 1U << 9U);
      add(GamepadButton::right_shoulder, 1U << 10U);
      add(GamepadButton::misc1, 1U << 11U);

      return bits;
    }

    std::uint8_t dualsense_battery_state(GamepadBatteryState state) {
      switch (state) {
        case GamepadBatteryState::discharging:
          return 0x00;
        case GamepadBatteryState::charging:
          return 0x01;
        case GamepadBatteryState::full:
          return 0x02;
        case GamepadBatteryState::voltage_or_temperature_error:
          return 0x0A;
        case GamepadBatteryState::temperature_error:
          return 0x0B;
        case GamepadBatteryState::charging_error:
          return 0x0F;
        case GamepadBatteryState::unknown:
          break;
      }

      return 0x02;
    }

    void write_dualsense_touch_contact(
      std::vector<std::uint8_t> &report,
      std::size_t offset,
      const GamepadTouchContact &contact
    ) {
      const auto x = static_cast<std::uint16_t>(std::lround(std::clamp(contact.x, 0.0F, 1.0F) * 1919.0F));
      const auto y = static_cast<std::uint16_t>(std::lround(std::clamp(contact.y, 0.0F, 1.0F) * 1079.0F));
      report[offset] = static_cast<std::uint8_t>((contact.id & 0x7FU) | (contact.active ? 0x00U : 0x80U));
      report[offset + 1U] = static_cast<std::uint8_t>(x & 0xFFU);
      report[offset + 2U] = static_cast<std::uint8_t>(((x >> 8U) & 0x0FU) | ((y & 0x0FU) << 4U));
      report[offset + 3U] = static_cast<std::uint8_t>((y >> 4U) & 0xFFU);
    }

    std::vector<std::uint8_t> pack_dualsense_input_report(const DeviceProfile &profile, const GamepadState &state) {
      const auto is_bluetooth = profile.bus_type == BusType::bluetooth;
      const auto payload_offset = is_bluetooth ? 2U : 1U;
      const auto minimum_report_size = is_bluetooth ? 78U : 64U;
      if (profile.input_report_size < minimum_report_size) {
        return {};
      }

      const auto normalized = normalize_state(state);
      std::vector<std::uint8_t> report(profile.input_report_size, 0);
      report[0] = is_bluetooth ? dualsense_bt_input_report_id : profile.report_id;
      if (is_bluetooth) {
        report[1] = dualsense_bt_input_report_reserved;
      }

      report[payload_offset + 0U] = normalize_dualsense_axis(normalized.left_stick.x);
      report[payload_offset + 1U] = normalize_dualsense_axis(normalized.left_stick.y);
      report[payload_offset + 2U] = normalize_dualsense_axis(normalized.right_stick.x);
      report[payload_offset + 3U] = normalize_dualsense_axis(normalized.right_stick.y);
      report[payload_offset + 4U] = normalize_trigger(normalized.left_trigger);
      report[payload_offset + 5U] = normalize_trigger(normalized.right_trigger);
      report[payload_offset + 7U] = hat_from_buttons(normalized.buttons);

      if (normalized.buttons.test(GamepadButton::x)) {
        report[payload_offset + 7U] |= 0x10;
      }
      if (normalized.buttons.test(GamepadButton::a)) {
        report[payload_offset + 7U] |= 0x20;
      }
      if (normalized.buttons.test(GamepadButton::b)) {
        report[payload_offset + 7U] |= 0x40;
      }
      if (normalized.buttons.test(GamepadButton::y)) {
        report[payload_offset + 7U] |= 0x80;
      }

      if (normalized.buttons.test(GamepadButton::left_shoulder)) {
        report[payload_offset + 8U] |= 0x01;
      }
      if (normalized.buttons.test(GamepadButton::right_shoulder)) {
        report[payload_offset + 8U] |= 0x02;
      }
      if (normalized.left_trigger > 0.0F) {
        report[payload_offset + 8U] |= 0x04;
      }
      if (normalized.right_trigger > 0.0F) {
        report[payload_offset + 8U] |= 0x08;
      }
      if (normalized.buttons.test(GamepadButton::back)) {
        report[payload_offset + 8U] |= 0x10;
      }
      if (normalized.buttons.test(GamepadButton::start)) {
        report[payload_offset + 8U] |= 0x20;
      }
      if (normalized.buttons.test(GamepadButton::left_stick)) {
        report[payload_offset + 8U] |= 0x40;
      }
      if (normalized.buttons.test(GamepadButton::right_stick)) {
        report[payload_offset + 8U] |= 0x80;
      }

      if (normalized.buttons.test(GamepadButton::guide)) {
        report[payload_offset + 9U] |= 0x01;
      }
      if (normalized.buttons.test(GamepadButton::misc1)) {
        report[payload_offset + 9U] |= 0x04;
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
        static_cast<std::uint8_t>(battery_charge | (dualsense_battery_state(battery.state) << 4U));
      report[payload_offset + 53U] = 0x0C;

      if (is_bluetooth) {
        write_dualsense_crc(report, dualsense_input_crc_seed);
      }
      return report;
    }

    std::optional<std::size_t> dualsense_common_output_offset(const std::vector<std::uint8_t> &report) {
      if (report.size() >= 48U && report[0] == dualsense_usb_output_report_id) {
        return 1U;
      }
      if (report.size() >= 49U && report[0] == dualsense_bt_output_report_id) {
        if (report.size() >= 78U) {
          const auto expected_crc = crc32(report.data(), report.size() - 4U, dualsense_crc_seed(dualsense_output_crc_seed));
          const auto actual_crc = static_cast<std::uint32_t>(report[report.size() - 4U]) |
                                  (static_cast<std::uint32_t>(report[report.size() - 3U]) << 8U) |
                                  (static_cast<std::uint32_t>(report[report.size() - 2U]) << 16U) |
                                  (static_cast<std::uint32_t>(report[report.size() - 1U]) << 24U);
          if (actual_crc != expected_crc) {
            return std::nullopt;
          }
        }

        const auto enable_hid = (report[1] & 0x02U) != 0;
        if (!enable_hid && report.size() < 50U) {
          return std::nullopt;
        }
        return enable_hid ? 2U : 3U;
      }
      return std::nullopt;
    }

    void append_dualsense_outputs(
      const std::vector<std::uint8_t> &report,
      std::size_t offset,
      std::vector<GamepadOutput> &outputs
    ) {
      const auto valid_flag0 = report[offset];
      const auto valid_flag1 = report[offset + 1U];
      const auto motor_right = report[offset + 2U];
      const auto motor_left = report[offset + 3U];
      const auto right_trigger_effect_type = report[offset + 10U];
      const auto left_trigger_effect_type = report[offset + 21U];
      const auto valid_flag2 = report[offset + 38U];

      if ((valid_flag0 & dualsense_flag0_rumble) != 0 || (valid_flag2 & dualsense_flag2_compatible_vibration) != 0) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rumble;
        output.low_frequency_rumble = scale_output_byte(motor_left);
        output.high_frequency_rumble = scale_output_byte(motor_right);
        output.raw_report = report;
        outputs.push_back(std::move(output));
      } else if (valid_flag0 == 0 && valid_flag1 == 0 && valid_flag2 == 0) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rumble;
        output.raw_report = report;
        outputs.push_back(std::move(output));
      }

      if ((valid_flag1 & dualsense_flag1_lightbar) != 0) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::rgb_led;
        output.red = report[offset + 44U];
        output.green = report[offset + 45U];
        output.blue = report[offset + 46U];
        output.raw_report = report;
        outputs.push_back(std::move(output));
      }

      const auto trigger_flags = static_cast<std::uint8_t>(
        valid_flag0 & (dualsense_flag0_left_trigger | dualsense_flag0_right_trigger)
      );
      if (trigger_flags != 0) {
        GamepadOutput output;
        output.kind = GamepadOutputKind::adaptive_triggers;
        output.adaptive_trigger_flags = trigger_flags;
        output.left_trigger_effect_type = left_trigger_effect_type;
        output.right_trigger_effect_type = right_trigger_effect_type;
        std::copy_n(report.begin() + static_cast<std::ptrdiff_t>(offset + 11U), output.right_trigger_effect.size(), output.right_trigger_effect.begin());
        std::copy_n(report.begin() + static_cast<std::ptrdiff_t>(offset + 22U), output.left_trigger_effect.size(), output.left_trigger_effect.begin());
        output.raw_report = report;
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

  std::vector<std::uint8_t> pack_input_report(const DeviceProfile &profile, const GamepadState &state) {
    if (profile.device_type == DeviceType::gamepad && profile.gamepad_kind == GamepadProfileKind::dualsense) {
      return pack_dualsense_input_report(profile, state);
    }

    constexpr std::size_t common_report_size = 14;
    if (profile.device_type != DeviceType::gamepad || profile.input_report_size < common_report_size) {
      return {};
    }

    const auto normalized = normalize_state(state);

    std::vector<std::uint8_t> report;
    report.reserve(common_report_size);
    report.push_back(profile.report_id);
    append_u16(report, report_button_bits(normalized.buttons));
    report.push_back(hat_from_buttons(normalized.buttons));
    append_i16(report, normalize_axis(normalized.left_stick.x));
    append_i16(report, normalize_axis(normalized.left_stick.y));
    append_i16(report, normalize_axis(normalized.right_stick.x));
    append_i16(report, normalize_axis(normalized.right_stick.y));
    report.push_back(normalize_trigger(normalized.left_trigger));
    report.push_back(normalize_trigger(normalized.right_trigger));

    report.resize(profile.input_report_size, 0);
    return report;
  }

  GamepadOutput parse_output_report(const DeviceProfile &profile, const std::vector<std::uint8_t> &report) {
    const auto outputs = parse_output_reports(profile, report);
    if (!outputs.empty()) {
      return outputs.front();
    }

    GamepadOutput output;
    output.raw_report = report;
    return output;
  }

  std::vector<GamepadOutput> parse_output_reports(const DeviceProfile &profile, const std::vector<std::uint8_t> &report) {
    std::vector<GamepadOutput> outputs;

    if (profile.gamepad_kind == GamepadProfileKind::dualsense) {
      if (const auto offset = dualsense_common_output_offset(report)) {
        append_dualsense_outputs(report, *offset, outputs);
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
