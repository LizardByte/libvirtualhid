/**
 * @file src/platform/esp32/credential_store.hpp
 * @brief Storage of ESP32-S3 bridge pairing secrets on the host.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

// local includes
#include "platform/esp32/esp32_bridge_protocol.hpp"

namespace lvh::detail::esp32 {

  /**
   * @brief Secret the bridge issued when this host was paired.
   */
  struct BridgeCredential {
    std::array<std::uint8_t, protocol::key_id_size> key_id {};  ///< Public pairing id.
    std::array<std::uint8_t, protocol::secret_size> secret {};  ///< Shared secret.
  };

  /**
   * @brief Keeps one credential per bridge, keyed by the bridge's device id.
   */
  class CredentialStore {
  public:
    /**
     * @brief Create an empty store.
     */
    CredentialStore() = default;
    CredentialStore(const CredentialStore &) = delete;
    CredentialStore &operator=(const CredentialStore &) = delete;
    CredentialStore(CredentialStore &&) noexcept = delete;
    CredentialStore &operator=(CredentialStore &&) noexcept = delete;
    virtual ~CredentialStore() = default;

    /**
     * @brief Load the credential for a bridge.
     *
     * @param device_id Bridge device id as upper-case hex.
     * @return Credential, or empty when the bridge was never paired.
     */
    virtual std::optional<BridgeCredential> load(const std::string &device_id) = 0;

    /**
     * @brief Store the credential for a bridge.
     *
     * @param device_id Bridge device id as upper-case hex.
     * @param credential Credential.
     * @return `true` when stored.
     */
    virtual bool save(const std::string &device_id, const BridgeCredential &credential) = 0;

    /**
     * @brief Forget the credential for a bridge.
     *
     * @param device_id Bridge device id as upper-case hex.
     */
    virtual void erase(const std::string &device_id) = 0;
  };

  /**
   * @brief Default directory for credential files.
   *
   * `%ProgramData%\libvirtualhid\esp32-bridge` on Windows,
   * `$XDG_CONFIG_HOME/libvirtualhid/esp32-bridge` (or `~/.config/...`) elsewhere.
   *
   * @return Directory path, or empty when it cannot be determined.
   */
  std::string default_credential_directory();

  /**
   * @brief Store credentials as one file per bridge in a directory.
   *
   * Files are readable only by their owner (POSIX mode 0600; on Windows a
   * protected DACL for SYSTEM, Administrators, and the file owner).
   *
   * @param directory Directory, created when missing; empty selects default_credential_directory().
   * @return Store; one that persists nothing when no directory can be determined.
   */
  std::shared_ptr<CredentialStore> make_file_credential_store(std::string directory);

}  // namespace lvh::detail::esp32
