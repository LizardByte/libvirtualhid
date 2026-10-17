/**
 * @file src/platform/esp32/esp32_backend.hpp
 * @brief ESP32-S3 USB bridge backend factory.
 */
#pragma once

// standard includes
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// local includes
#include "core/backend.hpp"
#include "platform/esp32/bridge_client.hpp"

namespace lvh::detail::esp32 {

  /**
   * @brief Bridge port chosen for a runtime.
   */
  struct BridgeSelection {
    std::string port;  ///< Serial port, empty to auto-detect.
    bool implicit = false;  ///< BackendKind::platform_default was redirected; gamepads fall back to the platform backend if no bridge answers.
  };

  /**
   * @brief Decide whether a runtime uses the bridge.
   *
   * BackendKind::esp32_bridge always does. BackendKind::platform_default does
   * when Esp32BridgeOptions::autodetect is set and `LVH_BACKEND=esp32_bridge`,
   * a port (`LVH_ESP32_PORT` or Esp32BridgeOptions::port) is configured, or a
   * bridge is attached; `LVH_BACKEND=platform` keeps the platform backend.
   * `LVH_ESP32_PORT` overrides the configured port in both cases.
   *
   * @param options Runtime options.
   * @param env_backend Value of `LVH_BACKEND`, empty when unset.
   * @param env_port Value of `LVH_ESP32_PORT`, empty when unset.
   * @param bridge_present Probe for an attached bridge; only called when needed.
   * @return Selection, or empty for the platform backend.
   */
  std::optional<BridgeSelection> select_bridge(
    const RuntimeOptions &options,
    std::string_view env_backend,
    std::string_view env_port,
    const std::function<bool()> &bridge_present
  );

  /**
   * @brief Create the bridge backend when select_bridge() picks it.
   *
   * Reads `LVH_BACKEND` and `LVH_ESP32_PORT` from the environment.
   *
   * @param options Runtime options.
   * @return Bridge backend, or `nullptr` to use the platform backend.
   */
  std::unique_ptr<Backend> create_backend_if_selected(const RuntimeOptions &options);

  /**
   * @brief Create the bridge backend with an injected client, used by tests.
   *
   * @param client Bridge client; started lazily on the first device.
   * @param fallback Backend for keyboard and mouse when the bridge is full, Unicode text, and touch or pen devices; `nullptr` reports them unsupported.
   * @param implicit Whether gamepads also fall back when no bridge answers.
   * @return Backend.
   */
  std::unique_ptr<Backend> create_esp32_backend(std::shared_ptr<BridgeClient> client, std::unique_ptr<Backend> fallback, bool implicit = false);

  /**
   * @brief Build the label the bridge stores for this host's pairing.
   *
   * @param application Application name, `libvirtualhid` when empty.
   * @param host Host name, omitted when empty.
   * @return `<application> on <host>`, at most #LVHB_MAX_PAIRING_NAME bytes.
   */
  std::string pairing_client_name(const std::string &application, const std::string &host);

  /**
   * @brief Get this computer's name for pairing labels.
   *
   * @return Host name, or empty when unavailable.
   */
  std::string local_host_name();

}  // namespace lvh::detail::esp32
