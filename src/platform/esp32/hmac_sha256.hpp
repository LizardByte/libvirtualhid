/**
 * @file src/platform/esp32/hmac_sha256.hpp
 * @brief HMAC-SHA256 for ESP32-S3 bridge authentication.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <span>

namespace lvh::detail::esp32 {

  /**
   * @brief Compute SHA-256.
   *
   * @param data Input bytes.
   * @return Digest.
   */
  std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data);

  /**
   * @brief Compute HMAC-SHA256 (RFC 2104).
   *
   * @param key Key bytes.
   * @param message Message bytes.
   * @return Authentication code.
   */
  std::array<std::uint8_t, 32> hmac_sha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> message);

}  // namespace lvh::detail::esp32
