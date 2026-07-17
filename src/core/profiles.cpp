/**
 * @file src/core/profiles.cpp
 * @brief Built-in virtual gamepad profile definitions.
 */

// standard includes
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// local includes
#include <libvirtualhid/profiles.hpp>

namespace lvh::profiles {
  namespace {

    constexpr std::uint8_t common_button_count = 12;

    constexpr std::uint8_t common_axis_count = 6;

    constexpr std::size_t common_button_bytes = 2;

    constexpr std::size_t common_report_size = 1 + common_button_bytes + common_axis_count;

    constexpr std::size_t common_output_report_size = 5;

    constexpr std::size_t xbox_gip_input_report_size = 17;

    constexpr std::uint8_t switch_pro_report_id = 0x30;

    constexpr std::size_t switch_pro_input_report_size = 64;

    constexpr std::size_t switch_pro_output_report_size = 64;

    constexpr std::size_t dualshock4_usb_input_report_size = 64;

    constexpr std::size_t dualshock4_usb_output_report_size = 32;

    constexpr std::size_t dualshock4_bluetooth_input_report_size = 78;

    constexpr std::size_t dualshock4_bluetooth_output_report_size = 78;

    constexpr std::size_t dualsense_usb_input_report_size = 64;

    constexpr std::size_t dualsense_usb_output_report_size = 48;

    constexpr std::size_t dualsense_bluetooth_input_report_size = 78;

    constexpr std::size_t dualsense_bluetooth_output_report_size = 78;

    std::byte hex_nibble(char digit) {
      if (digit >= '0' && digit <= '9') {
        return static_cast<std::byte>(digit - '0');
      }
      if (digit >= 'A' && digit <= 'F') {
        return static_cast<std::byte>(digit - 'A' + 10);
      }
      if (digit >= 'a' && digit <= 'f') {
        return static_cast<std::byte>(digit - 'a' + 10);
      }
      return std::byte {0};
    }

    std::byte byte_from_hex(char high, char low) {
      const auto high_nibble = hex_nibble(high);
      const auto low_nibble = hex_nibble(low);
      return (high_nibble << 4U) | low_nibble;
    }

    std::vector<std::uint8_t> bytes_from_hex(std::string_view hex) {
      std::vector<std::byte> parsed_bytes;
      parsed_bytes.reserve(hex.size() / 2U);
      for (std::size_t index = 0; index + 1U < hex.size(); index += 2U) {
        parsed_bytes.push_back(byte_from_hex(hex[index], hex[index + 1U]));
      }

      std::vector<std::uint8_t> descriptor;
      descriptor.reserve(parsed_bytes.size());
      for (const auto byte : parsed_bytes) {
        descriptor.push_back(std::to_integer<std::uint8_t>(byte));
      }
      return descriptor;
    }

    void append_common_gamepad_buttons(std::vector<std::uint8_t> &descriptor, bool include_misc_button) {
      descriptor.insert(
        descriptor.end(),
        {
          0x05,
          0x09,  // Usage Page (Button)
          0x09,
          0x01,  // Usage (Button 1 / BTN_SOUTH)
          0x09,
          0x02,  // Usage (Button 2 / BTN_EAST)
          0x09,
          0x04,  // Usage (Button 4 / BTN_NORTH)
          0x09,
          0x05,  // Usage (Button 5 / BTN_WEST)
          0x09,
          0x07,  // Usage (Button 7 / BTN_TL)
          0x09,
          0x08,  // Usage (Button 8 / BTN_TR)
          0x09,
          0x0B,  // Usage (Button 11 / BTN_SELECT)
          0x09,
          0x0C,  // Usage (Button 12 / BTN_START)
          0x09,
          0x0E,  // Usage (Button 14 / BTN_THUMBL)
          0x09,
          0x0F,  // Usage (Button 15 / BTN_THUMBR)
          0x09,
          0x0D,  // Usage (Button 13 / BTN_MODE)
        }
      );
      if (include_misc_button) {
        descriptor.insert(
          descriptor.end(),
          {
            0x09,
            0x06,  // Usage (Button 6 / BTN_Z)
          }
        );
      }
      descriptor.insert(
        descriptor.end(),
        {
          0x15,
          0x00,  // Logical Minimum (0)
          0x25,
          0x01,  // Logical Maximum (1)
          0x75,
          0x01,  // Report Size (1)
          0x95,
          static_cast<std::uint8_t>(include_misc_button ? common_button_count : common_button_count - 1U),
          0x81,
          0x02,  // Input (Data,Var,Abs)
        }
      );
      if (!include_misc_button) {
        descriptor.insert(
          descriptor.end(),
          {
            0x75,
            0x01,  // Report Size (1)
            0x95,
            0x01,  // Report Count (1)
            0x81,
            0x03,  // Input (Const,Var,Abs)
          }
        );
      }
    }

    std::vector<std::uint8_t> make_xbox_gip_report_descriptor(bool include_share_button) {
      constexpr std::string_view xbox_one_descriptor =
        "05010905a101a10009300931150027ffff0000950275108102c0a10009330934150027ffff0000950275108102c0"
        "05010932150026ff039501750a81021500250075069501810305010935150026ff039501750a8102150025007506"
        "950181030509090109020904090509070908090b090c090e090f150025017501950a81021500250075069501810305010939150125083500463b0166140075049501"
        "814275049501150025003500450065008103a102050f099715002501750495019102150025009103097015002564"
        "7508950491020950660110550e26ff009501910209a7910265005500097c9102c005010980a100098515002501"
        "95017501810215002500750795018103c005060920150026ff00750895018102c0";
      constexpr std::string_view xbox_series_descriptor =
        "05010905a101a10009300931150027ffff0000950275108102c0a10009330934150027ffff0000950275108102c0"
        "05010932150026ff039501750a81021500250075069501810305010935150026ff039501750a8102150025007506"
        "950181030509090109020904090509070908090b090c090e090f150025017501950a8102150025007501950181030906150025017501950181021500250075049501810305010939150125083500463b0166140075049501"
        "814275049501150025003500450065008103a102050f099715002501750495019102150025009103097015002564"
        "7508950491020950660110550e26ff009501910209a7910265005500097c9102c005010980a100098515002501"
        "95017501810215002500750795018103c005060920150026ff00750895018102c0";

      return bytes_from_hex(include_share_button ? xbox_series_descriptor : xbox_one_descriptor);
    }

