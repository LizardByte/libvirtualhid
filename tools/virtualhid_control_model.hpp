/**
 * @file tools/virtualhid_control_model.hpp
 * @brief Testable model helpers for the native libvirtualhid control UI.
 */

#pragma once

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include <libvirtualhid/libvirtualhid.hpp>

namespace lvh::tools::virtualhid_control {

  inline constexpr auto slider_scale = 100;

  struct DeviceTypeChoice {
    std::wstring_view label;
    DeviceType type;
  };

  struct ProfileChoice {
    std::wstring_view id;
    std::wstring_view label;
    GamepadProfileKind kind;
    ClientControllerType client_type;
  };

  struct ButtonChoice {
    std::wstring_view label;
    GamepadButton button;
  };

  struct MouseButtonChoice {
    std::wstring_view label;
    MouseButton button;
  };

  enum class MouseControlAction {
    move_left,
    move_right,
    move_up,
    move_down,
    wheel_up,
    wheel_down,
    pan_left,
    pan_right,
  };

  struct AxisChoice {
    std::wstring_view label;
    int minimum;
    int maximum;
  };

  struct BatteryChoice {
    std::wstring_view label;
    GamepadBatteryState state;
  };

  struct OutputLogEntry {
    std::uint64_t sequence = 0;
    GamepadOutput output;
  };

  struct OutputState {
    std::vector<OutputLogEntry> outputs;
    std::optional<GamepadOutput> latest_rumble;
    std::optional<GamepadOutput> latest_trigger_rumble;
    std::optional<GamepadOutput> latest_rgb_led;
    std::optional<GamepadOutput> latest_player_led;
    std::optional<GamepadOutput> latest_mic_led;
    std::optional<GamepadOutput> latest_adaptive_triggers;
    std::optional<GamepadOutput> latest_raw_report;
  };

  inline constexpr std::array device_type_choices {
    DeviceTypeChoice {L"Gamepad", DeviceType::gamepad},
    DeviceTypeChoice {L"Mouse", DeviceType::mouse},
  };

  inline constexpr std::array profile_choices {
    ProfileChoice {L"generic", L"Generic HID", GamepadProfileKind::generic, ClientControllerType::unknown},
    ProfileChoice {L"x360", L"Xbox 360", GamepadProfileKind::xbox_360, ClientControllerType::xbox},
    ProfileChoice {L"xone", L"Xbox One", GamepadProfileKind::xbox_one, ClientControllerType::xbox},
    ProfileChoice {L"xseries", L"Xbox Series", GamepadProfileKind::xbox_series, ClientControllerType::xbox},
    ProfileChoice {L"ds4", L"DualShock 4", GamepadProfileKind::dualshock4, ClientControllerType::playstation},
    ProfileChoice {L"ds5", L"DualSense", GamepadProfileKind::dualsense, ClientControllerType::playstation},
    ProfileChoice {L"switch", L"Switch Pro", GamepadProfileKind::switch_pro, ClientControllerType::nintendo},
  };

  inline constexpr std::array button_choices {
    ButtonChoice {L"A", GamepadButton::a},
    ButtonChoice {L"B", GamepadButton::b},
    ButtonChoice {L"X", GamepadButton::x},
    ButtonChoice {L"Y", GamepadButton::y},
    ButtonChoice {L"Back", GamepadButton::back},
    ButtonChoice {L"Start", GamepadButton::start},
    ButtonChoice {L"Guide", GamepadButton::guide},
    ButtonChoice {L"L3", GamepadButton::left_stick},
    ButtonChoice {L"R3", GamepadButton::right_stick},
    ButtonChoice {L"LB", GamepadButton::left_shoulder},
    ButtonChoice {L"RB", GamepadButton::right_shoulder},
    ButtonChoice {L"D-pad Up", GamepadButton::dpad_up},
    ButtonChoice {L"D-pad Down", GamepadButton::dpad_down},
    ButtonChoice {L"D-pad Left", GamepadButton::dpad_left},
    ButtonChoice {L"D-pad Right", GamepadButton::dpad_right},
    ButtonChoice {L"Misc", GamepadButton::misc1},
    ButtonChoice {L"Touchpad", GamepadButton::touchpad},
    ButtonChoice {L"Paddle 1", GamepadButton::paddle1},
    ButtonChoice {L"Paddle 2", GamepadButton::paddle2},
    ButtonChoice {L"Paddle 3", GamepadButton::paddle3},
    ButtonChoice {L"Paddle 4", GamepadButton::paddle4},
  };

