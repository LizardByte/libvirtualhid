/**
 * @file include/libvirtualhid/runtime.hpp
 * @brief Runtime and virtual device handle declarations.
 */
#pragma once

// standard includes
#include <cstddef>
#include <memory>
#include <vector>

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh {

  namespace detail {
    struct GamepadDevice;
    struct KeyboardDevice;
    struct MouseDevice;
    class RuntimeState;
  }  // namespace detail

  /**
   * @brief Common interface for virtual device handles.
   */
  class VirtualDevice {
  public:
    /**
     * @brief Destroy the virtual device handle.
     */
    virtual ~VirtualDevice() = default;

    /**
     * @brief Get the device identifier assigned by the runtime.
     *
     * @return Device identifier.
     */
    virtual DeviceId device_id() const = 0;

    /**
     * @brief Get the profile used to create this device.
     *
     * @return Device profile.
     */
    virtual const DeviceProfile &profile() const = 0;

    /**
     * @brief Check whether the device is open.
     *
     * @return `true` when the device can accept operations.
     */
    virtual bool is_open() const = 0;

    /**
     * @brief Close the virtual device.
     *
     * @return Close operation status.
     */
    virtual OperationStatus close() = 0;
  };

  /**
   * @brief Virtual gamepad device handle.
   */
  class Gamepad final: public VirtualDevice {
  public:
    /**
     * @brief Copy construction is disabled because the handle owns device lifetime.
     */
    Gamepad(const Gamepad &) = delete;

    /**
     * @brief Copy assignment is disabled because the handle owns device lifetime.
     *
     * @return This gamepad handle.
     */
    Gamepad &operator=(const Gamepad &) = delete;

    /**
     * @brief Move construct a gamepad handle.
     *
     * @param other Handle to move from.
     */
    Gamepad(Gamepad &&other) noexcept;

    /**
     * @brief Move assign a gamepad handle.
     *
     * @param other Handle to move from.
     * @return This gamepad handle.
     */
    Gamepad &operator=(Gamepad &&other) noexcept;

    /**
     * @brief Destroy the gamepad handle.
     */
    ~Gamepad() override;

    /**
     * @copydoc VirtualDevice::device_id
     */
    DeviceId device_id() const override;

    /**
     * @copydoc VirtualDevice::profile
     */
    const DeviceProfile &profile() const override;

    /**
     * @brief Get the metadata supplied when the gamepad was created.
     *
     * @return Gamepad metadata.
     */
    const GamepadMetadata &metadata() const;

    /**
     * @copydoc VirtualDevice::is_open
     */
    bool is_open() const override;

    /**
     * @copydoc VirtualDevice::close
     */
    OperationStatus close() override;

    /**
     * @brief Submit the latest gamepad input state.
     *
     * @param state Gamepad input state.
     * @return Submit operation status.
     */
    OperationStatus submit(const GamepadState &state);

    /**
     * @brief Register a callback for backend output events.
     *
     * @param callback Output callback. Passing an empty callback clears it.
     */
    void set_output_callback(OutputCallback callback);

    /**
     * @brief Dispatch an output event to the registered callback.
     *
     * @param output Output event.
     * @return Dispatch operation status.
     */
    OperationStatus dispatch_output(const GamepadOutput &output);

    /**
     * @brief Get the most recently submitted gamepad state.
     *
     * @return Last submitted state.
     */
    GamepadState last_submitted_state() const;

    /**
     * @brief Get the most recently packed input report.
     *
     * @return Last input report bytes.
     */
    std::vector<std::uint8_t> last_input_report() const;

    /**
     * @brief Get the number of successful submit operations.
     *
     * @return Submit count.
     */
    std::size_t submit_count() const;

  private:
    friend class Runtime;

    explicit Gamepad(std::shared_ptr<detail::GamepadDevice> device);

    std::shared_ptr<detail::GamepadDevice> device_;
  };

  /**
   * @brief Virtual keyboard device handle.
   */
  class Keyboard final: public VirtualDevice {
  public:
    /**
     * @brief Copy construction is disabled because the handle owns device lifetime.
     */
    Keyboard(const Keyboard &) = delete;

    /**
     * @brief Copy assignment is disabled because the handle owns device lifetime.
     *
     * @return This keyboard handle.
     */
    Keyboard &operator=(const Keyboard &) = delete;

    /**
     * @brief Move construct a keyboard handle.
     *
     * @param other Handle to move from.
     */
    Keyboard(Keyboard &&other) noexcept;

    /**
     * @brief Move assign a keyboard handle.
     *
     * @param other Handle to move from.
     * @return This keyboard handle.
     */
    Keyboard &operator=(Keyboard &&other) noexcept;

    /**
     * @brief Destroy the keyboard handle.
     */
    ~Keyboard() override;

    /**
     * @copydoc VirtualDevice::device_id
     */
    DeviceId device_id() const override;

    /**
     * @copydoc VirtualDevice::profile
     */
    const DeviceProfile &profile() const override;

    /**
     * @copydoc VirtualDevice::is_open
     */
    bool is_open() const override;

    /**
     * @copydoc VirtualDevice::close
     */
    OperationStatus close() override;

    /**
     * @brief Submit a keyboard key transition.
     *
     * @param event Keyboard event.
     * @return Submit operation status.
     */
    OperationStatus submit(const KeyboardEvent &event);

    /**
     * @brief Press a keyboard key.
     *
     * @param key_code Portable key code.
     * @return Submit operation status.
     */
    OperationStatus press(KeyboardKeyCode key_code);

    /**
     * @brief Release a keyboard key.
     *
     * @param key_code Portable key code.
     * @return Submit operation status.
     */
    OperationStatus release(KeyboardKeyCode key_code);

    /**
     * @brief Type UTF-8 text.
     *
     * @param event Text event.
     * @return Submit operation status.
     */
    OperationStatus type_text(const KeyboardTextEvent &event);

    /**
     * @brief Get the most recently submitted keyboard event.
     *
     * @return Last submitted keyboard event.
     */
    KeyboardEvent last_submitted_event() const;

    /**
     * @brief Get the number of successful submit operations.
     *
     * @return Submit count.
     */
    std::size_t submit_count() const;

  private:
    friend class Runtime;

    explicit Keyboard(std::shared_ptr<detail::KeyboardDevice> device);

    std::shared_ptr<detail::KeyboardDevice> device_;
  };

  /**
   * @brief Virtual mouse device handle.
   */
  class Mouse final: public VirtualDevice {
  public:
    /**
     * @brief Copy construction is disabled because the handle owns device lifetime.
     */
    Mouse(const Mouse &) = delete;

    /**
     * @brief Copy assignment is disabled because the handle owns device lifetime.
     *
     * @return This mouse handle.
     */
    Mouse &operator=(const Mouse &) = delete;

    /**
     * @brief Move construct a mouse handle.
     *
     * @param other Handle to move from.
     */
    Mouse(Mouse &&other) noexcept;

    /**
     * @brief Move assign a mouse handle.
     *
     * @param other Handle to move from.
     * @return This mouse handle.
     */
    Mouse &operator=(Mouse &&other) noexcept;

    /**
     * @brief Destroy the mouse handle.
     */
    ~Mouse() override;

    /**
     * @copydoc VirtualDevice::device_id
     */
    DeviceId device_id() const override;

    /**
     * @copydoc VirtualDevice::profile
     */
    const DeviceProfile &profile() const override;

    /**
     * @copydoc VirtualDevice::is_open
     */
    bool is_open() const override;

    /**
     * @copydoc VirtualDevice::close
     */
    OperationStatus close() override;

    /**
     * @brief Submit a mouse event.
     *
     * @param event Mouse event.
     * @return Submit operation status.
     */
    OperationStatus submit(const MouseEvent &event);

    /**
     * @brief Submit relative pointer movement.
     *
     * @param delta_x Horizontal delta.
     * @param delta_y Vertical delta.
     * @return Submit operation status.
     */
    OperationStatus move_relative(std::int32_t delta_x, std::int32_t delta_y);

    /**
     * @brief Submit absolute pointer movement.
     *
     * @param x Absolute X coordinate.
     * @param y Absolute Y coordinate.
     * @param width Width of the absolute coordinate space.
     * @param height Height of the absolute coordinate space.
     * @return Submit operation status.
     */
    OperationStatus move_absolute(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height);

    /**
     * @brief Submit a mouse button transition.
     *
     * @param button Mouse button.
     * @param pressed Whether the button is pressed.
     * @return Submit operation status.
     */
    OperationStatus button(MouseButton button, bool pressed);

    /**
     * @brief Submit high-resolution vertical scroll.
     *
     * @param distance High-resolution scroll distance.
     * @return Submit operation status.
     */
    OperationStatus vertical_scroll(std::int32_t distance);

    /**
     * @brief Submit high-resolution horizontal scroll.
     *
     * @param distance High-resolution scroll distance.
     * @return Submit operation status.
     */
    OperationStatus horizontal_scroll(std::int32_t distance);

    /**
     * @brief Get the most recently submitted mouse event.
     *
     * @return Last submitted mouse event.
     */
    MouseEvent last_submitted_event() const;

    /**
     * @brief Get the number of successful submit operations.
     *
     * @return Submit count.
     */
    std::size_t submit_count() const;

  private:
    friend class Runtime;

    explicit Mouse(std::shared_ptr<detail::MouseDevice> device);

    std::shared_ptr<detail::MouseDevice> device_;
  };

  /**
   * @brief Result returned by gamepad creation.
   */
  struct GamepadCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created gamepad handle when creation succeeds.
     */
    std::unique_ptr<Gamepad> gamepad;

    /**
     * @brief Check whether creation succeeded and produced a handle.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && gamepad != nullptr;
    }
  };

  /**
   * @brief Result returned by keyboard creation.
   */
  struct KeyboardCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created keyboard handle when creation succeeds.
     */
    std::unique_ptr<Keyboard> keyboard;

    /**
     * @brief Check whether creation succeeded and produced a handle.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && keyboard != nullptr;
    }
  };

  /**
   * @brief Result returned by mouse creation.
   */
  struct MouseCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created mouse handle when creation succeeds.
     */
    std::unique_ptr<Mouse> mouse;

    /**
     * @brief Check whether creation succeeded and produced a handle.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && mouse != nullptr;
    }
  };

  /**
   * @brief Runtime that owns backend state and creates virtual devices.
   */
  class Runtime final {
  public:
    /**
     * @brief Copy construction is disabled because the runtime owns backend state.
     */
    Runtime(const Runtime &) = delete;

    /**
     * @brief Copy assignment is disabled because the runtime owns backend state.
     *
     * @return This runtime.
     */
    Runtime &operator=(const Runtime &) = delete;

    /**
     * @brief Move construct a runtime.
     *
     * @param other Runtime to move from.
     */
    Runtime(Runtime &&other) noexcept;

    /**
     * @brief Move assign a runtime.
     *
     * @param other Runtime to move from.
     * @return This runtime.
     */
    Runtime &operator=(Runtime &&other) noexcept;

    /**
     * @brief Destroy the runtime and close any remaining devices.
     */
    ~Runtime();

    /**
     * @brief Create a runtime with the requested options.
     *
     * @param options Runtime creation options.
     * @return Runtime instance.
     */
    static std::unique_ptr<Runtime> create(RuntimeOptions options = {});

    /**
     * @brief Get capabilities for the selected backend.
     *
     * @return Backend capabilities.
     */
    const BackendCapabilities &capabilities() const;

    /**
     * @brief Get the backend kind used by this runtime.
     *
     * @return Backend kind.
     */
    BackendKind backend_kind() const;

    /**
     * @brief Create a gamepad from a profile.
     *
     * @param profile Device profile.
     * @return Gamepad creation result.
     */
    GamepadCreationResult create_gamepad(const DeviceProfile &profile);

    /**
     * @brief Create a gamepad from full creation options.
     *
     * @param options Gamepad creation options.
     * @return Gamepad creation result.
     */
    GamepadCreationResult create_gamepad(const CreateGamepadOptions &options);

    /**
     * @brief Create a keyboard with the built-in keyboard profile.
     *
     * @return Keyboard creation result.
     */
    KeyboardCreationResult create_keyboard();

    /**
     * @brief Create a keyboard from full creation options.
     *
     * @param options Keyboard creation options.
     * @return Keyboard creation result.
     */
    KeyboardCreationResult create_keyboard(const CreateKeyboardOptions &options);

    /**
     * @brief Create a mouse with the built-in mouse profile.
     *
     * @return Mouse creation result.
     */
    MouseCreationResult create_mouse();

    /**
     * @brief Create a mouse from full creation options.
     *
     * @param options Mouse creation options.
     * @return Mouse creation result.
     */
    MouseCreationResult create_mouse(const CreateMouseOptions &options);

    /**
     * @brief Get the number of open devices owned by the runtime.
     *
     * @return Active device count.
     */
    std::size_t active_device_count() const;

    /**
     * @brief Close every device owned by the runtime.
     */
    void close_all();

  private:
    explicit Runtime(RuntimeOptions options);

    std::shared_ptr<detail::RuntimeState> state_;
  };

}  // namespace lvh
