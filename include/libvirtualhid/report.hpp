#pragma once

#include <libvirtualhid/types.hpp>

#include <cstdint>
#include <vector>

namespace lvh::reports {

float clamp_axis(float value);
float clamp_trigger(float value);
std::int16_t normalize_axis(float value);
std::uint8_t normalize_trigger(float value);
GamepadState normalize_state(const GamepadState& state);
std::uint8_t hat_from_buttons(const ButtonSet& buttons);
std::vector<std::uint8_t> pack_input_report(const DeviceProfile& profile, const GamepadState& state);

}  // namespace lvh::reports
