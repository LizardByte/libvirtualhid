/**
 * @file src/include/libvirtualhid/types.hpp
 * @brief Core public types for libvirtualhid.
 */
#pragma once

// standard includes
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

/**
 * @brief Public libvirtualhid API namespace.
 */
namespace lvh {

  /**
   * @brief Stable identifier assigned to a virtual device instance.
   */
  using DeviceId = std::uint64_t;

  /**
   * @brief Error categories returned by libvirtualhid operations.
   */
  enum class ErrorCode {
    ok,  ///< Operation completed successfully.
    invalid_argument,  ///< Caller supplied invalid input.
    backend_unavailable,  ///< Requested backend is not available on this host.
    device_closed,  ///< Device operation was requested after the device closed.
    unsupported_profile,  ///< Backend cannot create the requested device profile.
    backend_failure,  ///< Backend-specific operation failed.
  };

  /**
   * @brief Result status with an error category and human-readable message.
   */
  class OperationStatus {
  public:
    /**
     * @brief Construct a successful status.
     */
    OperationStatus();

    /**
     * @brief Construct a status with an explicit error code and message.
     *
     * @param code Error category.
     * @param message Human-readable status message.
     */
    OperationStatus(ErrorCode code, std::string message);

    /**
     * @brief Create a successful status.
     *
     * @return Successful status object.
     */
    static OperationStatus success();

    /**
     * @brief Create a failing status.
     *
     * @param code Error category.
     * @param message Human-readable failure message.
     * @return Failing status object.
     */
    static OperationStatus failure(ErrorCode code, std::string message);

    /**
     * @brief Check whether the operation succeeded.
     *
     * @return `true` when the status is successful.
     */
    bool ok() const;

    /**
     * @brief Get the status error category.
     *
     * @return Error category.
     */
    ErrorCode code() const;

    /**
     * @brief Get the human-readable status message.
     *
     * @return Human-readable status message.
     */
    const std::string &message() const;

  private:
    ErrorCode code_;
    std::string message_;
  };

  /**
   * @brief Backend implementation selection.
   */
  enum class BackendKind {
    fake,  ///< In-memory backend for tests and API validation.
    platform_default,  ///< Native backend for the current platform.
  };

  /**
   * @brief Runtime creation options.
   */
  struct RuntimeOptions {
    /**
     * @brief Backend implementation requested by the caller.
     */
    BackendKind backend = BackendKind::fake;
  };

  /**
   * @brief Feature set exposed by the selected backend.
   */
  struct BackendCapabilities {
    /**
     * @brief Human-readable backend name.
     */
    std::string backend_name;

    /**
     * @brief Whether the backend can create virtual HID devices.
     */
    bool supports_virtual_hid = false;

    /**
     * @brief Whether the backend can create gamepad devices.
     */
    bool supports_gamepad = false;

    /**
     * @brief Whether the backend can create keyboard devices.
     */
    bool supports_keyboard = false;

    /**
     * @brief Whether the backend can create mouse devices.
     */
    bool supports_mouse = false;

    /**
     * @brief Whether the backend can create touchscreen devices.
     */
    bool supports_touchscreen = false;

    /**
     * @brief Whether the backend can create trackpad devices.
     */
    bool supports_trackpad = false;

    /**
     * @brief Whether the backend can create pen tablet devices.
     */
    bool supports_pen_tablet = false;

    /**
     * @brief Whether the backend can deliver output reports to callers.
     */
    bool supports_output_reports = false;

    /**
     * @brief Whether the backend can fall back to X11 XTest input.
     */
    bool supports_xtest_fallback = false;

    /**
     * @brief Whether the backend requires an installed driver package.
     */
    bool requires_installed_driver = false;
  };

  /**
   * @brief Platform device-node categories reported by virtual devices.
   */
  enum class DeviceNodeKind {
    input_event,  ///< Linux `/dev/input/event*` node or equivalent.
    joystick,  ///< Linux `/dev/input/js*` node or equivalent.
    hidraw,  ///< Linux `/dev/hidraw*` node or equivalent.
    sysfs,  ///< Linux sysfs path or equivalent diagnostic path.
    other,  ///< Other platform-specific device path.
  };

  /**
   * @brief Platform-visible node or path associated with a virtual device.
   */
  struct DeviceNode {
    /**
     * @brief Node category.
     */
    DeviceNodeKind kind = DeviceNodeKind::other;

    /**
     * @brief Platform path for this node.
     */
    std::string path;
  };

