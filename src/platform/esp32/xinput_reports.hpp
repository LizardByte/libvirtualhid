/**
 * @file src/platform/esp32/xinput_reports.hpp
 * @brief Xbox 360 wired (XUSB) report packing for the ESP32-S3 bridge.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <span>
#include <vector>

// local includes
#include "platform/esp32/esp32_bridge_protocol.hpp"

#include <libvirtualhid/types.hpp>

namespace lvh::detail::esp32 {

  /**
   * @brief Pack normalized state into an Xbox 360 wired (XUSB) input report.
   *
   * @param state Gamepad state.
   * @return 20-byte XUSB report.
   */
  std::array<std::uint8_t, protocol::xinput_report_size> pack_xinput_report(const GamepadState &state);

  /**
   * @brief Translate an XUSB interrupt OUT report into gamepad outputs.
   *
   * @param report Raw output report.
   * @return Rumble or player LED outputs; empty for unknown reports.
   */
  std::vector<GamepadOutput> parse_xinput_output(std::span<const std::uint8_t> report);

}  // namespace lvh::detail::esp32
