#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lvh {

using DeviceId = std::uint64_t;

enum class ErrorCode {
  ok,
  invalid_argument,
  backend_unavailable,
  device_closed,
  unsupported_profile,
  backend_failure,
};

class Status {
public:
  Status();
  Status(ErrorCode code, std::string message);

  static Status success();
  static Status failure(ErrorCode code, std::string message);

  bool ok() const;
  ErrorCode code() const;
  const std::string& message() const;

private:
  ErrorCode code_;
  std::string message_;
};

enum class BackendKind {
  fake,
  platform_default,
};

struct RuntimeOptions {
  BackendKind backend = BackendKind::fake;
};

struct BackendCapabilities {
  std::string backend_name;
  bool supports_virtual_hid = false;
  bool supports_gamepad = false;
  bool supports_keyboard = false;
  bool supports_mouse = false;
  bool supports_output_reports = false;
  bool supports_xtest_fallback = false;
  bool requires_installed_driver = false;
};

enum class DeviceType {
  gamepad,
  keyboard,
  mouse,
};

enum class BusType {
  unknown,
  usb,
  bluetooth,
};

enum class GamepadProfileKind {
  generic,
  xbox_360,
  xbox_one,
  xbox_series,
  dualsense,
  switch_pro,
};

struct GamepadProfileCapabilities {
  bool supports_rumble = false;
  bool supports_motion = false;
  bool supports_touchpad = false;
  bool supports_rgb_led = false;
  bool supports_battery = false;
  bool supports_adaptive_triggers = false;
};

struct DeviceProfile {
  DeviceType device_type = DeviceType::gamepad;
  GamepadProfileKind gamepad_kind = GamepadProfileKind::generic;
  BusType bus_type = BusType::usb;
  std::uint16_t vendor_id = 0;
  std::uint16_t product_id = 0;
  std::uint16_t version = 0;
  std::uint8_t report_id = 1;
  std::size_t input_report_size = 0;
  std::string name;
  std::string manufacturer;
  GamepadProfileCapabilities capabilities;
  std::vector<std::uint8_t> report_descriptor;
};

enum class ClientControllerType {
  unknown,
  xbox,
  playstation,
  nintendo,
};

struct GamepadMetadata {
  int global_index = -1;
  int client_relative_index = -1;
  ClientControllerType client_type = ClientControllerType::unknown;
  bool has_motion_sensors = false;
  bool has_touchpad = false;
  bool has_rgb_led = false;
  bool has_battery = false;
  std::string stable_id;
};

struct CreateGamepadOptions {
  DeviceProfile profile;
  GamepadMetadata metadata;
};

enum class GamepadButton: std::uint8_t {
  a = 0,
  b,
  x,
  y,
  back,
  start,
  guide,
  left_stick,
  right_stick,
  left_shoulder,
  right_shoulder,
  dpad_up,
  dpad_down,
  dpad_left,
  dpad_right,
  misc1,
};

class ButtonSet {
public:
  void set(GamepadButton button, bool pressed = true);
  void reset(GamepadButton button);
  void clear();
  bool test(GamepadButton button) const;
  std::uint32_t raw_bits() const;

private:
  std::uint32_t bits_ = 0;
};

struct Stick {
  float x = 0.0F;
  float y = 0.0F;
};

struct GamepadState {
  ButtonSet buttons;
  Stick left_stick;
  Stick right_stick;
  float left_trigger = 0.0F;
  float right_trigger = 0.0F;
};

enum class GamepadOutputKind {
  rumble,
  rgb_led,
  adaptive_triggers,
  raw_report,
};

struct GamepadOutput {
  GamepadOutputKind kind = GamepadOutputKind::raw_report;
  std::uint16_t low_frequency_rumble = 0;
  std::uint16_t high_frequency_rumble = 0;
  std::uint8_t red = 0;
  std::uint8_t green = 0;
  std::uint8_t blue = 0;
  std::vector<std::uint8_t> raw_report;
};

using OutputCallback = std::function<void(const GamepadOutput&)>;

}  // namespace lvh