  /**
   * @brief Device categories supported by the public profile model.
   */
  enum class DeviceType {
    gamepad,  ///< Game controller device.
    keyboard,  ///< Keyboard device.
    mouse,  ///< Mouse or pointer device.
    touchscreen,  ///< Direct touch display device.
    trackpad,  ///< Indirect touchpad device.
    pen_tablet,  ///< Pen tablet device.
  };

  /**
   * @brief Transport bus identity advertised by a device profile.
   */
  enum class BusType {
    unknown,  ///< Bus is unknown or not meaningful for the backend.
    usb,  ///< USB-style device identity.
    bluetooth,  ///< Bluetooth-style device identity.
  };

  /**
   * @brief Built-in gamepad profile identifiers.
   */
  enum class GamepadProfileKind {
    generic,  ///< Generic HID gamepad profile.
    xbox_360,  ///< Xbox 360-compatible profile.
    xbox_one,  ///< Xbox One-compatible profile.
    xbox_series,  ///< Xbox Series-compatible profile.
    dualsense,  ///< PlayStation DualSense-compatible profile.
    switch_pro,  ///< Nintendo Switch Pro-compatible profile.
    dualshock4,  ///< PlayStation DualShock 4-compatible profile.
  };

  /**
   * @brief Optional behavior advertised by a gamepad profile.
   */
  struct GamepadProfileCapabilities {
    /**
     * @brief Whether the profile supports rumble output.
     */
    bool supports_rumble = false;

    /**
     * @brief Whether the profile exposes motion sensors.
     */
    bool supports_motion = false;

    /**
     * @brief Whether the profile exposes touchpad input.
     */
    bool supports_touchpad = false;

    /**
     * @brief Whether the profile supports an RGB LED output.
     */
    bool supports_rgb_led = false;

    /**
     * @brief Whether the profile supports battery state.
     */
    bool supports_battery = false;

    /**
     * @brief Whether the profile supports adaptive trigger output.
     */
    bool supports_adaptive_triggers = false;
  };

  /**
   * @brief Descriptor and identity data used to create a virtual device.
   */
  struct DeviceProfile {
    /**
     * @brief Device category for this profile.
     */
    DeviceType device_type = DeviceType::gamepad;

    /**
     * @brief Built-in gamepad profile identifier.
     */
    GamepadProfileKind gamepad_kind = GamepadProfileKind::generic;

    /**
     * @brief Transport bus identity advertised by the profile.
     */
    BusType bus_type = BusType::usb;

    /**
     * @brief USB-style vendor identifier.
     */
    std::uint16_t vendor_id = 0;

    /**
     * @brief USB-style product identifier.
     */
    std::uint16_t product_id = 0;

    /**
     * @brief Device version number.
     */
    std::uint16_t version = 0;

    /**
     * @brief Primary input report identifier.
     */
    std::uint8_t report_id = 1;

    /**
     * @brief Expected packed input report size in bytes.
     */
    std::size_t input_report_size = 0;

    /**
     * @brief Expected packed output report size in bytes, or `0` when none is defined.
     */
    std::size_t output_report_size = 0;

    /**
     * @brief Human-readable device name.
     */
    std::string name;

    /**
     * @brief Human-readable device manufacturer.
     */
    std::string manufacturer;

    /**
     * @brief Profile feature flags.
     */
    GamepadProfileCapabilities capabilities;

    /**
     * @brief HID report descriptor bytes.
     */
    std::vector<std::uint8_t> report_descriptor;
  };

  /**
   * @brief Controller family reported by a streaming client.
   */
  enum class ClientControllerType {
    unknown,  ///< Controller family is unknown.
    xbox,  ///< Xbox-style client controller.
    playstation,  ///< PlayStation-style client controller.
    nintendo,  ///< Nintendo-style client controller.
  };

  /**
   * @brief Consumer-provided metadata for a gamepad device.
   */
  struct GamepadMetadata {
    /**
     * @brief Stable index across all connected controllers, or `-1` if unset.
     */
    int global_index = -1;

    /**
     * @brief Stable index within the client session, or `-1` if unset.
     */
    int client_relative_index = -1;

    /**
     * @brief Controller family reported by the client.
     */
    ClientControllerType client_type = ClientControllerType::unknown;

    /**
     * @brief Whether the client reports motion sensor capability.
     */
    bool has_motion_sensors = false;

    /**
     * @brief Whether the client reports touchpad capability.
     */
    bool has_touchpad = false;

    /**
     * @brief Whether the client reports RGB LED capability.
     */
    bool has_rgb_led = false;

    /**
     * @brief Whether the client reports battery state capability.
     */
    bool has_battery = false;

    /**
     * @brief Consumer-defined stable identity string.
     */
    std::string stable_id;
  };

