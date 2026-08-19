// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file tests/unit/test_windows_vhf_input_report_queue.cpp
 * @brief Tests for Windows VHF input-report coalescing.
 */

// standard includes
#include <cstddef>
#include <cstdint>
#include <vector>

// library includes
#include <gtest/gtest.h>
#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

// local includes
#include "vhf_input_report_queue.hpp"

namespace {

  using lvh::detail::windows::vhf_max_pending_input_reports;
  using lvh::detail::windows::VhfInputReportQueue;

  std::vector<std::uint8_t> make_report(std::size_t size, std::uint8_t report_id) {
    auto report = std::vector<std::uint8_t>(size, 0U);
    if (report_id != 0U) {
      report[0] = report_id;
    }
    return report;
  }

  TEST(WindowsVhfInputReportQueueTest, CollapsesAnAxisBurstToItsLatestState) {
    VhfInputReportQueue queue {LVH_WINDOWS_GAMEPAD_GENERIC, LVH_WINDOWS_BUS_USB, 1U};
    auto latest = make_report(9U, 1U);
    for (std::uint8_t axis = 0U; axis < 64U; ++axis) {
      latest[3] = axis;
      queue.push(latest);
    }

    ASSERT_EQ(queue.size(), 1U);
    EXPECT_EQ(queue.pop(), latest);
    EXPECT_TRUE(queue.empty());
  }

  TEST(WindowsVhfInputReportQueueTest, PreservesButtonTransitionsWhileCoalescingTheirAxes) {
    VhfInputReportQueue queue {LVH_WINDOWS_GAMEPAD_GENERIC, LVH_WINDOWS_BUS_USB, 1U};
    auto neutral = make_report(9U, 1U);
    neutral[3] = 10U;
    auto pressed = neutral;
    pressed[1] = 1U;
    pressed[3] = 20U;
    auto pressed_latest = pressed;
    pressed_latest[3] = 30U;
    auto released = pressed_latest;
    released[1] = 0U;
    released[3] = 40U;

    queue.push(neutral);
    queue.push(pressed);
    queue.push(pressed_latest);
    queue.push(released);

    ASSERT_EQ(queue.size(), 3U);
    EXPECT_EQ(queue.pop(), neutral);
    EXPECT_EQ(queue.pop(), pressed_latest);
    EXPECT_EQ(queue.pop(), released);
  }

  struct ProfileCase {
    lvh::DeviceProfile profile;
    std::uint32_t gamepad_kind;
    std::uint32_t bus_type;
    bool supports_touch;
  };