  inline constexpr std::array mouse_button_choices {
    MouseButtonChoice {L"Left", MouseButton::left},
    MouseButtonChoice {L"Middle", MouseButton::middle},
    MouseButtonChoice {L"Right", MouseButton::right},
    MouseButtonChoice {L"Side", MouseButton::side},
    MouseButtonChoice {L"Extra", MouseButton::extra},
  };

  inline constexpr std::array axis_choices {
    AxisChoice {L"Left X", -slider_scale, slider_scale},
    AxisChoice {L"Left Y", -slider_scale, slider_scale},
    AxisChoice {L"Right X", -slider_scale, slider_scale},
    AxisChoice {L"Right Y", -slider_scale, slider_scale},
    AxisChoice {L"Left Trigger", 0, slider_scale},
    AxisChoice {L"Right Trigger", 0, slider_scale},
  };

  inline constexpr std::array battery_choices {
    BatteryChoice {L"Unknown", GamepadBatteryState::unknown},
    BatteryChoice {L"Discharging", GamepadBatteryState::discharging},
    BatteryChoice {L"Charging", GamepadBatteryState::charging},
    BatteryChoice {L"Full", GamepadBatteryState::full},
    BatteryChoice {L"Voltage/temperature error", GamepadBatteryState::voltage_or_temperature_error},
    BatteryChoice {L"Temperature error", GamepadBatteryState::temperature_error},
    BatteryChoice {L"Charging error", GamepadBatteryState::charging_error},
  };

  std::wstring device_type_name(DeviceType type);
  std::wstring node_kind_name(DeviceNodeKind kind);
  std::wstring output_kind_name(GamepadOutputKind kind);
  std::wstring battery_state_name(GamepadBatteryState state);
  int battery_choice_index(GamepadBatteryState state);
  std::wstring raw_hex(const std::vector<std::uint8_t> &bytes);
  std::optional<DeviceProfile> profile_for_choice(const ProfileChoice &choice);
  MouseEvent mouse_event_for_action(
    MouseControlAction action,
    std::int32_t motion_step,
    std::int32_t scroll_step
  );
  MouseEvent mouse_button_event(MouseButton button, bool pressed);
  int axis_to_slider(float value);
  int trigger_to_slider(float value);
  float slider_to_float(long value);
  std::wstring yes_no(bool value);

  /**
   * @brief Trim surrounding whitespace from a pasted license key.
   *
   * @param license_key License-key text from the control UI input buffer.
   * @return A normalized key, or an empty string when the input has no key.
   */
  std::string normalized_license_key(std::string_view license_key);

  bool supports_normalized_feedback(const DeviceProfile &profile);
  std::wstring profile_feature_summary(const DeviceProfile &profile);
  std::wstring device_feature_summary(const DeviceProfile &profile);
  bool append_latest_output_summary(std::wostringstream &stream, const OutputState &state);
  std::wstring output_summary(const OutputState &state, const DeviceProfile &profile);
  bool update_visible_controls_for_profile(
    const DeviceProfile &profile,
    std::array<bool, button_choices.size()> &visible_buttons,
    bool &battery_controls_visible
  );
  void record_output(
    OutputState &state,
    const GamepadOutput &output,
    std::uint64_t &next_sequence,
    std::size_t max_output_events
  );

}  // namespace lvh::tools::virtualhid_control
