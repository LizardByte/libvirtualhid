/**
 * @file src/core/profiles.cpp
 * @brief Built-in virtual gamepad profile definitions.
 */

// standard includes
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// local includes
#include <libvirtualhid/profiles.hpp>

namespace lvh::profiles {
  namespace {

    constexpr std::size_t common_report_size = 14;

    constexpr std::size_t common_output_report_size = 5;

    constexpr std::size_t dualsense_usb_input_report_size = 64;

    constexpr std::size_t dualsense_usb_output_report_size = 48;

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
        0x05,
        0x09,  // Usage Page (Button)
        0x19,
        0x01,  // Usage Minimum (Button 1)
        0x29,
        0x0C,  // Usage Maximum (Button 12)
        0x15,
        0x00,  // Logical Minimum (0)
        0x25,
        0x01,  // Logical Maximum (1)
        0x75,
        0x01,  // Report Size (1)
        0x95,
        0x0C,  // Report Count (12)
        0x81,
        0x02,  // Input (Data,Var,Abs)
        0x75,
        0x01,  // Report Size (1)
        0x95,
        0x04,  // Report Count (4)
        0x81,
        0x03,  // Input (Const,Var,Abs)
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
        0x14,  // Unit (Degrees)
        0x75,
        0x04,  // Report Size (4)
        0x95,
        0x01,  // Report Count (1)
        0x81,
        0x42,  // Input (Data,Var,Abs,Null)
        0x75,
        0x04,  // Report Size (4)
        0x95,
        0x01,  // Report Count (1)
        0x81,
        0x03,  // Input (Const,Var,Abs)
        0x16,
        0x00,
        0x80,  // Logical Minimum (-32768)
        0x26,
        0xFF,
        0x7F,  // Logical Maximum (32767)
        0x75,
        0x10,  // Report Size (16)
        0x95,
        0x04,  // Report Count (4)
        0x09,
        0x30,  // Usage (X)
        0x09,
        0x31,  // Usage (Y)
        0x09,
        0x33,  // Usage (Rx)
        0x09,
        0x34,  // Usage (Ry)
        0x81,
        0x02,  // Input (Data,Var,Abs)
        0x15,
        0x00,  // Logical Minimum (0)
        0x26,
        0xFF,
        0x00,  // Logical Maximum (255)
        0x75,
        0x08,  // Report Size (8)
        0x95,
        0x02,  // Report Count (2)
        0x09,
        0x32,  // Usage (Z)
        0x09,
        0x35,  // Usage (Rz)
        0x81,
        0x02,  // Input (Data,Var,Abs)
      };

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

    DeviceProfile make_gamepad_profile(
      GamepadProfileKind kind,
      std::string name,
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
      profile.manufacturer = "LizardByte";
      profile.capabilities = capabilities;
      profile.report_descriptor = make_gamepad_report_descriptor(profile.report_id, profile.capabilities.supports_rumble);
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
      profile.report_id = 1;
      profile.input_report_size = dualsense_usb_input_report_size;
      profile.output_report_size = dualsense_usb_output_report_size;
      profile.name = "DualSense Wireless Controller";
      profile.manufacturer = "Sony Interactive Entertainment";
      profile.capabilities = {
        .supports_rumble = true,
        .supports_motion = true,
        .supports_touchpad = true,
        .supports_rgb_led = true,
        .supports_battery = true,
        .supports_adaptive_triggers = true,
      };
      profile.report_descriptor = make_dualsense_usb_report_descriptor();
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
    return make_gamepad_profile(
      GamepadProfileKind::generic,
      "libvirtualhid Generic Gamepad",
      0x1209,
      0x0001,
      0x0001,
      {}
    );
  }

  DeviceProfile xbox_360() {
    return make_gamepad_profile(
      GamepadProfileKind::xbox_360,
      "Microsoft X-Box 360 pad",
      0x045E,
      0x028E,
      0x0114,
      {.supports_rumble = true}
    );
  }

  DeviceProfile xbox_one() {
    return make_gamepad_profile(
      GamepadProfileKind::xbox_one,
      "Xbox One Controller",
      0x045E,
      0x02EA,
      0x0408,
      {.supports_rumble = true}
    );
  }

  DeviceProfile xbox_series() {
    return make_gamepad_profile(
      GamepadProfileKind::xbox_series,
      "Xbox Wireless Controller",
      0x045E,
      0x0B12,
      0x0500,
      {.supports_rumble = true, .supports_battery = true}
    );
  }

  DeviceProfile dualsense() {
    return dualsense_usb();
  }

  DeviceProfile dualsense_usb() {
    return make_dualsense_profile(BusType::usb);
  }

  DeviceProfile dualsense_bluetooth() {
    auto profile = make_dualsense_profile(BusType::bluetooth);
    profile.name = "DualSense Wireless Controller";
    return profile;
  }

  DeviceProfile switch_pro() {
    return make_gamepad_profile(
      GamepadProfileKind::switch_pro,
      "Nintendo Switch Pro Controller",
      0x057E,
      0x2009,
      0x8111,
      {.supports_rumble = true, .supports_motion = true, .supports_battery = true}
    );
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
      dualsense(),
      switch_pro(),
    };
  }

}  // namespace lvh::profiles
