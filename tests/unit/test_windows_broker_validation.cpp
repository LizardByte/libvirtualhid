/**
 * @file tests/unit/test_windows_broker_validation.cpp
 * @brief Unit tests for validation of untrusted Windows broker messages.
 */

// local includes
#include "broker_request_validation.hpp"
#include "platform/windows/control_protocol.hpp"

// test includes
#include <gtest/gtest.h>

// standard includes
#include <algorithm>
#include <cstring>
#include <utility>

// lib includes
#include <libvirtualhid/types.hpp>

namespace {

  LvhWindowsBrokerRequestHeader request_header(
    LvhWindowsBrokerRequestType type,
    std::uint32_t size
  ) {
    return {
      .version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION,
      .size = size,
      .type = std::to_underlying(type),
      .reserved0 = 0U,
    };
  }

  LvhWindowsBrokerCreateGamepadRequest valid_create_request() {
    lvh::CreateGamepadOptions options;
    options.profile.device_type = lvh::DeviceType::gamepad;
    options.profile.gamepad_kind = lvh::GamepadProfileKind::generic;
    options.profile.bus_type = lvh::BusType::usb;
    options.profile.vendor_id = 0x1209;
    options.profile.product_id = 0x0001;
    options.profile.version = 0x0001;
    options.profile.input_report_size = 4;
    options.profile.output_report_size = 2;
    options.profile.name = "Test gamepad";
    options.profile.manufacturer = "LizardByte";
    options.profile.report_descriptor = {0x05, 0x01, 0x09, 0x05};

    LvhWindowsBrokerCreateGamepadRequest request {};
    request.header = request_header(
      LvhWindowsBrokerRequestType::create_gamepad,
      sizeof(request)
    );
    request.client_control_handle = 1U;
    request.gamepad = lvh::detail::windows::make_create_gamepad_request(1U, options);
    return request;
  }

  LvhWindowsBrokerDestroyDeviceRequest valid_destroy_request() {
    LvhWindowsSessionToken token {};
    token.bytes[0] = 1U;

    LvhWindowsBrokerDestroyDeviceRequest request {};
    request.header = request_header(
      LvhWindowsBrokerRequestType::destroy_device,
      sizeof(request)
    );
    request.device = lvh::detail::windows::make_destroy_device_request(1U, token);
    return request;
  }

  LvhWindowsBrokerLicenseRequest valid_license_request(LvhWindowsBrokerRequestType type) {
    LvhWindowsBrokerLicenseRequest request {};
    request.header = request_header(type, sizeof(request));
    if (type == LvhWindowsBrokerRequestType::activate_license) {
      std::memcpy(request.license_key, "test-key", sizeof("test-key"));
      std::memcpy(request.instance_name, "test-machine", sizeof("test-machine"));
    }
    return request;
  }

}  // namespace

TEST(WindowsBrokerValidationTest, ValidatesStatusHeaderAndReservedField) {
  LvhWindowsBrokerStatusRequest request {};
  request.header = request_header(
    LvhWindowsBrokerRequestType::status,
    sizeof(request)
  );
  EXPECT_TRUE(lvh::windows::broker_validation::valid_request(request));

  request.header.reserved0 = 1U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));
  request.header.reserved0 = 0U;
  ++request.header.size;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));
  request.header.size = sizeof(request);
  ++request.header.version;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));
  request.header.version = LVH_WINDOWS_BROKER_PROTOCOL_VERSION;
  request.header.type = std::to_underlying(LvhWindowsBrokerRequestType::validate_license);
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));
}

