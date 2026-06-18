/**
 * @file include/libvirtualhid/types.hpp
 * @brief Core public types for libvirtualhid.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <functional>
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
  class Status {
  public:
    /**
     * @brief Construct a successful status.
     */
    Status();

    /**
     * @brief Construct a status with an explicit error code and message.
     *
     * @param code Error category.
     * @param message Human-readable status message.
     */
    Status(ErrorCode code, std::string message);

    /**
     * @brief Create a successful status.
     *
     * @return Successful status object.
     */
    static Status success();

    /**
     * @brief Create a failing status.
     *
     * @param code Error category.
     * @param message Human-readable failure message.
     * @return Failing status object.
     */
    static Status failure(ErrorCode code, std::string message);

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
     * @return Status message.
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
   * @brief Device categories supported by the public profile model.
   */
  enum class DeviceType {
    gamepad,  ///< Game controller device.
    keyboard,  ///< Keyboard device.
    mouse,  ///< Mouse or pointer device.
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
     * @brief Raw output report payload.
     */
    std::vector<std::uint8_t> raw_report;
  };

  /**
   * @brief Callback invoked when a gamepad receives output from the backend.
   */
  using OutputCallback = std::function<void(const GamepadOutput &)>;

}  // namespace lvh