  TEST(WindowsVhfInputReportQueueTest, RecognizesContinuousAndDiscreteStateForEveryVhfProfile) {
    const std::vector profiles {
      ProfileCase {lvh::profiles::generic_gamepad(), LVH_WINDOWS_GAMEPAD_GENERIC, LVH_WINDOWS_BUS_USB, false},
      ProfileCase {lvh::profiles::xbox_one(), LVH_WINDOWS_GAMEPAD_XBOX_ONE, LVH_WINDOWS_BUS_USB, false},
      ProfileCase {lvh::profiles::xbox_series(), LVH_WINDOWS_GAMEPAD_XBOX_SERIES, LVH_WINDOWS_BUS_USB, false},
      ProfileCase {lvh::profiles::switch_pro(), LVH_WINDOWS_GAMEPAD_SWITCH_PRO, LVH_WINDOWS_BUS_USB, false},
      ProfileCase {lvh::profiles::steam_deck(), LVH_WINDOWS_GAMEPAD_STEAM_DECK, LVH_WINDOWS_BUS_USB, true},
      ProfileCase {lvh::profiles::dualshock4_usb(), LVH_WINDOWS_GAMEPAD_DUALSHOCK4, LVH_WINDOWS_BUS_USB, true},
      ProfileCase {lvh::profiles::dualshock4_bluetooth(), LVH_WINDOWS_GAMEPAD_DUALSHOCK4, LVH_WINDOWS_BUS_BLUETOOTH, true},
      ProfileCase {lvh::profiles::dualsense_usb(), LVH_WINDOWS_GAMEPAD_DUALSENSE, LVH_WINDOWS_BUS_USB, true},
      ProfileCase {lvh::profiles::dualsense_bluetooth(), LVH_WINDOWS_GAMEPAD_DUALSENSE, LVH_WINDOWS_BUS_BLUETOOTH, true},
    };

    for (const auto &test_case : profiles) {
      SCOPED_TRACE(test_case.profile.name);
      VhfInputReportQueue queue {
        test_case.gamepad_kind,
        test_case.bus_type,
        test_case.profile.report_id,
      };
      auto state = lvh::GamepadState {};
      const auto initial = lvh::reports::pack_input_report(test_case.profile, state);
      state.left_stick.x = 0.75F;
      const auto moved = lvh::reports::pack_input_report(test_case.profile, state);
      state.buttons.set(lvh::GamepadButton::a);
      const auto button_changed = lvh::reports::pack_input_report(test_case.profile, state);

      ASSERT_FALSE(initial.empty());
      ASSERT_FALSE(moved.empty());
      ASSERT_FALSE(button_changed.empty());

      queue.push(initial);
      queue.push(moved);
      EXPECT_EQ(queue.size(), 1U);
      queue.push(button_changed);
      EXPECT_EQ(queue.size(), 2U);

      if (test_case.supports_touch) {
        state.touchpad_contacts[0] = {.id = 7U, .active = true, .x = 0.25F, .y = 0.5F};
        const auto touch_started = lvh::reports::pack_input_report(test_case.profile, state);
        state.touchpad_contacts[0].x = 0.75F;
        const auto touch_moved = lvh::reports::pack_input_report(test_case.profile, state);

        queue.push(touch_started);
        EXPECT_EQ(queue.size(), 3U);
        queue.push(touch_moved);
        EXPECT_EQ(queue.size(), 3U);
        state.touchpad_contacts[0].active = false;
        queue.push(lvh::reports::pack_input_report(test_case.profile, state));
        EXPECT_EQ(queue.size(), 4U);
      }
    }
  }

  TEST(WindowsVhfInputReportQueueTest, CoalescesContinuousTriggerMovementAndPreservesNativeThresholdChanges) {
    struct TriggerCase {
      lvh::DeviceProfile profile;
      std::uint32_t gamepad_kind;
      std::uint32_t bus_type;
      bool exposes_trigger_button;
    };

    const std::vector profiles {
      TriggerCase {lvh::profiles::generic_gamepad(), LVH_WINDOWS_GAMEPAD_GENERIC, LVH_WINDOWS_BUS_USB, false},
      TriggerCase {lvh::profiles::xbox_one(), LVH_WINDOWS_GAMEPAD_XBOX_ONE, LVH_WINDOWS_BUS_USB, false},
      TriggerCase {lvh::profiles::xbox_series(), LVH_WINDOWS_GAMEPAD_XBOX_SERIES, LVH_WINDOWS_BUS_USB, false},
      TriggerCase {lvh::profiles::switch_pro(), LVH_WINDOWS_GAMEPAD_SWITCH_PRO, LVH_WINDOWS_BUS_USB, true},
      TriggerCase {lvh::profiles::steam_deck(), LVH_WINDOWS_GAMEPAD_STEAM_DECK, LVH_WINDOWS_BUS_USB, true},
      TriggerCase {lvh::profiles::dualshock4_usb(), LVH_WINDOWS_GAMEPAD_DUALSHOCK4, LVH_WINDOWS_BUS_USB, true},
      TriggerCase {lvh::profiles::dualshock4_bluetooth(), LVH_WINDOWS_GAMEPAD_DUALSHOCK4, LVH_WINDOWS_BUS_BLUETOOTH, true},
      TriggerCase {lvh::profiles::dualsense_usb(), LVH_WINDOWS_GAMEPAD_DUALSENSE, LVH_WINDOWS_BUS_USB, true},
      TriggerCase {lvh::profiles::dualsense_bluetooth(), LVH_WINDOWS_GAMEPAD_DUALSENSE, LVH_WINDOWS_BUS_BLUETOOTH, true},
    };

    for (const auto &test_case : profiles) {
      SCOPED_TRACE(test_case.profile.name);
      VhfInputReportQueue queue {
        test_case.gamepad_kind,
        test_case.bus_type,
        test_case.profile.report_id,
      };
      auto state = lvh::GamepadState {};
      state.left_trigger = 0.1F;
      queue.push(lvh::reports::pack_input_report(test_case.profile, state));
      state.left_trigger = 0.9F;
      queue.push(lvh::reports::pack_input_report(test_case.profile, state));
      EXPECT_EQ(queue.size(), 1U);

      state.left_trigger = 0.0F;
      queue.push(lvh::reports::pack_input_report(test_case.profile, state));
      EXPECT_EQ(queue.size(), test_case.exposes_trigger_button ? 2U : 1U);
    }
  }

