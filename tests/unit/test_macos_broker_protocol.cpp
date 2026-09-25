/**
 * @file tests/unit/test_macos_broker_protocol.cpp
 * @brief macOS broker protocol and built-in profile capacity checks.
 */

#include "platform/macos/broker/io.hpp"

#include <array>
#include <gtest/gtest.h>
#include <libvirtualhid/profiles.hpp>
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
  std::thread sender {[&] {
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
