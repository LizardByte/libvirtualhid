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

  lvh::GamepadOutput rumble;
  rumble.kind = lvh::GamepadOutputKind::rumble;
  rumble.low_frequency_rumble = 0x4000;
  rumble.high_frequency_rumble = 0x2000;
  adapter.dispatch_output(rumble);
  adapter.close();

  return 0;
}
