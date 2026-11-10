/**
 * @file src/include/libvirtualhid/esp32_bridge.hpp
 * @brief ESP32-S3 USB bridge discovery helpers.
 */
#pragma once

// standard includes
#include <string>
#include <vector>

namespace lvh {

  /**
   * @brief List serial ports of attached ESP32-S3 USB bridges.
   *
   * Bridges are recognized by their `LVHB-` USB serial number. Discovery is
   * supported on Linux and Windows; other platforms return an empty list.
   *
   * @return Port paths such as `COM5` or `/dev/ttyACM0`.
   */
  std::vector<std::string> find_esp32_bridge_ports();

  /**
   * @brief State of the ESP32-S3 bridge used by this process.
   */
  struct Esp32BridgeStatus {
    bool active = false;  ///< A runtime in this process uses an ESP32-S3 bridge.
    bool connected = false;  ///< The bridge answered and accepts input from this host.
    bool awaiting_pairing = false;  ///< The bridge waits for its BOOT button to pair this host.
    bool pairing_required = false;  ///< The bridge requires pairing (reported by the last handshake).
    std::string port;  ///< Open serial port, empty while disconnected.
    std::string device_id;  ///< Bridge device id (upper-case hex MAC), empty before the first handshake.
    std::string firmware_version;  ///< Firmware version from the last handshake.
  };

  /**
   * @brief Get the state of the bridge used by the most recently started bridge runtime.
   *
   * Does not open any port. Returns `active == false` when no runtime in this
   * process uses the bridge.
   *
   * @return Bridge state.
   */
  Esp32BridgeStatus esp32_bridge_status();

}  // namespace lvh