    std::vector<std::uint8_t> make_switch_pro_report_descriptor() {
      constexpr std::string_view descriptor =
        "050115000904a1018530050105090902090109040905090709080909090a090b090c150025017501950a5500650081020509090e090f090d0906150025017501"
        "950481027501950281030b01000100a1000b300001000b310001000b320001000b35000100150027ffff0000"
        "751095048102c00b39000100150025073500463b016514750495018102750495018103"
        "7508953481030600ff852109017508953f8103858109027508953f8103850109037508953f9183851009047508"
        "953f9183858009057508953f9183858209067508953f9183c0";

      return bytes_from_hex(descriptor);
    }

    std::vector<std::uint8_t> make_gamepad_report_descriptor(std::uint8_t report_id, bool supports_rumble) {
      std::vector<std::uint8_t> descriptor {
        0x05,
        0x01,  // Usage Page (Generic Desktop)
        0x09,
        0x05,  // Usage (Game Pad)
        0xA1,
        0x01,  // Collection (Application)
        0x85,
        report_id,  // Report ID
      };
      append_common_gamepad_buttons(descriptor, false);
      descriptor.insert(
        descriptor.end(),
        {
          0x05,
          0x01,  // Usage Page (Generic Desktop)
          0x09,
          0x39,  // Usage (Hat switch)
          0x15,
          0x00,  // Logical Minimum (0)
          0x25,
          0x07,  // Logical Maximum (7)
          0x35,
          0x00,  // Physical Minimum (0)
          0x46,
          0x3B,
          0x01,  // Physical Maximum (315)
          0x65,
          0x14,  // Unit (Eng Rot:Angular Pos)
          0x75,
          0x04,  // Report Size (4)
          0x95,
          0x01,  // Report Count (1)
          0x81,
          0x42,  // Input (Data,Var,Abs,Null)
          0x65,
          0x00,  // Unit (None)
          0x05,
          0x01,  // Usage Page (Generic Desktop)
          0x15,
          0x00,  // Logical Minimum (0)
          0x26,
          0xFF,
          0x00,  // Logical Maximum (255)
          0x75,
          0x08,  // Report Size (8)
          0x95,
          common_axis_count,  // Report Count
          0x09,
          0x30,  // Usage (X)
          0x09,
          0x31,  // Usage (Y)
          0x09,
          0x32,  // Usage (Z)
          0x09,
          0x33,  // Usage (Rx)
          0x09,
          0x34,  // Usage (Ry)
          0x09,
          0x35,  // Usage (Rz)
          0x81,
          0x02,  // Input (Data,Var,Abs)
        }
      );

      if (supports_rumble) {
        descriptor.insert(
          descriptor.end(),
          {
            0x06,
            0x00,
            0xFF,  // Usage Page (Vendor Defined)
            0x09,
            0x01,  // Usage (Vendor Usage 1)
            0x15,
            0x00,  // Logical Minimum (0)
            0x26,
            0xFF,
            0x00,  // Logical Maximum (255)
            0x75,
            0x08,  // Report Size (8)
            0x95,
            0x04,  // Report Count (4)
            0x91,
            0x02,  // Output (Data,Var,Abs)
          }
        );
      }

      descriptor.push_back(0xC0);  // End Collection
      return descriptor;
    }

    std::vector<std::uint8_t> make_standard_gamepad_report_descriptor(
      std::uint8_t report_id,
      bool supports_rumble
    ) {
      std::vector<std::uint8_t> descriptor {
        0x05,
        0x01,  // Usage Page (Generic Desktop)
        0x09,
        0x05,  // Usage (Game Pad)
        0xA1,
        0x01,  // Collection (Application)
        0x85,
        report_id,  // Report ID
      };
      append_common_gamepad_buttons(descriptor, true);
      descriptor.insert(
        descriptor.end(),
        {
          0x05,
          0x01,  // Usage Page (Generic Desktop)
          0x09,
          0x39,  // Usage (Hat switch)
          0x15,
          0x00,  // Logical Minimum (0)
          0x25,
          0x07,  // Logical Maximum (7)
          0x35,
          0x00,  // Physical Minimum (0)
          0x46,
          0x3B,
          0x01,  // Physical Maximum (315)
          0x65,
          0x14,  // Unit (Eng Rot:Angular Pos)
          0x75,
          0x04,  // Report Size (4)
          0x95,
          0x01,  // Report Count (1)
          0x81,
          0x42,  // Input (Data,Var,Abs,Null)
          0x65,
          0x00,  // Unit (None)
          0x05,
          0x01,  // Usage Page (Generic Desktop)
          0x15,
          0x00,  // Logical Minimum (0)
          0x26,
          0xFF,
          0x00,  // Logical Maximum (255)
          0x75,
          0x08,  // Report Size (8)
          0x95,
          common_axis_count,  // Report Count
          0x09,
          0x30,  // Usage (X)
          0x09,
          0x31,  // Usage (Y)
          0x09,
          0x33,  // Usage (Rx)
          0x09,
          0x34,  // Usage (Ry)
          0x09,
          0x32,  // Usage (Z)
          0x09,
          0x35,  // Usage (Rz)
          0x81,
          0x02,  // Input (Data,Var,Abs)
        }
      );

      if (supports_rumble) {
        descriptor.insert(
          descriptor.end(),
          {
            0x06,
            0x00,
            0xFF,  // Usage Page (Vendor Defined)
            0x09,
            0x01,  // Usage (Vendor Usage 1)
            0x15,
            0x00,  // Logical Minimum (0)
            0x26,
            0xFF,
            0x00,  // Logical Maximum (255)
            0x75,
            0x08,  // Report Size (8)
            0x95,
            0x04,  // Report Count (4)
            0x91,
            0x02,  // Output (Data,Var,Abs)
          }
        );
      }

      descriptor.push_back(0xC0);  // End Collection
      return descriptor;
    }

