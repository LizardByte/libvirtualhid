/**
 * @file tests/consumers/linux_sdl3_xbox_consumer.cpp
 * @brief SDL3 consumer probe for Linux Xbox UHID gamepads.
 */

// standard includes
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

// platform includes
#include <unistd.h>

// lib includes
#include <libvirtualhid/libvirtualhid.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_hidapi.h>

namespace {

  using namespace std::chrono_literals;
  using SdlGamepad = std::unique_ptr<SDL_Gamepad, void (*)(SDL_Gamepad *)>;

  struct ProfileCase {
    lvh::DeviceProfile profile;
    std::uint16_t product_id;
    std::string_view identity_token;
  };

  struct OutputCapture {
    std::mutex mutex;
    std::atomic_bool ordinary_rumble = false;
    std::atomic_bool trigger_rumble = false;
    std::uint16_t low_frequency = 0;
    std::uint16_t high_frequency = 0;
    std::uint16_t left_trigger = 0;
    std::uint16_t right_trigger = 0;
  };

  struct ButtonCase {
    lvh::GamepadButton logical_button;
    SDL_GamepadButton sdl_button;
  };

  using enum lvh::GamepadButton;
  constexpr std::array button_cases {
    ButtonCase {a, SDL_GAMEPAD_BUTTON_SOUTH},
    ButtonCase {b, SDL_GAMEPAD_BUTTON_EAST},
    ButtonCase {x, SDL_GAMEPAD_BUTTON_WEST},
    ButtonCase {y, SDL_GAMEPAD_BUTTON_NORTH},
    ButtonCase {left_shoulder, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    ButtonCase {right_shoulder, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    ButtonCase {back, SDL_GAMEPAD_BUTTON_BACK},
    ButtonCase {start, SDL_GAMEPAD_BUTTON_START},
    ButtonCase {guide, SDL_GAMEPAD_BUTTON_GUIDE},
    ButtonCase {left_stick, SDL_GAMEPAD_BUTTON_LEFT_STICK},
    ButtonCase {right_stick, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    ButtonCase {dpad_up, SDL_GAMEPAD_BUTTON_DPAD_UP},
    ButtonCase {dpad_down, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    ButtonCase {dpad_left, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    ButtonCase {dpad_right, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
  };

  class SdlSubsystem {
  public:
    SdlSubsystem() {
      SDL_SetLogPriority(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_DEBUG);
      SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
      SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
      SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1");
      SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_XBOX_ONE, "1");
      initialized_ = SDL_Init(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS);
    }

    SdlSubsystem(const SdlSubsystem &) = delete;
    SdlSubsystem &operator=(const SdlSubsystem &) = delete;

    ~SdlSubsystem() {
      if (initialized_) {
        SDL_Quit();
      }
    }

    bool initialized() const {
      return initialized_;
    }

  private:
    bool initialized_ = false;
  };

  int fail(std::string_view message) {
    std::cerr << message;
    if (const auto *error = SDL_GetError(); error != nullptr && *error != '\0') {
      std::cerr << ": " << error;
    }
    std::cerr << '\n';
    return EXIT_FAILURE;
  }

  std::string_view nullable_text(const char *text) {
    return text == nullptr ? "<null>" : text;
  }

  void log_discovery_state() {
    int joystick_count = 0;
    auto *joysticks = SDL_GetJoysticks(&joystick_count);
    std::cerr << "SDL3 joystick count: " << joystick_count << '\n';
    for (int index = 0; index < joystick_count; ++index) {
      const auto id = joysticks[index];
      std::cerr << std::format(
        "  id={} {:04x}:{:04x} gamepad={} name={} path={}\n",
        id,
        SDL_GetJoystickVendorForID(id),
        SDL_GetJoystickProductForID(id),
        SDL_IsGamepad(id),
        nullable_text(SDL_GetJoystickNameForID(id)),
        nullable_text(SDL_GetJoystickPathForID(id))
      );
    }
    SDL_free(joysticks);

    auto *devices = SDL_hid_enumerate(0x045E, 0);
    std::cerr << "SDL3 Microsoft HIDAPI devices:\n";
    for (auto *device = devices; device != nullptr; device = device->next) {
      std::cerr << std::format(
        "  {:04x}:{:04x} bus={} usage={:04x}:{:04x} path={}\n",
        device->vendor_id,
        device->product_id,
        std::to_underlying(device->bus_type),
        device->usage_page,
        device->usage,
        nullable_text(device->path)
      );
    }
    SDL_hid_free_enumeration(devices);
  }

  void log_device_nodes(const lvh::Gamepad &gamepad) {
    std::cerr << "libvirtualhid device nodes:\n";
    for (const auto &node : gamepad.device_nodes()) {
      errno = 0;
      const auto accessible = ::access(node.path.c_str(), R_OK | W_OK) == 0;
      const auto access_error = errno;
      std::cerr << std::format(
        "  kind={} path={} read-write={} errno={}\n",
        std::to_underlying(node.kind),
        node.path,
        accessible,
        access_error
      );
    }
  }

  void log_sysfs_files(
    std::string_view heading,
    const std::filesystem::path &root,
    const std::filesystem::path &relative_path
  ) {
    std::cerr << heading << ":\n";
    std::error_code error;
    for (std::filesystem::directory_iterator it {root, error}, end; !error && it != end; it.increment(error)) {
      const auto path = it->path() / relative_path;
      std::ifstream file {path};
      if (!file) {
        continue;
      }

      std::cerr << "  " << path << ":\n";
      std::string line;
      while (std::getline(file, line)) {
        std::cerr << "    " << line << '\n';
      }
    }
    if (error) {
      std::cerr << "  scan failed: " << error.message() << '\n';
    }
  }

  void log_kernel_hid_state() {
    log_sysfs_files("Kernel HID devices", "/sys/bus/hid/devices", "uevent");
    log_sysfs_files("Kernel hidraw devices", "/sys/class/hidraw", "device/uevent");
    log_sysfs_files("Kernel input device names", "/sys/class/input", "device/name");
  }

  bool wait_for_accessible_hidraw_node(const lvh::Gamepad &gamepad) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
      for (const auto &node : gamepad.device_nodes()) {
        if (
          node.kind == lvh::DeviceNodeKind::hidraw &&
          ::access(node.path.c_str(), R_OK | W_OK) == 0
        ) {
          return true;
        }
      }
      std::this_thread::sleep_for(50ms);
    }
    return false;
  }

  SDL_JoystickID wait_for_gamepad(const ProfileCase &test_case) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
      SDL_UpdateGamepads();
      SDL_PumpEvents();

      int count = 0;
      auto *gamepads = SDL_GetGamepads(&count);
      for (int index = 0; index < count; ++index) {
        const auto id = gamepads[index];
        if (
          SDL_GetGamepadVendorForID(id) == 0x045EU &&
          SDL_GetGamepadProductForID(id) == test_case.product_id
        ) {
          SDL_free(gamepads);
          return id;
        }
      }
      SDL_free(gamepads);
      std::this_thread::sleep_for(50ms);
    }
    return 0;
  }

  bool wait_for_button(SDL_Gamepad *gamepad, SDL_GamepadButton button, bool pressed) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      SDL_UpdateGamepads();
      SDL_PumpEvents();
      if (SDL_GetGamepadButton(gamepad, button) == pressed) {
        return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    return false;
  }

  bool wait_for_axis(
    SDL_Gamepad *gamepad,
    SDL_GamepadAxis axis,
    std::int16_t expected,
    std::int16_t tolerance
  ) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      SDL_UpdateGamepads();
      SDL_PumpEvents();
      if (std::abs(static_cast<int>(SDL_GetGamepadAxis(gamepad, axis)) - expected) <= tolerance) {
        return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    return false;
  }

  bool wait_for_output(const std::atomic_bool &observed) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      SDL_UpdateGamepads();
      SDL_PumpEvents();
      if (observed.load()) {
        return true;
      }
      std::this_thread::sleep_for(20ms);
    }
    return false;
  }

  void capture_output(const std::shared_ptr<OutputCapture> &output, const lvh::GamepadOutput &gamepad_output) {
    std::lock_guard lock {output->mutex};
    if (
      gamepad_output.kind == lvh::GamepadOutputKind::rumble &&
      gamepad_output.low_frequency_rumble > 0U && gamepad_output.high_frequency_rumble > 0U
    ) {
      output->low_frequency = gamepad_output.low_frequency_rumble;
      output->high_frequency = gamepad_output.high_frequency_rumble;
      output->ordinary_rumble = true;
    } else if (
      gamepad_output.kind == lvh::GamepadOutputKind::trigger_rumble &&
      gamepad_output.left_trigger_rumble > 0U && gamepad_output.right_trigger_rumble > 0U
    ) {
      output->left_trigger = gamepad_output.left_trigger_rumble;
      output->right_trigger = gamepad_output.right_trigger_rumble;
      output->trigger_rumble = true;
    }
  }

  int validate_identity(SDL_JoystickID gamepad_id, const ProfileCase &test_case) {
    // Linux hidapi leaves the release number unset for Bluetooth devices.
    // Validate it when the SDL backend exposes the UHID version.
    if (
      const auto product_version = SDL_GetGamepadProductVersionForID(gamepad_id);
      product_version != 0U && product_version != 0x0513U
    ) {
      return fail("SDL3 observed the wrong Xbox Bluetooth product version");
    }
    if (
      const auto *name = SDL_GetGamepadNameForID(gamepad_id);
      name == nullptr || !std::string_view {name}.contains(test_case.identity_token)
    ) {
      return fail(std::format("SDL3 exposed the wrong Xbox identity name: {}", name == nullptr ? "<null>" : name));
    }
    return EXIT_SUCCESS;
  }

  int validate_capabilities(SDL_Gamepad *gamepad) {
    if (SDL_GetGamepadType(gamepad) != SDL_GAMEPAD_TYPE_XBOXONE) {
      return fail("SDL3 did not classify the device as an Xbox One-family gamepad");
    }

    const auto properties = SDL_GetGamepadProperties(gamepad);
    if (properties == 0U) {
      return fail("SDL3 did not expose gamepad properties");
    }
    if (!SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false)) {
      return fail("SDL3 did not advertise ordinary rumble");
    }
    if (!SDL_GetBooleanProperty(properties, SDL_PROP_GAMEPAD_CAP_TRIGGER_RUMBLE_BOOLEAN, false)) {
      return fail("SDL3 did not advertise independent trigger rumble");
    }
    return EXIT_SUCCESS;
  }

  int validate_buttons(lvh::Gamepad &virtual_gamepad, SDL_Gamepad *gamepad, const ProfileCase &test_case) {
    for (const auto &[logical_button, sdl_button] : button_cases) {
      lvh::GamepadState state;
      state.buttons.set(logical_button);
      if (!virtual_gamepad.submit(state).ok() || !wait_for_button(gamepad, sdl_button, true)) {
        return fail(std::format("SDL3 did not map logical button {}", std::to_underlying(logical_button)));
      }
      for (const auto &[other_logical_button, other_sdl_button] : button_cases) {
        if (other_logical_button != logical_button && SDL_GetGamepadButton(gamepad, other_sdl_button)) {
          return fail(std::format("SDL3 mapped logical button {} to multiple controls", std::to_underlying(logical_button)));
        }
      }
    }

    if (test_case.profile.gamepad_kind == lvh::GamepadProfileKind::xbox_series) {
      lvh::GamepadState state;
      state.buttons.set(misc1);
      if (!virtual_gamepad.submit(state).ok() || !wait_for_button(gamepad, SDL_GAMEPAD_BUTTON_MISC1, true)) {
        return fail("SDL3 did not map the Xbox Series Share button");
      }
    }
    return EXIT_SUCCESS;
  }

  int validate_trigger_inputs(lvh::Gamepad &virtual_gamepad, SDL_Gamepad *gamepad) {
    constexpr auto trigger_tolerance = std::int16_t {256};
    constexpr auto left_trigger_expected = std::int16_t {SDL_JOYSTICK_AXIS_MAX / 4};
    constexpr auto right_trigger_expected = std::int16_t {(SDL_JOYSTICK_AXIS_MAX * 3) / 4};
    lvh::GamepadState trigger_state;
    trigger_state.left_trigger = 0.25F;
    trigger_state.right_trigger = 0.75F;
    if (!virtual_gamepad.submit(trigger_state).ok()) {
      return fail("Submitting Xbox trigger input failed");
    }
    if (!wait_for_axis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, left_trigger_expected, trigger_tolerance)) {
      return fail(std::format("SDL3 mapped the left trigger to an unexpected value: {}", SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
    }
    if (!wait_for_axis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, right_trigger_expected, trigger_tolerance)) {
      return fail(std::format("SDL3 mapped the right trigger to an unexpected value: {}", SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
    }
    return EXIT_SUCCESS;
  }

  int validate_rumble(SDL_Gamepad *gamepad, const std::shared_ptr<OutputCapture> &output) {
    if (!SDL_RumbleGamepad(gamepad, 0x5678U, 0x1234U, 1000U)) {
      return fail("SDL3 ordinary rumble request failed");
    }
    if (!wait_for_output(output->ordinary_rumble)) {
      return fail("SDL3 ordinary rumble did not reach the normalized output callback");
    }
    if (!SDL_RumbleGamepadTriggers(gamepad, 0x3456U, 0x789AU, 1000U)) {
      return fail("SDL3 trigger-rumble request failed");
    }
    if (!wait_for_output(output->trigger_rumble)) {
      return fail("SDL3 trigger rumble did not reach the normalized output callback");
    }
    return EXIT_SUCCESS;
  }

  int exercise_sdl_profile(
    const ProfileCase &test_case,
    lvh::Gamepad &virtual_gamepad,
    const std::shared_ptr<OutputCapture> &output
  ) {
    const auto gamepad_id = wait_for_gamepad(test_case);
    if (gamepad_id == 0) {
      log_discovery_state();
      return fail(std::format("SDL3 did not discover product 045e:{:04x}", test_case.product_id));
    }
    if (validate_identity(gamepad_id, test_case) != EXIT_SUCCESS) {
      return EXIT_FAILURE;
    }

    SdlGamepad gamepad {SDL_OpenGamepad(gamepad_id), &SDL_CloseGamepad};
    if (gamepad == nullptr) {
      return fail("SDL3 could not open the Xbox gamepad");
    }
    if (validate_capabilities(gamepad.get()) != EXIT_SUCCESS) {
      return EXIT_FAILURE;
    }
    if (validate_buttons(virtual_gamepad, gamepad.get(), test_case) != EXIT_SUCCESS) {
      return EXIT_FAILURE;
    }
    if (validate_trigger_inputs(virtual_gamepad, gamepad.get()) != EXIT_SUCCESS) {
      return EXIT_FAILURE;
    }
    return validate_rumble(gamepad.get(), output);
  }

  int run_sdl_profile(
    const ProfileCase &test_case,
    lvh::Gamepad &virtual_gamepad,
    const std::shared_ptr<OutputCapture> &output
  ) {
    // Initialize SDL only after udev has applied the hidraw access rules. If
    // SDL3 sees the add event before that point, its HIDAPI driver can fail the
    // initial open while still suppressing the duplicate evdev joystick.
    if (SdlSubsystem sdl; sdl.initialized()) {
      return exercise_sdl_profile(test_case, virtual_gamepad, output);
    }
    return fail("SDL3 initialization failed");
  }

  int exercise_profile(const ProfileCase &test_case) {
    lvh::RuntimeOptions runtime_options;
    runtime_options.backend = lvh::BackendKind::platform_default;
    auto runtime = lvh::Runtime::create(runtime_options);
    if (runtime == nullptr || !runtime->capabilities().supports_gamepad) {
      return fail("The Linux backend does not report gamepad support");
    }

    lvh::CreateGamepadOptions options;
    options.profile = test_case.profile;
    options.profile.name = std::format("libvirtualhid SDL3 {} {}", test_case.identity_token, ::getpid());
    options.metadata.stable_id = std::format("libvirtualhid-sdl3-xbox-{:04x}-{}", test_case.product_id, ::getpid());
    auto created = runtime->create_gamepad(options);
    if (!created) {
      return fail(std::format("Gamepad creation failed: {}", created.status.message()));
    }
    if (!created.gamepad->profile().capabilities.supports_trigger_rumble) {
      return fail("Xbox creation did not select the trigger-rumble-capable UHID backend");
    }
    if (!wait_for_accessible_hidraw_node(*created.gamepad)) {
      log_device_nodes(*created.gamepad);
      log_kernel_hid_state();
      return fail("The Xbox UHID device did not expose an accessible hidraw node");
    }

    const auto output = std::make_shared<OutputCapture>();
    created.gamepad->set_output_callback([output](const lvh::GamepadOutput &gamepad_output) {
      capture_output(output, gamepad_output);
    });
    return run_sdl_profile(test_case, *created.gamepad, output);
  }

}  // namespace

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: linux_sdl3_xbox_consumer <xone|xseries>\n";
    return EXIT_FAILURE;
  }

  const std::string_view profile_name {argv[1]};
  if (profile_name == "xone") {
    return exercise_profile({lvh::profiles::xbox_one(), 0x0B20, "Xbox One"});
  }
  if (profile_name == "xseries") {
    return exercise_profile({lvh::profiles::xbox_series(), 0x0B13, "Xbox Series"});
  }

  std::cerr << "Unknown profile: " << profile_name << '\n';
  return EXIT_FAILURE;
}
