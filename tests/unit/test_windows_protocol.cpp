/**
 * @file tests/unit/test_windows_protocol.cpp
 * @brief Unit tests for the Windows control protocol helpers.
 */

// local includes
#include "fixtures/fixtures.hpp"
#include "generic_pid_rumble.hpp"
#include "platform/windows/control_protocol.hpp"

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

// lib includes
#include <libvirtualhid/profiles.hpp>

namespace {

  lvh::DeviceProfile minimal_gamepad_profile() {
    lvh::DeviceProfile profile;
    profile.device_type = lvh::DeviceType::gamepad;
    profile.gamepad_kind = lvh::GamepadProfileKind::generic;
    profile.bus_type = lvh::BusType::usb;
    profile.vendor_id = 0x1209;
    profile.product_id = 0x0001;
    profile.version = 0x0001;
    profile.report_id = 1;
    profile.input_report_size = 4;
    profile.output_report_size = 2;
    profile.name = "test gamepad";
    profile.manufacturer = "test manufacturer";
    profile.report_descriptor = {0x05, 0x01, 0x09, 0x05};
    return profile;
  }

  std::vector<std::uint8_t> byte_report(std::initializer_list<std::uint8_t> values) {
    return values;
  }

}  // namespace

TEST(WindowsProtocolTest, ExposesStableProtocolConstants) {
  EXPECT_STREQ(lvh::detail::windows::default_control_device_path.data(), R"(\\.\LibVirtualHid)");
  EXPECT_STREQ(lvh::detail::windows::global_control_device_path.data(), R"(\\.\Global\LibVirtualHid)");

  EXPECT_EQ(LVH_WINDOWS_IOCTL_CREATE_GAMEPAD, 0x8000E000U);
  EXPECT_EQ(LVH_WINDOWS_IOCTL_DESTROY_DEVICE, 0x8000E004U);
  EXPECT_EQ(LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT, 0x8000E008U);
  EXPECT_EQ(LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT, 0x8000600CU);

  EXPECT_EQ(sizeof(LvhWindowsGamepadHardwareIds), 14U);
  EXPECT_EQ(sizeof(LvhWindowsGamepadReportSizes), 24U);
  EXPECT_EQ(sizeof(LvhWindowsCreateGamepadRequest), 2498U);
  EXPECT_EQ(sizeof(LvhWindowsCreateGamepadResponse), 284U);
  EXPECT_EQ(sizeof(LvhWindowsDestroyDeviceRequest), 16U);
  EXPECT_EQ(sizeof(LvhWindowsSubmitInputReportRequest), 280U);
  EXPECT_EQ(sizeof(LvhWindowsOutputReportEvent), 280U);
}

TEST(WindowsProtocolTest, MapsBusTypesAndGamepadKinds) {
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(lvh::BusType::unknown), LVH_WINDOWS_BUS_UNKNOWN);
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(lvh::BusType::usb), LVH_WINDOWS_BUS_USB);
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(lvh::BusType::bluetooth), LVH_WINDOWS_BUS_BLUETOOTH);
  EXPECT_EQ(lvh::detail::windows::protocol_bus_type(static_cast<lvh::BusType>(255)), LVH_WINDOWS_BUS_UNKNOWN);

  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::generic),
    LVH_WINDOWS_GAMEPAD_GENERIC
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::xbox_360),
    LVH_WINDOWS_GAMEPAD_XBOX_360
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::xbox_one),
    LVH_WINDOWS_GAMEPAD_XBOX_ONE
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::xbox_series),
    LVH_WINDOWS_GAMEPAD_XBOX_SERIES
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::dualshock4),
    LVH_WINDOWS_GAMEPAD_DUALSHOCK4
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::dualsense),
    LVH_WINDOWS_GAMEPAD_DUALSENSE
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(lvh::GamepadProfileKind::switch_pro),
    LVH_WINDOWS_GAMEPAD_SWITCH_PRO
  );
  EXPECT_EQ(
    lvh::detail::windows::protocol_gamepad_kind(static_cast<lvh::GamepadProfileKind>(255)),
    LVH_WINDOWS_GAMEPAD_GENERIC
  );
}

