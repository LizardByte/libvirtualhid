/**
 * @file src/platform/windows/shared/switch_pro_protocol.hpp
 * @brief Nintendo Switch Pro initialization replies used by the Windows VHF driver.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace lvh::detail::windows {

  inline constexpr std::size_t switch_pro_report_size = 64U;
  using SwitchProReport = std::array<std::uint8_t, switch_pro_report_size>;

  namespace switch_pro_protocol_detail {

    inline constexpr std::array<std::uint8_t, 6> controller_mac {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

    inline constexpr std::array<std::uint8_t, 18> factory_stick_calibration {
      0xFF,
      0xF7,
      0x7F,
      0x00,
      0x08,
      0x80,
      0xFF,
      0xF7,
      0x7F,
      0x00,
      0x08,
      0x80,
      0xFF,
      0xF7,
      0x7F,
      0xFF,
      0xF7,
      0x7F,
    };

    inline constexpr std::array<std::uint8_t, 24> factory_imu_calibration {
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x40,
      0x00,
      0x40,
      0x00,
      0x40,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x00,
      0x3B,
      0x34,
      0x3B,
      0x34,
      0x3B,
      0x34,
    };

    constexpr std::uint32_t read_u32(std::span<const std::uint8_t> report, std::size_t offset) {
      return static_cast<std::uint32_t>(report[offset]) |
             (static_cast<std::uint32_t>(report[offset + 1U]) << 8U) |
             (static_cast<std::uint32_t>(report[offset + 2U]) << 16U) |
             (static_cast<std::uint32_t>(report[offset + 3U]) << 24U);
    }

    template<std::size_t Size>
    void copy_spi_data(
      SwitchProReport &reply,
      std::uint32_t requested_address,
      std::uint8_t requested_size,
      std::uint32_t data_address,
      const std::array<std::uint8_t, Size> &data
    ) {
      constexpr auto reply_data_offset = 20U;
      const auto requested_end = static_cast<std::uint64_t>(requested_address) + requested_size;
      const auto data_end = static_cast<std::uint64_t>(data_address) + data.size();
      const auto copy_begin = std::max<std::uint64_t>(requested_address, data_address);
      const auto copy_end = std::min(requested_end, data_end);
      if (copy_begin >= copy_end) {
        return;
      }

      const auto source_offset = static_cast<std::size_t>(copy_begin - data_address);
      const auto destination_offset = reply_data_offset + static_cast<std::size_t>(copy_begin - requested_address);
      if (destination_offset >= reply.size()) {
        return;
      }

      const auto copy_size = std::min(
        static_cast<std::size_t>(copy_end - copy_begin),
        reply.size() - destination_offset
      );
      std::copy_n(
        data.begin() + static_cast<std::ptrdiff_t>(source_offset),
        copy_size,
        reply.begin() + static_cast<std::ptrdiff_t>(destination_offset)
      );
    }

    inline void set_neutral_controller_state(SwitchProReport &reply, std::uint8_t timer) {
      reply[1] = timer;
      reply[2] = 0x81;  // Full battery, connected over USB.
      reply[6] = 0x00;
      reply[7] = 0x08;
      reply[8] = 0x80;
      reply[9] = 0x00;
      reply[10] = 0x08;
      reply[11] = 0x80;
    }

    inline void set_spi_reply(SwitchProReport &reply, std::span<const std::uint8_t> output_report) {
      constexpr auto subcommand_data_offset = 11U;
      constexpr auto reply_data_offset = 15U;
      const auto address = read_u32(output_report, subcommand_data_offset);
      const auto size = output_report[subcommand_data_offset + 4U];
      std::copy_n(
        output_report.begin() + static_cast<std::ptrdiff_t>(subcommand_data_offset),
        5U,
        reply.begin() + static_cast<std::ptrdiff_t>(reply_data_offset)
      );

      copy_spi_data(reply, address, size, 0x603DU, factory_stick_calibration);
      copy_spi_data(reply, address, size, 0x6020U, factory_imu_calibration);
    }

    inline void set_device_info_reply(SwitchProReport &reply) {
      constexpr auto data_offset = 15U;
      reply[data_offset] = 0x04;
      reply[data_offset + 1U] = 0x33;
      reply[data_offset + 2U] = 0x03;  // Pro Controller.
      reply[data_offset + 3U] = 0x02;
      std::ranges::copy(controller_mac, reply.begin() + static_cast<std::ptrdiff_t>(data_offset + 4U));
      reply[data_offset + 10U] = 0x01;
      reply[data_offset + 11U] = 0x01;
    }

  }  // namespace switch_pro_protocol_detail

  inline std::optional<SwitchProReport> make_switch_pro_reply(std::span<const std::uint8_t> output_report) {
    if (output_report.empty()) {
      return std::nullopt;
    }

    if (output_report[0] == 0x80U) {
      if (output_report.size() < 2U || output_report[1] == 0x04U) {
        return std::nullopt;
      }

      SwitchProReport reply {};
      reply[0] = 0x81;
      reply[1] = output_report[1];
      if (output_report[1] == 0x01U) {
        reply[3] = 0x03;  // Pro Controller.
        std::copy(
          switch_pro_protocol_detail::controller_mac.rbegin(),
          switch_pro_protocol_detail::controller_mac.rend(),
          reply.begin() + 4
        );
      }
      return reply;
    }

    if (output_report[0] != 0x01U || output_report.size() < 11U) {
      return std::nullopt;
    }

    const auto subcommand = output_report[10];
    if (subcommand == 0x10U && output_report.size() < 16U) {
      return std::nullopt;
    }

    SwitchProReport reply {};
    reply[0] = 0x21;
    switch_pro_protocol_detail::set_neutral_controller_state(reply, output_report[1]);
    auto acknowledgement = std::uint8_t {0x80};
    if (subcommand == 0x10U) {
      acknowledgement = 0x90;
    } else if (subcommand == 0x02U) {
      acknowledgement = 0x82;
    }
    reply[13] = acknowledgement;
    reply[14] = subcommand;
    if (subcommand == 0x10U) {
      switch_pro_protocol_detail::set_spi_reply(reply, output_report);
    } else if (subcommand == 0x02U) {
      switch_pro_protocol_detail::set_device_info_reply(reply);
    }
    return reply;
  }

}  // namespace lvh::detail::windows
