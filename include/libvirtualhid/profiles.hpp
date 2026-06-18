#pragma once

#include <libvirtualhid/types.hpp>

#include <optional>
#include <vector>

namespace lvh::profiles {

/**
 * @brief Create the generic HID gamepad profile.
 *
 * @return Generic gamepad device profile.
 */
DeviceProfile generic_gamepad();

/**
 * @brief Create the Xbox 360-compatible gamepad profile.
 *
 * @return Xbox 360-compatible device profile.
 */
DeviceProfile xbox_360();

/**
 * @brief Create the Xbox One-compatible gamepad profile.
 *
 * @return Xbox One-compatible device profile.
 */
DeviceProfile xbox_one();

/**
 * @brief Create the Xbox Series-compatible gamepad profile.
 *
 * @return Xbox Series-compatible device profile.
 */
DeviceProfile xbox_series();

/**
 * @brief Create the PlayStation DualSense-compatible gamepad profile.
 *
 * @return DualSense-compatible device profile.
 */
DeviceProfile dualsense();

/**
 * @brief Create the Nintendo Switch Pro-compatible gamepad profile.
 *
 * @return Switch Pro-compatible device profile.
 */
DeviceProfile switch_pro();

/**
 * @brief Look up a built-in gamepad profile by kind.
 *
 * @param kind Built-in gamepad profile kind.
 * @return Matching profile, or `std::nullopt` when the kind is unknown.
 */
std::optional<DeviceProfile> gamepad_profile(GamepadProfileKind kind);

/**
 * @brief Get every built-in gamepad profile.
 *
 * @return Built-in gamepad profiles.
 */
std::vector<DeviceProfile> built_in_gamepad_profiles();

}  // namespace lvh::profiles