  TEST(WindowsVhfInputReportQueueTest, PrioritizesSwitchProtocolRepliesWithoutCoalescingThem) {
    VhfInputReportQueue queue {LVH_WINDOWS_GAMEPAD_SWITCH_PRO, LVH_WINDOWS_BUS_USB, 0x30U};
    auto controller_state = make_report(64U, 0x30U);
    auto first_reply = make_report(64U, 0x21U);
    auto second_reply = first_reply;
    first_reply[15] = 1U;
    second_reply[15] = 2U;

    queue.push(controller_state);
    queue.push(first_reply);
    queue.push(second_reply);

    ASSERT_EQ(queue.size(), 3U);
    EXPECT_EQ(queue.pop(), first_reply);
    EXPECT_EQ(queue.pop(), second_reply);
    EXPECT_EQ(queue.pop(), controller_state);
  }

  TEST(WindowsVhfInputReportQueueTest, BoundsSwitchProtocolReplyHistory) {
    VhfInputReportQueue queue {LVH_WINDOWS_GAMEPAD_SWITCH_PRO, LVH_WINDOWS_BUS_USB, 0x30U};
    constexpr auto submitted_reports = vhf_max_pending_input_reports + 8U;
    for (std::size_t index = 0U; index < submitted_reports; ++index) {
      auto reply = make_report(64U, 0x21U);
      reply[15] = static_cast<std::uint8_t>(index);
      queue.push(std::move(reply));
    }

    ASSERT_EQ(queue.size(), vhf_max_pending_input_reports);
    const auto first = queue.pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ((*first)[15], 8U);
  }

  TEST(WindowsVhfInputReportQueueTest, KeepsOnlyTheNewestBoundedTransitionHistory) {
    VhfInputReportQueue queue {LVH_WINDOWS_GAMEPAD_GENERIC, LVH_WINDOWS_BUS_USB, 1U};
    constexpr auto submitted_reports = vhf_max_pending_input_reports + 8U;
    for (std::size_t index = 0U; index < submitted_reports; ++index) {
      auto report = make_report(9U, 1U);
      report[1] = static_cast<std::uint8_t>(index % 2U);
      report[3] = static_cast<std::uint8_t>(index);
      queue.push(std::move(report));
    }

    ASSERT_EQ(queue.size(), vhf_max_pending_input_reports);
    const auto first = queue.pop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ((*first)[3], 8U);

    auto last = first;
    while (!queue.empty()) {
      last = queue.pop();
    }
    ASSERT_TRUE(last.has_value());
    EXPECT_EQ((*last)[3], submitted_reports - 1U);
  }

  TEST(WindowsVhfInputReportQueueTest, KeepsMalformedAndUnknownReportsDistinctAndCanClearThem) {
    VhfInputReportQueue malformed_queue {LVH_WINDOWS_GAMEPAD_GENERIC, LVH_WINDOWS_BUS_USB, 0U};
    malformed_queue.push({});
    malformed_queue.push({});
    malformed_queue.push({0U});
    EXPECT_EQ(malformed_queue.size(), 3U);
    malformed_queue.clear();
    EXPECT_TRUE(malformed_queue.empty());
    EXPECT_FALSE(malformed_queue.pop().has_value());

    constexpr auto unknown_gamepad_kind = 0xFFFFFFFFU;
    VhfInputReportQueue unknown_queue {unknown_gamepad_kind, LVH_WINDOWS_BUS_UNKNOWN, 0U};
    unknown_queue.push(make_report(9U, 0U));
    unknown_queue.push(make_report(9U, 0U));
    EXPECT_EQ(unknown_queue.size(), 2U);
  }

}  // namespace
