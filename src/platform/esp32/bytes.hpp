/**
 * @file src/platform/esp32/bytes.hpp
 * @brief Little-endian and hex helpers for the ESP32-S3 bridge protocol.
 */
#pragma once

// standard includes
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace lvh::detail::esp32 {

  /**
   * @brief Append an unsigned integer in little-endian byte order.
   *
   * @param buffer Destination.
   * @param value Value to append.
   */
  template<std::unsigned_integral T>
  void append_le(std::vector<std::uint8_t> &buffer, T value) {
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      buffer.push_back(static_cast<std::uint8_t>(value >> (index * 8U)));
    }
  }

  /**
   * @brief Write an unsigned integer in little-endian byte order.
   *
   * @param buffer Destination with at least `offset + sizeof(T)` bytes.
   * @param offset Byte offset of the value.
   * @param value Value to write.
   */
  template<std::unsigned_integral T>
  void write_le(std::span<std::uint8_t> buffer, std::size_t offset, T value) {
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      buffer[offset + index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
  }

  /**
   * @brief Read an unsigned integer stored in little-endian byte order.
   *
   * @param data Source with at least `sizeof(T)` bytes.
   * @return Decoded value.
   */
  template<std::unsigned_integral T>
  T read_le(std::span<const std::uint8_t> data) {
    T value = 0;
    for (std::size_t index = 0; index < sizeof(T); ++index) {
      value = static_cast<T>(value | (static_cast<T>(data[index]) << (index * 8U)));
    }
    return value;
  }

  /**
   * @brief Append a string prefixed with its one-byte length.
   *
   * @param buffer Destination.
   * @param value String, truncated to @p max_size bytes.
   * @param max_size Largest string length to send.
   */
  inline void append_length_prefixed(std::vector<std::uint8_t> &buffer, std::string_view value, std::size_t max_size) {
    const auto size = value.size() < max_size ? value.size() : max_size;
    buffer.push_back(static_cast<std::uint8_t>(size));
    buffer.insert(buffer.end(), value.begin(), value.begin() + static_cast<std::ptrdiff_t>(size));
  }

  /**
   * @brief Encode bytes as hexadecimal text.
   *
   * @param bytes Bytes to encode.
   * @param upper_case Use `A-F` instead of `a-f`.
   * @return Hex string, two characters per byte.
   */
  inline std::string hex_encode(std::span<const std::uint8_t> bytes, bool upper_case = false) {
    const std::string_view digits = upper_case ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2U);
    for (const auto byte : bytes) {
      text.push_back(digits[byte / 16U]);
      text.push_back(digits[byte % 16U]);
    }
    return text;
  }

  /**
   * @brief Decode hexadecimal text of an exact length.
   *
   * @param text Hex string in either case.
   * @return Decoded bytes, or empty when @p text is not `2 * N` hex digits.
   */
  template<std::size_t N>
  std::optional<std::array<std::uint8_t, N>> hex_decode(std::string_view text) {
    const auto nibble = [](char c) {
      if (c >= '0' && c <= '9') {
        return c - '0';
      }
      if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
      }
      if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
      }
      return -1;
    };
    if (text.size() != N * 2U) {
      return std::nullopt;
    }
    std::array<std::uint8_t, N> bytes {};
    for (std::size_t index = 0; index < N; ++index) {
      const auto high = nibble(text[index * 2U]);
      const auto low = nibble(text[index * 2U + 1U]);
      if (high < 0 || low < 0) {
        return std::nullopt;
      }
      bytes[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return bytes;
  }

}  // namespace lvh::detail::esp32
