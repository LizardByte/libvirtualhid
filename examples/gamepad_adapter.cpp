/**
 * @file examples/gamepad_adapter.cpp
 * @brief Minimal gamepad adapter example.
 */

// standard includes
#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

// local includes
#include <libvirtualhid/libvirtualhid.hpp>

namespace {
  using namespace std::chrono_literals;

  std::optional<lvh::DeviceProfile> profile_for_name(std::string_view name) {
    if (name == "generic") {
      return lvh::profiles::generic_gamepad();
    }
    if (name == "x360") {
      return lvh::profiles::xbox_360();
    }
    if (name == "xone") {
      return lvh::profiles::xbox_one();
    }
    if (name == "xseries") {
      return lvh::profiles::xbox_series();
    }
    if (name == "ds4") {
      return lvh::profiles::dualshock4();
    }
    if (name == "ds5") {
      return lvh::profiles::dualsense();
    }
    if (name == "switch") {
      return lvh::profiles::switch_pro();
    }

    return std::nullopt;
  }

  lvh::ClientControllerType client_type_for_profile(lvh::GamepadProfileKind kind) {
    switch (kind) {
      using enum lvh::ClientControllerType;
      using enum lvh::GamepadProfileKind;

      case xbox_360:
      case xbox_one:
      case xbox_series:
        return xbox;
      case dualshock4:
      case dualsense:
        return playstation;
      case switch_pro:
        return nintendo;
      case generic:
        return unknown;
    }

    return lvh::ClientControllerType::unknown;
  }
}  // namespace

/**
 * @brief Run the minimal virtual gamepad adapter example.
 * @param argc Number of command-line arguments.
 * @param argv Command-line arguments; the first positional argument selects
 * the gamepad profile, and `--hold` keeps the device active for validation.
 * @return Zero on success; nonzero when setup or input submission fails.
 */
int main(int argc, char *argv[]) {
  auto profile_name = std::string_view {"ds5"};
  auto hold = false;
  auto hold_seconds = 60;
  auto index = 1;
  while (index < argc) {
    const auto argument = std::string_view {argv[index]};
    ++index;
    if (argument == "--hold") {
      hold = true;
    } else if (argument == "--hold-seconds" && index < argc) {
      hold = true;
      hold_seconds = std::stoi(argv[index]);
      ++index;
    } else {
      profile_name = argument;
    }
  }

  auto profile = profile_for_name(profile_name);
  if (!profile) {
    std::cerr << "Unknown profile: " << profile_name << '\n';
    return 1;
  }

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);

  lvh::CreateGamepadOptions options;
  options.profile = *profile;
  options.metadata.global_index = 0;
  options.metadata.client_relative_index = 0;
  options.metadata.client_type = client_type_for_profile(profile->gamepad_kind);
  options.metadata.has_motion_sensors = true;
  options.metadata.has_touchpad = true;
  options.metadata.has_rgb_led = true;
  options.metadata.has_battery = true;
  options.metadata.stable_id = "remote-client-0";

  auto created = lvh::GamepadStateAdapter::create(*runtime, options);
  if (!created) {
    std::cerr << created.status.message() << '\n';
    return 1;
  }

  auto &adapter = *created.adapter;
  adapter.set_output_callback([](const lvh::GamepadOutput &output) {
    if (output.kind == lvh::GamepadOutputKind::rumble) {
      std::cout << "rumble " << output.low_frequency_rumble << ' '
                << output.high_frequency_rumble << '\n';
    }
  });

  if (const auto status = adapter.set_button(lvh::GamepadButton::a, true); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  adapter.set_left_stick({0.25F, -0.5F});
  adapter.set_right_trigger(1.0F);

  if (hold) {
    std::cout << "Holding " << profile->name << " for " << hold_seconds << " seconds\n";
    for (auto step = 0; step < hold_seconds * 5; ++step) {
      const auto direction = step % 40 < 20 ? 1.0F : -1.0F;
      const auto sweep = static_cast<float>(step % 20) / 19.0F;
      adapter.set_left_stick({direction, direction * 0.5F});
      adapter.set_right_stick({-direction * 0.5F, -direction});
      adapter.set_left_trigger(sweep);
      adapter.set_right_trigger(1.0F - sweep);
      adapter.set_button(lvh::GamepadButton::a, step % 20 < 10);
      std::this_thread::sleep_for(200ms);
    }
  }

  lvh::GamepadOutput rumble;
  rumble.kind = lvh::GamepadOutputKind::rumble;
  rumble.low_frequency_rumble = 0x4000;
  rumble.high_frequency_rumble = 0x2000;
  adapter.dispatch_output(rumble);
  adapter.close();

  return 0;
}
