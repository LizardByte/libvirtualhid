#pragma once

#include <libvirtualhid/types.hpp>

#include <optional>
#include <vector>

namespace lvh::profiles {

DeviceProfile generic_gamepad();
DeviceProfile xbox_360();
DeviceProfile xbox_one();
DeviceProfile xbox_series();
DeviceProfile dualsense();
DeviceProfile switch_pro();

std::optional<DeviceProfile> gamepad_profile(GamepadProfileKind kind);
std::vector<DeviceProfile> built_in_gamepad_profiles();

}  // namespace lvh::profiles
