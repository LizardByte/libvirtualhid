#pragma once

#include <libvirtualhid/types.hpp>

#include <cstdint>
#include <vector>

namespace lvh::reports {

/**
 * @brief Clamp a stick axis value to the normalized range.
 *
 * @param value Axis value.
 * @return Clamped axis value in the inclusive range `[-1.0, 1.0]`.
 */
float clamp_axis(float value);

/**
 * @brief Clamp a trigger value to the normalized range.
 *
 * @param value Trigger value.
 * @return Clamped trigger value in the inclusive range `[0.0, 1.0]`.
 */
float clamp_trigger(float value);

/**
 * @brief Convert a normalized axis value to a signed HID axis value.
 *
 * @param value Axis value in the inclusive range `[-1.0, 1.0]`.
 * @return Signed 16-bit HID axis value.
 */
std::int16_t normalize_axis(float value);

/**
 * @brief Convert a normalized trigger value to an unsigned HID trigger value.
 *
 * @param value Trigger value in the inclusive range `[0.0, 1.0]`.
 * @return Unsigned 8-bit HID trigger value.
 */
std::uint8_t normalize_trigger(float value);

/**
 * @brief Normalize all scalar fields in a gamepad state.
 *
 * @param state Gamepad state to normalize.
 * @return Normalized gamepad state.
 */
GamepadState normalize_state(const GamepadState& state);

/**
 * @brief Convert directional pad buttons to a HID hat switch value.
 *
 * @param buttons Button set containing directional pad state.
 * @return HID hat switch value, or `8` for neutral.
 */
std::uint8_t hat_from_buttons(const ButtonSet& buttons);

/**
 * @brief Pack a gamepad state into the profile's common input report format.
 *
 * @param profile Device profile used for report identity and size.
 * @param state Gamepad state to pack.
 * @return Packed input report bytes.
 */
std::vector<std::uint8_t> pack_input_report(const DeviceProfile& profile, const GamepadState& state);

}  // namespace lvh::reports
