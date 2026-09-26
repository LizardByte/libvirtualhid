/**
 * @file tests/unit/test_macos_broker_protocol.cpp
 * @brief macOS broker protocol and built-in profile capacity checks.
 */

#include "platform/macos/broker/io.hpp"
#include "platform/macos/macos_xbox_transport.hpp"

#include <array>
#include <bitset>
#include <gtest/gtest.h>
#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

TEST(MacosBrokerProtocolTest, BuiltInProfilesFitBrokerTransport) {
  auto profiles = lvh::profiles::built_in_gamepad_profiles();
  profiles.push_back(lvh::profiles::dualshock4_usb());
  profiles.push_back(lvh::profiles::dualshock4_bluetooth());
  profiles.push_back(lvh::profiles::dualsense_usb());
  profiles.push_back(lvh::profiles::dualsense_bluetooth());
  ASSERT_GE(profiles.size(), 11U);
  for (const auto &profile : profiles) {
    SCOPED_TRACE(profile.name);
    EXPECT_FALSE(profile.report_descriptor.empty());
    EXPECT_LE(profile.report_descriptor.size(), lvh::detail::macos_broker::max_descriptor_size);
    EXPECT_GT(profile.input_report_size, 0U);
    EXPECT_LE(profile.input_report_size, lvh::detail::macos_broker::max_report_size);
    EXPECT_LE(profile.output_report_size, lvh::detail::macos_broker::max_report_size);
    const auto transport = lvh::detail::macos::xbox_transport_profile(profile);
    EXPECT_LE(transport.report_descriptor.size(), lvh::detail::macos_broker::max_descriptor_size);
    EXPECT_LE(transport.input_report_size, lvh::detail::macos_broker::max_report_size);
    EXPECT_LE(transport.output_report_size, lvh::detail::macos_broker::max_report_size);
  }
}

TEST(MacosBrokerProtocolTest, TransfersVersionedMessagesWithoutTruncation) {
  std::array<int, 2> sockets {};
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets.data()), 0);
  lvh::detail::macos_broker::Message sent;
  sent.type = lvh::detail::macos_broker::MessageType::create;
  sent.descriptor_size = 3;
  sent.data[0] = 0x05;
  sent.data[1] = 0x01;
  sent.data[2] = 0x09;
  bool sent_ok = false;
  std::jthread sender {[&sent_ok, &sockets, &sent] {
    sent_ok = lvh::detail::macos_broker::send_message(sockets[0], sent);
  }};
  lvh::detail::macos_broker::Message received;
  EXPECT_TRUE(lvh::detail::macos_broker::receive_message(sockets[1], received));
  sender.join();
  EXPECT_TRUE(sent_ok);
  EXPECT_EQ(received.type, sent.type);
  EXPECT_EQ(received.descriptor_size, 3U);
  EXPECT_EQ(received.data[2], 0x09);
  ::close(sockets[0]);
  ::close(sockets[1]);
}

TEST(MacosBrokerProtocolTest, XboxTransportUsesSteamMacOSIdentityAndButtonLayout) {
  using enum lvh::GamepadButton;

  const auto requested = lvh::profiles::xbox_360();
  const auto transport = lvh::detail::macos::xbox_transport_profile(requested);
  EXPECT_EQ(transport.vendor_id, 0x045E);
  EXPECT_EQ(transport.product_id, 0x028E);
  EXPECT_EQ(transport.version, 0x0114);
  EXPECT_EQ(transport.input_report_size, 9U);
  EXPECT_EQ(transport.report_id, 1);
  EXPECT_NE(transport.report_descriptor, requested.report_descriptor);

  const std::array buttons {a, b, x, y, left_shoulder, right_shoulder, left_stick, right_stick, start, back, guide, dpad_up, dpad_down, dpad_left, dpad_right, misc1};
  for (std::size_t bit = 0; bit < buttons.size(); ++bit) {
    lvh::GamepadState state;
    state.buttons.set(buttons[bit]);
    const auto report = lvh::detail::macos::xbox_transport_input_report(state);
    ASSERT_EQ(report.size(), transport.input_report_size);
    const auto flags = std::bitset<16> {report[1] + 256U * report[2]};
    EXPECT_TRUE(flags.test(bit));
    EXPECT_EQ(flags.count(), 1U);
  }
}

