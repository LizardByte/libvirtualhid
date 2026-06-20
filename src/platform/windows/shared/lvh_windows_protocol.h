/**
 * @file src/platform/windows/shared/lvh_windows_protocol.h
 * @brief Stable control protocol shared by the Windows client backend and UMDF driver.
 */
#pragma once

#include <stdint.h>

#define LVH_WINDOWS_CONTROL_PROTOCOL_VERSION 1u
#define LVH_WINDOWS_CONTROL_DEVICE_PATH "\\\\.\\LibVirtualHid"

#define LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE 1024u
#define LVH_WINDOWS_MAX_INPUT_REPORT_SIZE 256u
#define LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE 256u
#define LVH_WINDOWS_MAX_DEVICE_PATH_SIZE 260u
#define LVH_WINDOWS_MAX_DEVICE_NAME_SIZE 128u
#define LVH_WINDOWS_MAX_MANUFACTURER_SIZE 128u
#define LVH_WINDOWS_MAX_STABLE_ID_SIZE 128u

#define LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID 0x8000u
#define LVH_WINDOWS_METHOD_BUFFERED 0u
#define LVH_WINDOWS_FILE_ANY_ACCESS 0u
#define LVH_WINDOWS_FILE_READ_ACCESS 1u
#define LVH_WINDOWS_FILE_WRITE_ACCESS 2u
#define LVH_WINDOWS_CTL_CODE(device_type, function_code, method, access) \
  (((device_type) << 16u) | ((access) << 14u) | ((function_code) << 2u) | (method))

#define LVH_WINDOWS_IOCTL_CREATE_GAMEPAD \
  LVH_WINDOWS_CTL_CODE( \
    LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID, \
    0x800u, \
    LVH_WINDOWS_METHOD_BUFFERED, \
    LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS \
  )
#define LVH_WINDOWS_IOCTL_DESTROY_DEVICE \
  LVH_WINDOWS_CTL_CODE( \
    LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID, \
    0x801u, \
    LVH_WINDOWS_METHOD_BUFFERED, \
    LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS \
  )
#define LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT \
  LVH_WINDOWS_CTL_CODE( \
    LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID, \
    0x802u, \
    LVH_WINDOWS_METHOD_BUFFERED, \
    LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS \
  )
#define LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT \
  LVH_WINDOWS_CTL_CODE( \
    LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID, \
    0x803u, \
    LVH_WINDOWS_METHOD_BUFFERED, \
    LVH_WINDOWS_FILE_READ_ACCESS \
  )

#define LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE 0x00000001u
#define LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION 0x00000002u
#define LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD 0x00000004u
#define LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED 0x00000008u
#define LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY 0x00000010u
#define LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS 0x00000020u

#ifdef __cplusplus
extern "C" {
#endif

  enum LvhWindowsProtocolStatus {
    LVH_WINDOWS_STATUS_SUCCESS = 0,
    LVH_WINDOWS_STATUS_INVALID_ARGUMENT = 1,
    LVH_WINDOWS_STATUS_UNSUPPORTED_PROFILE = 2,
    LVH_WINDOWS_STATUS_DEVICE_NOT_FOUND = 3,
    LVH_WINDOWS_STATUS_BACKEND_FAILURE = 4,
  };

  enum LvhWindowsBusType {
    LVH_WINDOWS_BUS_UNKNOWN = 0,
    LVH_WINDOWS_BUS_USB = 1,
    LVH_WINDOWS_BUS_BLUETOOTH = 2,
  };

  enum LvhWindowsGamepadProfileKind {
    LVH_WINDOWS_GAMEPAD_GENERIC = 0,
    LVH_WINDOWS_GAMEPAD_XBOX_360 = 1,
    LVH_WINDOWS_GAMEPAD_XBOX_ONE = 2,
    LVH_WINDOWS_GAMEPAD_XBOX_SERIES = 3,
    LVH_WINDOWS_GAMEPAD_DUALSENSE = 4,
    LVH_WINDOWS_GAMEPAD_SWITCH_PRO = 5,
  };

#pragma pack(push, 1)

  struct LvhWindowsCreateGamepadRequest {
    uint32_t version;
    uint32_t size;
    uint64_t client_device_id;
    uint32_t bus_type;
    uint32_t gamepad_kind;
    uint32_t flags;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t report_id;
    uint8_t reserved0[7];
    uint32_t input_report_size;
    uint32_t output_report_size;
    uint32_t report_descriptor_size;
    uint32_t name_size;
    uint32_t manufacturer_size;
    uint32_t stable_id_size;
    uint8_t report_descriptor[LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE];
    char name[LVH_WINDOWS_MAX_DEVICE_NAME_SIZE];
    char manufacturer[LVH_WINDOWS_MAX_MANUFACTURER_SIZE];
    char stable_id[LVH_WINDOWS_MAX_STABLE_ID_SIZE];
  };

  struct LvhWindowsCreateGamepadResponse {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t reserved0;
    uint64_t driver_device_id;
    char device_path[LVH_WINDOWS_MAX_DEVICE_PATH_SIZE];
  };

  struct LvhWindowsDestroyDeviceRequest {
    uint32_t version;
    uint32_t size;
    uint64_t driver_device_id;
  };

  struct LvhWindowsSubmitInputReportRequest {
    uint32_t version;
    uint32_t size;
    uint64_t driver_device_id;
    uint32_t report_size;
    uint32_t reserved0;
    uint8_t report[LVH_WINDOWS_MAX_INPUT_REPORT_SIZE];
  };

  struct LvhWindowsOutputReportEvent {
    uint32_t version;
    uint32_t size;
    uint64_t driver_device_id;
    uint32_t report_size;
    uint32_t reserved0;
    uint8_t report[LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE];
  };

#pragma pack(pop)

#ifdef __cplusplus
}
#endif