  /**
   * @brief Full gamepad creation request.
   */
  struct CreateGamepadOptions {
    /**
     * @brief Device profile to instantiate.
     */
    DeviceProfile profile;

    /**
     * @brief Consumer metadata associated with the device.
     */
    GamepadMetadata metadata;
  };

  /**
   * @brief Full keyboard creation request.
   */
  struct CreateKeyboardOptions {
    /**
     * @brief Device profile to instantiate.
     */
    DeviceProfile profile;

    /**
     * @brief Held-key repeat interval in milliseconds, or `0` to disable repeat.
     */
    std::uint32_t auto_repeat_interval_ms = 50;

    /**
     * @brief Consumer-defined stable identity string.
     */
    std::string stable_id;
  };

  /**
   * @brief Full mouse creation request.
   */
  struct CreateMouseOptions {
    /**
     * @brief Device profile to instantiate.
     */
    DeviceProfile profile;

    /**
     * @brief Consumer-defined stable identity string.
     */
    std::string stable_id;
  };

  /**
   * @brief Full touchscreen creation request.
   */
  struct CreateTouchscreenOptions {
    /**
     * @brief Device profile to instantiate.
     */
    DeviceProfile profile;

    /**
     * @brief Consumer-defined stable identity string.
     */
    std::string stable_id;
  };

  /**
   * @brief Full trackpad creation request.
   */
  struct CreateTrackpadOptions {
    /**
     * @brief Device profile to instantiate.
     */
    DeviceProfile profile;

    /**
     * @brief Consumer-defined stable identity string.
     */
    std::string stable_id;
  };

  /**
   * @brief Full pen tablet creation request.
   */
  struct CreatePenTabletOptions {
    /**
     * @brief Device profile to instantiate.
     */
    DeviceProfile profile;

    /**
     * @brief Consumer-defined stable identity string.
     */
    std::string stable_id;
  };

  /**
   * @brief Logical gamepad buttons accepted by the common gamepad state model.
   */
  enum class GamepadButton : std::uint8_t {
    a = 0,  ///< South face button.
    b,  ///< East face button.
    x,  ///< West face button.
    y,  ///< North face button.
    back,  ///< Back, select, or share button.
    start,  ///< Start or options button.
    guide,  ///< System guide button.
    left_stick,  ///< Left stick press.
    right_stick,  ///< Right stick press.
    left_shoulder,  ///< Left shoulder button.
    right_shoulder,  ///< Right shoulder button.
    dpad_up,  ///< Directional pad up.
    dpad_down,  ///< Directional pad down.
    dpad_left,  ///< Directional pad left.
    dpad_right,  ///< Directional pad right.
    misc1,  ///< Profile-specific miscellaneous button.
    touchpad,  ///< Touchpad click button.
  };

  /**
   * @brief Compact set of pressed gamepad buttons.
   */
  class ButtonSet {
  public:
    /**
     * @brief Set or clear a button.
     *
     * @param button Button to update.
     * @param pressed Whether the button is pressed.
     */
    void set(GamepadButton button, bool pressed = true);

    /**
     * @brief Clear a button.
     *
     * @param button Button to clear.
     */
    void reset(GamepadButton button);

    /**
     * @brief Clear all buttons.
     */
    void clear();

    /**
     * @brief Check whether a button is pressed.
     *
     * @param button Button to test.
     * @return `true` when the button is pressed.
     */
    bool test(GamepadButton button) const;

    /**
     * @brief Get the raw bitset value.
     *
     * @return Raw button bits.
     */
    std::uint32_t raw_bits() const;

  private:
    std::uint32_t bits_ = 0;
  };

  /**
   * @brief Normalized two-axis stick state.
   */
  struct Stick {
    /**
     * @brief Horizontal axis in the inclusive range `[-1.0, 1.0]`.
     */
    float x = 0.0F;

    /**
     * @brief Vertical axis in the inclusive range `[-1.0, 1.0]`.
     */
    float y = 0.0F;
  };

  /**
   * @brief Normalized three-axis sensor state.
   */
  struct Vector3 {
    /**
     * @brief X-axis value.
     */
    float x = 0.0F;

    /**
     * @brief Y-axis value.
     */
    float y = 0.0F;

    /**
     * @brief Z-axis value.
     */
    float z = 0.0F;
  };

  /**
   * @brief Common gamepad battery states.
   */
  enum class GamepadBatteryState : std::uint8_t {
    unknown,  ///< Battery state is unknown.
    discharging,  ///< Battery is discharging.
    charging,  ///< Battery is charging.
    full,  ///< Battery is fully charged.
    voltage_or_temperature_error,  ///< Battery reports voltage or temperature outside the supported range.
    temperature_error,  ///< Battery reports a temperature error.
    charging_error,  ///< Battery reports a charging error.
  };