TEST(MacosBrokerProtocolTest, XboxOneAndSeriesUseDistinctIdentitiesAndGipInput) {
  for (const auto &requested : {lvh::profiles::xbox_one(), lvh::profiles::xbox_series()}) {
    SCOPED_TRACE(requested.name);
    const auto transport = lvh::detail::macos::xbox_transport_profile(requested);
    EXPECT_EQ(transport.vendor_id, 0x045E);
    EXPECT_EQ(transport.product_id, requested.gamepad_kind == lvh::GamepadProfileKind::xbox_one ? 0x0B20 : 0x0B13);
    EXPECT_EQ(transport.version, 0x0513);
    EXPECT_EQ(transport.bus_type, lvh::BusType::bluetooth);
    EXPECT_EQ(transport.report_id, lvh::detail::macos::xbox_gip_input_command);
    EXPECT_EQ(transport.input_report_size, 25U);
    EXPECT_EQ(transport.output_report_size, 9U);
    EXPECT_NE(transport.report_descriptor, requested.report_descriptor);

    lvh::GamepadState state;
    state.buttons.set(lvh::GamepadButton::a);
    state.buttons.set(lvh::GamepadButton::start);
    state.buttons.set(lvh::GamepadButton::dpad_left);
    state.buttons.set(lvh::GamepadButton::misc1);
    state.left_stick = {1.0F, -1.0F};
    const auto original = lvh::reports::pack_input_report(requested, state);
    const auto report = lvh::detail::macos::xbox_gip_transport_input_report(
      state,
      original,
      requested.gamepad_kind == lvh::GamepadProfileKind::xbox_series
    );
    ASSERT_EQ(report.size(), transport.input_report_size);
    EXPECT_EQ(report[0], 0x20U);
    EXPECT_EQ(report[3], 16U);
    EXPECT_EQ(report[4], 0x14U);  // Start and A.
    EXPECT_EQ(report[5], 0x04U);  // D-pad left.
    EXPECT_EQ(report[10], 0xFFU);  // Left X fully right.
    EXPECT_EQ(report[11], 0x7FU);
    EXPECT_EQ(report[12], 0xFFU);  // Left Y fully up before consumer inversion.
    EXPECT_EQ(report[13], 0x7FU);
    EXPECT_EQ(report[18], requested.gamepad_kind == lvh::GamepadProfileKind::xbox_series ? 1U : 0U);
    EXPECT_EQ(report[20], 0x07U);  // Separate Guide packet.
    EXPECT_EQ(report[21], 0x20U);
    EXPECT_EQ(report[23], 1U);
    EXPECT_EQ(report[24], 0U);
  }
}

TEST(MacosBrokerProtocolTest, XboxGipTransportMapsEveryButtonAndGuide) {
  using enum lvh::GamepadButton;

  struct ButtonCase {
    lvh::GamepadButton button;
    std::size_t index;
    std::uint8_t mask;
  };

  const std::array buttons {
    ButtonCase {start, 4, 0x04},
    ButtonCase {back, 4, 0x08},
    ButtonCase {a, 4, 0x10},
    ButtonCase {b, 4, 0x20},
    ButtonCase {x, 4, 0x40},
    ButtonCase {y, 4, 0x80},
    ButtonCase {dpad_up, 5, 0x01},
    ButtonCase {dpad_down, 5, 0x02},
    ButtonCase {dpad_left, 5, 0x04},
    ButtonCase {dpad_right, 5, 0x08},
    ButtonCase {left_shoulder, 5, 0x10},
    ButtonCase {right_shoulder, 5, 0x20},
    ButtonCase {left_stick, 5, 0x40},
    ButtonCase {right_stick, 5, 0x80},
    ButtonCase {misc1, 18, 0x01},
    ButtonCase {guide, 24, 0x01},
  };
  for (const auto &item : buttons) {
    lvh::GamepadState state;
    state.buttons.set(item.button);
    const auto packed = lvh::reports::pack_input_report(lvh::profiles::xbox_series(), state);
    const auto report = lvh::detail::macos::xbox_gip_transport_input_report(state, packed, true);
    ASSERT_EQ(report.size(), 25U);
    EXPECT_EQ(report[item.index], item.mask);
  }
}

TEST(MacosBrokerProtocolTest, XboxTransportKeepsAxesAndSharedReportsUntouched) {
  lvh::GamepadState state;
  state.left_stick = {1.0F, -1.0F};
  state.right_stick = {-1.0F, 1.0F};
  state.left_trigger = 0.5F;
  state.right_trigger = 1.0F;
  state.buttons.set(lvh::GamepadButton::back);
  const auto shared = lvh::reports::pack_input_report(lvh::profiles::xbox_360(), state);
  const auto transport = lvh::detail::macos::xbox_transport_input_report(state);
  ASSERT_EQ(shared.size(), 9U);
  ASSERT_EQ(transport.size(), 9U);
  EXPECT_EQ(shared[1], 0x40U);
  EXPECT_EQ(transport[2], 0x02U);
  for (std::size_t index = 3; index < shared.size(); ++index) {
    EXPECT_EQ(transport[index], shared[index]);
  }
}

TEST(MacosBrokerProtocolTest, XboxTransportLeavesCustomHidProfilesUntouched) {
  auto custom = lvh::profiles::xbox_360();
  custom.vendor_id = 0x1209;
  const auto transport = lvh::detail::macos::xbox_transport_profile(custom);
  EXPECT_FALSE(lvh::detail::macos::uses_xbox_transport(custom));
  EXPECT_EQ(transport.vendor_id, custom.vendor_id);
  EXPECT_EQ(transport.product_id, custom.product_id);
  EXPECT_EQ(transport.report_descriptor, custom.report_descriptor);
}
