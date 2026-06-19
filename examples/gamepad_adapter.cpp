/**
 * @file examples/gamepad_adapter.cpp
 * @brief Minimal gamepad adapter example.
 */

// standard includes
#include <iostream>

// local includes
#include <libvirtualhid/libvirtualhid.hpp>

int main() {
  auto runtime = lvh::Runtime::create();

  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::dualsense();
  options.metadata.global_index = 0;
  options.metadata.client_relative_index = 0;
  options.metadata.client_type = lvh::ClientControllerType::playstation;
  options.metadata.has_motion_sensors = true;
  options.metadata.has_touchpad = true;
  options.metadata.has_rgb_led = true;
  options.metadata.has_battery = true;
  options.metadata.stable_id = "remote-client-0";

  auto created = runtime->create_gamepad(options);
  if (!created) {
    std::cerr << created.status.message() << '\n';
    return 1;
  }

  created.gamepad->set_output_callback([](const lvh::GamepadOutput &output) {
    if (output.kind == lvh::GamepadOutputKind::rumble) {
      std::cout << "rumble " << output.low_frequency_rumble << ' '
                << output.high_frequency_rumble << '\n';
    }
  });

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.left_stick = {0.25F, -0.5F};
  state.right_trigger = 1.0F;

  if (const auto status = created.gamepad->submit(state); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }

  lvh::GamepadOutput rumble;
  rumble.kind = lvh::GamepadOutputKind::rumble;
  rumble.low_frequency_rumble = 0x4000;
  rumble.high_frequency_rumble = 0x2000;
  created.gamepad->dispatch_output(rumble);
  created.gamepad->close();

  return 0;
}