    std::vector<std::uint8_t> make_playstation_common_gamepad_descriptor_prefix(std::uint8_t report_id) {
      return {
        // Usage Page (Generic Desktop)
        0x05,
        0x01,
        // Usage (Game Pad)
        0x09,
        0x05,
        // Collection (Application)
        0xA1,
        0x01,
        // Report ID
        0x85,
        report_id,
        // Usage (X)
        0x09,
        0x30,
        // Usage (Y)
        0x09,
        0x31,
        // Usage (Z)
        0x09,
        0x32,
        // Usage (Rz)
        0x09,
        0x35,
        // Logical Minimum (0)
        0x15,
        0x00,
        // Logical Maximum (255)
        0x26,
        0xFF,
        0x00,
        // Report Size (8)
        0x75,
        0x08,
        // Report Count (4)
        0x95,
        0x04,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
        // Usage (Hat switch)
        0x09,
        0x39,
        // Logical Minimum (0)
        0x15,
        0x00,
        // Logical Maximum (7)
        0x25,
        0x07,
        // Physical Minimum (0)
        0x35,
        0x00,
        // Physical Maximum (315)
        0x46,
        0x3B,
        0x01,
        // Unit (Degrees)
        0x65,
        0x14,
        // Report Size (4)
        0x75,
        0x04,
        // Report Count (1)
        0x95,
        0x01,
        // Input (Data,Var,Abs,Null)
        0x81,
        0x42,
        // Unit (None)
        0x65,
        0x00,
        // Usage Page (Button)
        0x05,
        0x09,
        // Usage Minimum (Button 1)
        0x19,
        0x01,
        // Usage Maximum (Button 14)
        0x29,
        0x0E,
        // Logical Minimum (0)
        0x15,
        0x00,
        // Logical Maximum (1)
        0x25,
        0x01,
        // Report Size (1)
        0x75,
        0x01,
        // Report Count (14)
        0x95,
        0x0E,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
      };
    }

