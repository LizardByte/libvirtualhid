/**
 * @file src/core/backend.hpp
 * @brief Internal backend interfaces for virtual HID implementations.
 */
#pragma once

// standard includes
#include <cstdint>
#include <memory>
#include <vector>

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail {

  /**
   * @brief Backend-owned gamepad device implementation.
   */
  class BackendGamepad {
  public:
    BackendGamepad(const BackendGamepad &) = delete;
    BackendGamepad &operator=(const BackendGamepad &) = delete;
    BackendGamepad(BackendGamepad &&) noexcept = delete;
    BackendGamepad &operator=(BackendGamepad &&) noexcept = delete;

    /**
     * @brief Destroy the backend gamepad.
     */
    virtual ~BackendGamepad() = default;

    /**
     * @brief Submit a packed input report to the backend.
     *
     * @param report Packed HID input report.
     * @return Submit status.
     */
    virtual OperationStatus submit(const std::vector<std::uint8_t> &report) = 0;

    /**
     * @brief Register a callback for backend output reports.
     *
     * @param callback Output callback.
     */
    virtual void set_output_callback(OutputCallback callback) = 0;

    /**
     * @brief Close the backend device.
     *
     * @return Close status.
     */
    virtual OperationStatus close() = 0;

  protected:
    BackendGamepad() = default;
  };

  /**
   * @brief Backend-owned keyboard device implementation.
   */
  class BackendKeyboard {
  public:
    BackendKeyboard(const BackendKeyboard &) = delete;
    BackendKeyboard &operator=(const BackendKeyboard &) = delete;
    BackendKeyboard(BackendKeyboard &&) noexcept = delete;
    BackendKeyboard &operator=(BackendKeyboard &&) noexcept = delete;

    /**
     * @brief Destroy the backend keyboard.
     */
    virtual ~BackendKeyboard() = default;

    /**
     * @brief Submit a keyboard key transition to the backend.
     *
     * @param event Keyboard event.
     * @return Submit status.
     */
    virtual OperationStatus submit(const KeyboardEvent &event) = 0;

    /**
     * @brief Submit UTF-8 text to the backend.
     *
     * @param event Text event.
     * @return Submit status.
     */
    virtual OperationStatus type_text(const KeyboardTextEvent &event) = 0;

    /**
     * @brief Close the backend device.
     *
     * @return Close status.
     */
    virtual OperationStatus close() = 0;

  protected:
    BackendKeyboard() = default;
  };

  /**
   * @brief Backend-owned mouse device implementation.
   */
  class BackendMouse {
  public:
    BackendMouse(const BackendMouse &) = delete;
    BackendMouse &operator=(const BackendMouse &) = delete;
    BackendMouse(BackendMouse &&) noexcept = delete;
    BackendMouse &operator=(BackendMouse &&) noexcept = delete;

    /**
     * @brief Destroy the backend mouse.
     */
    virtual ~BackendMouse() = default;

    /**
     * @brief Submit a mouse event to the backend.
     *
     * @param event Mouse event.
     * @return Submit status.
     */
    virtual OperationStatus submit(const MouseEvent &event) = 0;

    /**
     * @brief Close the backend device.
     *
     * @return Close status.
     */
    virtual OperationStatus close() = 0;

  protected:
    BackendMouse() = default;
  };

  /**
   * @brief Result returned by an internal backend gamepad creation request.
   */
  struct BackendGamepadCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Backend device when creation succeeds.
     */
    std::unique_ptr<BackendGamepad> gamepad;

    /**
     * @brief Check whether creation succeeded.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && gamepad != nullptr;
    }
  };

  /**
   * @brief Result returned by an internal backend keyboard creation request.
   */
  struct BackendKeyboardCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Backend device when creation succeeds.
     */
    std::unique_ptr<BackendKeyboard> keyboard;

    /**
     * @brief Check whether creation succeeded.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && keyboard != nullptr;
    }
  };

  /**
   * @brief Result returned by an internal backend mouse creation request.
   */
  struct BackendMouseCreationResult {
    /**
     * @brief Creation status.
     */
    OperationStatus status;

    /**
     * @brief Backend device when creation succeeds.
     */
    std::unique_ptr<BackendMouse> mouse;

    /**
     * @brief Check whether creation succeeded.
     *
     * @return `true` when creation succeeded.
     */
    explicit operator bool() const {
      return status.ok() && mouse != nullptr;
    }
  };

  /**
   * @brief Runtime-selected backend implementation.
   */
  class Backend {
  public:
    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;
    Backend(Backend &&) noexcept = delete;
    Backend &operator=(Backend &&) noexcept = delete;

    /**
     * @brief Destroy the backend.
     */
    virtual ~Backend() = default;

    /**
     * @brief Get backend capabilities.
     *
     * @return Backend capabilities.
     */
    virtual const BackendCapabilities &capabilities() const = 0;

    /**
     * @brief Create a backend gamepad device.
     *
     * @param id Runtime-assigned device id.
     * @param options Gamepad creation options.
     * @return Backend gamepad creation result.
     */
    virtual BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) = 0;

    /**
     * @brief Create a backend keyboard device.
     *
     * @param id Runtime-assigned device id.
     * @param options Keyboard creation options.
     * @return Backend keyboard creation result.
     */
    virtual BackendKeyboardCreationResult create_keyboard(DeviceId id, const CreateKeyboardOptions &options) = 0;

    /**
     * @brief Create a backend mouse device.
     *
     * @param id Runtime-assigned device id.
     * @param options Mouse creation options.
     * @return Backend mouse creation result.
     */
    virtual BackendMouseCreationResult create_mouse(DeviceId id, const CreateMouseOptions &options) = 0;

  protected:
    Backend() = default;
  };

  /**
   * @brief Create a backend for the requested backend kind.
   *
   * @param kind Requested backend kind.
   * @return Backend implementation.
   */
  std::unique_ptr<Backend> create_backend(BackendKind kind);

  /**
   * @brief Create the platform default backend for the current operating system.
   *
   * @return Platform default backend implementation.
   */
  std::unique_ptr<Backend> create_platform_backend();

}  // namespace lvh::detail