TEST(WindowsBrokerValidationTest, RejectsMalformedCreateFields) {
  const auto valid = valid_create_request();
  EXPECT_TRUE(lvh::windows::broker_validation::valid_request(valid));

  auto request = valid;
  request.client_control_handle = 0U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  ++request.header.version;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  ++request.gamepad.version;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.client_device_id = 0U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  ++request.gamepad.size;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.bus_type = LVH_WINDOWS_BUS_UNKNOWN;
  EXPECT_TRUE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.bus_type = LVH_WINDOWS_BUS_BLUETOOTH;
  EXPECT_TRUE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.bus_type = 99U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.gamepad_kind = 99U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.flags = 0x80000000U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.hardware_ids.reserved0[0] = 1U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.report_sizes.report_descriptor_size = 0U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.report_sizes.input_report_size = 0U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.report_sizes.input_report_size = LVH_WINDOWS_MAX_INPUT_REPORT_SIZE + 1U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.report_sizes.output_report_size = LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE + 1U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.report_sizes.report_descriptor_size = LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE + 1U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.report_sizes.name_size = sizeof(request.gamepad.name);
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.name[1] = '\0';
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.manufacturer[1] = '\0';
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  std::memcpy(request.gamepad.stable_id, "stable", sizeof("stable"));
  request.gamepad.report_sizes.stable_id_size = sizeof("stable") - 1U;
  request.gamepad.stable_id[1] = '\0';
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.name[request.gamepad.report_sizes.name_size] = 'x';
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.gamepad.report_descriptor[request.gamepad.report_sizes.report_descriptor_size] = 1U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));
}

TEST(WindowsBrokerValidationTest, RejectsMalformedDestroyFields) {
  const auto valid = valid_destroy_request();
  EXPECT_TRUE(lvh::windows::broker_validation::valid_request(valid));

  auto request = valid;
  request.device.size = 0U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  ++request.header.version;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  ++request.device.version;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  request.device.driver_device_id = 0U;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));

  request = valid;
  std::ranges::fill(request.device.session_token.bytes, std::uint8_t {});
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request));
}

TEST(WindowsBrokerValidationTest, RequiresBoundedActivationStrings) {
  const auto valid = valid_license_request(LvhWindowsBrokerRequestType::activate_license);
  EXPECT_TRUE(lvh::windows::broker_validation::valid_request(valid, LvhWindowsBrokerRequestType::activate_license));

  auto request = valid;
  std::ranges::fill(request.license_key, 'x');
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, LvhWindowsBrokerRequestType::activate_license));

  request = valid;
  std::ranges::fill(request.instance_name, 'x');
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, LvhWindowsBrokerRequestType::activate_license));

  request = valid;
  std::ranges::fill(request.license_key, '\0');
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, LvhWindowsBrokerRequestType::activate_license));

  request = valid;
  std::ranges::fill(request.instance_name, '\0');
  EXPECT_TRUE(lvh::windows::broker_validation::valid_request(request, LvhWindowsBrokerRequestType::activate_license));

  request = valid;
  request.license_key[sizeof("test-key")] = 'x';
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, LvhWindowsBrokerRequestType::activate_license));
}

TEST(WindowsBrokerValidationTest, RequiresEmptyValidationAndDeactivationPayloads) {
  for (const auto type : {
         LvhWindowsBrokerRequestType::validate_license,
         LvhWindowsBrokerRequestType::deactivate_license,
       }) {
    auto request = valid_license_request(type);
    EXPECT_TRUE(lvh::windows::broker_validation::valid_request(request, type));

    request.license_key[0] = 'x';
    EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, type));

    request = valid_license_request(type);
    request.instance_name[0] = 'x';
    EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, type));
  }
}

TEST(WindowsBrokerValidationTest, RejectsInvalidLicenseHeadersAndRequestTypes) {
  using enum LvhWindowsBrokerRequestType;

  auto request = valid_license_request(validate_license);
  ++request.header.size;
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, validate_license));

  for (const auto type : {
         status,
         create_gamepad,
         destroy_device,
       }) {
    request = valid_license_request(type);
    EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, type));
  }

  const auto unknown_type = static_cast<LvhWindowsBrokerRequestType>(999U);
  request = valid_license_request(unknown_type);
  EXPECT_FALSE(lvh::windows::broker_validation::valid_request(request, unknown_type));
}
