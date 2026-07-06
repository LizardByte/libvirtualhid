/**
 * @file tools/virtualhid_control_model.cpp
 * @brief Testable model helper definitions for the native libvirtualhid control UI.
 */

// standard includes
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

// local includes
#include "virtualhid_control_model.hpp"

namespace lvh::tools::virtualhid_control {
  namespace {

    void append_summary_separator(std::wostringstream &stream, bool wrote) {
      if (wrote) {
        stream << L" | ";
      }
    }

  }  // namespace

  std::wstring device_type_name(DeviceType type) {
    switch (type) {
      using enum DeviceType;

      case gamepad:
        return L"gamepad";
      case keyboard:
        return L"keyboard";
      case mouse:
        return L"mouse";
      case touchscreen:
        return L"touchscreen";
      case trackpad:
        return L"trackpad";
      case pen_tablet:
        return L"pen tablet";
    }
    return L"unknown";
  }

  std::wstring node_kind_name(DeviceNodeKind kind) {
    switch (kind) {
      using enum DeviceNodeKind;

      case input_event:
        return L"input";
      case joystick:
        return L"joystick";
      case hidraw:
        return L"hidraw";
      case sysfs:
        return L"sysfs";
      case other:
        return L"other";
    }
    return L"other";
  }

  std::wstring output_kind_name(GamepadOutputKind kind) {
    switch (kind) {
      using enum GamepadOutputKind;

      case rumble:
        return L"rumble";
      case rgb_led:
        return L"rgb led";
      case adaptive_triggers:
        return L"adaptive triggers";
      case raw_report:
        return L"raw report";
      case trigger_rumble:
        return L"trigger rumble";
    }
    return L"raw report";
  }

  std::wstring battery_state_name(GamepadBatteryState state) {
    switch (state) {
      using enum GamepadBatteryState;

      case unknown:
        return L"unknown";
      case discharging:
        return L"discharging";
      case charging:
        return L"charging";
      case full:
        return L"full";
      case voltage_or_temperature_error:
        return L"voltage/temperature error";
      case temperature_error:
        return L"temperature error";
      case charging_error:
        return L"charging error";
    }
    return L"unknown";
  }

  int battery_choice_index(GamepadBatteryState state) {
    for (std::size_t index = 0; index < battery_choices.size(); ++index) {
      if (battery_choices[index].state == state) {
        return static_cast<int>(index);
      }
    }
    return 0;
  }

  std::wstring raw_hex(const std::vector<std::uint8_t> &bytes) {
    std::wostringstream stream;
    stream << std::hex << std::setfill(L'0');
    for (const auto value : bytes) {
      stream << std::setw(2) << static_cast<unsigned>(value);
    }
    return stream.str();
  }

  std::optional<DeviceProfile> profile_for_choice(const ProfileChoice &choice) {
    switch (choice.kind) {
      using enum GamepadProfileKind;

      case generic:
        return profiles::generic_gamepad();
      case xbox_360:
        return profiles::xbox_360();
      case xbox_one:
        return profiles::xbox_one();
      case xbox_series:
        return profiles::xbox_series();
      case dualshock4:
        return profiles::dualshock4();
      case dualsense:
        return profiles::dualsense();
      case switch_pro:
        return profiles::switch_pro();
    }
    return std::nullopt;
  }

