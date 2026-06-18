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
    virtual Status close() = 0;
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
    Status close() override;

    /**
     * @brief Submit the latest gamepad input state.
     *
     * @param state Gamepad input state.
     * @return Submit operation status.
     */
    Status submit(const GamepadState &state);

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
    Status dispatch_output(const GamepadOutput &output);

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
   * @brief Result returned by gamepad creation.
   */
  struct GamepadCreationResult {
    /**
     * @brief Creation status.
     */
    Status status;

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