  /**
   * @brief Gamepad battery charge metadata.
   */
  struct GamepadBattery {
    /**
     * @brief Current battery state.
     */
    GamepadBatteryState state = GamepadBatteryState::unknown;

    /**
     * @brief Battery percentage in the inclusive range `[0, 100]`.
     */
    std::uint8_t percentage = 100;
  };

  /**
   * @brief Touchpad contact carried by a gamepad report.
   */
  struct GamepadTouchContact {
    /**
     * @brief Consumer-stable contact identifier.
     */
    std::uint8_t id = 0;

    /**
     * @brief Whether this contact is active.
     */
    bool active = false;

    /**
     * @brief Normalized X coordinate in the inclusive range `[0.0, 1.0]`.
     */
    float x = 0.0F;

    /**
     * @brief Normalized Y coordinate in the inclusive range `[0.0, 1.0]`.
     */
    float y = 0.0F;
  };

  /**
   * @brief Common gamepad input state accepted by libvirtualhid.
   */
  struct GamepadState {
    /**
     * @brief Pressed button set.
     */
    ButtonSet buttons;

    /**
     * @brief Left stick state.
     */
    Stick left_stick;

    /**
     * @brief Right stick state.
     */
    Stick right_stick;

    /**
     * @brief Left trigger value in the inclusive range `[0.0, 1.0]`.
     */
    float left_trigger = 0.0F;

    /**
     * @brief Right trigger value in the inclusive range `[0.0, 1.0]`.
     */
    float right_trigger = 0.0F;

    /**
     * @brief Accelerometer data in meters per second squared, when available.
     */
    std::optional<Vector3> acceleration;

    /**
     * @brief Gyroscope data in degrees per second, when available.
     */
    std::optional<Vector3> gyroscope;

    /**
     * @brief Battery metadata, when available.
     */
    std::optional<GamepadBattery> battery;

    /**
     * @brief Gamepad touchpad contacts.
     */
    std::array<GamepadTouchContact, 2> touchpad_contacts {};
  };

  /**
   * @brief Keyboard key code accepted by the keyboard event model.
   *
   * The initial Linux backend treats this as a Windows virtual-key code so
   * streaming hosts can pass common client key codes without exposing platform
   * backends. Backends translate this value to their native key representation.
   */
  using KeyboardKeyCode = std::uint16_t;

  /**
   * @brief Keyboard key transition.
   */
  struct KeyboardEvent {
    /**
     * @brief Portable key code.
     */
    KeyboardKeyCode key_code = 0;

    /**
     * @brief Whether the key is pressed.
     */
    bool pressed = false;
  };

  /**
   * @brief UTF-8 text input request.
   */
  struct KeyboardTextEvent {
    /**
     * @brief UTF-8 text to type.
     */
    std::string text;
  };

  /**
   * @brief Mouse buttons accepted by the mouse event model.
   */
  enum class MouseButton : std::uint8_t {
    left = 0,  ///< Primary mouse button.
    middle,  ///< Middle mouse button.
    right,  ///< Secondary mouse button.
    side,  ///< First auxiliary mouse button.
    extra,  ///< Second auxiliary mouse button.
  };

  /**
   * @brief Mouse event categories accepted by the mouse event model.
   */
  enum class MouseEventKind {
    relative_motion,  ///< Relative pointer movement.
    absolute_motion,  ///< Absolute pointer movement inside a target area.
    button,  ///< Mouse button transition.
    vertical_scroll,  ///< High-resolution vertical scroll event.
    horizontal_scroll,  ///< High-resolution horizontal scroll event.
  };

  /**
   * @brief Mouse input event.
   */
  struct MouseEvent {
    /**
     * @brief Event category.
     */
    MouseEventKind kind = MouseEventKind::relative_motion;

    /**
     * @brief Relative delta or absolute X coordinate.
     */
    std::int32_t x = 0;

    /**
     * @brief Relative delta or absolute Y coordinate.
     */
    std::int32_t y = 0;

    /**
     * @brief Width of the absolute coordinate space.
     */
    std::int32_t width = 0;

    /**
     * @brief Height of the absolute coordinate space.
     */
    std::int32_t height = 0;

    /**
     * @brief Button used by `MouseEventKind::button`.
     */
    MouseButton button = MouseButton::left;

    /**
     * @brief Whether the button is pressed.
     */
    bool pressed = false;