    std::vector<std::uint8_t> make_dualshock4_usb_report_descriptor() {
      // DualShock 4 USB descriptor data is derived from the public descriptor used by WinUHid.
      auto descriptor = make_playstation_common_gamepad_descriptor_prefix(0x01);
      descriptor.insert(
        descriptor.end(),
        {
          // Usage Page (Vendor Defined)
          0x06,
          0x00,
          0xFF,
          // Usage (Vendor Usage 0x20)
          0x09,
          0x20,
          // Report Size (6)
          0x75,
          0x06,
          // Report Count (1)
          0x95,
          0x01,
          // Logical Minimum (0)
          0x15,
          0x00,
          // Logical Maximum (127)
          0x25,
          0x7F,
          // Input (Data,Var,Abs)
          0x81,
          0x02,
          // Usage Page (Generic Desktop)
          0x05,
          0x01,
          // Usage (Rx)
          0x09,
          0x33,
          // Usage (Ry)
          0x09,
          0x34,
          // Logical Minimum (0)
          0x15,
          0x00,
          // Logical Maximum (255)
          0x26,
          0xFF,
          0x00,
          // Report Size (8)
          0x75,
          0x08,
          // Report Count (2)
          0x95,
          0x02,
          // Input (Data,Var,Abs)
          0x81,
          0x02,
          // Usage Page (Vendor Defined)
          0x06,
          0x00,
          0xFF,
          // Usage (Vendor Usage 0x21)
          0x09,
          0x21,
          // Report Count (54)
          0x95,
          0x36,
          // Input (Data,Var,Abs)
          0x81,
          0x02,
          // Report ID (5)
          0x85,
          0x05,
          // Usage (Vendor Usage 0x22)
          0x09,
          0x22,
          // Report Count (31)
          0x95,
          0x1F,
          // Output (Data,Var,Abs)
          0x91,
          0x02,
          // Report ID (4)
          0x85,
          0x04,
          // Usage (Vendor Usage 0x23)
          0x09,
          0x23,
          // Report Count (36)
          0x95,
          0x24,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (2)
          0x85,
          0x02,
          // Usage (Vendor Usage 0x24)
          0x09,
          0x24,
          // Report Count (36)
          0x95,
          0x24,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (8)
          0x85,
          0x08,
          // Usage (Vendor Usage 0x25)
          0x09,
          0x25,
          // Report Count (3)
          0x95,
          0x03,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (16)
          0x85,
          0x10,
          // Usage (Vendor Usage 0x26)
          0x09,
          0x26,
          // Report Count (4)
          0x95,
          0x04,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (17)
          0x85,
          0x11,
          // Usage (Vendor Usage 0x27)
          0x09,
          0x27,
          // Report Count (2)
          0x95,
          0x02,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (18)
          0x85,
          0x12,
          // Usage Page (Vendor Defined 0xFF02)
          0x06,
          0x02,
          0xFF,
          // Usage (Vendor Usage 0x21)
          0x09,
          0x21,
          // Report Count (15)
          0x95,
          0x0F,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (19)
          0x85,
          0x13,
          // Usage (Vendor Usage 0x22)
          0x09,
          0x22,
          // Report Count (22)
          0x95,
          0x16,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (20)
          0x85,
          0x14,
          // Usage Page (Vendor Defined 0xFF05)
          0x06,
          0x05,
          0xFF,
          // Usage (Vendor Usage 0x20)
          0x09,
          0x20,
          // Report Count (16)
          0x95,
          0x10,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (21)
          0x85,
          0x15,
          // Usage (Vendor Usage 0x21)
          0x09,
          0x21,
          // Report Count (44)
          0x95,
          0x2C,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Usage Page (Vendor Defined 0xFF80)
          0x06,
          0x80,
          0xFF,
          // Report ID (128)
          0x85,
          0x80,
          // Usage (Vendor Usage 0x20)
          0x09,
          0x20,
          // Report Count (6)
          0x95,
          0x06,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (129)
          0x85,
          0x81,
          // Usage (Vendor Usage 0x21)
          0x09,
          0x21,
          // Report Count (6)
          0x95,
          0x06,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (130)
          0x85,
          0x82,
          // Usage (Vendor Usage 0x22)
          0x09,
          0x22,
          // Report Count (5)
          0x95,
          0x05,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (131)
          0x85,
          0x83,
          // Usage (Vendor Usage 0x23)
          0x09,
          0x23,
          // Report Count (1)
          0x95,
          0x01,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (132)
          0x85,
          0x84,
          // Usage (Vendor Usage 0x24)
          0x09,
          0x24,
          // Report Count (4)
          0x95,
          0x04,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (133)
          0x85,
          0x85,
          // Usage (Vendor Usage 0x25)
          0x09,
          0x25,
          // Report Count (6)
          0x95,
          0x06,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (134)
          0x85,
          0x86,
          // Usage (Vendor Usage 0x26)
          0x09,
          0x26,
          // Report Count (6)
          0x95,
          0x06,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (135)
          0x85,
          0x87,
          // Usage (Vendor Usage 0x27)
          0x09,
          0x27,
          // Report Count (35)
          0x95,
          0x23,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (136)
          0x85,
          0x88,
          // Usage (Vendor Usage 0x28)
          0x09,
          0x28,
          // Report Count (34)
          0x95,
          0x22,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (137)
          0x85,
          0x89,
          // Usage (Vendor Usage 0x29)
          0x09,
          0x29,
          // Report Count (2)
          0x95,
          0x02,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (144)
          0x85,
          0x90,
          // Usage (Vendor Usage 0x30)
          0x09,
          0x30,
          // Report Count (5)
          0x95,
          0x05,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (145)
          0x85,
          0x91,
          // Usage (Vendor Usage 0x31)
          0x09,
          0x31,
          // Report Count (3)
          0x95,
          0x03,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (146)
          0x85,
          0x92,
          // Usage (Vendor Usage 0x32)
          0x09,
          0x32,
          // Report Count (3)
          0x95,
          0x03,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (147)
          0x85,
          0x93,
          // Usage (Vendor Usage 0x33)
          0x09,
          0x33,
          // Report Count (12)
          0x95,
          0x0C,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (160)
          0x85,
          0xA0,
          // Usage (Vendor Usage 0x40)
          0x09,
          0x40,
          // Report Count (6)
          0x95,
          0x06,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (161)
          0x85,
          0xA1,
          // Usage (Vendor Usage 0x41)
          0x09,
          0x41,
          // Report Count (1)
          0x95,
          0x01,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (162)
          0x85,
          0xA2,
          // Usage (Vendor Usage 0x42)
          0x09,
          0x42,
          // Report Count (1)
          0x95,
          0x01,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (163)
          0x85,
          0xA3,
          // Usage (Vendor Usage 0x43)
          0x09,
          0x43,
          // Report Count (48)
          0x95,
          0x30,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (164)
          0x85,
          0xA4,
          // Usage (Vendor Usage 0x44)
          0x09,
          0x44,
          // Report Count (13)
          0x95,
          0x0D,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (165)
          0x85,
          0xA5,
          // Usage (Vendor Usage 0x45)
          0x09,
          0x45,
          // Report Count (21)
          0x95,
          0x15,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (166)
          0x85,
          0xA6,
          // Usage (Vendor Usage 0x46)
          0x09,
          0x46,
          // Report Count (21)
          0x95,
          0x15,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (240)
          0x85,
          0xF0,
          // Usage (Vendor Usage 0x47)
          0x09,
          0x47,
          // Report Count (63)
          0x95,
          0x3F,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (241)
          0x85,
          0xF1,
          // Usage (Vendor Usage 0x48)
          0x09,
          0x48,
          // Report Count (63)
          0x95,
          0x3F,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (242)
          0x85,
          0xF2,
          // Usage (Vendor Usage 0x49)
          0x09,
          0x49,
          // Report Count (15)
          0x95,
          0x0F,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (167)
          0x85,
          0xA7,
          // Usage (Vendor Usage 0x4A)
          0x09,
          0x4A,
          // Report Count (1)
          0x95,
          0x01,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (168)
          0x85,
          0xA8,
          // Usage (Vendor Usage 0x4B)
          0x09,
          0x4B,
          // Report Count (1)
          0x95,
          0x01,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (169)
          0x85,
          0xA9,
          // Usage (Vendor Usage 0x4C)
          0x09,
          0x4C,
          // Report Count (8)
          0x95,
          0x08,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (170)
          0x85,
          0xAA,
          // Usage (Vendor Usage 0x4E)
          0x09,
          0x4E,
          // Report Count (1)
          0x95,
          0x01,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (171)
          0x85,
          0xAB,
          // Usage (Vendor Usage 0x4F)
          0x09,
          0x4F,
          // Report Count (57)
          0x95,
          0x39,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (172)
          0x85,
          0xAC,
          // Usage (Vendor Usage 0x50)
          0x09,
          0x50,
          // Report Count (57)
          0x95,
          0x39,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (173)
          0x85,
          0xAD,
          // Usage (Vendor Usage 0x51)
          0x09,
          0x51,
          // Report Count (11)
          0x95,
          0x0B,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (174)
          0x85,
          0xAE,
          // Usage (Vendor Usage 0x52)
          0x09,
          0x52,
          // Report Count (1)
          0x95,
          0x01,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (175)
          0x85,
          0xAF,
          // Usage (Vendor Usage 0x53)
          0x09,
          0x53,
          // Report Count (2)
          0x95,
          0x02,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // Report ID (176)
          0x85,
          0xB0,
          // Usage (Vendor Usage 0x54)
          0x09,
          0x54,
          // Report Count (63)
          0x95,
          0x3F,
          // Feature (Data,Var,Abs)
          0xB1,
          0x02,
          // End Collection
          0xC0,
        }
      );

      return descriptor;
    }

