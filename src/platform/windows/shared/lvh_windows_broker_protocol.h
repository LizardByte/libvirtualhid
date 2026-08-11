/**
 * @file src/platform/windows/shared/lvh_windows_broker_protocol.h
 * @brief Stable named-pipe protocol shared by the Windows backend, broker, and control UI.
 */
#pragma once

#include "lvh_windows_protocol.h"

#include <stdint.h>

#ifdef __cplusplus

inline constexpr uint32_t LVH_WINDOWS_BROKER_PROTOCOL_VERSION = 2u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE = 512u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_LICENSE_KEY_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_INSTANCE_NAME_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_PLAN_NAME_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_CUSTOMER_EMAIL_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_BROKER_MAX_TIMESTAMP_SIZE = 64u;
inline constexpr char LVH_WINDOWS_BROKER_PIPE_PATH[] = R"(\\.\pipe\libvirtualhid-broker)";

enum class LvhWindowsBrokerRequestType : uint32_t {
  status = 1,
  create_gamepad = 2,
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

#else

enum {
  LVH_WINDOWS_BROKER_PROTOCOL_VERSION = 2u,
  LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE = 512u,
  LVH_WINDOWS_BROKER_MAX_LICENSE_KEY_SIZE = 128u,
  LVH_WINDOWS_BROKER_MAX_INSTANCE_NAME_SIZE = 128u,
  LVH_WINDOWS_BROKER_MAX_PLAN_NAME_SIZE = 128u,
  LVH_WINDOWS_BROKER_MAX_CUSTOMER_EMAIL_SIZE = 128u,
  LVH_WINDOWS_BROKER_MAX_TIMESTAMP_SIZE = 64u,
};

static const char LVH_WINDOWS_BROKER_PIPE_PATH[] = "\\\\.\\pipe\\libvirtualhid-broker";

enum LvhWindowsBrokerRequestType {
  LVH_WINDOWS_BROKER_REQUEST_STATUS = 1,
  LVH_WINDOWS_BROKER_REQUEST_CREATE_GAMEPAD = 2,
  LVH_WINDOWS_BROKER_REQUEST_DESTROY_DEVICE = 3,
  LVH_WINDOWS_BROKER_REQUEST_ACTIVATE_LICENSE = 4,
  LVH_WINDOWS_BROKER_REQUEST_VALIDATE_LICENSE = 5,
  LVH_WINDOWS_BROKER_REQUEST_DEACTIVATE_LICENSE = 6,
};

enum LvhWindowsBrokerStatusCode {
  LVH_WINDOWS_BROKER_STATUS_SUCCESS = 0,
  LVH_WINDOWS_BROKER_STATUS_INVALID_ARGUMENT = 1,
  LVH_WINDOWS_BROKER_STATUS_UNSUPPORTED_PROFILE = 2,
  LVH_WINDOWS_BROKER_STATUS_DEVICE_NOT_FOUND = 3,
  LVH_WINDOWS_BROKER_STATUS_BACKEND_UNAVAILABLE = 4,
  LVH_WINDOWS_BROKER_STATUS_BACKEND_FAILURE = 5,
  LVH_WINDOWS_BROKER_STATUS_LICENSE_REQUIRED = 6,
  LVH_WINDOWS_BROKER_STATUS_LICENSE_INVALID = 7,
  LVH_WINDOWS_BROKER_STATUS_ACTIVATION_LIMIT_REACHED = 8,
  LVH_WINDOWS_BROKER_STATUS_NETWORK_UNAVAILABLE = 9,
};

enum LvhWindowsBrokerLicenseState {
  LVH_WINDOWS_BROKER_LICENSE_FREE = 0,
  LVH_WINDOWS_BROKER_LICENSE_LICENSED = 1,
  LVH_WINDOWS_BROKER_LICENSE_EXPIRED = 2,
  LVH_WINDOWS_BROKER_LICENSE_DISABLED = 3,
  LVH_WINDOWS_BROKER_LICENSE_INVALID = 4,
};

#endif

extern "C" {

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
    char plan_name[LVH_WINDOWS_BROKER_MAX_PLAN_NAME_SIZE];
    char customer_email[LVH_WINDOWS_BROKER_MAX_CUSTOMER_EMAIL_SIZE];
    char expires_at[LVH_WINDOWS_BROKER_MAX_TIMESTAMP_SIZE];
    char message[LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE];
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
    char message[LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE];
  };

  struct LvhWindowsBrokerCreateGamepadRequest {
    LvhWindowsBrokerRequestHeader header;
    uint64_t client_control_handle;
    LvhWindowsCreateGamepadRequest gamepad;
  };

  struct LvhWindowsBrokerCreateGamepadResponse {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t reserved0;
    LvhWindowsCreateGamepadResponse gamepad;
    LvhWindowsBrokerLicenseStatus license;
    char message[LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE];
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
    char message[LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE];
  };

  struct LvhWindowsBrokerLicenseRequest {
    LvhWindowsBrokerRequestHeader header;
    char license_key[LVH_WINDOWS_BROKER_MAX_LICENSE_KEY_SIZE];
    char instance_name[LVH_WINDOWS_BROKER_MAX_INSTANCE_NAME_SIZE];
  };

  struct LvhWindowsBrokerLicenseResponse {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t reserved0;
    LvhWindowsBrokerLicenseStatus license;
    char message[LVH_WINDOWS_BROKER_MAX_MESSAGE_SIZE];
  };
}
