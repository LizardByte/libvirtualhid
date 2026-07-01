/**
 * @file src/platform/windows/shared/lvh_windows_protocol.h
 * @brief Stable control protocol shared by the Windows client backend and UMDF driver.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus

inline constexpr uint32_t LVH_WINDOWS_CONTROL_PROTOCOL_VERSION = 1u;
inline constexpr char LVH_WINDOWS_CONTROL_DEVICE_PATH[] = R"(\\.\LibVirtualHid)";
inline constexpr char LVH_WINDOWS_GLOBAL_CONTROL_DEVICE_PATH[] = R"(\\.\Global\LibVirtualHid)";

inline constexpr uint32_t LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE = 1024u;
inline constexpr uint32_t LVH_WINDOWS_MAX_INPUT_REPORT_SIZE = 256u;
inline constexpr uint32_t LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE = 256u;
inline constexpr uint32_t LVH_WINDOWS_MAX_DEVICE_PATH_SIZE = 260u;
inline constexpr uint32_t LVH_WINDOWS_MAX_DEVICE_NAME_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_MAX_MANUFACTURER_SIZE = 128u;
inline constexpr uint32_t LVH_WINDOWS_MAX_STABLE_ID_SIZE = 128u;

inline constexpr uint32_t LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID = 0x8000u;
inline constexpr uint32_t LVH_WINDOWS_METHOD_BUFFERED = 0u;
inline constexpr uint32_t LVH_WINDOWS_FILE_READ_ACCESS = 1u;
inline constexpr uint32_t LVH_WINDOWS_FILE_WRITE_ACCESS = 2u;

constexpr uint32_t lvh_windows_ctl_code(
  uint32_t device_type,
  uint32_t function_code,
  uint32_t method,
  uint32_t access
) noexcept {
  return (device_type << 16u) | (access << 14u) | (function_code << 2u) | method;
}

inline constexpr uint32_t LVH_WINDOWS_IOCTL_CREATE_GAMEPAD = lvh_windows_ctl_code(
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID,
  0x800u,
  LVH_WINDOWS_METHOD_BUFFERED,
  LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS
);
inline constexpr uint32_t LVH_WINDOWS_IOCTL_DESTROY_DEVICE = lvh_windows_ctl_code(
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID,
  0x801u,
  LVH_WINDOWS_METHOD_BUFFERED,
  LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS
);
inline constexpr uint32_t LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT = lvh_windows_ctl_code(
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID,
  0x802u,
  LVH_WINDOWS_METHOD_BUFFERED,
  LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS
);
inline constexpr uint32_t LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT = lvh_windows_ctl_code(
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID,
  0x803u,
  LVH_WINDOWS_METHOD_BUFFERED,
  LVH_WINDOWS_FILE_READ_ACCESS
);

inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE = 0x00000001u;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION = 0x00000002u;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD = 0x00000004u;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED = 0x00000008u;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY = 0x00000010u;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS = 0x00000020u;

enum class LvhWindowsProtocolStatus : uint32_t {
  success = 0,
  invalid_argument = 1,
  unsupported_profile = 2,
  device_not_found = 3,
  backend_failure = 4,
};

enum class LvhWindowsBusType : uint32_t {
  unknown = 0,
  usb = 1,
  bluetooth = 2,
};

enum class LvhWindowsGamepadProfileKind : uint32_t {
  generic = 0,
  xbox_360 = 1,
  xbox_one = 2,
  xbox_series = 3,
  dualsense = 4,
  switch_pro = 5,
  dualshock4 = 6,
};

namespace lvh_windows_protocol_detail {
  constexpr uint32_t to_uint32(auto value) noexcept {
    return static_cast<uint32_t>(value);
  }

  using enum LvhWindowsProtocolStatus;
  inline constexpr uint32_t status_success = to_uint32(success);
  inline constexpr uint32_t status_invalid_argument = to_uint32(invalid_argument);
  inline constexpr uint32_t status_unsupported_profile = to_uint32(unsupported_profile);
  inline constexpr uint32_t status_device_not_found = to_uint32(device_not_found);
  inline constexpr uint32_t status_backend_failure = to_uint32(backend_failure);

  using enum LvhWindowsBusType;
  inline constexpr uint32_t bus_unknown = to_uint32(unknown);
  inline constexpr uint32_t bus_usb = to_uint32(usb);
  inline constexpr uint32_t bus_bluetooth = to_uint32(bluetooth);

  using enum LvhWindowsGamepadProfileKind;
  inline constexpr uint32_t gamepad_generic = to_uint32(generic);
  inline constexpr uint32_t gamepad_xbox_360 = to_uint32(xbox_360);
  inline constexpr uint32_t gamepad_xbox_one = to_uint32(xbox_one);
  inline constexpr uint32_t gamepad_xbox_series = to_uint32(xbox_series);
  inline constexpr uint32_t gamepad_dualsense = to_uint32(dualsense);
  inline constexpr uint32_t gamepad_switch_pro = to_uint32(switch_pro);
  inline constexpr uint32_t gamepad_dualshock4 = to_uint32(dualshock4);
}  // namespace lvh_windows_protocol_detail

inline constexpr uint32_t LVH_WINDOWS_STATUS_SUCCESS = lvh_windows_protocol_detail::status_success;
inline constexpr uint32_t LVH_WINDOWS_STATUS_INVALID_ARGUMENT =
  lvh_windows_protocol_detail::status_invalid_argument;
inline constexpr uint32_t LVH_WINDOWS_STATUS_UNSUPPORTED_PROFILE =
  lvh_windows_protocol_detail::status_unsupported_profile;
inline constexpr uint32_t LVH_WINDOWS_STATUS_DEVICE_NOT_FOUND = lvh_windows_protocol_detail::status_device_not_found;
inline constexpr uint32_t LVH_WINDOWS_STATUS_BACKEND_FAILURE = lvh_windows_protocol_detail::status_backend_failure;

inline constexpr uint32_t LVH_WINDOWS_BUS_UNKNOWN = lvh_windows_protocol_detail::bus_unknown;
inline constexpr uint32_t LVH_WINDOWS_BUS_USB = lvh_windows_protocol_detail::bus_usb;
inline constexpr uint32_t LVH_WINDOWS_BUS_BLUETOOTH = lvh_windows_protocol_detail::bus_bluetooth;

inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_GENERIC = lvh_windows_protocol_detail::gamepad_generic;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_XBOX_360 = lvh_windows_protocol_detail::gamepad_xbox_360;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_XBOX_ONE = lvh_windows_protocol_detail::gamepad_xbox_one;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_XBOX_SERIES = lvh_windows_protocol_detail::gamepad_xbox_series;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_DUALSENSE = lvh_windows_protocol_detail::gamepad_dualsense;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_SWITCH_PRO = lvh_windows_protocol_detail::gamepad_switch_pro;
inline constexpr uint32_t LVH_WINDOWS_GAMEPAD_DUALSHOCK4 =
  lvh_windows_protocol_detail::gamepad_dualshock4;

#else

enum {
  LVH_WINDOWS_CONTROL_PROTOCOL_VERSION = 1u,
  LVH_WINDOWS_MAX_REPORT_DESCRIPTOR_SIZE = 1024u,
  LVH_WINDOWS_MAX_INPUT_REPORT_SIZE = 256u,
  LVH_WINDOWS_MAX_OUTPUT_REPORT_SIZE = 256u,
  LVH_WINDOWS_MAX_DEVICE_PATH_SIZE = 260u,
  LVH_WINDOWS_MAX_DEVICE_NAME_SIZE = 128u,
  LVH_WINDOWS_MAX_MANUFACTURER_SIZE = 128u,
  LVH_WINDOWS_MAX_STABLE_ID_SIZE = 128u,
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID = 0x8000u,
  LVH_WINDOWS_METHOD_BUFFERED = 0u,
  LVH_WINDOWS_FILE_READ_ACCESS = 1u,
  LVH_WINDOWS_FILE_WRITE_ACCESS = 2u,
  LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RUMBLE = 0x00000001u,
  LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_MOTION = 0x00000002u,
  LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_TOUCHPAD = 0x00000004u,
  LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_RGB_LED = 0x00000008u,
  LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_BATTERY = 0x00000010u,
  LVH_WINDOWS_GAMEPAD_FLAG_SUPPORTS_ADAPTIVE_TRIGGERS = 0x00000020u,
};

static const char LVH_WINDOWS_CONTROL_DEVICE_PATH[] = "\\\\.\\LibVirtualHid";
static const char LVH_WINDOWS_GLOBAL_CONTROL_DEVICE_PATH[] = "\\\\.\\Global\\LibVirtualHid";

static const uint32_t LVH_WINDOWS_IOCTL_CREATE_GAMEPAD = 0x8000E000u;
static const uint32_t LVH_WINDOWS_IOCTL_DESTROY_DEVICE = 0x8000E004u;
static const uint32_t LVH_WINDOWS_IOCTL_SUBMIT_INPUT_REPORT = 0x8000E008u;
static const uint32_t LVH_WINDOWS_IOCTL_READ_OUTPUT_REPORT = 0x8000600Cu;

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
  LVH_WINDOWS_GAMEPAD_DUALSHOCK4 = 6,
};

#endif

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

  struct LvhWindowsGamepadHardwareIds {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t device_version;
    uint8_t report_id;
    uint8_t reserved0[7];
  };

  struct LvhWindowsGamepadReportSizes {
    uint32_t input_report_size;
    uint32_t output_report_size;
    uint32_t report_descriptor_size;
    uint32_t name_size;
    uint32_t manufacturer_size;
    uint32_t stable_id_size;
  };

  struct LvhWindowsCreateGamepadRequest {
    uint32_t version;
    uint32_t size;
    uint64_t client_device_id;
    uint32_t bus_type;
    uint32_t gamepad_kind;
    uint32_t flags;
    LvhWindowsGamepadHardwareIds hardware_ids;
    LvhWindowsGamepadReportSizes report_sizes;
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