TEST(WindowsProtocolTest, BuildsCapabilityFlags) {
  lvh::GamepadProfileCapabilities capabilities;
  EXPECT_EQ(lvh::detail::windows::gamepad_flags(capabilities), 0U);

  capabilities.supports_rumble = true;
  capabilities.supports_motion = true;
  capabilities.supports_touchpad = true;
  capabilities.supports_rgb_led = true;
  capabilities.supports_battery = true;
  capabilities.supports_adaptive_triggers = true;

  const auto flags = lvh::detail::windows::gamepad_flags(capabilities);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY, 0U);
  EXPECT_NE(flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS, 0U);
}

TEST(WindowsProtocolTest, CopyHelpersTruncateAndZeroFill) {
  LvhWindowsCreateGamepadRequest create_request {};
  create_request.name[0] = 'x';
  create_request.name[1] = 'x';
  create_request.name[2] = 'x';
  create_request.name[3] = 'x';
  create_request.name[4] = 'x';
  const std::string oversized_name(LVH_WINDOWS_MAX_DEVICE_NAME_SIZE + 5U, 'a');
  EXPECT_EQ(
    lvh::detail::windows::copy_string(create_request.name, oversized_name),
    LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U
  );
  EXPECT_EQ(
    std::string_view {create_request.name},
    std::string_view {oversized_name}.substr(0U, LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U)
  );
  EXPECT_EQ(create_request.name[LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U], '\0');

  LvhWindowsSubmitInputReportRequest submit_request {};
  submit_request.report[0] = 0xFF;
  submit_request.report[1] = 0xFF;
  submit_request.report[2] = 0xFF;
  submit_request.report[3] = 0xFF;
  submit_request.report[4] = 0xFF;
  const std::vector<std::uint8_t> source {1, 2};
  EXPECT_EQ(lvh::detail::windows::copy_bytes(submit_request.report, source), 2U);
  EXPECT_EQ(submit_request.report[0], 1U);
  EXPECT_EQ(submit_request.report[1], 2U);
  EXPECT_EQ(submit_request.report[2], 0U);
  EXPECT_EQ(submit_request.report[3], 0U);
  EXPECT_EQ(submit_request.report[4], 0U);
}

TEST(WindowsProtocolTest, PacksGamepadCreateRequest) {
  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::dualsense_bluetooth();
  options.metadata.stable_id = "client-0-controller-1";

  const auto request = lvh::detail::windows::make_create_gamepad_request(42, options);

  EXPECT_EQ(request.version, LVH_WINDOWS_CONTROL_PROTOCOL_VERSION);
  EXPECT_EQ(request.size, sizeof(request));
  EXPECT_EQ(request.client_device_id, 42U);
  EXPECT_EQ(request.bus_type, LVH_WINDOWS_BUS_BLUETOOTH);
  EXPECT_EQ(request.gamepad_kind, LVH_WINDOWS_GAMEPAD_DUALSENSE);
  EXPECT_EQ(request.hardware_ids.vendor_id, options.profile.vendor_id);
  EXPECT_EQ(request.hardware_ids.product_id, options.profile.product_id);
  EXPECT_EQ(request.hardware_ids.device_version, options.profile.version);
  EXPECT_EQ(request.hardware_ids.report_id, options.profile.report_id);
  EXPECT_EQ(request.report_sizes.input_report_size, options.profile.input_report_size);
  EXPECT_EQ(request.report_sizes.output_report_size, options.profile.output_report_size);
  EXPECT_EQ(request.report_sizes.report_descriptor_size, options.profile.report_descriptor.size());
  EXPECT_EQ(request.report_descriptor[0], options.profile.report_descriptor[0]);
  EXPECT_STREQ(request.name, options.profile.name.c_str());
  EXPECT_STREQ(request.manufacturer, options.profile.manufacturer.c_str());
  EXPECT_STREQ(request.stable_id, options.metadata.stable_id.c_str());
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD, 0U);
  EXPECT_NE(request.flags & LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY, 0U);
}

