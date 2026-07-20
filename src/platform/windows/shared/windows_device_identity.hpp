/**
 * @file src/platform/windows/shared/windows_device_identity.hpp
 * @brief Windows virtual gamepad hardware-ID helpers shared by the driver and tests.
 */
#pragma once

// standard includes
#include <cstdint>
#include <string>

// local includes
#include "lvh_windows_protocol.h"

namespace lvh::detail::windows {

  constexpr wchar_t hardware_id_hex_digit(std::uint16_t value) {
    constexpr auto digits = L"0123456789ABCDEF";
    return digits[value & 0x0FU];
  }

  inline void append_hardware_id_hex4(std::wstring &text, std::uint16_t value) {
    text.push_back(hardware_id_hex_digit(value >> 12U));
    text.push_back(hardware_id_hex_digit(value >> 8U));
    text.push_back(hardware_id_hex_digit(value >> 4U));
    text.push_back(hardware_id_hex_digit(value));
  }

  inline void append_hid_vid_pid(std::wstring &hardware_ids, std::uint16_t vendor_id, std::uint16_t product_id) {
    hardware_ids.append(L"HID\\VID_");
    append_hardware_id_hex4(hardware_ids, vendor_id);
    hardware_ids.append(L"&PID_");
    append_hardware_id_hex4(hardware_ids, product_id);
  }

  inline void append_xinputhid_match_id(
    std::wstring &hardware_ids,
    std::uint16_t vendor_id,
    std::uint16_t product_id
  ) {
    append_hid_vid_pid(hardware_ids, vendor_id, product_id);
    hardware_ids.append(L"&IG_00");
    hardware_ids.push_back(L'\0');
  }

  inline void append_gamepad_compatible_ids(std::wstring &hardware_ids, std::uint16_t vendor_id) {
    hardware_ids.append(L"HID\\VID_");
    append_hardware_id_hex4(hardware_ids, vendor_id);
    hardware_ids.append(L"&UP:0001_U:0005");
    hardware_ids.push_back(L'\0');

    hardware_ids.append(L"HID_DEVICE_SYSTEM_GAME");
    hardware_ids.push_back(L'\0');

    hardware_ids.append(L"HID_DEVICE_UP:0001_U:0005");
    hardware_ids.push_back(L'\0');

    hardware_ids.append(L"HID_DEVICE");
    hardware_ids.push_back(L'\0');
  }

  constexpr bool is_xbox_gamepad(std::uint32_t gamepad_kind) {
    return gamepad_kind == LVH_WINDOWS_GAMEPAD_XBOX_360 || gamepad_kind == LVH_WINDOWS_GAMEPAD_XBOX_ONE ||
           gamepad_kind == LVH_WINDOWS_GAMEPAD_XBOX_SERIES;
  }

  constexpr bool uses_xinputhid_match_id(std::uint32_t gamepad_kind) {
    return is_xbox_gamepad(gamepad_kind);
  }

  inline std::wstring make_hardware_ids(const LvhWindowsCreateGamepadRequest &request) {
    const auto &ids = request.hardware_ids;
    std::wstring hardware_ids;
    if (uses_xinputhid_match_id(request.gamepad_kind)) {
      append_xinputhid_match_id(hardware_ids, ids.vendor_id, ids.product_id);
      if (request.gamepad_kind == LVH_WINDOWS_GAMEPAD_XBOX_SERIES) {
        append_xinputhid_match_id(hardware_ids, ids.vendor_id, 0x02FFU);
      }
    }

    append_hid_vid_pid(hardware_ids, ids.vendor_id, ids.product_id);
    hardware_ids.append(L"&REV_");
    append_hardware_id_hex4(hardware_ids, ids.device_version);
    hardware_ids.push_back(L'\0');

    append_hid_vid_pid(hardware_ids, ids.vendor_id, ids.product_id);
    hardware_ids.push_back(L'\0');

    if (request.gamepad_kind == LVH_WINDOWS_GAMEPAD_XBOX_SERIES) {
      append_gamepad_compatible_ids(hardware_ids, ids.vendor_id);
    }

    hardware_ids.push_back(L'\0');
    return hardware_ids;
  }

}  // namespace lvh::detail::windows
