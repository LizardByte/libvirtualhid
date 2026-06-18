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
    virtual Status submit(const std::vector<std::uint8_t> &report) = 0;

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
    virtual Status close() = 0;

  protected:
    BackendGamepad() = default;
  };

  /**
   * @brief Result returned by an internal backend gamepad creation request.
   */
  struct BackendGamepadCreationResult {
    /**
     * @brief Creation status.
     */
    Status status;

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