TEST(WindowsProtocolTest, PacksGenericUnknownBusGamepadCreateRequestWithoutOptionalFlags) {
  lvh::CreateGamepadOptions options;
  options.profile = minimal_gamepad_profile();
  options.profile.bus_type = lvh::BusType::unknown;
  options.profile.output_report_size = 0;

  const auto request = lvh::detail::windows::make_create_gamepad_request(7, options);

  EXPECT_EQ(request.bus_type, LVH_WINDOWS_BUS_UNKNOWN);
  EXPECT_EQ(request.gamepad_kind, LVH_WINDOWS_GAMEPAD_GENERIC);
  EXPECT_EQ(request.flags, 0U);
  EXPECT_EQ(request.report_sizes.output_report_size, 0U);
  EXPECT_EQ(request.report_sizes.name_size, options.profile.name.size());
  EXPECT_EQ(request.report_sizes.manufacturer_size, options.profile.manufacturer.size());
  EXPECT_EQ(request.report_sizes.stable_id_size, 0U);
}

TEST(WindowsProtocolTest, PresentsXboxSeriesThroughShareCapableBluetoothHid) {
  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::xbox_series();

  const auto request = lvh::detail::windows::make_create_gamepad_request(8, options);
  const std::vector<std::uint8_t> descriptor(
    request.report_descriptor,
    request.report_descriptor + request.report_sizes.report_descriptor_size
  );

  EXPECT_EQ(request.bus_type, LVH_WINDOWS_BUS_BLUETOOTH);
  EXPECT_EQ(request.hardware_ids.product_id, lvh::detail::windows::xbox_series_bluetooth_product_id);
  EXPECT_EQ(request.hardware_ids.report_id, lvh::detail::windows::xbox_series_bluetooth_input_report_id);
  EXPECT_EQ(request.report_sizes.input_report_size, lvh::detail::windows::xbox_series_bluetooth_input_report_size);
  EXPECT_EQ(request.report_sizes.output_report_size, lvh::detail::windows::xbox_series_bluetooth_output_report_size);
  EXPECT_EQ(descriptor, lvh::detail::windows::xbox_series_bluetooth_report_descriptor());

  constexpr std::array<std::uint8_t, 5> share_usage {0x05, 0x0C, 0x0A, 0x24, 0x02};
  constexpr std::array<std::uint8_t, 4> rumble_report {0x85, 0x03, 0xA1, 0x02};
  EXPECT_NE(std::ranges::search(descriptor, share_usage).begin(), descriptor.end());
  EXPECT_NE(std::ranges::search(descriptor, rumble_report).begin(), descriptor.end());
}

TEST(WindowsProtocolTest, ConvertsXboxSeriesButtonsToBluetoothPacketSlots) {
  const std::vector<std::uint8_t> report {
    0x00,
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    0xFF,
    0x0B,
    0x07,
    0x01,
    0x80,
  };

  const auto windows_report = lvh::detail::windows::make_xbox_series_windows_input_report(report);

  ASSERT_EQ(windows_report.size(), lvh::detail::windows::xbox_series_bluetooth_input_report_size);
  EXPECT_EQ(windows_report[0], lvh::detail::windows::xbox_series_bluetooth_input_report_id);
  EXPECT_TRUE(std::ranges::equal(std::span {report}.first(12U), std::span {windows_report}.subspan(1U, 12U)));
  EXPECT_EQ(windows_report[13], 0x07U);
  EXPECT_EQ(windows_report[14], 0xDBU);  // A/B/X/Y/LB/RB.
  EXPECT_EQ(windows_report[15], 0x7CU);  // Back/Start/Guide/L3/R3.
  EXPECT_EQ(windows_report[16], 0x01U);  // Share.

  const std::vector<std::uint8_t> short_report {1, 2, 3};
  EXPECT_EQ(lvh::detail::windows::make_xbox_series_windows_input_report(short_report), short_report);
}

