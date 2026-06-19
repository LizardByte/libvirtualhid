/**
 * @file src/include/libvirtualhid/profiles.hpp
 * @brief Built-in virtual device profile declarations.
 */
#pragma once

// standard includes
#include <optional>
#include <vector>

// local includes
#include <libvirtualhid/types.hpp>

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
   * @return Default DualSense-compatible device profile.
   */
  DeviceProfile dualsense();

  /**
   * @brief Create the USB PlayStation DualSense-compatible gamepad profile.
   *
   * @return USB DualSense-compatible device profile.
   */
  DeviceProfile dualsense_usb();

  /**
   * @brief Create the Bluetooth PlayStation DualSense-compatible gamepad profile.
   *
   * @return Bluetooth DualSense-compatible device profile.
   */
  DeviceProfile dualsense_bluetooth();

  /**
   * @brief Create the Nintendo Switch Pro-compatible gamepad profile.
   *
   * @return Switch Pro-compatible device profile.
   */
  DeviceProfile switch_pro();

  /**
   * @brief Create the generic keyboard profile.
   *
   * @return Generic keyboard device profile.
   */
  DeviceProfile keyboard();

  /**
   * @brief Create the generic mouse profile.
   *
   * @return Generic mouse device profile.
   */
  DeviceProfile mouse();

  /**
   * @brief Create the generic touchscreen profile.
   *
   * @return Generic touchscreen device profile.
   */
  DeviceProfile touchscreen();

  /**
   * @brief Create the generic trackpad profile.
   *
   * @return Generic trackpad device profile.
   */
  DeviceProfile trackpad();

  /**
   * @brief Create the generic pen tablet profile.
   *
   * @return Generic pen tablet device profile.
   */
  DeviceProfile pen_tablet();

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
