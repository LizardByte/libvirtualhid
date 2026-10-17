/**
 * @file src/platform/esp32/serial_port.hpp
 * @brief Minimal serial port abstraction used by the ESP32-S3 bridge backend.
 */
#pragma once

// standard includes
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace lvh::detail::esp32 {

  /**
   * @brief Byte stream to the bridge's CDC-ACM interface.
   */
  class SerialPort {
  public:
    SerialPort(const SerialPort &) = delete;
    SerialPort &operator=(const SerialPort &) = delete;
    SerialPort(SerialPort &&) noexcept = delete;
    SerialPort &operator=(SerialPort &&) noexcept = delete;

    /**
     * @brief Close the port.
     */
    virtual ~SerialPort() = default;

    /**
     * @brief Write every byte or fail.
     *
     * @param data Bytes to write.
     * @return `true` when all bytes were written.
     */
    virtual bool write(std::span<const std::uint8_t> data) = 0;

    /**
     * @brief Read available bytes, waiting up to @p timeout for the first one.
     *
     * @param buffer Destination.
     * @param timeout Maximum wait.
     * @return Bytes read, 0 on timeout, or -1 when the port is gone.
     */
    virtual std::ptrdiff_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) = 0;

    /**
     * @brief Get the port path for diagnostics.
     *
     * @return Port path.
     */
    virtual std::string path() const = 0;

  protected:
    SerialPort() = default;
  };

  /**
   * @brief Opens the bridge port; called again after every re-enumeration.
   *
   * Returns `nullptr` while no bridge port is present.
   */
  using SerialPortOpener = std::function<std::unique_ptr<SerialPort>()>;

  /**
   * @brief Open a serial port in raw 8N1 mode.
   *
   * @param path Device path such as `/dev/ttyACM0` or `COM5`.
   * @return Open port, or `nullptr` on failure.
   */
  std::unique_ptr<SerialPort> open_serial_port(const std::string &path);

  /**
   * @brief List serial ports whose USB serial number starts with the bridge prefix.
   *
   * Supported on Linux and Windows. Other platforms return an empty list and
   * require an explicit port path.
   *
   * @return Matching port paths.
   */
  std::vector<std::string> find_bridge_ports();

  /**
   * @brief Build the default opener for a configured or auto-detected port.
   *
   * @param configured_path Explicit port path, or empty to auto-detect.
   * @return Port opener.
   */
  SerialPortOpener make_serial_port_opener(std::string configured_path);

}  // namespace lvh::detail::esp32