TEST(WindowsProtocolTest, PresentsBuiltInGenericControllerAsDirectInputPidJoystick) {
  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::generic_gamepad();

  const auto request = lvh::detail::windows::make_create_gamepad_request(8, options);
  const std::vector<std::uint8_t> descriptor(
    request.report_descriptor,
    request.report_descriptor + request.report_sizes.report_descriptor_size
  );

  ASSERT_GE(descriptor.size(), 6U);
  EXPECT_EQ(descriptor[0], 0x05U);
  EXPECT_EQ(descriptor[1], 0x01U);
  EXPECT_EQ(descriptor[2], 0x09U);
  EXPECT_EQ(descriptor[3], 0x04U);  // Joystick TLC required by DirectInput PID.
  EXPECT_EQ(descriptor[4], 0xA1U);
  EXPECT_EQ(descriptor[5], 0x01U);
  EXPECT_LE(descriptor.size(), LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE);
  EXPECT_EQ(request.report_sizes.output_report_size, lvh::detail::windows::generic_pid_output_report_size);

  constexpr std::array<std::uint8_t, 6> set_effect {0x09, 0x21, 0xA1, 0x02, 0x85, 0x11};
  constexpr std::array<std::uint8_t, 6> set_periodic {0x09, 0x6E, 0xA1, 0x02, 0x85, 0x14};
  constexpr std::array<std::uint8_t, 6> set_constant {0x09, 0x73, 0xA1, 0x02, 0x85, 0x15};
  constexpr std::array<std::uint8_t, 6> create_effect {0x09, 0xAB, 0xA1, 0x02, 0x85, 0x11};
  EXPECT_NE(std::ranges::search(descriptor, set_effect).begin(), descriptor.end());
  EXPECT_NE(std::ranges::search(descriptor, set_periodic).begin(), descriptor.end());
  EXPECT_NE(std::ranges::search(descriptor, set_constant).begin(), descriptor.end());
  EXPECT_NE(std::ranges::search(descriptor, create_effect).begin(), descriptor.end());

  // HIDMaestro's working UMDF contract intentionally serves these through
  // GetFeature without declaring the three Feature collections. Declaring
  // them alongside Create New Effect is known to fault pid.dll CreateEffect.
  constexpr std::array<std::uint8_t, 6> block_load_feature {0x09, 0x89, 0xA1, 0x02, 0x85, 0x12};
  constexpr std::array<std::uint8_t, 6> pool_feature {0x09, 0x7F, 0xA1, 0x02, 0x85, 0x13};
  constexpr std::array<std::uint8_t, 6> state_feature {0x09, 0x92, 0xA1, 0x02, 0x85, 0x14};
  EXPECT_EQ(std::ranges::search(descriptor, block_load_feature).begin(), descriptor.end());
  EXPECT_EQ(std::ranges::search(descriptor, pool_feature).begin(), descriptor.end());
  EXPECT_EQ(std::ranges::search(descriptor, state_feature).begin(), descriptor.end());

  // DirectInput requires the complete PID Output report set even though the
  // backend only normalizes Constant and Sine effects as gamepad rumble.
  constexpr std::array<std::uint8_t, 6> set_ramp {0x09, 0x74, 0xA1, 0x02, 0x85, 0x16};
  EXPECT_NE(std::ranges::search(descriptor, set_ramp).begin(), descriptor.end());

  // The public profile remains platform-neutral; Windows applies its transport contract at creation.
  ASSERT_GE(options.profile.report_descriptor.size(), 4U);
  EXPECT_EQ(options.profile.report_descriptor[3], 0x05U);
  EXPECT_EQ(options.profile.output_report_size, 9U);
}

TEST(WindowsProtocolTest, InvertsOnlyWindowsGenericTriggerBytes) {
  const std::vector<std::uint8_t> neutral {1, 0, 0, 128, 128, 128, 128, 0, 0};
  const auto neutral_windows = lvh::detail::windows::make_generic_windows_input_report(neutral);

  ASSERT_EQ(neutral_windows.size(), neutral.size());
  EXPECT_EQ(neutral_windows[7], 255U);
  EXPECT_EQ(neutral_windows[8], 255U);

  const std::vector<std::uint8_t> active {1, 0, 0, 128, 128, 128, 128, 64, 255};
  const auto active_windows = lvh::detail::windows::make_generic_windows_input_report(active);
  EXPECT_EQ(active_windows[7], 191U);
  EXPECT_EQ(active_windows[8], 0U);

  EXPECT_EQ(
    lvh::detail::windows::make_generic_windows_input_report(byte_report({1, 2, 3})),
    byte_report({1, 2, 3})
  );
}

