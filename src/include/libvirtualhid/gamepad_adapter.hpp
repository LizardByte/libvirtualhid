/**
 * @file src/include/libvirtualhid/gamepad_adapter.hpp
 * @brief Platform-neutral gamepad adapter helpers.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

// local includes
#include <libvirtualhid/runtime.hpp>
#include <libvirtualhid/types.hpp>

namespace lvh {

  /**
   * @brief Profile support summary for portable gamepad adapter code.
   */
  struct GamepadProfileSupport {
    /**
     * @brief Whether the profile supports main rumble output.
     */
    bool supports_rumble = false;

    /**
     * @brief Whether the profile supports independent trigger rumble output.
     */
    bool supports_trigger_rumble = false;

    /**
     * @brief Whether the profile supports RGB LED output.
     */
    bool supports_rgb_led = false;

    /**
     * @brief Whether the profile supports adaptive trigger output.
     */
    bool supports_adaptive_triggers = false;

    /**
     * @brief Whether the profile exposes motion sensor input.
     */
    bool supports_motion = false;

    /**
     * @brief Whether the profile exposes touchpad contact input.
     */
    bool supports_touchpad = false;

    /**
     * @brief Whether the profile exposes battery state input.
     */
    bool supports_battery = false;

    /**
     * @brief Whether the profile exposes the miscellaneous profile-specific button.
     */
    bool supports_misc1_button = false;

    /**
     * @brief Whether the profile exposes a touchpad click button.
     */
    bool supports_touchpad_button = false;

    /**
     * @brief Number of rear paddle buttons exposed by the profile.
     */
    std::uint8_t supported_rear_paddle_count = 0;
  };

  /**
   * @brief Get portable support flags for a gamepad profile.
   *
   * @param profile Gamepad profile to inspect.
   * @return Profile support summary.
   */
  GamepadProfileSupport gamepad_profile_support(const DeviceProfile &profile);

  /**
   * @brief Check whether a gamepad profile can expose a logical button.
   *
   * @param profile Gamepad profile to inspect.
   * @param button Logical gamepad button.
   * @return `true` when the selected profile can carry the button.
   */
  bool supports_gamepad_button(const DeviceProfile &profile, GamepadButton button);

  /**
   * @brief Check whether a gamepad profile can emit an output category.
   *
   * @param profile Gamepad profile to inspect.
   * @param output_kind Output category.
   * @return `true` when the selected profile can emit the output category.
   */
  bool supports_gamepad_output(const DeviceProfile &profile, GamepadOutputKind output_kind);

  struct GamepadAdapterCreationResult;

  /**
   * @brief Caches a full gamepad state and resubmits it after partial updates.
   */
  class GamepadStateAdapter final {
  public:
    /**
     * @brief Copy construction is disabled because the adapter owns the gamepad handle.
     */
    GamepadStateAdapter(const GamepadStateAdapter &) = delete;

    /**
     * @brief Copy assignment is disabled because the adapter owns the gamepad handle.
     *
     * @return This adapter.
     */
    GamepadStateAdapter &operator=(const GamepadStateAdapter &) = delete;

    /**
     * @brief Move construct an adapter.
     *
     * @param other Adapter to move from.
     */
    GamepadStateAdapter(GamepadStateAdapter &&other) noexcept;

    /**
     * @brief Move assign an adapter.
     *
     * @param other Adapter to move from.
     * @return This adapter.
     */
    GamepadStateAdapter &operator=(GamepadStateAdapter &&other) noexcept;

    /**
     * @brief Construct an adapter around a created gamepad handle.
     *
     * @param gamepad Gamepad handle owned by the adapter.
     */
    explicit GamepadStateAdapter(std::unique_ptr<Gamepad> gamepad);

    /**
     * @brief Destroy the adapter and close the owned gamepad if still open.
     */
    ~GamepadStateAdapter();

    /**
     * @brief Create a gamepad and wrap it in a state adapter.
     *
     * @param runtime Runtime used to create the gamepad.
     * @param options Gamepad creation options.
     * @return Adapter creation result.
     */
    static GamepadAdapterCreationResult create(Runtime &runtime, const CreateGamepadOptions &options);

    /**
     * @brief Get the owned gamepad handle.
     *
     * @return Owned gamepad handle, or `nullptr` after move.
     */
    Gamepad *gamepad();

    /**
     * @brief Get the owned gamepad handle.
     *
     * @return Owned gamepad handle, or `nullptr` after move.
     */
    const Gamepad *gamepad() const;

    /**
     * @brief Get profile support flags captured at creation time.
     *
     * @return Profile support summary.
     */
    const GamepadProfileSupport &support() const;

    /**
     * @brief Get the cached full gamepad state.
     *
     * @return Cached gamepad state.
     */
    const GamepadState &state() const;

    /**
     * @brief Check whether the owned gamepad is open.
     *
     * @return `true` when the owned gamepad can accept operations.
     */
    bool is_open() const;

    /**
     * @brief Submit the cached full gamepad state.
     *
     * @return Submit operation status.
     */
    OperationStatus submit();

    /**
     * @brief Replace and submit the cached full gamepad state.
     *
     * @param state New full gamepad state.
     * @return Submit operation status.
     */
    OperationStatus set_state(const GamepadState &state);

    /**
     * @brief Update one logical button and submit the full cached state.
     *
     * @param button Logical button to update.
     * @param pressed Whether the button is pressed.
     * @return Submit operation status.
     */
    OperationStatus set_button(GamepadButton button, bool pressed);

    /**
     * @brief Update the left stick and submit the full cached state.
     *
     * @param stick Left stick state.
     * @return Submit operation status.
     */
    OperationStatus set_left_stick(Stick stick);

    /**
     * @brief Update the right stick and submit the full cached state.
     *
     * @param stick Right stick state.
     * @return Submit operation status.
     */
    OperationStatus set_right_stick(Stick stick);

    /**
     * @brief Update the left trigger and submit the full cached state.
     *
     * @param value Normalized left trigger value.
     * @return Submit operation status.
     */
    OperationStatus set_left_trigger(float value);

    /**
     * @brief Update the right trigger and submit the full cached state.
     *
     * @param value Normalized right trigger value.
     * @return Submit operation status.
     */
    OperationStatus set_right_trigger(float value);

    /**
     * @brief Update accelerometer data and submit the full cached state.
     *
     * @param acceleration Accelerometer data, or `std::nullopt` to clear it.
     * @return Submit operation status.
     */
    OperationStatus set_acceleration(std::optional<Vector3> acceleration);

    /**
     * @brief Update gyroscope data and submit the full cached state.
     *
     * @param gyroscope Gyroscope data, or `std::nullopt` to clear it.
     * @return Submit operation status.
     */
    OperationStatus set_gyroscope(std::optional<Vector3> gyroscope);

    /**
     * @brief Update accelerometer and gyroscope data and submit the full cached state.
     *
     * @param acceleration Accelerometer data.
     * @param gyroscope Gyroscope data.
     * @return Submit operation status.
     */
    OperationStatus set_motion(Vector3 acceleration, Vector3 gyroscope);

    /**
     * @brief Clear motion data and submit the full cached state.
     *
     * @return Submit operation status.
     */
    OperationStatus clear_motion();

    /**
     * @brief Update battery metadata and submit the full cached state.
     *
     * @param battery Battery metadata.
     * @return Submit operation status.
     */
    OperationStatus set_battery(GamepadBattery battery);

    /**
     * @brief Clear battery metadata and submit the full cached state.
     *
     * @return Submit operation status.
     */
    OperationStatus clear_battery();

    /**
     * @brief Update one touchpad contact and submit the full cached state.
     *
     * @param index Touchpad contact slot.
     * @param contact Touchpad contact state.
     * @return Submit operation status.
     */
    OperationStatus set_touchpad_contact(std::size_t index, GamepadTouchContact contact);

    /**
     * @brief Clear one touchpad contact and submit the full cached state.
     *
     * @param index Touchpad contact slot.
     * @return Submit operation status.
     */
    OperationStatus clear_touchpad_contact(std::size_t index);

    /**
     * @brief Register a callback for backend output events.
     *
     * @param callback Output callback copied into the owned gamepad.
     */
    void set_output_callback(const OutputCallback &callback);

    /**
     * @brief Dispatch an output event to the owned gamepad callback.
     *
     * @param output Output event.
     * @return Dispatch operation status.
     */
    OperationStatus dispatch_output(const GamepadOutput &output);

    /**
     * @brief Close the owned gamepad.
     *
     * @return Close operation status.
     */
    OperationStatus close();

  private:
    std::unique_ptr<Gamepad> gamepad_;
    GamepadState state_;
    GamepadProfileSupport support_;
  };

  /**
   * @brief Result returned by gamepad adapter creation.
   */
  struct GamepadAdapterCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created adapter when creation succeeds.
     */
    std::unique_ptr<GamepadStateAdapter> adapter;

    /**
     * @brief Check whether creation succeeded and produced an adapter.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && adapter != nullptr;
    }
  };

}  // namespace lvh
