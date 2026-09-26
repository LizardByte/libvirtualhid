/**
 * @file tests/unit/test_macos_broker_protocol.cpp
 * @brief macOS broker protocol and built-in profile capacity checks.
 */

#include "platform/macos/broker/io.hpp"
#include "platform/macos/macos_xbox_transport.hpp"

#include <array>
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
    const auto flags = static_cast<std::uint16_t>(report[1] | (report[2] << 8U));
    EXPECT_EQ(flags, static_cast<std::uint16_t>(1U << bit));
  }
}

TEST(MacosBrokerProtocolTest, XboxOneAndSeriesUseDistinctBluetoothIdentities) {
  for (const auto &requested : {lvh::profiles::xbox_one(), lvh::profiles::xbox_series()}) {
    SCOPED_TRACE(requested.name);
    const auto transport = lvh::detail::macos::xbox_transport_profile(requested);
    EXPECT_EQ(transport.vendor_id, 0x045E);
    EXPECT_EQ(transport.product_id, requested.gamepad_kind == lvh::GamepadProfileKind::xbox_one ? 0x0B20 : 0x0B13);
    EXPECT_EQ(transport.version, 0x0513);
    EXPECT_EQ(transport.bus_type, lvh::BusType::bluetooth);
    EXPECT_EQ(transport.report_id, 1);
    EXPECT_EQ(transport.input_report_size, 17U);
    EXPECT_EQ(transport.output_report_size, 9U);
    EXPECT_NE(transport.report_descriptor, requested.report_descriptor);

    lvh::GamepadState state;
    state.buttons.set(lvh::GamepadButton::a);
    state.buttons.set(lvh::GamepadButton::start);
    state.buttons.set(lvh::GamepadButton::dpad_left);
    state.buttons.set(lvh::GamepadButton::misc1);
    state.left_stick = {1.0F, -1.0F};
    const auto original = lvh::reports::pack_input_report(requested, state);
    const auto report = lvh::detail::xbox_bluetooth::make_xbox_bluetooth_input_report(
      state,
      original,
      requested.gamepad_kind == lvh::GamepadProfileKind::xbox_series
    );
    ASSERT_EQ(report.size(), transport.input_report_size);
    EXPECT_EQ(report[0], 1U);
    EXPECT_EQ(report[14], 0x01U);
    EXPECT_EQ(report[15], 0x08U);
    EXPECT_EQ(report[16], requested.gamepad_kind == lvh::GamepadProfileKind::xbox_series ? 1U : 0U);
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