    /**
     * @brief High-resolution scroll distance.
     */
    std::int32_t high_resolution_scroll = 0;
  };

  /**
   * @brief Touch contact event for touchscreen and trackpad devices.
   */
  struct TouchContact {
    /**
     * @brief Consumer-stable contact identifier.
     */
    std::int32_t id = 0;

    /**
     * @brief Normalized X coordinate in the inclusive range `[0.0, 1.0]`.
     */
    float x = 0.0F;

    /**
     * @brief Normalized Y coordinate in the inclusive range `[0.0, 1.0]`.
     */
    float y = 0.0F;

    /**
     * @brief Normalized pressure in the inclusive range `[0.0, 1.0]`.
     */
    float pressure = 0.0F;

    /**
     * @brief Contact orientation in degrees, typically in the inclusive range `[-90, 90]`.
     */
    std::int32_t orientation = 0;
  };

  /**
   * @brief Pen tablet tool categories.
   */
  enum class PenToolType : std::uint8_t {
    pen,  ///< Pen tool.
    eraser,  ///< Eraser tool.
    brush,  ///< Brush tool.
    pencil,  ///< Pencil tool.
    airbrush,  ///< Airbrush tool.
    touch,  ///< Direct touch tool.
    unchanged,  ///< Keep the previously selected tool.
  };

  /**
   * @brief Pen tablet buttons.
   */
  enum class PenButton : std::uint8_t {
    primary,  ///< Primary stylus button.
    secondary,  ///< Secondary stylus button.
    tertiary,  ///< Tertiary stylus button.
  };

  /**
   * @brief Pen tablet tool position and analog state.
   */
  struct PenToolState {
    /**
     * @brief Tool category.
     */
    PenToolType tool = PenToolType::pen;

    /**
     * @brief Normalized X coordinate in the inclusive range `[0.0, 1.0]`.
     */
    float x = 0.0F;

    /**
     * @brief Normalized Y coordinate in the inclusive range `[0.0, 1.0]`.
     */
    float y = 0.0F;

    /**
     * @brief Normalized pressure in the inclusive range `[0.0, 1.0]`, or negative to leave pressure unchanged.
     */
    float pressure = -1.0F;

    /**
     * @brief Normalized distance in the inclusive range `[0.0, 1.0]`, or negative to leave distance unchanged.
     */
    float distance = -1.0F;

    /**
     * @brief X-axis tilt in degrees.
     */
    float tilt_x = 0.0F;

    /**
     * @brief Y-axis tilt in degrees.
     */
    float tilt_y = 0.0F;
  };

  /**
   * @brief Output report categories delivered by a gamepad backend.
   */
  enum class GamepadOutputKind {
    rumble,  ///< Rumble motor output.
    rgb_led,  ///< RGB LED color output.
    adaptive_triggers,  ///< Adaptive trigger output.
    raw_report,  ///< Raw output report bytes.
  };

  /**
   * @brief Normalized gamepad output event delivered to the consumer.
   */
  struct GamepadOutput {
    /**
     * @brief Output event category.
     */
    GamepadOutputKind kind = GamepadOutputKind::raw_report;

    /**
     * @brief Low-frequency rumble motor strength.
     */
    std::uint16_t low_frequency_rumble = 0;

    /**
     * @brief High-frequency rumble motor strength.
     */
    std::uint16_t high_frequency_rumble = 0;

    /**
     * @brief Red LED channel value.
     */
    std::uint8_t red = 0;

    /**
     * @brief Green LED channel value.
     */
    std::uint8_t green = 0;

    /**
     * @brief Blue LED channel value.
     */
    std::uint8_t blue = 0;

    /**
     * @brief Adaptive trigger event flags from a profile-specific output report.
     */
    std::uint8_t adaptive_trigger_flags = 0;

    /**
     * @brief Profile-specific left trigger effect type.
     */
    std::uint8_t left_trigger_effect_type = 0;

    /**
     * @brief Profile-specific right trigger effect type.
     */
    std::uint8_t right_trigger_effect_type = 0;

    /**
     * @brief Profile-specific left trigger effect payload.
     */
    std::array<std::uint8_t, 10> left_trigger_effect {};

    /**
     * @brief Profile-specific right trigger effect payload.
     */
    std::array<std::uint8_t, 10> right_trigger_effect {};

    /**
     * @brief Raw output report payload.
     */
    std::vector<std::uint8_t> raw_report;
  };

  /**
   * @brief Callback invoked when a gamepad receives output from the backend.
   */
  using OutputCallback = std::function<void(const GamepadOutput &)>;

}  // namespace lvh
