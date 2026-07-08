/**
 * @file src/core/gamepad_adapter.cpp
 * @brief Platform-neutral gamepad adapter helper definitions.
 */

// standard includes
#include <algorithm>
#include <array>
#include <string>
#include <utility>

// local includes
#include <libvirtualhid/gamepad_adapter.hpp>

namespace lvh {
  namespace {

    using enum GamepadButton;

    constexpr std::array common_buttons {
      a,
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
    };

    bool is_common_button(GamepadButton button) {
      return std::find(common_buttons.begin(), common_buttons.end(), button) != common_buttons.end();
    }

    bool supports_common_misc1_button(GamepadProfileKind kind) {
      switch (kind) {
        using enum GamepadProfileKind;

        case generic:
        case xbox_360:
        case xbox_one:
        case xbox_series:
        case dualsense:
        case switch_pro:
          return true;
        case dualshock4:
          return false;
      }

      return false;
    }

    OperationStatus missing_gamepad() {
      return OperationStatus::failure(ErrorCode::device_closed, "gamepad adapter has no owned gamepad");
    }

    OperationStatus unsupported_feature(std::string feature) {
      return OperationStatus::failure(ErrorCode::unsupported_profile, std::move(feature));
    }

    OperationStatus validate_gamepad(const Gamepad *gamepad) {
      if (!gamepad) {
        return missing_gamepad();
      }
      return OperationStatus::success();
    }

  }  // namespace

  GamepadProfileSupport gamepad_profile_support(const DeviceProfile &profile) {
    GamepadProfileSupport support;
    if (profile.device_type != DeviceType::gamepad) {
      return support;
    }

    support.supports_rumble = profile.capabilities.supports_rumble;
    support.supports_rgb_led = profile.capabilities.supports_rgb_led;
    support.supports_adaptive_triggers = profile.capabilities.supports_adaptive_triggers;
    support.supports_motion = profile.capabilities.supports_motion;
    support.supports_touchpad = profile.capabilities.supports_touchpad;
    support.supports_battery = profile.capabilities.supports_battery;
    support.supports_misc1_button = supports_common_misc1_button(profile.gamepad_kind);
    support.supports_touchpad_button = profile.capabilities.supports_touchpad;

    return support;
  }

  bool supports_gamepad_button(const DeviceProfile &profile, GamepadButton button) {
    using enum GamepadButton;

    if (profile.device_type != DeviceType::gamepad) {
      return false;
    }

    const auto support = gamepad_profile_support(profile);
    if (is_common_button(button)) {
      return true;
    }
    if (button == misc1) {
      return support.supports_misc1_button;
    }
    if (button == touchpad) {
      return support.supports_touchpad_button;
    }

    const auto paddle_count = support.supported_rear_paddle_count;
    switch (button) {
      case paddle1:
        return paddle_count >= 1U;
      case paddle2:
        return paddle_count >= 2U;
      case paddle3:
        return paddle_count >= 3U;
      case paddle4:
        return paddle_count >= 4U;
      default:
        return false;
    }
  }

  bool supports_gamepad_output(const DeviceProfile &profile, GamepadOutputKind output_kind) {
    using enum GamepadOutputKind;

    if (profile.device_type != DeviceType::gamepad) {
      return false;
    }

    const auto support = gamepad_profile_support(profile);
    switch (output_kind) {
      case rumble:
        return support.supports_rumble;
      case trigger_rumble:
        return support.supports_trigger_rumble;
      case rgb_led:
        return support.supports_rgb_led;
      case adaptive_triggers:
        return support.supports_adaptive_triggers;
      case raw_report:
        return profile.output_report_size > 0U;
    }

    return false;
  }

  GamepadStateAdapter::GamepadStateAdapter(std::unique_ptr<Gamepad> gamepad):
      gamepad_ {std::move(gamepad)} {
    if (gamepad_) {
      support_ = gamepad_profile_support(gamepad_->profile());
    }
  }

  GamepadStateAdapter::GamepadStateAdapter(GamepadStateAdapter &&) noexcept = default;
  GamepadStateAdapter &GamepadStateAdapter::operator=(GamepadStateAdapter &&) noexcept = default;

  GamepadStateAdapter::~GamepadStateAdapter() {
    if (gamepad_) {
      static_cast<void>(gamepad_->close());
    }
  }

