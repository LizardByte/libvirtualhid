/**
 * @file src/platform/windows/shared/lvh_windows_broker_protocol.h
 * @brief Stable named-pipe protocol shared by the Windows backend, broker, and control UI.
 * @note This internal protocol header is C++-only.
 */
#pragma once

#include "lvh_windows_protocol.h"

#include <array>
#include <stdint.h>

inline constexpr uint32_t LVH_WINDOWS_BROKER_PROTOCOL_VERSION = 4u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE = 512u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_LICENSE_KEY_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_INSTANCE_NAME_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_PLAN_NAME_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_CUSTOMER_EMAIL_SIZE = 128u;
inline constexpr char LVH_WINDOWS_BROKER_PIPE_PATH[] = R"(\\.\pipe\libvirtualhid-broker)";

enum class LvhWindowsBrokerRequestType : uint32_t {
  status = 1,
  create_device = 2,
  destroy_device = 3,
  activate_license = 4,
  validate_license = 5,
  deactivate_license = 6,
};

enum class LvhWindowsBrokerStatusCode : uint32_t {
  success = 0,
  invalid_argument = 1,
  unsupported_profile = 2,
  device_not_found = 3,
  backend_unavailable = 4,
  backend_failure = 5,
  license_required = 6,
  license_invalid = 7,
  activation_limit_reached = 8,
  network_unavailable = 9,
};

enum class LvhWindowsBrokerLicenseState : uint32_t {
  free = 0,
  licensed = 1,
  expired = 2,
  disabled = 3,
  invalid = 4,
};

struct LvhWindowsBrokerRequestHeader {
  uint32_t version;
  uint32_t size;
  uint32_t type;
  uint32_t reserved0;
};

struct LvhWindowsBrokerLicenseStatus {
  uint32_t version;
  uint32_t size;
  uint32_t state;
  uint32_t active_devices;
  uint32_t free_active_device_limit;
  uint32_t activation_limit;
  uint32_t activation_usage;
  std::array<char, LVH_WINDOWS_BROKER_MAX_PLAN_NAME_SIZE> plan_name;
  std::array<char, LVH_WINDOWS_BROKER_MAX_CUSTOMER_EMAIL_SIZE> customer_email;
  std::array<char, LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE> message;
};

struct LvhWindowsBrokerStatusRequest {
  LvhWindowsBrokerRequestHeader header;
};

struct LvhWindowsBrokerStatusResponse {
  uint32_t version;
  uint32_t size;
  uint32_t status;
  uint32_t reserved0;
  LvhWindowsBrokerLicenseStatus license;
  std::array<char, LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE> message;
};

struct LvhWindowsBrokerCreateDeviceRequest {
  LvhWindowsBrokerRequestHeader header;
  uint64_t client_control_handle;
  LvhWindowsCreateDeviceRequest device;
};

struct LvhWindowsBrokerCreateDeviceResponse {
  uint32_t version;
  uint32_t size;
  uint32_t status;
  uint32_t reserved0;
  LvhWindowsCreateDeviceResponse device;
  LvhWindowsBrokerLicenseStatus license;
  std::array<char, LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE> message;
};

struct LvhWindowsBrokerDestroyDeviceRequest {
  LvhWindowsBrokerRequestHeader header;
  LvhWindowsDestroyDeviceRequest device;
};

struct LvhWindowsBrokerDestroyDeviceResponse {
  uint32_t version;
  uint32_t size;
  uint32_t status;
  uint32_t reserved0;
  LvhWindowsBrokerLicenseStatus license;
  std::array<char, LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE> message;
};

struct LvhWindowsBrokerLicenseRequest {
  LvhWindowsBrokerRequestHeader header;
  std::array<char, LVH_WINDOWS_BROKER_MAX_LICENSE_KEY_SIZE> license_key;
  std::array<char, LVH_WINDOWS_BROKER_MAX_INSTANCE_NAME_SIZE> instance_name;
};

struct LvhWindowsBrokerLicenseResponse {
  uint32_t version;
  uint32_t size;
  uint32_t status;
  uint32_t reserved0;
  LvhWindowsBrokerLicenseStatus license;
  std::array<char, LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE> message;
};