  int axis_to_slider(float value) {
    return static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * static_cast<float>(slider_scale)));
  }

  int trigger_to_slider(float value) {
    return static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * static_cast<float>(slider_scale)));
  }

  float slider_to_float(long value) {
    return static_cast<float>(value) / static_cast<float>(slider_scale);
  }

  std::wstring yes_no(bool value) {
    return value ? L"yes" : L"no";
  }

  bool supports_normalized_feedback(const DeviceProfile &profile) {
    using enum GamepadOutputKind;

    return supports_gamepad_output(profile, rumble) ||
           supports_gamepad_output(profile, rgb_led) ||
           supports_gamepad_output(profile, adaptive_triggers) ||
           supports_gamepad_output(profile, trigger_rumble);
  }

  std::wstring profile_feature_summary(const DeviceProfile &profile) {
    using enum GamepadOutputKind;

    const auto support = gamepad_profile_support(profile);
    std::wostringstream stream;
    stream << L"Features: battery " << yes_no(support.supports_battery);
    stream << L" | rumble " << yes_no(supports_gamepad_output(profile, rumble));
    stream << L" | trigger rumble " << yes_no(supports_gamepad_output(profile, trigger_rumble));
    stream << L" | RGB LED " << yes_no(supports_gamepad_output(profile, rgb_led));
    stream << L" | adaptive triggers " << yes_no(supports_gamepad_output(profile, adaptive_triggers));
    stream << L" | raw output " << yes_no(supports_gamepad_output(profile, raw_report));
    return stream.str();
  }

  bool append_latest_output_summary(std::wostringstream &stream, const OutputState &state) {
    auto wrote = false;
    if (state.latest_rumble) {
      stream << L"rumble low=" << state.latest_rumble->low_frequency_rumble
             << L" high=" << state.latest_rumble->high_frequency_rumble;
      wrote = true;
    }
    if (state.latest_trigger_rumble) {
      append_summary_separator(stream, wrote);
      stream << L"trigger rumble L=" << state.latest_trigger_rumble->left_trigger_rumble
             << L" R=" << state.latest_trigger_rumble->right_trigger_rumble;
      wrote = true;
    }
    if (state.latest_rgb_led) {
      append_summary_separator(stream, wrote);
      stream << L"RGB " << static_cast<unsigned>(state.latest_rgb_led->red) << L","
             << static_cast<unsigned>(state.latest_rgb_led->green) << L","
             << static_cast<unsigned>(state.latest_rgb_led->blue);
      wrote = true;
    }
    if (state.latest_adaptive_triggers) {
      append_summary_separator(stream, wrote);
      stream << L"adaptive flags=" << static_cast<unsigned>(state.latest_adaptive_triggers->adaptive_trigger_flags);
      wrote = true;
    }
    if (!wrote && state.latest_raw_report) {
      stream << L"raw report";
      wrote = true;
    }
    return wrote;
  }

  std::wstring output_summary(const OutputState &state, const DeviceProfile &profile) {
    std::wostringstream stream;
    stream << L"Output: ";
    if (state.outputs.empty()) {
      stream << L"no reports received";
    } else if (!append_latest_output_summary(stream, state)) {
      stream << L"reports received";
    }

    if (!supports_normalized_feedback(profile)) {
      stream << L" | profile has no normalized feedback categories";
    }
    return stream.str();
  }

  bool update_visible_controls_for_profile(
    const DeviceProfile &profile,
    std::array<bool, button_choices.size()> &visible_buttons,
    bool &battery_controls_visible
  ) {
    auto changed = false;
    for (std::size_t index = 0; index < button_choices.size(); ++index) {
      const auto visible = supports_gamepad_button(profile, button_choices[index].button);
      changed = changed || visible_buttons[index] != visible;
      visible_buttons[index] = visible;
    }
    changed = changed || battery_controls_visible != profile.capabilities.supports_battery;
    battery_controls_visible = profile.capabilities.supports_battery;
    return changed;
  }

  void record_output(
    OutputState &state,
    const GamepadOutput &output,
    std::uint64_t &next_sequence,
    std::size_t max_output_events
  ) {
    state.outputs.push_back({.sequence = next_sequence++, .output = output});
    if (state.outputs.size() > max_output_events) {
      state.outputs.erase(
        state.outputs.begin(),
        state.outputs.begin() + static_cast<std::ptrdiff_t>(state.outputs.size() - max_output_events)
      );
    }

    switch (output.kind) {
      using enum GamepadOutputKind;

      case rumble:
        state.latest_rumble = output;
        break;
      case trigger_rumble:
        state.latest_trigger_rumble = output;
        break;
      case rgb_led:
        state.latest_rgb_led = output;
        break;
      case adaptive_triggers:
        state.latest_adaptive_triggers = output;
        break;
      case raw_report:
        state.latest_raw_report = output;
        break;
    }
  }

}  // namespace lvh::tools::virtualhid_control