    std::vector<std::uint8_t> make_dualshock4_bluetooth_report_descriptor() {
      // DualShock 4 Bluetooth report framing follows the Linux hid-playstation DS4 layout.
      return {
        // Usage Page (Generic Desktop)
        0x05,
        0x01,
        // Usage (Game Pad)
        0x09,
        0x05,
        // Collection (Application)
        0xA1,
        0x01,
        // Report ID (17)
        0x85,
        0x11,
        // Usage Page (Vendor Defined)
        0x06,
        0x00,
        0xFF,
        // Usage (Vendor Usage 0x20)
        0x09,
        0x20,
        // Logical Minimum (0)
        0x15,
        0x00,
        // Logical Maximum (255)
        0x26,
        0xFF,
        0x00,
        // Report Size (8)
        0x75,
        0x08,
        // Report Count (2)
        0x95,
        0x02,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
        // Usage Page (Generic Desktop)
        0x05,
        0x01,
        // Usage (X)
        0x09,
        0x30,
        // Usage (Y)
        0x09,
        0x31,
        // Usage (Z)
        0x09,
        0x32,
        // Usage (Rz)
        0x09,
        0x35,
        // Report Size (8)
        0x75,
        0x08,
        // Report Count (4)
        0x95,
        0x04,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
        // Usage (Hat switch)
        0x09,
        0x39,
        // Logical Minimum (0)
        0x15,
        0x00,
        // Logical Maximum (7)
        0x25,
        0x07,
        // Physical Minimum (0)
        0x35,
        0x00,
        // Physical Maximum (315)
        0x46,
        0x3B,
        0x01,
        // Unit (Degrees)
        0x65,
        0x14,
        // Report Size (4)
        0x75,
        0x04,
        // Report Count (1)
        0x95,
        0x01,
        // Input (Data,Var,Abs,Null)
        0x81,
        0x42,
        // Unit (None)
        0x65,
        0x00,
        // Usage Page (Button)
        0x05,
        0x09,
        // Usage Minimum (Button 1)
        0x19,
        0x01,
        // Usage Maximum (Button 14)
        0x29,
        0x0E,
        // Logical Minimum (0)
        0x15,
        0x00,
        // Logical Maximum (1)
        0x25,
        0x01,
        // Report Size (1)
        0x75,
        0x01,
        // Report Count (14)
        0x95,
        0x0E,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
        // Usage Page (Vendor Defined)
        0x06,
        0x00,
        0xFF,
        // Usage (Vendor Usage 0x21)
        0x09,
        0x21,
        // Report Size (6)
        0x75,
        0x06,
        // Report Count (1)
        0x95,
        0x01,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
        // Usage Page (Generic Desktop)
        0x05,
        0x01,
        // Usage (Rx)
        0x09,
        0x33,
        // Usage (Ry)
        0x09,
        0x34,
        // Report Size (8)
        0x75,
        0x08,
        // Report Count (2)
        0x95,
        0x02,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
        // Usage Page (Vendor Defined)
        0x06,
        0x00,
        0xFF,
        // Usage (Vendor Usage 0x22)
        0x09,
        0x22,
        // Report Count (66)
        0x95,
        0x42,
        // Input (Data,Var,Abs)
        0x81,
        0x02,
        // Report ID (17)
        0x85,
        0x11,
        // Usage (Vendor Usage 0x23)
        0x09,
        0x23,
        // Report Count (77)
        0x95,
        0x4D,
        // Output (Data,Var,Abs)
        0x91,
        0x02,
        // Report ID (5)
        0x85,
        0x05,
        // Usage (Vendor Usage 0x24)
        0x09,
        0x24,
        // Report Count (40)
        0x95,
        0x28,
        // Feature (Data,Var,Abs)
        0xB1,
        0x02,
        // Report ID (18)
        0x85,
        0x12,
        // Usage (Vendor Usage 0x25)
        0x09,
        0x25,
        // Report Count (15)
        0x95,
        0x0F,
        // Feature (Data,Var,Abs)
        0xB1,
        0x02,
        // Report ID (163)
        0x85,
        0xA3,
        // Usage (Vendor Usage 0x26)
        0x09,
        0x26,
        // Report Count (48)
        0x95,
        0x30,
        // Feature (Data,Var,Abs)
        0xB1,
        0x02,
        // End Collection
        0xC0,
      };
    }

