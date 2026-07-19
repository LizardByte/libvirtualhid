/**
 * @file src/platform/windows/shared/generic_pid_rumble.hpp
 * @brief Runtime-only Windows DirectInput PID rumble scheduling helpers.
 */
#pragma once

// local includes
#include "generic_pid_protocol.hpp"

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace lvh::detail::windows {

  namespace generic_pid_detail {

    inline std::uint16_t scale_force(std::int16_t value, std::uint8_t effect_gain, std::uint8_t device_gain) {
      constexpr auto maximum_pid_force = 10000.0;
      const auto magnitude = std::min(std::abs(static_cast<int>(value)), static_cast<int>(maximum_pid_force));
      const auto scaled = static_cast<double>(magnitude) / maximum_pid_force * 65535.0 *
                          (static_cast<double>(effect_gain) / 255.0) *
                          (static_cast<double>(device_gain) / 255.0);
      return static_cast<std::uint16_t>(std::lround(std::clamp(scaled, 0.0, 65535.0)));
    }

  }  // namespace generic_pid_detail

  struct GenericPidRumbleUpdate {
    bool recognized = false;
    bool rumble_changed = false;
    std::uint16_t strength = 0;
  };

  /**
   * @brief Stateful decoder for DirectInput PID constant and sine effects.
   */
  class GenericPidRumbleState {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    GenericPidRumbleUpdate handle_output_report(
      std::span<const std::uint8_t> report,
      TimePoint now = Clock::now()
    ) {
      if (report.empty()) {
        return {};
      }

      expire_effects(now);
      const auto report_id = report.front();
      const auto data = report.subspan(1U);
      switch (report_id) {
        case generic_pid_set_effect_report_id:
          return handle_set_effect(data, now);
        case generic_pid_set_envelope_report_id:
          return {.recognized = true};
        case generic_pid_set_periodic_report_id:
          return handle_magnitude(data, 2U, now);
        case generic_pid_set_constant_force_report_id:
          return handle_magnitude(data, 1U, now);
        case generic_pid_effect_operation_report_id:
          return handle_effect_operation(data, now);
        case generic_pid_block_free_report_id:
          return handle_block_free(data, now);
        case generic_pid_device_control_report_id:
          return handle_device_control(data, now);
        case generic_pid_device_gain_report_id:
          return handle_device_gain(data, now);
        default:
          return {};
      }
    }

    GenericPidRumbleUpdate advance(TimePoint now = Clock::now()) {
      expire_effects(now);
      const auto strength = current_strength(now);
      if (strength == last_strength_) {
        return {.recognized = true};
      }
      last_strength_ = strength;
      return {.recognized = true, .rumble_changed = true, .strength = strength};
    }

    std::optional<TimePoint> next_transition() const {
      if (paused_) {
        return std::nullopt;
      }

      std::optional<TimePoint> next;
      for (const auto &effect : effects_) {
        if (!effect.running) {
          continue;
        }
        const auto candidate = effect.started ? effect.end : std::optional<TimePoint> {effect.start};
        if (candidate.has_value() && (!next.has_value() || *candidate < *next)) {
          next = candidate;
        }
      }
      return next;
    }

  private:
    using Milliseconds = std::chrono::milliseconds;

    struct Effect {
      std::uint8_t type = 0;
      std::uint8_t gain = 255;
      std::int16_t magnitude = 0;
      Milliseconds duration {};
      Milliseconds start_delay {};
      TimePoint operation_start {};
      TimePoint start {};
      std::optional<TimePoint> end;
      bool duration_is_infinite = true;
      std::uint8_t loop_count = 1;
      bool loop_count_is_infinite = false;
      bool running = false;
      bool started = false;
    };

    Effect *effect(std::uint8_t effect_block_index) {
      if (effect_block_index == 0U || effect_block_index > effects_.size()) {
        return nullptr;
      }
      return &effects_[effect_block_index - 1U];
    }

    GenericPidRumbleUpdate handle_set_effect(std::span<const std::uint8_t> data, TimePoint now) {
      // EBI, Effect Type, four 16-bit timing values, then the 8-bit gain.
      constexpr std::size_t duration_offset = 2;
      constexpr std::size_t start_delay_offset = 8;
      constexpr std::size_t gain_offset = 10;
      if (data.size() <= gain_offset) {
        return {.recognized = true};
      }
      auto *selected = effect(data[0]);
      if (selected == nullptr) {
        return {.recognized = true};
      }
      const auto was_running = selected->running;
      selected->type = data[1];
      const auto duration = generic_pid_detail::read_u16(data, duration_offset);
      selected->duration = Milliseconds {duration};
      selected->duration_is_infinite = duration == std::numeric_limits<std::uint16_t>::max();
      selected->start_delay = Milliseconds {generic_pid_detail::read_u16(data, start_delay_offset)};
      selected->gain = data[gain_offset];
      reschedule_running_effect(*selected, now);
      return current_update(was_running, now);
    }

    GenericPidRumbleUpdate handle_magnitude(
      std::span<const std::uint8_t> data,
      std::uint8_t expected_type,
      TimePoint now
    ) {
      if (data.size() < 3U) {
        return {.recognized = true};
      }
      auto *selected = effect(data[0]);
      if (selected == nullptr) {
        return {.recognized = true};
      }
      selected->type = expected_type;
      selected->magnitude = static_cast<std::int16_t>(generic_pid_detail::read_u16(data, 1U));
      return current_update(selected->running, now);
    }

    GenericPidRumbleUpdate handle_effect_operation(std::span<const std::uint8_t> data, TimePoint now) {
      if (data.size() < 2U) {
        return {.recognized = true};
      }
      auto *selected = effect(data[0]);
      if (selected == nullptr) {
        return {.recognized = true};
      }

      if (data[1] == 2U) {
        stop_all_effects();
      }
      if (data[1] == 1U || data[1] == 2U) {
        start_effect(*selected, data.size() > 2U ? data[2] : 1U, now);
      } else if (data[1] == 3U) {
        selected->running = false;
        selected->started = false;
        selected->end.reset();
      }
      return current_update(true, now);
    }

    GenericPidRumbleUpdate handle_block_free(std::span<const std::uint8_t> data, TimePoint now) {
      if (!data.empty()) {
        if (auto *selected = effect(data[0]); selected != nullptr) {
          *selected = {};
        }
      }
      return current_update(true, now);
    }

    GenericPidRumbleUpdate handle_device_control(std::span<const std::uint8_t> data, TimePoint now) {
      if (data.empty()) {
        return {.recognized = true};
      }
      switch (data[0]) {
        case 1:
          actuators_enabled_ = true;
          break;
        case 2:
          actuators_enabled_ = false;
          break;
        case 3:
          stop_all_effects();
          break;
        case 4:
          effects_.fill({});
          device_gain_ = 255;
          actuators_enabled_ = true;
          paused_ = false;
          pause_started_.reset();
          break;
        case 5:
          if (!paused_) {
            paused_ = true;
            pause_started_ = now;
          }
          break;
        case 6:
          continue_effects(now);
          break;
        default:
          break;
      }
      return current_update(true, now);
    }

    GenericPidRumbleUpdate handle_device_gain(std::span<const std::uint8_t> data, TimePoint now) {
      if (!data.empty()) {
        device_gain_ = data[0];
      }
      return current_update(true, now);
    }

    void stop_all_effects() {
      for (auto &effect : effects_) {
        effect.running = false;
        effect.started = false;
        effect.end.reset();
      }
    }

    void start_effect(Effect &effect, std::uint8_t loop_count, TimePoint now) {
      effect.running = true;
      effect.operation_start = now;
      effect.started = effect.start_delay == Milliseconds::zero();
      effect.start = effect.operation_start + effect.start_delay;
      effect.loop_count = std::max<std::uint8_t>(loop_count, 1U);

      constexpr auto infinite_loop_count = std::numeric_limits<std::uint8_t>::max();
      effect.loop_count_is_infinite = loop_count == infinite_loop_count;
      update_effect_end(effect);
    }

    void reschedule_running_effect(Effect &effect, TimePoint now) {
      if (!effect.running) {
        return;
      }
      if (!effect.started) {
        effect.start = effect.operation_start + effect.start_delay;
        effect.started = now >= effect.start;
      }
      update_effect_end(effect);
      expire_effect(effect, now);
    }

    static void update_effect_end(Effect &effect) {
      if (effect.duration_is_infinite || effect.loop_count_is_infinite) {
        effect.end.reset();
        return;
      }
      effect.end = effect.start + effect.duration * effect.loop_count;
    }

    void continue_effects(TimePoint now) {
      if (!paused_ || !pause_started_.has_value()) {
        paused_ = false;
        pause_started_.reset();
        return;
      }

      const auto pause_duration = now - *pause_started_;
      for (auto &effect : effects_) {
        if (!effect.running) {
          continue;
        }
        effect.operation_start += pause_duration;
        effect.start += pause_duration;
        if (effect.end.has_value()) {
          *effect.end += pause_duration;
        }
      }
      paused_ = false;
      pause_started_.reset();
    }

    void expire_effects(TimePoint now) {
      if (paused_) {
        return;
      }

      for (auto &effect : effects_) {
        expire_effect(effect, now);
      }
    }

    static void expire_effect(Effect &effect, TimePoint now) {
      if (!effect.running) {
        return;
      }
      if (!effect.started && now >= effect.start) {
        effect.started = true;
      }
      if (effect.end.has_value() && now >= *effect.end) {
        effect.running = false;
        effect.started = false;
        effect.end.reset();
      }
    }

    std::uint16_t current_strength(TimePoint now) const {
      if (!actuators_enabled_ || paused_) {
        return 0;
      }

      std::uint32_t combined = 0;
      for (const auto &effect : effects_) {
        if (!effect.running || !effect.started || now < effect.start || (effect.type != 1U && effect.type != 2U)) {
          continue;
        }
        combined += generic_pid_detail::scale_force(effect.magnitude, effect.gain, device_gain_);
      }
      return static_cast<std::uint16_t>(std::min(combined, 65535U));
    }

    GenericPidRumbleUpdate current_update(bool changed, TimePoint now) {
      if (!changed) {
        return {.recognized = true};
      }
      last_strength_ = current_strength(now);
      return {.recognized = true, .rumble_changed = true, .strength = last_strength_};
    }

    std::array<Effect, generic_pid_max_effects> effects_ {};
    std::uint8_t device_gain_ = 255;
    std::uint16_t last_strength_ = 0;
    bool actuators_enabled_ = true;
    bool paused_ = false;
    std::optional<TimePoint> pause_started_;
  };

}  // namespace lvh::detail::windows