TEST(WindowsProtocolTest, ImplementsGenericPidFeatureAllocationHandshake) {
  using namespace lvh::detail::windows;

  GenericPidFeatureState state;
  ASSERT_TRUE(
    state.handle_set_feature(
      generic_pid_create_new_effect_report_id,
      byte_report({0x11, 0x01, 0x00, 0x00})
    )
  );

  const auto block_load = state.get_feature_report(generic_pid_block_load_report_id);
  ASSERT_TRUE(block_load.has_value());
  ASSERT_EQ(block_load->size(), 5U);
  EXPECT_EQ((*block_load)[0], generic_pid_block_load_report_id);
  EXPECT_EQ((*block_load)[1], 1U);
  EXPECT_EQ((*block_load)[2], 1U);  // Block Load Success.

  const auto pool = state.get_feature_report(generic_pid_pool_report_id);
  ASSERT_TRUE(pool.has_value());
  ASSERT_EQ(pool->size(), 5U);
  EXPECT_EQ((*pool)[3], generic_pid_max_effects);
  EXPECT_EQ(std::byte {(*pool)[4]} & std::byte {0x01}, std::byte {0x01});  // Device-managed pool.

  ASSERT_TRUE(
    state.handle_output_report(
      generic_pid_effect_operation_report_id,
      byte_report({0x1A, 0x01, 0x01, 0x01})
    )
  );
  const auto playing = state.get_feature_report(generic_pid_state_report_id);
  ASSERT_TRUE(playing.has_value());
  EXPECT_EQ((*playing)[1], 1U);
  EXPECT_EQ(std::byte {(*playing)[2]} & std::byte {0x20}, std::byte {0x20});

  ASSERT_TRUE(
    state.handle_output_report(generic_pid_block_free_report_id, byte_report({0x1B, 0x01}))
  );
  const auto freed = state.get_feature_report(generic_pid_state_report_id);
  ASSERT_TRUE(freed.has_value());
  EXPECT_EQ((*freed)[1], 0U);
  EXPECT_EQ(std::byte {(*freed)[2]} & std::byte {0x20}, std::byte {0x00});

  EXPECT_FALSE(state.handle_set_feature(0xFF, byte_report({0xFF})));
  EXPECT_FALSE(state.get_feature_report(0xFF).has_value());
}

TEST(WindowsProtocolTest, DecodesGenericPidConstantAndPeriodicEffectsAsRumble) {
  using namespace lvh::detail::windows;

  GenericPidRumbleState state;
  std::vector<std::uint8_t> set_effect(generic_pid_output_report_size, 0);
  set_effect[0] = generic_pid_set_effect_report_id;
  set_effect[1] = 1;  // Effect Block Index.
  set_effect[2] = 1;  // Constant Force.
  set_effect[3] = 0xFF;  // Infinite duration (Null).
  set_effect[4] = 0xFF;
  set_effect[11] = 255;  // Effect gain.
  auto update = state.handle_output_report(set_effect);
  EXPECT_TRUE(update.recognized);
  EXPECT_FALSE(update.rumble_changed);

  update = state.handle_output_report(byte_report({generic_pid_set_constant_force_report_id, 1, 0x10, 0x27}));
  EXPECT_TRUE(update.recognized);
  EXPECT_FALSE(update.rumble_changed);

  update = state.handle_output_report(byte_report({generic_pid_effect_operation_report_id, 1, 1, 1}));
  EXPECT_TRUE(update.recognized);
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 65535U);

  update = state.handle_output_report(byte_report({generic_pid_device_gain_report_id, 128}));
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 32896U);

  update = state.handle_output_report(byte_report({generic_pid_effect_operation_report_id, 1, 3, 0}));
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 0U);

  std::ranges::fill(set_effect, 0);
  set_effect[0] = generic_pid_set_effect_report_id;
  set_effect[1] = 2;
  set_effect[2] = 2;  // Sine periodic.
  set_effect[3] = 0xFF;
  set_effect[4] = 0xFF;
  set_effect[11] = 255;
  EXPECT_TRUE(state.handle_output_report(set_effect).recognized);
  EXPECT_TRUE(
    state.handle_output_report(byte_report({generic_pid_set_periodic_report_id, 2, 0x88, 0x13})).recognized
  );
  update = state.handle_output_report(byte_report({generic_pid_effect_operation_report_id, 2, 1, 1}));
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_GT(update.strength, 0U);

  update = state.handle_output_report(byte_report({generic_pid_device_control_report_id, 2}));
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 0U);
  EXPECT_FALSE(state.handle_output_report(byte_report({0xFF})).recognized);
}