  GamepadAdapterCreationResult GamepadStateAdapter::create(Runtime &runtime, const CreateGamepadOptions &options) {
    auto created = runtime.create_gamepad(options);
    if (!created) {
      return {std::move(created.status), nullptr};
    }

    auto adapter = std::make_unique<GamepadStateAdapter>(std::move(created.gamepad));
    if (const auto status = adapter->submit(); !status.ok()) {
      static_cast<void>(adapter->close());
      return {status, nullptr};
    }

    return {OperationStatus::success(), std::move(adapter)};
  }

  Gamepad *GamepadStateAdapter::gamepad() {
    return gamepad_.get();
  }

  const Gamepad *GamepadStateAdapter::gamepad() const {
    return gamepad_.get();
  }

  const GamepadProfileSupport &GamepadStateAdapter::support() const {
    return support_;
  }

  const GamepadState &GamepadStateAdapter::state() const {
    return state_;
  }

  bool GamepadStateAdapter::is_open() const {
    return gamepad_ && gamepad_->is_open();
  }

  OperationStatus GamepadStateAdapter::submit() {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    return gamepad_->submit(state_);
  }

  OperationStatus GamepadStateAdapter::set_state(const GamepadState &state) {
    state_ = state;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_button(GamepadButton button, bool pressed) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!supports_gamepad_button(gamepad_->profile(), button)) {
      return unsupported_feature("selected gamepad profile cannot expose the requested button");
    }

    state_.buttons.set(button, pressed);
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_left_stick(Stick stick) {
    state_.left_stick = stick;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_right_stick(Stick stick) {
    state_.right_stick = stick;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_left_trigger(float value) {
    state_.left_trigger = value;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_right_trigger(float value) {
    state_.right_trigger = value;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_acceleration(std::optional<Vector3> acceleration) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_motion) {
      return unsupported_feature("selected gamepad profile cannot expose motion sensor input");
    }

    state_.acceleration = acceleration;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_gyroscope(std::optional<Vector3> gyroscope) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_motion) {
      return unsupported_feature("selected gamepad profile cannot expose motion sensor input");
    }

    state_.gyroscope = gyroscope;
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_motion(Vector3 acceleration, Vector3 gyroscope) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_motion) {
      return unsupported_feature("selected gamepad profile cannot expose motion sensor input");
    }

    state_.acceleration = acceleration;
    state_.gyroscope = gyroscope;
    return submit();
  }

  OperationStatus GamepadStateAdapter::clear_motion() {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_motion) {
      return unsupported_feature("selected gamepad profile cannot expose motion sensor input");
    }

    state_.acceleration.reset();
    state_.gyroscope.reset();
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_battery(GamepadBattery battery) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_battery) {
      return unsupported_feature("selected gamepad profile cannot expose battery input");
    }

    state_.battery = battery;
    return submit();
  }

  OperationStatus GamepadStateAdapter::clear_battery() {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_battery) {
      return unsupported_feature("selected gamepad profile cannot expose battery input");
    }

    state_.battery.reset();
    return submit();
  }

  OperationStatus GamepadStateAdapter::set_touchpad_contact(std::size_t index, GamepadTouchContact contact) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_touchpad) {
      return unsupported_feature("selected gamepad profile cannot expose touchpad input");
    }
    if (index >= state_.touchpad_contacts.size()) {
      return OperationStatus::failure(ErrorCode::invalid_argument, "touchpad contact index is out of range");
    }

    state_.touchpad_contacts[index] = contact;
    return submit();
  }

  OperationStatus GamepadStateAdapter::clear_touchpad_contact(std::size_t index) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    if (!support_.supports_touchpad) {
      return unsupported_feature("selected gamepad profile cannot expose touchpad input");
    }
    if (index >= state_.touchpad_contacts.size()) {
      return OperationStatus::failure(ErrorCode::invalid_argument, "touchpad contact index is out of range");
    }

    state_.touchpad_contacts[index] = {};
    return submit();
  }

  void GamepadStateAdapter::set_output_callback(const OutputCallback &callback) {
    if (gamepad_) {
      gamepad_->set_output_callback(callback);
    }
  }

  OperationStatus GamepadStateAdapter::dispatch_output(const GamepadOutput &output) {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    return gamepad_->dispatch_output(output);
  }

  OperationStatus GamepadStateAdapter::close() {
    if (const auto status = validate_gamepad(gamepad_.get()); !status.ok()) {
      return status;
    }
    return gamepad_->close();
  }

}  // namespace lvh