    std::vector<std::uint8_t> make_dualsense_usb_report_descriptor() {
      // DualSense USB descriptor data is derived from the public reverse-engineered descriptor used by inputtino.
      return {
        0x05,
        0x01,
        0x09,
        0x05,
        0xA1,
        0x01,
        0x85,
        0x01,
        0x09,
        0x30,
        0x09,
        0x31,
        0x09,
        0x32,
        0x09,
        0x35,
        0x09,
        0x33,
        0x09,
        0x34,
        0x15,
        0x00,
        0x26,
        0xFF,
        0x00,
        0x75,
        0x08,
        0x95,
        0x06,
        0x81,
        0x02,
        0x06,
        0x00,
        0xFF,
        0x09,
        0x20,
        0x95,
        0x01,
        0x81,
        0x02,
        0x05,
        0x01,
        0x09,
        0x39,
        0x15,
        0x00,
        0x25,
        0x07,
        0x35,
        0x00,
        0x46,
        0x3B,
        0x01,
        0x65,
        0x14,
        0x75,
        0x04,
        0x95,
        0x01,
        0x81,
        0x42,
        0x65,
        0x00,
        0x05,
        0x09,
        0x19,
        0x01,
        0x29,
        0x0F,
        0x15,
        0x00,
        0x25,
        0x01,
        0x75,
        0x01,
        0x95,
        0x0F,
        0x81,
        0x02,
        0x06,
        0x00,
        0xFF,
        0x09,
        0x21,
        0x95,
        0x0D,
        0x81,
        0x02,
        0x06,
        0x00,
        0xFF,
        0x09,
        0x22,
        0x15,
        0x00,
        0x26,
        0xFF,
        0x00,
        0x75,
        0x08,
        0x95,
        0x34,
        0x81,
        0x02,
        0x85,
        0x02,
        0x09,
        0x23,
        0x95,
        0x2F,
        0x91,
        0x02,
        0x85,
        0x05,
        0x09,
        0x33,
        0x95,
        0x28,
        0xB1,
        0x02,
        0x85,
        0x08,
        0x09,
        0x34,
        0x95,
        0x2F,
        0xB1,
        0x02,
        0x85,
        0x09,
        0x09,
        0x24,
        0x95,
        0x13,
        0xB1,
        0x02,
        0x85,
        0x0A,
        0x09,
        0x25,
        0x95,
        0x1A,
        0xB1,
        0x02,
        0x85,
        0x20,
        0x09,
        0x26,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0x21,
        0x09,
        0x27,
        0x95,
        0x04,
        0xB1,
        0x02,
        0x85,
        0x22,
        0x09,
        0x40,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0x80,
        0x09,
        0x28,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0x81,
        0x09,
        0x29,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0x82,
        0x09,
        0x2A,
        0x95,
        0x09,
        0xB1,
        0x02,
        0x85,
        0x83,
        0x09,
        0x2B,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0x84,
        0x09,
        0x2C,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0x85,
        0x09,
        0x2D,
        0x95,
        0x02,
        0xB1,
        0x02,
        0x85,
        0xA0,
        0x09,
        0x2E,
        0x95,
        0x01,
        0xB1,
        0x02,
        0x85,
        0xE0,
        0x09,
        0x2F,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0xF0,
        0x09,
        0x30,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0xF1,
        0x09,
        0x31,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0xF2,
        0x09,
        0x32,
        0x95,
        0x0F,
        0xB1,
        0x02,
        0x85,
        0xF4,
        0x09,
        0x35,
        0x95,
        0x3F,
        0xB1,
        0x02,
        0x85,
        0xF5,
        0x09,
        0x36,
        0x95,
        0x03,
        0xB1,
        0x02,
        0xC0,
      };
    }

    std::vector<std::uint8_t> make_dualsense_bluetooth_report_descriptor() {
      // DualSense Bluetooth descriptor data is derived from the public reverse-engineered descriptor used by inputtino.
      auto descriptor = make_playstation_common_gamepad_descriptor_prefix(0x01);
      descriptor.insert(
        descriptor.end(),
        {
          0x75,
          0x06,
          0x95,
          0x01,
          0x81,
          0x01,
          0x05,
          0x01,
          0x09,
          0x33,
          0x09,
          0x34,
          0x15,
          0x00,
          0x26,
          0xFF,
          0x00,
          0x75,
          0x08,
          0x95,
          0x02,
          0x81,
          0x02,
          0x06,
          0x00,
          0xFF,
          0x15,
          0x00,
          0x26,
          0xFF,
          0x00,
          0x75,
          0x08,
          0x95,
          0x4D,
          0x85,
          0x31,
          0x09,
          0x31,
          0x91,
          0x02,
          0x09,
          0x3B,
          0x81,
          0x02,
          0x85,
          0x32,
          0x09,
          0x32,
          0x95,
          0x8D,
          0x91,
          0x02,
          0x85,
          0x33,
          0x09,
          0x33,
          0x95,
          0xCD,
          0x91,
          0x02,
          0x85,
          0x34,
          0x09,
          0x34,
          0x96,
          0x0D,
          0x01,
          0x91,
          0x02,
          0x85,
          0x35,
          0x09,
          0x35,
          0x96,
          0x4D,
          0x01,
          0x91,
          0x02,
          0x85,
          0x36,
          0x09,
          0x36,
          0x96,
          0x8D,
          0x01,
          0x91,
          0x02,
          0x85,
          0x37,
          0x09,
          0x37,
          0x96,
          0xCD,
          0x01,
          0x91,
          0x02,
          0x85,
          0x38,
          0x09,
          0x38,
          0x96,
          0x0D,
          0x02,
          0x91,
          0x02,
          0x85,
          0x39,
          0x09,
          0x39,
          0x96,
          0x22,
          0x02,
          0x91,
          0x02,
          0x06,
          0x80,
          0xFF,
          0x85,
          0x05,
          0x09,
          0x33,
          0x95,
          0x28,
          0xB1,
          0x02,
          0x85,
          0x08,
          0x09,
          0x34,
          0x95,
          0x2F,
          0xB1,
          0x02,
          0x85,
          0x09,
          0x09,
          0x24,
          0x95,
          0x13,
          0xB1,
          0x02,
          0x85,
          0x20,
          0x09,
          0x26,
          0x95,
          0x3F,
          0xB1,
          0x02,
          0x85,
          0x22,
          0x09,
          0x40,
          0x95,
          0x3F,
          0xB1,
          0x02,
          0x85,
          0x80,
          0x09,
          0x28,
          0x95,
          0x3F,
          0xB1,
          0x02,
          0x85,
          0x81,
          0x09,
          0x29,
          0x95,
          0x3F,
          0xB1,
          0x02,
          0x85,
          0x82,
          0x09,
          0x2A,
          0x95,
          0x09,
          0xB1,
          0x02,
          0x85,
          0x83,
          0x09,
          0x2B,
          0x95,
          0x3F,
          0xB1,
          0x02,
          0x85,
          0xF1,
          0x09,
          0x31,
          0x95,
          0x3F,
          0xB1,
          0x02,
          0x85,
          0xF2,
          0x09,
          0x32,
          0x95,
          0x0F,
          0xB1,
          0x02,
          0x85,
          0xF0,
          0x09,
          0x30,
          0x95,
          0x3F,
          0xB1,
          0x02,
          0xC0,
        }
      );

      return descriptor;
    }

