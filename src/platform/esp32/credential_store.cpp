/**
 * @file src/platform/esp32/credential_store.cpp
 * @brief Storage of ESP32-S3 bridge pairing secrets on the host.
 */

// standard includes
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <utility>

// local includes
#include "platform/esp32/bytes.hpp"
#include "platform/esp32/credential_store.hpp"

// lib includes
#include <lizardbyte/common/env.h>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
// clang-format off
  #include <sddl.h>
// clang-format on
#else
  #include <fcntl.h>
  #include <unistd.h>
#endif

namespace lvh::detail::esp32 {
  namespace {

    /**
     * @brief Write a file readable only by its owner, replacing any previous version atomically.
     *
     * @param path Destination.
     * @param contents File contents.
     * @return `true` when the file was written.
     */
    bool write_private_file(const std::filesystem::path &path, const std::string &contents) {
      auto temporary = path;
      temporary += ".tmp";
#ifdef _WIN32
      // A protected DACL: SYSTEM, Administrators, and the file owner only.
      PSECURITY_DESCRIPTOR descriptor = nullptr;
      if (!::ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;OW)", SDDL_REVISION_1, &descriptor, nullptr)) {
        return false;
      }
      SECURITY_ATTRIBUTES attributes {sizeof(attributes), descriptor, FALSE};
      const HANDLE handle = ::CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      ::LocalFree(descriptor);
      if (handle == INVALID_HANDLE_VALUE) {
        return false;
      }
      DWORD written = 0;
      const auto ok = ::WriteFile(handle, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr) && written == contents.size() && ::FlushFileBuffers(handle);
      ::CloseHandle(handle);
      // Renaming keeps the temporary file's DACL; replacing in place would keep the old one.
      if (!ok || !::MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(temporary.c_str());
        return false;
      }
      return true;
#else
      const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
      if (fd < 0) {
        return false;
      }
      const auto written = ::write(fd, contents.data(), contents.size());
      const auto ok = written == static_cast<ssize_t>(contents.size()) && ::fsync(fd) == 0;
      ::close(fd);
      if (!ok || std::rename(temporary.c_str(), path.c_str()) != 0) {
        ::unlink(temporary.c_str());
        return false;
      }
      return true;
#endif
    }

    /**
     * @brief One `bridge-<device id>.key` file per bridge holding `key_id secret` as hex.
     */
    class FileCredentialStore final: public CredentialStore {
    public:
      /**
       * @brief Create a store rooted at a directory.
       *
       * @param directory Directory, created on the first save.
       */
      explicit FileCredentialStore(std::filesystem::path directory):
          directory_ {std::move(directory)} {}

      std::optional<BridgeCredential> load(const std::string &device_id) override {
        std::ifstream file {path_for(device_id)};
        std::string key_id_text;
        std::string secret_text;
        if (!(file >> key_id_text >> secret_text)) {
          return std::nullopt;
        }
        const auto key_id = hex_decode<LVHB_KEY_ID_SIZE>(key_id_text);
        const auto secret = hex_decode<LVHB_SECRET_SIZE>(secret_text);
        if (!key_id.has_value() || !secret.has_value()) {
          return std::nullopt;
        }
        return BridgeCredential {*key_id, *secret};
      }

      bool save(const std::string &device_id, const BridgeCredential &credential) override {
        std::error_code error;
        std::filesystem::create_directories(directory_, error);
        if (error) {
          return false;
        }
        return write_private_file(path_for(device_id), hex_encode(credential.key_id) + " " + hex_encode(credential.secret) + "\n");
      }

      void erase(const std::string &device_id) override {
        std::error_code error;
        std::filesystem::remove(path_for(device_id), error);
      }

    private:
      std::filesystem::path path_for(const std::string &device_id) const {
        return directory_ / ("bridge-" + device_id + ".key");
      }

      std::filesystem::path directory_;  ///< Directory holding the key files.
    };

    /**
     * @brief Store used when no credential directory can be determined; nothing persists.
     */
    class NullCredentialStore final: public CredentialStore {
    public:
      std::optional<BridgeCredential> load(const std::string &) override {
        return std::nullopt;
      }

      bool save(const std::string &, const BridgeCredential &) override {
        return false;
      }

      void erase(const std::string &) override {}
    };

  }  // namespace

  std::string default_credential_directory() {
#ifdef _WIN32
    const auto base = lizardbyte::common::get_env("ProgramData");
    return base.empty() ? std::string {} : (std::filesystem::path {base} / "libvirtualhid" / "esp32-bridge").string();
#else
    if (const auto xdg = lizardbyte::common::get_env("XDG_CONFIG_HOME"); !xdg.empty()) {
      return (std::filesystem::path {xdg} / "libvirtualhid" / "esp32-bridge").string();
    }
    const auto home = lizardbyte::common::get_env("HOME");
    return home.empty() ? std::string {} : (std::filesystem::path {home} / ".config" / "libvirtualhid" / "esp32-bridge").string();
#endif
  }

  std::shared_ptr<CredentialStore> make_file_credential_store(std::string directory) {
    if (directory.empty()) {
      directory = default_credential_directory();
    }
    if (directory.empty()) {
      // Never fall back to the working directory: a relative path would read whatever happens to be there.
      return std::make_shared<NullCredentialStore>();
    }
    return std::make_shared<FileCredentialStore>(std::move(directory));
  }

}  // namespace lvh::detail::esp32
