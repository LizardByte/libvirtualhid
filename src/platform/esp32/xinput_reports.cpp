/**
 * @file src/platform/esp32/xinput_reports.cpp
 * @brief Xbox 360 wired (XUSB) report packing for the ESP32-S3 bridge.
 */

// standard includes
#include <utility>

// local includes
#include "platform/esp32/bytes.hpp"
#include "platform/esp32/xinput_reports.hpp"

#include <libvirtualhid/report.hpp>

namespace lvh::detail::esp32 {

  std::array<std::uint8_t, LVHB_XINPUT_REPORT_SIZE> pack_xinput_report(const GamepadState &state) {
    const auto normalized = reports::normalize_state(state);
    const auto &buttons = normalized.buttons;
    const auto bit = [&buttons](GamepadButton button, std::uint8_t mask) {
      return buttons.test(button) ? mask : std::uint8_t {0};
    };

    std::array<std::uint8_t, LVHB_XINPUT_REPORT_SIZE> report {};
    report[0] = 0x00;
    report[1] = LVHB_XINPUT_REPORT_SIZE;
    report[2] = static_cast<std::uint8_t>(
      bit(GamepadButton::dpad_up, 0x01) | bit(GamepadButton::dpad_down, 0x02) | bit(GamepadButton::dpad_left, 0x04) |
      bit(GamepadButton::dpad_right, 0x08) | bit(GamepadButton::start, 0x10) | bit(GamepadButton::back, 0x20) |
      bit(GamepadButton::left_stick, 0x40) | bit(GamepadButton::right_stick, 0x80)
    );
    report[3] = static_cast<std::uint8_t>(
      bit(GamepadButton::left_shoulder, 0x01) | bit(GamepadButton::right_shoulder, 0x02) | bit(GamepadButton::guide, 0x04) |
      bit(GamepadButton::a, 0x10) | bit(GamepadButton::b, 0x20) | bit(GamepadButton::x, 0x40) | bit(GamepadButton::y, 0x80)
    );
    report[4] = reports::normalize_trigger(normalized.left_trigger);
    report[5] = reports::normalize_trigger(normalized.right_trigger);
    // XInput Y axes point up, matching libvirtualhid's stick convention.
    const std::array<float, 4> axes {normalized.left_stick.x, normalized.left_stick.y, normalized.right_stick.x, normalized.right_stick.y};
    for (std::size_t index = 0; index < axes.size(); ++index) {
      const auto value = static_cast<std::uint16_t>(reports::normalize_axis(axes[index]));
      write_le(std::span<std::uint8_t> {report}, 6U + index * 2U, value);
    }
    return report;
  }

  std::vector<GamepadOutput> parse_xinput_output(std::span<const std::uint8_t> report) {
    std::vector<GamepadOutput> outputs;
    if (report.size() >= 5U && report[0] == 0x00U && report[1] >= 0x05U) {
      GamepadOutput output;
      output.kind = GamepadOutputKind::rumble;
      output.low_frequency_rumble = static_cast<std::uint16_t>(report[3] * 257U);
      output.high_frequency_rumble = static_cast<std::uint16_t>(report[4] * 257U);
      output.raw_report.assign(report.begin(), report.end());
      outputs.push_back(std::move(output));
    } else if (report.size() >= 3U && report[0] == 0x01U) {
      // XUSB LED patterns: 0x01 blink all, 0x02-0x05 flash then hold player N, 0x06-0x09 hold player N.
      GamepadOutput output;
      output.kind = GamepadOutputKind::player_leds;
      const auto pattern = report[2];
      if (pattern == 0x01U) {
        output.player_leds.fill(true);
        output.flashing_player_leds.fill(true);
      } else if (pattern >= 0x02U && pattern <= 0x05U) {
        output.player_leds[pattern - 0x02U] = true;
        output.flashing_player_leds[pattern - 0x02U] = true;
      } else if (pattern >= 0x06U && pattern <= 0x09U) {
        output.player_leds[pattern - 0x06U] = true;
      }
      output.raw_report.assign(report.begin(), report.end());
      outputs.push_back(std::move(output));
    }
    return outputs;
  }

}  // namespace lvh::detail::esp32