    DeviceProfile make_base_gamepad_profile(
      GamepadProfileKind kind,
      std::string name,
      std::string manufacturer,
      std::uint16_t vendor_id,
      std::uint16_t product_id,
      std::uint16_t version,
      GamepadProfileCapabilities capabilities
    ) {
      DeviceProfile profile;
      profile.device_type = DeviceType::gamepad;
      profile.gamepad_kind = kind;
      profile.bus_type = BusType::usb;
      profile.vendor_id = vendor_id;
      profile.product_id = product_id;
      profile.version = version;
      profile.report_id = 1;
      profile.input_report_size = common_report_size;
      if (capabilities.supports_rumble) {
        profile.output_report_size = common_output_report_size;
      }
      profile.name = std::move(name);
      profile.manufacturer = std::move(manufacturer);
      profile.capabilities = capabilities;
      return profile;
    }

    DeviceProfile make_gamepad_profile(
      GamepadProfileKind kind,
      std::string name,
      std::string manufacturer,
      std::uint16_t vendor_id,
      std::uint16_t product_id,
      std::uint16_t version,
      GamepadProfileCapabilities capabilities
    ) {
      auto profile = make_base_gamepad_profile(
        kind,
        std::move(name),
        std::move(manufacturer),
        vendor_id,
        product_id,
        version,
        capabilities
      );
      profile.report_descriptor = make_gamepad_report_descriptor(profile.report_id, profile.capabilities.supports_rumble);
      return profile;
    }

    DeviceProfile make_standard_gamepad_profile(
      GamepadProfileKind kind,
      std::string name,
      std::string manufacturer,
      std::uint16_t vendor_id,
      std::uint16_t product_id,
      std::uint16_t version,
      GamepadProfileCapabilities capabilities
    ) {
      auto profile = make_base_gamepad_profile(
        kind,
        std::move(name),
        std::move(manufacturer),
        vendor_id,
        product_id,
        version,
        capabilities
      );
      profile.report_descriptor =
        make_standard_gamepad_report_descriptor(profile.report_id, profile.capabilities.supports_rumble);
      return profile;
    }

    DeviceProfile make_xbox_gip_profile(
      GamepadProfileKind kind,
      std::string name,
      std::uint16_t product_id,
      std::uint16_t version,
      bool include_share_button
    ) {
      DeviceProfile profile;
      profile.device_type = DeviceType::gamepad;
      profile.gamepad_kind = kind;
      profile.bus_type = BusType::usb;
      profile.vendor_id = 0x045E;
      profile.product_id = product_id;
      profile.version = version;
      profile.report_id = 0;
      profile.input_report_size = xbox_gip_input_report_size;
      profile.output_report_size = common_output_report_size;
      profile.name = std::move(name);
      profile.manufacturer = "Microsoft";
      profile.capabilities = {.supports_rumble = true, .supports_battery = include_share_button};
      profile.report_descriptor = make_xbox_gip_report_descriptor(include_share_button);
      return profile;
    }

    DeviceProfile make_dualshock4_profile(BusType bus_type) {
      DeviceProfile profile;
      profile.device_type = DeviceType::gamepad;
      profile.gamepad_kind = GamepadProfileKind::dualshock4;
      profile.bus_type = bus_type;
      profile.vendor_id = 0x054C;
      profile.product_id = 0x05C4;
      profile.version = 0x0100;
      profile.report_id = bus_type == BusType::bluetooth ? 0x11 : 1;
      profile.input_report_size =
        bus_type == BusType::bluetooth ? dualshock4_bluetooth_input_report_size : dualshock4_usb_input_report_size;
      profile.output_report_size =
        bus_type == BusType::bluetooth ? dualshock4_bluetooth_output_report_size : dualshock4_usb_output_report_size;
      profile.name = "Wireless Controller";
      profile.manufacturer = "Sony Computer Entertainment";
      profile.capabilities = {
        .supports_rumble = true,
        .supports_motion = true,
        .supports_touchpad = true,
        .supports_rgb_led = true,
        .supports_battery = true,
      };
      profile.report_descriptor =
        bus_type == BusType::bluetooth ? make_dualshock4_bluetooth_report_descriptor() : make_dualshock4_usb_report_descriptor();
      return profile;
    }

