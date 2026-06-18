#include <libvirtualhid/report.hpp>

#include <algorithm>
#include <cmath>

namespace lvh::reports {
namespace {

constexpr std::uint8_t neutral_hat = 8;

void append_u16(std::vector<std::uint8_t>& report, std::uint16_t value) {
  report.push_back(static_cast<std::uint8_t>(value & 0xFFU));
  report.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_i16(std::vector<std::uint8_t>& report, std::int16_t value) {
  append_u16(report, static_cast<std::uint16_t>(value));
}

std::uint16_t report_button_bits(const ButtonSet& buttons) {
  std::uint16_t bits = 0;

  const auto add = [&bits, &buttons](GamepadButton button, std::uint16_t bit) {
    if(buttons.test(button)) {
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

}  // namespace

float clamp_axis(float value) {
  if(std::isnan(value)) {
    return 0.0F;
  }

  return std::clamp(value, -1.0F, 1.0F);
}

float clamp_trigger(float value) {
  if(std::isnan(value)) {
    return 0.0F;
  }

  return std::clamp(value, 0.0F, 1.0F);
}

std::int16_t normalize_axis(float value) {
  const auto clamped = clamp_axis(value);
  if(clamped <= -1.0F) {
    return -32768;
  }
  if(clamped >= 1.0F) {
    return 32767;
  }
  if(clamped < 0.0F) {
    return static_cast<std::int16_t>(std::lround(clamped * 32768.0F));
  }
  return static_cast<std::int16_t>(std::lround(clamped * 32767.0F));
}

std::uint8_t normalize_trigger(float value) {
  return static_cast<std::uint8_t>(std::lround(clamp_trigger(value) * 255.0F));
}

GamepadState normalize_state(const GamepadState& state) {
  auto normalized = state;
  normalized.left_stick.x = clamp_axis(state.left_stick.x);
  normalized.left_stick.y = clamp_axis(state.left_stick.y);
  normalized.right_stick.x = clamp_axis(state.right_stick.x);
  normalized.right_stick.y = clamp_axis(state.right_stick.y);
  normalized.left_trigger = clamp_trigger(state.left_trigger);
  normalized.right_trigger = clamp_trigger(state.right_trigger);
  return normalized;
}

std::uint8_t hat_from_buttons(const ButtonSet& buttons) {
  auto up = buttons.test(GamepadButton::dpad_up);
  auto down = buttons.test(GamepadButton::dpad_down);
  auto left = buttons.test(GamepadButton::dpad_left);
  auto right = buttons.test(GamepadButton::dpad_right);

  if(up && down) {
    up = false;
    down = false;
  }
  if(left && right) {
    left = false;
    right = false;
  }

  if(up && right) {
    return 1;
  }
  if(down && right) {
    return 3;
  }
  if(down && left) {
    return 5;
  }
  if(up && left) {
    return 7;
  }
  if(up) {
    return 0;
  }
  if(right) {
    return 2;
  }
  if(down) {
    return 4;
  }
  if(left) {
    return 6;
  }

  return neutral_hat;
}

std::vector<std::uint8_t> pack_input_report(const DeviceProfile& profile, const GamepadState& state) {
  constexpr std::size_t common_report_size = 14;
  if(profile.device_type != DeviceType::gamepad || profile.input_report_size < common_report_size) {
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

}  // namespace lvh::reports