TEST(WindowsProtocolTest, HonorsGenericPidStartDelayDurationLoopCountAndExplicitStop) {
  using namespace lvh::detail::windows;
  using namespace std::chrono_literals;

  GenericPidRumbleState state;
  const auto start_time = GenericPidRumbleState::TimePoint {};
  std::vector<std::uint8_t> set_effect(generic_pid_output_report_size, 0);
  set_effect[0] = generic_pid_set_effect_report_id;
  set_effect[1] = 1;  // Effect Block Index.
  set_effect[2] = 1;  // Constant Force.
  set_effect[3] = 100;  // 100 ms duration.
  set_effect[9] = 20;  // 20 ms start delay.
  set_effect[11] = 255;  // Effect gain.
  EXPECT_TRUE(state.handle_output_report(set_effect, start_time).recognized);
  EXPECT_TRUE(
    state
      .handle_output_report(
        byte_report({generic_pid_set_constant_force_report_id, 1, 0x10, 0x27}),
        start_time
      )
      .recognized
  );

  auto update = state.handle_output_report(
    byte_report({generic_pid_effect_operation_report_id, 1, 1, 2}),
    start_time
  );
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 0U);
  ASSERT_TRUE(state.next_transition().has_value());
  EXPECT_EQ(*state.next_transition(), start_time + 20ms);

  set_effect[9] = 30;  // Reconfigure the pending start delay.
  update = state.handle_output_report(set_effect, start_time + 5ms);
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 0U);
  ASSERT_TRUE(state.next_transition().has_value());
  EXPECT_EQ(*state.next_transition(), start_time + 30ms);

  update = state.advance(start_time + 29ms);
  EXPECT_FALSE(update.rumble_changed);
  update = state.advance(start_time + 30ms);
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 65535U);

  set_effect[3] = 150;  // Extend each loop while the effect is active.
  set_effect[9] = 30;
  update = state.handle_output_report(set_effect, start_time + 40ms);
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 65535U);
  ASSERT_TRUE(state.next_transition().has_value());
  EXPECT_EQ(*state.next_transition(), start_time + 330ms);

  update = state.advance(start_time + 329ms);
  EXPECT_FALSE(update.rumble_changed);
  update = state.advance(start_time + 330ms);
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 0U);
  EXPECT_FALSE(state.next_transition().has_value());
  EXPECT_FALSE(state.advance(start_time + 500ms).rumble_changed);

  set_effect[3] = 0xFF;  // Infinite duration (Null).
  set_effect[4] = 0xFF;
  set_effect[9] = 0;
  EXPECT_TRUE(state.handle_output_report(set_effect, start_time + 500ms).recognized);
  update = state.handle_output_report(
    byte_report({generic_pid_effect_operation_report_id, 1, 1, 1}),
    start_time + 500ms
  );
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 65535U);
  EXPECT_FALSE(state.next_transition().has_value());

  update = state.handle_output_report(
    byte_report({generic_pid_effect_operation_report_id, 1, 3, 0}),
    start_time + 501ms
  );
  EXPECT_TRUE(update.rumble_changed);
  EXPECT_EQ(update.strength, 0U);
}

