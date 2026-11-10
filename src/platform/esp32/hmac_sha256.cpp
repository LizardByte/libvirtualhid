/**
 * @file src/platform/esp32/hmac_sha256.cpp
 * @brief HMAC-SHA256 for ESP32-S3 bridge authentication.
 */

// standard includes
#include <algorithm>
#include <cstddef>
#include <vector>

// local includes
#include "platform/esp32/hmac_sha256.hpp"

namespace lvh::detail::esp32 {
  namespace {

    constexpr std::array<std::uint32_t, 64> round_constants {
      0x428a2f98U,
      0x71374491U,
      0xb5c0fbcfU,
      0xe9b5dba5U,
      0x3956c25bU,
      0x59f111f1U,
      0x923f82a4U,
      0xab1c5ed5U,
      0xd807aa98U,
      0x12835b01U,
      0x243185beU,
      0x550c7dc3U,
      0x72be5d74U,
      0x80deb1feU,
      0x9bdc06a7U,
      0xc19bf174U,
      0xe49b69c1U,
      0xefbe4786U,
      0x0fc19dc6U,
      0x240ca1ccU,
      0x2de92c6fU,
      0x4a7484aaU,
      0x5cb0a9dcU,
      0x76f988daU,
      0x983e5152U,
      0xa831c66dU,
      0xb00327c8U,
      0xbf597fc7U,
      0xc6e00bf3U,
      0xd5a79147U,
      0x06ca6351U,
      0x14292967U,
      0x27b70a85U,
      0x2e1b2138U,
      0x4d2c6dfcU,
      0x53380d13U,
      0x650a7354U,
      0x766a0abbU,
      0x81c2c92eU,
      0x92722c85U,
      0xa2bfe8a1U,
      0xa81a664bU,
      0xc24b8b70U,
      0xc76c51a3U,
      0xd192e819U,
      0xd6990624U,
      0xf40e3585U,
      0x106aa070U,
      0x19a4c116U,
      0x1e376c08U,
      0x2748774cU,
      0x34b0bcb5U,
      0x391c0cb3U,
      0x4ed8aa4aU,
      0x5b9cca4fU,
      0x682e6ff3U,
      0x748f82eeU,
      0x78a5636fU,
      0x84c87814U,
      0x8cc70208U,
      0x90befffaU,
      0xa4506cebU,
      0xbef9a3f7U,
      0xc67178f2U,
    };

    constexpr std::uint32_t rotr(std::uint32_t value, unsigned bits) {
      return (value >> bits) | (value << (32U - bits));
    }

    void compress(std::array<std::uint32_t, 8> &state, const std::uint8_t *block) {
      std::array<std::uint32_t, 64> w {};
      for (std::size_t index = 0; index < 16U; ++index) {
        w[index] = (static_cast<std::uint32_t>(block[index * 4U]) << 24U) | (static_cast<std::uint32_t>(block[index * 4U + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(block[index * 4U + 2U]) << 8U) | static_cast<std::uint32_t>(block[index * 4U + 3U]);
      }
      for (std::size_t index = 16; index < 64U; ++index) {
        const auto s0 = rotr(w[index - 15U], 7U) ^ rotr(w[index - 15U], 18U) ^ (w[index - 15U] >> 3U);
        const auto s1 = rotr(w[index - 2U], 17U) ^ rotr(w[index - 2U], 19U) ^ (w[index - 2U] >> 10U);
        w[index] = w[index - 16U] + s0 + w[index - 7U] + s1;
      }
      auto [a, b, c, d, e, f, g, h] = state;
      for (std::size_t index = 0; index < 64U; ++index) {
        const auto s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
        const auto choice = (e & f) ^ (~e & g);
        const auto temp1 = h + s1 + choice + round_constants[index] + w[index];
        const auto s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        const auto temp2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
      }
      state[0] += a;
      state[1] += b;
      state[2] += c;
      state[3] += d;
      state[4] += e;
      state[5] += f;
      state[6] += g;
      state[7] += h;
    }

  }  // namespace

  std::array<std::uint8_t, 32> sha256(std::span<const std::uint8_t> data) {
    std::array<std::uint32_t, 8> state {0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU, 0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::vector<std::uint8_t> padded {data.begin(), data.end()};
    padded.push_back(0x80U);
    while (padded.size() % 64U != 56U) {
      padded.push_back(0U);
    }
    const auto bits = static_cast<std::uint64_t>(data.size()) * 8U;
    for (int shift = 56; shift >= 0; shift -= 8) {
      padded.push_back(static_cast<std::uint8_t>(bits >> static_cast<unsigned>(shift)));
    }
    for (std::size_t offset = 0; offset < padded.size(); offset += 64U) {
      compress(state, padded.data() + offset);
    }
    std::array<std::uint8_t, 32> digest {};
    for (std::size_t index = 0; index < state.size(); ++index) {
      digest[index * 4U] = static_cast<std::uint8_t>(state[index] >> 24U);
      digest[index * 4U + 1U] = static_cast<std::uint8_t>(state[index] >> 16U);
      digest[index * 4U + 2U] = static_cast<std::uint8_t>(state[index] >> 8U);
      digest[index * 4U + 3U] = static_cast<std::uint8_t>(state[index]);
    }
    return digest;
  }

  std::array<std::uint8_t, 32> hmac_sha256(std::span<const std::uint8_t> key, std::span<const std::uint8_t> message) {
    std::array<std::uint8_t, 64> block {};
    if (key.size() > block.size()) {
      const auto hashed = sha256(key);
      std::ranges::copy(hashed, block.begin());
    } else {
      std::ranges::copy(key, block.begin());
    }
    std::vector<std::uint8_t> inner(block.size() + message.size());
    std::vector<std::uint8_t> outer(block.size() + 32U);
    for (std::size_t index = 0; index < block.size(); ++index) {
      inner[index] = std::to_integer<std::uint8_t>(std::byte {block[index]} ^ std::byte {0x36});
      outer[index] = std::to_integer<std::uint8_t>(std::byte {block[index]} ^ std::byte {0x5c});
    }
    std::ranges::copy(message, inner.begin() + static_cast<std::ptrdiff_t>(block.size()));
    const auto inner_hash = sha256(inner);
    std::ranges::copy(inner_hash, outer.begin() + static_cast<std::ptrdiff_t>(block.size()));
    return sha256(outer);
  }

}  // namespace lvh::detail::esp32
