/**
 * @file src/platform/windows/shared/generic_pid_protocol.hpp
 * @brief Windows DirectInput PID force-feedback helpers for the Generic Controller.
 */
#pragma once

// standard includes
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace lvh::detail::windows {

  inline constexpr std::uint8_t generic_pid_set_effect_report_id = 0x11;
  inline constexpr std::uint8_t generic_pid_set_envelope_report_id = 0x12;
  inline constexpr std::uint8_t generic_pid_set_periodic_report_id = 0x14;
  inline constexpr std::uint8_t generic_pid_set_constant_force_report_id = 0x15;
  inline constexpr std::uint8_t generic_pid_effect_operation_report_id = 0x1A;
  inline constexpr std::uint8_t generic_pid_block_free_report_id = 0x1B;
  inline constexpr std::uint8_t generic_pid_device_control_report_id = 0x1C;
  inline constexpr std::uint8_t generic_pid_device_gain_report_id = 0x1D;

  inline constexpr std::uint8_t generic_pid_create_new_effect_report_id = 0x11;
  inline constexpr std::uint8_t generic_pid_block_load_report_id = 0x12;
  inline constexpr std::uint8_t generic_pid_pool_report_id = 0x13;
  inline constexpr std::uint8_t generic_pid_state_report_id = 0x14;

  inline constexpr std::size_t generic_pid_max_effects = 40;
  inline constexpr std::size_t generic_pid_output_report_size = 22;

  namespace generic_pid_detail {

    inline constexpr std::array<std::uint8_t, 6> legacy_rumble_prefix {0xA1, 0x02, 0x05, 0x0F, 0x09, 0x97};
    inline constexpr std::array<std::uint8_t, 6> gamepad_collection_prefix {0x05, 0x01, 0x09, 0x05, 0xA1, 0x01};

    inline std::optional<std::uint8_t> hex_nibble(char value) {
      if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
      }
      if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
      }
      if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
      }
      return std::nullopt;
    }

    inline void append_hex(std::vector<std::uint8_t> &target, std::string_view hex) {
      std::optional<std::uint8_t> high;
      for (const auto character : hex) {
        const auto nibble = hex_nibble(character);
        if (!nibble.has_value()) {
          continue;
        }
        if (!high.has_value()) {
          high = nibble;
          continue;
        }
        const auto byte_value = static_cast<unsigned int>(*high) * 16U + static_cast<unsigned int>(*nibble);
        target.push_back(static_cast<std::uint8_t>(byte_value));
        high.reset();
      }
    }

    inline std::span<const std::uint8_t> payload(
      std::uint8_t report_id,
      std::span<const std::uint8_t> report
    ) {
      if (!report.empty() && report.front() == report_id) {
        return report.subspan(1U);
      }
      return report;
    }

    inline std::uint16_t read_u16(std::span<const std::uint8_t> report, std::size_t offset) {
      return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(report[offset]) +
        static_cast<std::uint16_t>(report[offset + 1U]) * 256U
      );
    }

  }  // namespace generic_pid_detail

  /**
   * @brief Replace the built-in Generic Controller's placeholder output block with DirectInput PID reports.
   *
   * The report layout is the constant-force and sine-periodic subset of the
   * PID descriptor proven by HIDMaestro. DirectInput's PID mapper requires a
   * Joystick top-level collection; exposing PID reports from a Game Pad
   * collection can fault pid.dll during effect creation.
   *
   * @param source Built-in Generic Controller report descriptor.
   * @return Windows descriptor, or an empty vector when @p source is not the expected built-in layout.
   */
  inline std::vector<std::uint8_t> make_generic_pid_report_descriptor(
    const std::vector<std::uint8_t> &source
  ) {
    using namespace generic_pid_detail;

    if (source.size() < gamepad_collection_prefix.size() + 1U ||
        !std::ranges::equal(std::span {source}.first(gamepad_collection_prefix.size()), gamepad_collection_prefix) ||
        source.back() != 0xC0) {
      return {};
    }

    const auto legacy = std::ranges::search(source, legacy_rumble_prefix);
    if (legacy.begin() == source.end()) {
      return {};
    }

    std::vector<std::uint8_t> descriptor(source.begin(), legacy.begin());
    descriptor[3] = 0x04;  // Usage (Joystick), required by DirectInput's PID mapper.

    // Derived from HIDMaestro's MIT-licensed MinimumViablePidFfbBlock.
    // Only the two effects normalized as gamepad rumble are advertised: a
    // constant force and a sine-periodic force. Omitting the other effect
    // usages prevents consumers from creating effects this backend cannot
    // render while retaining the standard PID allocation/control contract.
    append_hex(descriptor, "050F");

    // Set Effect (Output, ID 0x11). Effect array entries are Constant and Sine.
    append_hex(
      descriptor,
      "0921A1028511092215012528350145287508950191020925A1020926093125021501350145027508"
      "95019100C009500954095109A7150026FF7F350046FF7F66031055FD7510950491025500660000"
      "0952150026FF003500461027750895019102095315012508350145087508950191020955A1020501"
      "0930093115002501750195029102C0050F095695019102950591030957A1020B01000A000B0200"
      "0A0066140055FE150027FF7F0000350047A08C00006600007510950291025500660000C0050F"
      "0958A1020B01000A000B02000A0026FD7F751095029102C0C0"
    );

    // Set Envelope (Output, ID 0x12).
    append_hex(
      descriptor,
      "095AA102851209221501252835014528750895019102095B095D1600002610273600004610277510"
      "95029102095C095E66031055FD27FF7F000047FF7F00007520910245006600005500C0"
    );

    // Set Periodic (Output, ID 0x14).
    append_hex(
      descriptor,
      "096EA102851409221501252835014528750895019102097016000026102736000046102775109501"
      "9102096F16F0D826102736F0D8461027950175109102097166140055FE1500279F8C0000350047"
      "9F8C00007510950191020972150027FF7F0000350047FF7F000066031055FD7520950191026600"
      "005500C0"
    );

    // Set Constant Force (Output, ID 0x15).
    append_hex(
      descriptor,
      "0973A102851509221501252835014528750895019102097016F0D826102736F0D846102775109501"
      "9102C0"
    );

    // Effect Operation, Block Free, Device Control, and Device Gain outputs.
    append_hex(
      descriptor,
      "050F0977A102851A092215012528350145287508950191020978A1020979097A097B150125037508"
      "95019100C0097C150026FF00350046FF009102C0"
      "0990A102851B09222528150135014528750895019102C0"
      "0996A102851C099709980999099A099B099C15012506750895019100C0"
      "097DA102851D097E150026FF003500461027750895019102C0"
    );

    // Create New Effect is deliberately the only declared Feature report.
    // HIDMaestro found that also declaring Block Load, Pool, and State makes
    // pid.dll fault during CreateEffect; its working UMDF path serves those
    // three report IDs from GetFeature without descriptor Feature items.
    // See https://github.com/hifihedgehog/HIDMaestro/blob/master/sdk/HIDMaestro.Core/HidDescriptorBuilder.cs
    // Create New Effect (Feature, ID 0x11), with the same two effect types.
    append_hex(
      descriptor,
      "09ABA10285110925A10209260931250215013501450275089501B100C00501093B150026FF013500"
      "46FF01750A9501B1027506B101C0"
    );

    descriptor.push_back(0xC0);  // End Joystick application collection.
    return descriptor;
  }

  /**
   * @brief Convert the built-in Generic Controller trigger bytes to Windows DirectInput polarity.
   *
   * @param report Packed, platform-neutral Generic Controller input report.
   * @return Report with DirectInput's idle-at-maximum Z/Rz convention.
   */
  inline std::vector<std::uint8_t> make_generic_windows_input_report(
    const std::vector<std::uint8_t> &report
  ) {
    constexpr std::size_t left_trigger_offset = 7;
    constexpr std::size_t right_trigger_offset = 8;
    if (report.size() <= right_trigger_offset) {
      return report;
    }

    auto windows_report = report;
    windows_report[left_trigger_offset] = static_cast<std::uint8_t>(255U - windows_report[left_trigger_offset]);
    windows_report[right_trigger_offset] = static_cast<std::uint8_t>(255U - windows_report[right_trigger_offset]);
    return windows_report;
  }

  /**
   * @brief Driver-side state for the PID Create/Load/Pool/State feature handshake.
   */
  class GenericPidFeatureState {
  public:
    bool handle_set_feature(std::uint8_t report_id, std::span<const std::uint8_t> report) {
      const auto data = generic_pid_detail::payload(report_id, report);
      switch (report_id) {
        case generic_pid_create_new_effect_report_id:
          create_effect(data);
          return true;
        case generic_pid_block_free_report_id:
          if (!data.empty()) {
            free_effect(data[0]);
          }
          return true;
        case generic_pid_device_control_report_id:
          if (!data.empty()) {
            device_control(data[0]);
          }
          return true;
        default:
          return false;
      }
    }

    bool handle_output_report(std::uint8_t report_id, std::span<const std::uint8_t> report) {
      const auto data = generic_pid_detail::payload(report_id, report);
      switch (report_id) {
        case generic_pid_effect_operation_report_id:
          if (data.size() >= 2U) {
            effect_operation(data[0], data[1]);
          }
          return true;
        case generic_pid_block_free_report_id:
          if (!data.empty()) {
            free_effect(data[0]);
          }
          return true;
        case generic_pid_device_control_report_id:
          if (!data.empty()) {
            device_control(data[0]);
          }
          return true;
        default:
          return false;
      }
    }

    std::optional<std::vector<std::uint8_t>> get_feature_report(std::uint8_t report_id) const {
      switch (report_id) {
        case generic_pid_create_new_effect_report_id:
          return std::vector<std::uint8_t> {report_id, 0, 0, 0};
        case generic_pid_block_load_report_id:
          {
            const auto available = ram_pool_available();
            return std::vector<std::uint8_t> {
              report_id,
              last_effect_block_index_,
              load_status_,
              static_cast<std::uint8_t>(available % 256U),
              static_cast<std::uint8_t>(available / 256U),
            };
          }
        case generic_pid_pool_report_id:
          return std::vector<std::uint8_t> {
            report_id,
            0xFF,
            0xFF,
            static_cast<std::uint8_t>(generic_pid_max_effects),
            0x01,  // Device-managed pool; parameters are not shared.
          };
        case generic_pid_state_report_id:
          return std::vector<std::uint8_t> {
            report_id,
            state_effect_block_index_,
            std::to_integer<std::uint8_t>(state_flags_),
          };
        default:
          return std::nullopt;
      }
    }

  private:
    static constexpr std::uint8_t load_success = 1;
    static constexpr std::uint8_t load_full = 2;
    static constexpr std::uint8_t load_error = 3;
    static constexpr std::byte state_device_paused {0x01};
    static constexpr std::byte state_actuators_enabled {0x02};
    static constexpr std::byte state_actuator_power {0x10};
    static constexpr std::byte state_effect_playing {0x20};

    void create_effect(std::span<const std::uint8_t> data) {
      // Array values 1 and 2 select the advertised Constant and Sine usages.
      if (data.empty() || data[0] < 1U || data[0] > 2U) {
        last_effect_block_index_ = 0;
        load_status_ = load_error;
        return;
      }

      for (std::size_t index = 0; index < allocated_.size(); ++index) {
        if (allocated_[index]) {
          continue;
        }
        allocated_[index] = true;
        last_effect_block_index_ = static_cast<std::uint8_t>(index + 1U);
        load_status_ = load_success;
        return;
      }

      last_effect_block_index_ = 0;
      load_status_ = load_full;
    }

    void free_effect(std::uint8_t effect_block_index) {
      if (effect_block_index == 0U || effect_block_index > allocated_.size()) {
        return;
      }
      allocated_[effect_block_index - 1U] = false;
      if (state_effect_block_index_ == effect_block_index) {
        state_effect_block_index_ = 0;
        state_flags_ &= ~state_effect_playing;
      }
    }

    void effect_operation(std::uint8_t effect_block_index, std::uint8_t operation) {
      if (effect_block_index == 0U || effect_block_index > allocated_.size() ||
          !allocated_[effect_block_index - 1U]) {
        return;
      }
      state_effect_block_index_ = effect_block_index;
      if (operation == 1U || operation == 2U) {
        state_flags_ |= state_effect_playing;
      } else if (operation == 3U) {
        state_flags_ &= ~state_effect_playing;
      }
    }

    void device_control(std::uint8_t command) {
      switch (command) {
        case 1:  // Enable actuators.
          state_flags_ |= state_actuators_enabled | state_actuator_power;
          break;
        case 2:  // Disable actuators.
          state_flags_ &= ~state_actuators_enabled;
          break;
        case 3:  // Stop all effects.
          state_effect_block_index_ = 0;
          state_flags_ &= ~state_effect_playing;
          break;
        case 4:  // Device reset.
          allocated_.fill(false);
          last_effect_block_index_ = 0;
          load_status_ = 0;
          state_effect_block_index_ = 0;
          state_flags_ = state_actuators_enabled | state_actuator_power;
          break;
        case 5:  // Device pause.
          state_flags_ |= state_device_paused;
          break;
        case 6:  // Device continue.
          state_flags_ &= ~state_device_paused;
          break;
        default:
          break;
      }
    }

    std::uint16_t ram_pool_available() const {
      const auto allocated_count = static_cast<std::uint16_t>(std::ranges::count(allocated_, true));
      return static_cast<std::uint16_t>(0xFFFFU - allocated_count);
    }

    std::array<bool, generic_pid_max_effects> allocated_ {};
    std::uint8_t last_effect_block_index_ = 0;
    std::uint8_t load_status_ = 0;
    std::uint8_t state_effect_block_index_ = 0;
    std::byte state_flags_ = state_actuators_enabled | state_actuator_power;
  };

}  // namespace lvh::detail::windows
