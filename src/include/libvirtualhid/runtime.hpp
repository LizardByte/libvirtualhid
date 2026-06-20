/**
 * @file src/include/libvirtualhid/runtime.hpp
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

  class Runtime;

  namespace detail {
    /**
     * @brief Token used by Runtime to construct runtime-owned handles.
     */
    struct RuntimeConstructionToken {
    private:
      friend class ::lvh::Runtime;

      RuntimeConstructionToken() = default;
    };

    struct GamepadDevice;
    struct KeyboardDevice;
    struct MouseDevice;
    struct TouchscreenDevice;
    struct TrackpadDevice;
    struct PenTabletDevice;
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
     * @brief Get platform-visible nodes associated with the device.
     *
     * @return Device nodes and diagnostic paths currently known to the backend.
     */
    virtual std::vector<DeviceNode> device_nodes() const = 0;

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
     * @brief Construct a gamepad handle for Runtime-owned state.
     *
     * @param token Runtime construction token.
     * @param device Shared gamepad state.
     */
    Gamepad(detail::RuntimeConstructionToken token, std::shared_ptr<detail::GamepadDevice> device);

    /**
     * @brief Destroy the gamepad handle and close the virtual device if it is still open.
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
     * @copydoc VirtualDevice::device_nodes
     */
    std::vector<DeviceNode> device_nodes() const override;

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
     * @param callback Output callback copied into the handle. Passing an empty callback clears it.
     */
    void set_output_callback(const OutputCallback &callback);

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
     * @brief Construct a keyboard handle for Runtime-owned state.
     *
     * @param token Runtime construction token.
     * @param device Shared keyboard state.
     */
    Keyboard(detail::RuntimeConstructionToken token, std::shared_ptr<detail::KeyboardDevice> device);

    /**
     * @brief Destroy the keyboard handle and close the virtual device if it is still open.
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
     * @copydoc VirtualDevice::device_nodes
     */
    std::vector<DeviceNode> device_nodes() const override;

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
     * @brief Construct a mouse handle for Runtime-owned state.
     *
     * @param token Runtime construction token.
     * @param device Shared mouse state.
     */
    Mouse(detail::RuntimeConstructionToken token, std::shared_ptr<detail::MouseDevice> device);

    /**
     * @brief Destroy the mouse handle and close the virtual device if it is still open.
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
     * @copydoc VirtualDevice::device_nodes
     */
    std::vector<DeviceNode> device_nodes() const override;

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
    std::shared_ptr<detail::MouseDevice> device_;
  };

  /**
   * @brief Virtual touchscreen device handle.
   */
  class Touchscreen final: public VirtualDevice {
  public:
    /**
     * @brief Copy construction is disabled because the handle owns device lifetime.
     */
    Touchscreen(const Touchscreen &) = delete;

    /**
     * @brief Copy assignment is disabled because the handle owns device lifetime.
     *
     * @return This touchscreen handle.
     */
    Touchscreen &operator=(const Touchscreen &) = delete;

    /**
     * @brief Move construct a touchscreen handle.
     *
     * @param other Handle to move from.
     */
    Touchscreen(Touchscreen &&other) noexcept;

    /**
     * @brief Move assign a touchscreen handle.
     *
     * @param other Handle to move from.
     * @return This touchscreen handle.
     */
    Touchscreen &operator=(Touchscreen &&other) noexcept;

    /**
     * @brief Construct a touchscreen handle for Runtime-owned state.
     *
     * @param token Runtime construction token.
     * @param device Shared touchscreen state.
     */
    Touchscreen(detail::RuntimeConstructionToken token, std::shared_ptr<detail::TouchscreenDevice> device);

    /**
     * @brief Destroy the touchscreen handle and close the virtual device if it is still open.
     */
    ~Touchscreen() override;

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
     * @copydoc VirtualDevice::device_nodes
     */
    std::vector<DeviceNode> device_nodes() const override;

    /**
     * @copydoc VirtualDevice::close
     */
    OperationStatus close() override;

    /**
     * @brief Place or move a touch contact.
     *
     * @param contact Touch contact state.
     * @return Submit operation status.
     */
    OperationStatus place_contact(const TouchContact &contact);

    /**
     * @brief Release a touch contact.
     *
     * @param contact_id Consumer-stable contact identifier.
     * @return Submit operation status.
     */
    OperationStatus release_contact(std::int32_t contact_id);

    /**
     * @brief Get the most recently submitted touch contact.
     *
     * @return Last submitted touch contact.
     */
    TouchContact last_submitted_contact() const;

    /**
     * @brief Get the number of successful submit operations.
     *
     * @return Submit count.
     */
    std::size_t submit_count() const;

  private:
    std::shared_ptr<detail::TouchscreenDevice> device_;
  };

  /**
   * @brief Virtual trackpad device handle.
   */
  class Trackpad final: public VirtualDevice {
  public:
    /**
     * @brief Copy construction is disabled because the handle owns device lifetime.
     */
    Trackpad(const Trackpad &) = delete;

    /**
     * @brief Copy assignment is disabled because the handle owns device lifetime.
     *
     * @return This trackpad handle.
     */
    Trackpad &operator=(const Trackpad &) = delete;

    /**
     * @brief Move construct a trackpad handle.
     *
     * @param other Handle to move from.
     */
    Trackpad(Trackpad &&other) noexcept;

    /**
     * @brief Move assign a trackpad handle.
     *
     * @param other Handle to move from.
     * @return This trackpad handle.
     */
    Trackpad &operator=(Trackpad &&other) noexcept;

    /**
     * @brief Construct a trackpad handle for Runtime-owned state.
     *
     * @param token Runtime construction token.
     * @param device Shared trackpad state.
     */
    Trackpad(detail::RuntimeConstructionToken token, std::shared_ptr<detail::TrackpadDevice> device);

    /**
     * @brief Destroy the trackpad handle and close the virtual device if it is still open.
     */
    ~Trackpad() override;

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
     * @copydoc VirtualDevice::device_nodes
     */
    std::vector<DeviceNode> device_nodes() const override;

    /**
     * @copydoc VirtualDevice::close
     */
    OperationStatus close() override;

    /**
     * @brief Place or move a trackpad contact.
     *
     * @param contact Touch contact state.
     * @return Submit operation status.
     */
    OperationStatus place_contact(const TouchContact &contact);

    /**
     * @brief Release a trackpad contact.
     *
     * @param contact_id Consumer-stable contact identifier.
     * @return Submit operation status.
     */
    OperationStatus release_contact(std::int32_t contact_id);

    /**
     * @brief Submit a physical trackpad button transition.
     *
     * @param pressed Whether the primary trackpad button is pressed.
     * @return Submit operation status.
     */
    OperationStatus button(bool pressed);

    /**
     * @brief Get the most recently submitted touch contact.
     *
     * @return Last submitted touch contact.
     */
    TouchContact last_submitted_contact() const;

    /**
     * @brief Get the number of successful submit operations.
     *
     * @return Submit count.
     */
    std::size_t submit_count() const;

  private:
    std::shared_ptr<detail::TrackpadDevice> device_;
  };

  /**
   * @brief Virtual pen tablet device handle.
   */
  class PenTablet final: public VirtualDevice {
  public:
    /**
     * @brief Copy construction is disabled because the handle owns device lifetime.
     */
    PenTablet(const PenTablet &) = delete;

    /**
     * @brief Copy assignment is disabled because the handle owns device lifetime.
     *
     * @return This pen tablet handle.
     */
    PenTablet &operator=(const PenTablet &) = delete;

    /**
     * @brief Move construct a pen tablet handle.
     *
     * @param other Handle to move from.
     */
    PenTablet(PenTablet &&other) noexcept;

    /**
     * @brief Move assign a pen tablet handle.
     *
     * @param other Handle to move from.
     * @return This pen tablet handle.
     */
    PenTablet &operator=(PenTablet &&other) noexcept;

    /**
     * @brief Construct a pen tablet handle for Runtime-owned state.
     *
     * @param token Runtime construction token.
     * @param device Shared pen tablet state.
     */
    PenTablet(detail::RuntimeConstructionToken token, std::shared_ptr<detail::PenTabletDevice> device);

    /**
     * @brief Destroy the pen tablet handle and close the virtual device if it is still open.
     */
    ~PenTablet() override;

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
     * @copydoc VirtualDevice::device_nodes
     */
    std::vector<DeviceNode> device_nodes() const override;

    /**
     * @copydoc VirtualDevice::close
     */
    OperationStatus close() override;

    /**
     * @brief Place or move the active tablet tool.
     *
     * @param state Tool state.
     * @return Submit operation status.
     */
    OperationStatus place_tool(const PenToolState &state);

    /**
     * @brief Submit a tablet button transition.
     *
     * @param button Button to update.
     * @param pressed Whether the button is pressed.
     * @return Submit operation status.
     */
    OperationStatus button(PenButton button, bool pressed);

    /**
     * @brief Get the most recently submitted tool state.
     *
     * @return Last submitted tool state.
     */
    PenToolState last_submitted_tool() const;

    /**
     * @brief Get the number of successful submit operations.
     *
     * @return Submit count.
     */
    std::size_t submit_count() const;

  private:
    std::shared_ptr<detail::PenTabletDevice> device_;
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
   * @brief Result returned by touchscreen creation.
   */
  struct TouchscreenCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created touchscreen handle when creation succeeds.
     */
    std::unique_ptr<Touchscreen> touchscreen;

    /**
     * @brief Check whether creation succeeded and produced a handle.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && touchscreen != nullptr;
    }
  };

  /**
   * @brief Result returned by trackpad creation.
   */
  struct TrackpadCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created trackpad handle when creation succeeds.
     */
    std::unique_ptr<Trackpad> trackpad;

    /**
     * @brief Check whether creation succeeded and produced a handle.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && trackpad != nullptr;
    }
  };

  /**
   * @brief Result returned by pen tablet creation.
   */
  struct PenTabletCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Created pen tablet handle when creation succeeds.
     */
    std::unique_ptr<PenTablet> pen_tablet;

    /**
     * @brief Check whether creation succeeded and produced a handle.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && pen_tablet != nullptr;
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
     * @brief Construct a runtime for Runtime::create.
     *
     * @param token Runtime construction token.
     * @param options Runtime configuration.
     */
    Runtime(detail::RuntimeConstructionToken token, RuntimeOptions options);

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
     * @brief Create a touchscreen with the built-in touchscreen profile.
     *
     * @return Touchscreen creation result.
     */
    TouchscreenCreationResult create_touchscreen();

    /**
     * @brief Create a touchscreen from full creation options.
     *
     * @param options Touchscreen creation options.
     * @return Touchscreen creation result.
     */
    TouchscreenCreationResult create_touchscreen(const CreateTouchscreenOptions &options);

    /**
     * @brief Create a trackpad with the built-in trackpad profile.
     *
     * @return Trackpad creation result.
     */
    TrackpadCreationResult create_trackpad();

    /**
     * @brief Create a trackpad from full creation options.
     *
     * @param options Trackpad creation options.
     * @return Trackpad creation result.
     */
    TrackpadCreationResult create_trackpad(const CreateTrackpadOptions &options);

    /**
     * @brief Create a pen tablet with the built-in pen tablet profile.
     *
     * @return Pen tablet creation result.
     */
    PenTabletCreationResult create_pen_tablet();

    /**
     * @brief Create a pen tablet from full creation options.
     *
     * @param options Pen tablet creation options.
     * @return Pen tablet creation result.
     */
    PenTabletCreationResult create_pen_tablet(const CreatePenTabletOptions &options);

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
    std::shared_ptr<detail::RuntimeState> state_;
  };

}  // namespace lvh
