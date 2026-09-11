/**
 * @file src/platform/windows/shared/xbox_360_protocol.hpp
 * @brief Private and XUSB wire protocol for the Windows Xbox 360 personality.
 *
 * @note The XUSB device-interface GUID and IOCTL layouts are compatibility
 *       wire formats used by the inbox XInput stack. Microsoft does not
 *       publish them as a supported third-party driver contract.
 */
#pragma once

#include "lvh_windows_protocol.h"

#include <array>
#include <cstdint>

#if defined(_WIN32)
  #include <guiddef.h>
#endif

inline constexpr std::uint32_t LVH_WINDOWS_XBOX360_PROTOCOL_VERSION = 1U;
inline constexpr wchar_t LVH_WINDOWS_XBOX360_ENUMERATOR[] = L"LibVirtualHid";
inline constexpr wchar_t LVH_WINDOWS_XBOX360_HARDWARE_ID[] = L"SWD\\LIBVIRTUALHID_XBOX360";

#if defined(_WIN32)
inline constexpr GUID LVH_WINDOWS_XUSB_INTERFACE_GUID {
  0xEC87F1E3,
  0xC13B,
  0x4100,
  {0xB5, 0xF7, 0x8B, 0x84, 0xD5, 0x42, 0x60, 0xCB}
};
#endif

// XUSB wire operations consumed by xinput1_*.dll and Windows.Gaming.Input.
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_GET_INFORMATION = 0x80006000U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_GET_CAPABILITIES = 0x8000E004U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_GET_LED_STATE = 0x8000E008U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_GET_STATE = 0x8000E00CU;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_SET_STATE = 0x8000A010U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_WAIT_GUIDE = 0x8000E014U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_GET_BATTERY_INFO = 0x8000E018U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_POWER_DOWN = 0x8000A01CU;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_GET_AUDIO_INFO = 0x8000E020U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_POWER_INFO = 0x80006380U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_WAIT_FOR_SYSTEM_BUTTONS = 0x8000E384U;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_WAIT_FOR_INPUT = 0x8000E3ACU;
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XUSB_GET_INFORMATION_EX = 0x8000E3FCU;

inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XBOX360_INITIALIZE = lvh_windows_ctl_code(
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID,
  0x900U,
  LVH_WINDOWS_METHOD_BUFFERED,
  LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS
);
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XBOX360_SUBMIT_INPUT = lvh_windows_ctl_code(
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID,
  0x901U,
  LVH_WINDOWS_METHOD_BUFFERED,
  LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS
);
inline constexpr std::uint32_t LVH_WINDOWS_IOCTL_XBOX360_READ_OUTPUT = lvh_windows_ctl_code(
  LVH_WINDOWS_FILE_DEVICE_LIBVIRTUALHID,
  0x902U,
  LVH_WINDOWS_METHOD_BUFFERED,
  LVH_WINDOWS_FILE_READ_ACCESS | LVH_WINDOWS_FILE_WRITE_ACCESS
);

inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_DPAD_UP = 0x0001U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_DPAD_DOWN = 0x0002U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_DPAD_LEFT = 0x0004U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_DPAD_RIGHT = 0x0008U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_START = 0x0010U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_BACK = 0x0020U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_LEFT_THUMB = 0x0040U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_RIGHT_THUMB = 0x0080U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_LEFT_SHOULDER = 0x0100U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_RIGHT_SHOULDER = 0x0200U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_GUIDE = 0x0400U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_A = 0x1000U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_B = 0x2000U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_X = 0x4000U;
inline constexpr std::uint16_t LVH_WINDOWS_XINPUT_Y = 0x8000U;

#pragma pack(push, 1)

struct LvhWindowsXbox360InitializeRequest {
  std::uint32_t version;
  std::uint32_t size;
  std::uint64_t driver_device_id;
  LvhWindowsSessionToken session_token;
  LvhWindowsCreateDeviceRequest device;
};

struct LvhWindowsXbox360InputState {
  std::uint16_t buttons;
  std::uint8_t left_trigger;
  std::uint8_t right_trigger;
  std::int16_t left_thumb_x;
  std::int16_t left_thumb_y;
  std::int16_t right_thumb_x;
  std::int16_t right_thumb_y;
};

struct LvhWindowsXbox360SubmitInputRequest {
  std::uint32_t version;
  std::uint32_t size;
  std::uint64_t driver_device_id;
  LvhWindowsSessionToken session_token;
  LvhWindowsXbox360InputState state;
};

struct LvhWindowsXbox360ReadOutputRequest {
  std::uint32_t version;
  std::uint32_t size;
  std::uint64_t driver_device_id;
  LvhWindowsSessionToken session_token;
};

#pragma pack(pop)