TEST(WindowsProtocolTest, TruncatesOversizedGamepadCreateRequestFields) {
  lvh::CreateGamepadOptions options;
  options.profile = minimal_gamepad_profile();
  options.profile.input_report_size = LVH_WINDOWS_MAX_INPUT_REPORT_SIZE + 1U;
  options.profile.output_report_size = LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE + 1U;
  options.profile.report_descriptor.assign(LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE + 5U, 0xAB);
  options.profile.name.assign(LVH_WINDOWS_MAX_DEVICE_NAME_SIZE + 5U, 'n');
  options.profile.manufacturer.assign(LVH_WINDOWS_MAX_MANUFACTURER_SIZE + 5U, 'm');
  options.metadata.stable_id.assign(LVH_WINDOWS_MAX_STABLE_ID_SIZE + 5U, 's');

  const auto request = lvh::detail::windows::make_create_gamepad_request(9, options);

  EXPECT_EQ(request.report_sizes.input_report_size, LVH_WINDOWS_MAX_INPUT_REPORT_SIZE);
  EXPECT_EQ(request.report_sizes.output_report_size, LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE);
  EXPECT_EQ(request.report_sizes.report_descriptor_size, LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE);
  EXPECT_EQ(request.report_descriptor[0], 0xABU);
  EXPECT_EQ(request.report_descriptor[LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE - 1U], 0xABU);

  EXPECT_EQ(request.report_sizes.name_size, LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U);
  EXPECT_EQ(request.name[LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 2U], 'n');
  EXPECT_EQ(request.name[LVH_WINDOWS_MAX_DEVICE_NAME_SIZE - 1U], '\0');

  EXPECT_EQ(request.report_sizes.manufacturer_size, LVH_WINDOWS_MAX_MANUFACTURER_SIZE - 1U);
  EXPECT_EQ(request.manufacturer[LVH_WINDOWS_MAX_MANUFACTURER_SIZE - 2U], 'm');
  EXPECT_EQ(request.manufacturer[LVH_WINDOWS_MAX_MANUFACTURER_SIZE - 1U], '\0');

  EXPECT_EQ(request.report_sizes.stable_id_size, LVH_WINDOWS_MAX_STABLE_ID_SIZE - 1U);
  EXPECT_EQ(request.stable_id[LVH_WINDOWS_MAX_STABLE_ID_SIZE - 2U], 's');
  EXPECT_EQ(request.stable_id[LVH_WINDOWS_MAX_STABLE_ID_SIZE - 1U], '\0');
}

TEST(WindowsProtocolTest, PacksSubmitAndDestroyRequests) {
  const std::vector<std::uint8_t> report {1, 2, 3, 4, 5};

  const auto submit = lvh::detail::windows::make_submit_input_report_request(17, report);
  EXPECT_EQ(submit.version, LVH_WINDOWS_CONTROL_PROTOCOL_VERSION);
  EXPECT_EQ(submit.size, sizeof(submit));
  EXPECT_EQ(submit.driver_device_id, 17U);
  EXPECT_EQ(submit.report_size, report.size());
  EXPECT_EQ(submit.report[0], report[0]);
  EXPECT_EQ(submit.report[4], report[4]);

  const auto destroy = lvh::detail::windows::make_destroy_device_request(17);
  EXPECT_EQ(destroy.version, LVH_WINDOWS_CONTROL_PROTOCOL_VERSION);
  EXPECT_EQ(destroy.size, sizeof(destroy));
  EXPECT_EQ(destroy.driver_device_id, 17U);
}

TEST(WindowsProtocolTest, SubmitInputReportTruncatesAndZeroFills) {
  std::vector<std::uint8_t> oversized_report(LVH_WINDOWS_MAX_INPUT_REPORT_SIZE + 3U, 0x7F);
  const auto oversized = lvh::detail::windows::make_submit_input_report_request(19, oversized_report);

  EXPECT_EQ(oversized.report_size, LVH_WINDOWS_MAX_INPUT_REPORT_SIZE);
  EXPECT_EQ(oversized.report[0], 0x7FU);
  EXPECT_EQ(oversized.report[LVH_WINDOWS_MAX_INPUT_REPORT_SIZE - 1U], 0x7FU);

  const auto empty = lvh::detail::windows::make_submit_input_report_request(19, {});
  EXPECT_EQ(empty.report_size, 0U);
  EXPECT_EQ(empty.report[0], 0U);
  EXPECT_EQ(empty.report[LVH_WINDOWS_MAX_INPUT_REPORT_SIZE - 1U], 0U);
}