    DeviceProfile make_dualsense_profile(BusType bus_type) {
      DeviceProfile profile;
      profile.device_type = DeviceType::gamepad;
      profile.gamepad_kind = GamepadProfileKind::dualsense;
      profile.bus_type = bus_type;
      profile.vendor_id = 0x054C;
      profile.product_id = 0x0CE6;
      profile.version = 0x8111;
      profile.report_id = bus_type == BusType::bluetooth ? 0x31 : 1;
      profile.input_report_size =
        bus_type == BusType::bluetooth ? dualsense_bluetooth_input_report_size : dualsense_usb_input_report_size;
      profile.output_report_size =
        bus_type == BusType::bluetooth ? dualsense_bluetooth_output_report_size : dualsense_usb_output_report_size;
      profile.name = "Wireless Controller";
      profile.manufacturer = "Sony Interactive Entertainment";
      profile.capabilities = {
        .supports_rumble = true,
        .supports_motion = true,
        .supports_touchpad = true,
        .supports_rgb_led = true,
        .supports_battery = true,
        .supports_adaptive_triggers = true,
      };
      profile.report_descriptor =
        bus_type == BusType::bluetooth ? make_dualsense_bluetooth_report_descriptor() : make_dualsense_usb_report_descriptor();
      return profile;
    }

    DeviceProfile make_switch_pro_profile() {
      DeviceProfile profile;
      profile.device_type = DeviceType::gamepad;
      profile.gamepad_kind = GamepadProfileKind::switch_pro;
      profile.bus_type = BusType::usb;
      profile.vendor_id = 0x057E;
      profile.product_id = 0x2009;
      profile.version = 0x8111;
      profile.report_id = switch_pro_report_id;
      profile.input_report_size = switch_pro_input_report_size;
      profile.output_report_size = switch_pro_output_report_size;
      profile.name = "Pro Controller";
      profile.manufacturer = "Nintendo Co., Ltd.";
      profile.capabilities = {.supports_rumble = true, .supports_motion = true, .supports_battery = true};
      profile.report_descriptor = make_switch_pro_report_descriptor();
      return profile;
    }

    DeviceProfile make_simple_profile(DeviceType device_type, std::string name, std::uint16_t product_id) {
      DeviceProfile profile;
      profile.device_type = device_type;
      profile.bus_type = BusType::usb;
      profile.vendor_id = 0x1209;
      profile.product_id = product_id;
      profile.version = 0x0001;
      profile.name = std::move(name);
      profile.manufacturer = "LizardByte";
      return profile;
    }

  }  // namespace

  DeviceProfile generic_gamepad() {
    return make_standard_gamepad_profile(
      GamepadProfileKind::generic,
      "libvirtualhid Generic Gamepad",
      "LizardByte",
      0x1209,
      0x0001,
      0x0001,
      {.supports_rumble = true}
    );
  }

  DeviceProfile xbox_360() {
    return make_gamepad_profile(
      GamepadProfileKind::xbox_360,
      "Microsoft X-Box 360 pad",
      "Microsoft",
      0x045E,
      0x028E,
      0x0114,
      {.supports_rumble = true}
    );
  }

  DeviceProfile xbox_one() {
    return make_xbox_gip_profile(
      GamepadProfileKind::xbox_one,
      "Xbox One Controller",
      0x02EA,
      0x0408,
      false
    );
  }

  DeviceProfile xbox_series() {
    return make_xbox_gip_profile(
      GamepadProfileKind::xbox_series,
      "Xbox Controller",
      0x0B12,
      0x0500,
      true
    );
  }

  DeviceProfile dualshock4() {
    return dualshock4_usb();
  }

  DeviceProfile dualshock4_usb() {
    return make_dualshock4_profile(BusType::usb);
  }

  DeviceProfile dualshock4_bluetooth() {
    return make_dualshock4_profile(BusType::bluetooth);
  }

  DeviceProfile dualsense() {
    return dualsense_usb();
  }

  DeviceProfile dualsense_usb() {
    return make_dualsense_profile(BusType::usb);
  }

  DeviceProfile dualsense_bluetooth() {
    auto profile = make_dualsense_profile(BusType::bluetooth);
    profile.name = "Wireless Controller";
    return profile;
  }

  DeviceProfile switch_pro() {
    return make_switch_pro_profile();
  }

  DeviceProfile keyboard() {
    return make_simple_profile(DeviceType::keyboard, "libvirtualhid Keyboard", 0x0002);
  }

  DeviceProfile mouse() {
    return make_simple_profile(DeviceType::mouse, "libvirtualhid Mouse", 0x0003);
  }

  DeviceProfile touchscreen() {
    return make_simple_profile(DeviceType::touchscreen, "libvirtualhid Touchscreen", 0x0004);
  }

  DeviceProfile trackpad() {
    return make_simple_profile(DeviceType::trackpad, "libvirtualhid Trackpad", 0x0005);
  }

  DeviceProfile pen_tablet() {
    return make_simple_profile(DeviceType::pen_tablet, "libvirtualhid Pen Tablet", 0x0006);
  }

  std::optional<DeviceProfile> gamepad_profile(GamepadProfileKind kind) {
    switch (kind) {
      case GamepadProfileKind::generic:
        return generic_gamepad();
      case GamepadProfileKind::xbox_360:
        return xbox_360();
      case GamepadProfileKind::xbox_one:
        return xbox_one();
      case GamepadProfileKind::xbox_series:
        return xbox_series();
      case GamepadProfileKind::dualshock4:
        return dualshock4();
      case GamepadProfileKind::dualsense:
        return dualsense();
      case GamepadProfileKind::switch_pro:
        return switch_pro();
    }

    return std::nullopt;
  }

  std::vector<DeviceProfile> built_in_gamepad_profiles() {
    return {
      generic_gamepad(),
      xbox_360(),
      xbox_one(),
      xbox_series(),
      dualshock4(),
      dualsense(),
      switch_pro(),
    };
  }

}  // namespace lvh::profiles
