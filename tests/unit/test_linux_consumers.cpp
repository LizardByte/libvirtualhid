/**
 * @file tests/unit/test_linux_consumers.cpp
 * @brief Linux integration tests for SDL2 and libinput consumers.
 */

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

// platform includes
#include <cerrno>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

// lib includes
#include <libinput.h>
#include <libvirtualhid/libvirtualhid.hpp>
#include <SDL.h>

// local includes
#include "fixtures/fixtures.hpp"

/**
 * @brief Test fixture for Linux consumer input libraries.
 */
class LinuxConsumerTest: public LinuxTest {};

namespace {

  using LibinputContext = std::unique_ptr<libinput, void (*)(libinput *)>;
  using LibinputEvent = std::unique_ptr<libinput_event, void (*)(libinput_event *)>;
  using SdlGameController = std::unique_ptr<SDL_GameController, void (*)(SDL_GameController *)>;

  /**
   * @brief SDL-visible gamepad case.
   */
  struct SdlGamepadConsumerCase {
    lvh::DeviceProfile profile;
    std::string_view name_suffix;
    std::string_view stable_id;
    std::optional<std::uint16_t> expected_product_id;
    int minimum_buttons = 1;
    int minimum_axes = 2;
    bool expect_live_input = true;
  };

  /**
   * @brief Execute cleanup code when a scope exits.
   */
  class ScopeExit {
  public:
    /**
     * @brief Construct a scope-exit guard.
     *
     * @param function Cleanup function.
     */
    explicit ScopeExit(std::function<void()> function):
        function_ {std::move(function)} {}

    /**
     * @brief Execute the cleanup function.
     */
    ~ScopeExit() noexcept {
      try {
        function_();
      } catch (const std::exception &exception) {
        ADD_FAILURE() << "Scope-exit cleanup failed: " << exception.what();
      } catch (...) {
        ADD_FAILURE() << "Scope-exit cleanup failed with an unknown exception.";
      }
    }

    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ScopeExit(ScopeExit &&) noexcept = delete;
    ScopeExit &operator=(ScopeExit &&) noexcept = delete;

  private:
    std::function<void()> function_;
  };

  std::string unique_device_name(std::string_view suffix) {
    return std::format("libvirtualhid {} {}", suffix, ::getpid());
  }

  std::optional<std::string> read_first_line(const std::filesystem::path &path) {
    std::ifstream file {path};
    if (!file) {
      return std::nullopt;
    }

    std::string line;
    std::getline(file, line);
    return line;
  }

  std::vector<std::filesystem::path> input_event_nodes_named(std::string_view name) {
    std::vector<std::filesystem::path> nodes;

    std::error_code error;
    const std::filesystem::path sysfs_input {"/sys/class/input"};
    if (!std::filesystem::exists(sysfs_input, error)) {
      return nodes;
    }

    for (std::filesystem::directory_iterator it {sysfs_input, error}, end; !error && it != end; it.increment(error)) {
      const auto filename = it->path().filename().string();
      if (!filename.starts_with("event")) {
        continue;
      }

      const auto sysfs_name = read_first_line(it->path() / "device" / "name");
      if (sysfs_name && *sysfs_name == name) {
        nodes.emplace_back(std::filesystem::path {"/dev/input"} / filename);
      }
    }

    return nodes;
  }

  std::optional<std::filesystem::path> wait_for_readable_event_node(std::string_view name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};

    while (std::chrono::steady_clock::now() < deadline) {
      for (const auto &node : input_event_nodes_named(name)) {
        const auto node_string = node.string();
        if (::access(node_string.c_str(), R_OK) == 0) {
          return node;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }

    return std::nullopt;
  }

  bool sdl_joystick_matches_profile(int index, const lvh::DeviceProfile &profile) {
    if (const auto *name = SDL_JoystickNameForIndex(index); name != nullptr && profile.name == name) {
      return true;
    }

    const auto vendor_id = SDL_JoystickGetDeviceVendor(index);
    const auto product_id = SDL_JoystickGetDeviceProduct(index);
    return vendor_id == profile.vendor_id && product_id == profile.product_id;
  }

  void pump_sdl_events() {
    SDL_JoystickUpdate();

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
      std::cout << "SDL event type: " << event.type << '\n';
    }
  }

  void dump_sdl_joysticks() {
    const auto joystick_count = SDL_NumJoysticks();
    std::cout << "SDL joystick count: " << joystick_count << '\n';
    for (int index = 0; index < joystick_count; ++index) {
      const auto *name = SDL_JoystickNameForIndex(index);
      std::cout << "SDL joystick[" << index << "]: " << (name == nullptr ? "<unknown>" : name)
                << " vendor=" << SDL_JoystickGetDeviceVendor(index)
                << " product=" << SDL_JoystickGetDeviceProduct(index) << '\n';
    }
  }

  int wait_for_sdl_joystick(const lvh::DeviceProfile &profile) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};

    while (std::chrono::steady_clock::now() < deadline) {
      pump_sdl_events();

      const auto joystick_count = SDL_NumJoysticks();
      for (int index = 0; index < joystick_count; ++index) {
        if (sdl_joystick_matches_profile(index, profile)) {
          return index;
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }

    dump_sdl_joysticks();
    return -1;
  }

  std::string describe_sdl_state(SDL_Joystick *joystick) {
    std::ostringstream stream;
    stream << "buttons=" << SDL_JoystickNumButtons(joystick) << " axes=" << SDL_JoystickNumAxes(joystick);

    for (int button = 0; button < SDL_JoystickNumButtons(joystick); ++button) {
      stream << " button[" << button << "]=" << static_cast<int>(SDL_JoystickGetButton(joystick, button));
    }

    for (int axis = 0; axis < SDL_JoystickNumAxes(joystick); ++axis) {
      stream << " axis[" << axis << "]=" << SDL_JoystickGetAxis(joystick, axis);
    }

    return stream.str();
  }

  bool sdl_joystick_has_pressed_button(SDL_Joystick *joystick) {
    for (int button = 0; button < SDL_JoystickNumButtons(joystick); ++button) {
      if (SDL_JoystickGetButton(joystick, button) != 0) {
        return true;
      }
    }

    return false;
  }

  bool sdl_joystick_has_moved_axis(SDL_Joystick *joystick) {
    for (int axis = 0; axis < SDL_JoystickNumAxes(joystick); ++axis) {
      if (std::abs(static_cast<int>(SDL_JoystickGetAxis(joystick, axis))) > 8000) {
        return true;
      }
    }

    return false;
  }

  bool sdl_controller_has_pressed_button(SDL_GameController *controller) {
    for (int button = SDL_CONTROLLER_BUTTON_A; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
      if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(button)) != 0) {
        return true;
      }
    }

    return false;
  }

  bool sdl_controller_has_moved_axis(SDL_GameController *controller) {
    for (int axis = SDL_CONTROLLER_AXIS_LEFTX; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
      if (std::abs(static_cast<int>(SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis)))) > 8000) {
        return true;
      }
    }

    return false;
  }

  std::string describe_sdl_controller_state(SDL_GameController *controller) {
    std::ostringstream stream;
    stream << "controller_type=" << SDL_GameControllerGetType(controller);

    for (int button = SDL_CONTROLLER_BUTTON_A; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
      stream << " controller_button[" << button << "]="
             << static_cast<int>(SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(button)));
    }

    for (int axis = SDL_CONTROLLER_AXIS_LEFTX; axis < SDL_CONTROLLER_AXIS_MAX; ++axis) {
      stream << " controller_axis[" << axis << "]="
             << SDL_GameControllerGetAxis(controller, static_cast<SDL_GameControllerAxis>(axis));
    }

    if (auto *joystick = SDL_GameControllerGetJoystick(controller)) {
      stream << " " << describe_sdl_state(joystick);
    }

    return stream.str();
  }

  bool wait_for_sdl_controller_input(SDL_GameController *controller) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    auto *joystick = SDL_GameControllerGetJoystick(controller);

    while (std::chrono::steady_clock::now() < deadline) {
      SDL_GameControllerUpdate();
      pump_sdl_events();

      const auto controller_button_pressed = sdl_controller_has_pressed_button(controller);
      const auto controller_axis_moved = sdl_controller_has_moved_axis(controller);
      const auto joystick_button_pressed = joystick != nullptr && sdl_joystick_has_pressed_button(joystick);

      if (const auto joystick_axis_moved = joystick != nullptr && sdl_joystick_has_moved_axis(joystick); (controller_button_pressed || joystick_button_pressed) && (controller_axis_moved || joystick_axis_moved)) {
        return true;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }

    return false;
  }

  bool wait_for_sdl_controller_button(
    SDL_GameController *controller,
    SDL_GameControllerButton expected_button
  ) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    while (std::chrono::steady_clock::now() < deadline) {
      SDL_GameControllerUpdate();
      pump_sdl_events();
      if (SDL_GameControllerGetButton(controller, expected_button) != 0) {
        return true;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {20});
    }
    return false;
  }

  void configure_sdl_hidapi_hints() {
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5", "1");
  }

  lvh::GamepadCreationResult create_sdl_gamepad(lvh::Runtime &runtime, const SdlGamepadConsumerCase &test_case) {
    lvh::CreateGamepadOptions options;
    options.profile = test_case.profile;
    options.profile.name = unique_device_name(test_case.name_suffix);
    options.metadata.stable_id = std::string {test_case.stable_id};

    return runtime.create_gamepad(options);
  }

  template<class TestBody>
  void run_sdl_gamepad_test(const SdlGamepadConsumerCase &test_case, Uint32 init_flags, TestBody test_body) {
    configure_sdl_hidapi_hints();
    ASSERT_EQ(SDL_Init(init_flags), 0) << SDL_GetError();
    ScopeExit sdl_quit {[]() {
      SDL_Quit();
    }};

    lvh::RuntimeOptions runtime_options;
    runtime_options.backend = lvh::BackendKind::platform_default;
    auto runtime = lvh::Runtime::create(runtime_options);
    ASSERT_TRUE(runtime->capabilities().supports_gamepad);

    const auto expected_profile = [&test_case]() {
      auto profile = test_case.profile;
      profile.name = unique_device_name(test_case.name_suffix);
      if (test_case.expected_product_id) {
        profile.product_id = *test_case.expected_product_id;
      }
      return profile;
    }();

    auto created = create_sdl_gamepad(*runtime, test_case);
    ASSERT_TRUE(created) << created.status.message();

    const auto joystick_index = wait_for_sdl_joystick(expected_profile);
    ASSERT_GE(joystick_index, 0);

    test_body(expected_profile, joystick_index, *created.gamepad);
  }

  void expect_sdl_joystick_profile(SDL_Joystick *joystick, const lvh::DeviceProfile &profile, int minimum_buttons, int minimum_axes) {
    EXPECT_EQ(SDL_JoystickGetVendor(joystick), profile.vendor_id);
    EXPECT_EQ(SDL_JoystickGetProduct(joystick), profile.product_id);
    EXPECT_GE(SDL_JoystickNumButtons(joystick), minimum_buttons);
    EXPECT_GE(SDL_JoystickNumAxes(joystick), minimum_axes);
  }

  void expect_sdl_playstation_controller_profile(SDL_GameController *controller) {
    auto *mapping = SDL_GameControllerMapping(controller);
    EXPECT_NE(mapping, nullptr) << SDL_GetError();
    if (mapping != nullptr) {
      SDL_free(mapping);
    }
  }

  void exercise_sdl_playstation_controller(
    const SdlGamepadConsumerCase &test_case,
    const lvh::DeviceProfile &expected_profile,
    int joystick_index,
    lvh::Gamepad &gamepad
  ) {
    ASSERT_EQ(SDL_IsGameController(joystick_index), SDL_TRUE) << SDL_GetError();

    SdlGameController controller {SDL_GameControllerOpen(joystick_index), &SDL_GameControllerClose};
    ASSERT_NE(controller.get(), nullptr) << SDL_GetError();

    auto *joystick = SDL_GameControllerGetJoystick(controller.get());
    ASSERT_NE(joystick, nullptr) << SDL_GetError();
    expect_sdl_joystick_profile(
      joystick,
      expected_profile,
      test_case.minimum_buttons,
      test_case.minimum_axes
    );

    lvh::GamepadState state;
    state.buttons.set(lvh::GamepadButton::a);
    state.buttons.set(lvh::GamepadButton::b);
    state.buttons.set(lvh::GamepadButton::x);
    state.buttons.set(lvh::GamepadButton::y);
    state.left_stick = {0.75F, -0.5F};
    state.right_stick = {-0.25F, 0.5F};
    state.left_trigger = 0.25F;
    state.right_trigger = 0.75F;
    state.acceleration = lvh::Vector3 {.x = 1.0F, .y = 2.0F, .z = 3.0F};
    state.gyroscope = lvh::Vector3 {.x = 4.0F, .y = 5.0F, .z = 6.0F};
    state.battery = lvh::GamepadBattery {.state = lvh::GamepadBatteryState::charging, .percentage = 80};
    state.touchpad_contacts[0] = {.id = 1, .active = true, .x = 0.5F, .y = 0.25F};
    ASSERT_TRUE(gamepad.submit(state).ok());

    expect_sdl_playstation_controller_profile(controller.get());
    if (test_case.expect_live_input) {
      EXPECT_TRUE(wait_for_sdl_controller_input(controller.get())) << describe_sdl_controller_state(controller.get());
    }
  }

  void run_sdl_playstation_controller_test(const SdlGamepadConsumerCase &test_case) {
    run_sdl_gamepad_test(
      test_case,
      SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS,
      [&test_case](const auto &expected_profile, int joystick_index, lvh::Gamepad &gamepad) {
        exercise_sdl_playstation_controller(test_case, expected_profile, joystick_index, gamepad);
      }
    );
  }

  void exercise_sdl_canonical_gamepad_controller(
    const SdlGamepadConsumerCase &test_case,
    const lvh::DeviceProfile &expected_profile,
    int joystick_index,
    lvh::Gamepad &gamepad
  ) {
    using enum lvh::GamepadButton;

    ASSERT_EQ(SDL_IsGameController(joystick_index), SDL_TRUE) << SDL_GetError();
    SdlGameController controller {SDL_GameControllerOpen(joystick_index), &SDL_GameControllerClose};
    ASSERT_NE(controller.get(), nullptr) << SDL_GetError();

    auto *joystick = SDL_GameControllerGetJoystick(controller.get());
    ASSERT_NE(joystick, nullptr) << SDL_GetError();
    expect_sdl_joystick_profile(joystick, expected_profile, test_case.minimum_buttons, test_case.minimum_axes);

    struct ButtonCase {
      lvh::GamepadButton logical_button;
      SDL_GameControllerButton sdl_button;
    };

    constexpr std::array button_cases {
      ButtonCase {a, SDL_CONTROLLER_BUTTON_A},
      ButtonCase {b, SDL_CONTROLLER_BUTTON_B},
      ButtonCase {x, SDL_CONTROLLER_BUTTON_X},
      ButtonCase {y, SDL_CONTROLLER_BUTTON_Y},
      ButtonCase {left_shoulder, SDL_CONTROLLER_BUTTON_LEFTSHOULDER},
      ButtonCase {right_shoulder, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER},
      ButtonCase {back, SDL_CONTROLLER_BUTTON_BACK},
      ButtonCase {start, SDL_CONTROLLER_BUTTON_START},
      ButtonCase {guide, SDL_CONTROLLER_BUTTON_GUIDE},
      ButtonCase {left_stick, SDL_CONTROLLER_BUTTON_LEFTSTICK},
      ButtonCase {right_stick, SDL_CONTROLLER_BUTTON_RIGHTSTICK},
    };

    for (const auto &[logical_button, sdl_button] : button_cases) {
      lvh::GamepadState state;
      state.buttons.set(logical_button);
      ASSERT_TRUE(gamepad.submit(state).ok());
      ASSERT_TRUE(wait_for_sdl_controller_button(controller.get(), sdl_button))
        << "logical button " << static_cast<int>(std::to_underlying(logical_button)) << " "
        << describe_sdl_controller_state(controller.get());
      for (const auto &[other_logical_button, other_sdl_button] : button_cases) {
        if (other_logical_button != logical_button) {
          EXPECT_EQ(SDL_GameControllerGetButton(controller.get(), other_sdl_button), 0)
            << "logical button " << static_cast<int>(std::to_underlying(logical_button));
        }
      }
    }

    lvh::GamepadState trigger_state;
    trigger_state.left_trigger = 0.25F;
    trigger_state.right_trigger = 0.75F;
    ASSERT_TRUE(gamepad.submit(trigger_state).ok());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    while (
      std::chrono::steady_clock::now() < deadline &&
      SDL_GameControllerGetAxis(controller.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT) < 16000
    ) {
      SDL_GameControllerUpdate();
      pump_sdl_events();
      std::this_thread::sleep_for(std::chrono::milliseconds {20});
    }
    EXPECT_GT(SDL_GameControllerGetAxis(controller.get(), SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0);
    EXPECT_GT(SDL_GameControllerGetAxis(controller.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 16000);
  }

  void run_sdl_canonical_gamepad_test(const SdlGamepadConsumerCase &test_case) {
    run_sdl_gamepad_test(
      test_case,
      SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK | SDL_INIT_EVENTS,
      [&test_case](const auto &expected_profile, int joystick_index, lvh::Gamepad &gamepad) {
        exercise_sdl_canonical_gamepad_controller(test_case, expected_profile, joystick_index, gamepad);
      }
    );
  }

  void destroy_libinput_event(libinput_event *event) {
    if (event != nullptr) {
      libinput_event_destroy(event);
    }
  }

  void unref_libinput(libinput *context) {
    if (context != nullptr) {
      static_cast<void>(libinput_unref(context));
    }
  }

  int open_restricted(const char *path, int flags, void *user_data) {  // NOSONAR(cpp:S5008): libinput_interface is a C callback ABI with void* user data.
    static_cast<void>(user_data);

    const auto fd = ::openat(AT_FDCWD, path, flags);
    return fd < 0 ? -errno : fd;
  }

  void close_restricted(int fd, void *user_data) {  // NOSONAR(cpp:S5008): libinput_interface is a C callback ABI with void* user data.
    static_cast<void>(user_data);
    ::close(fd);
  }

  const libinput_interface test_libinput_interface {
    open_restricted,
    close_restricted,
  };

  LibinputContext create_libinput_context(const std::filesystem::path &node) {
    LibinputContext context {libinput_path_create_context(&test_libinput_interface, nullptr), unref_libinput};
    if (context == nullptr) {
      return context;
    }

    const auto node_string = node.string();
    auto *device = libinput_path_add_device(context.get(), node_string.c_str());
    if (device == nullptr) {
      return LibinputContext {nullptr, unref_libinput};
    }

    return context;
  }

  LibinputEvent wait_for_libinput_event(
    libinput *context,
    std::initializer_list<libinput_event_type> expected_types
  ) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};

    while (std::chrono::steady_clock::now() < deadline) {
      static_cast<void>(libinput_dispatch(context));

      while (auto *raw_event = libinput_get_event(context)) {
        LibinputEvent event {raw_event, destroy_libinput_event};
        const auto event_type = libinput_event_get_type(event.get());
        if (std::ranges::find(expected_types, event_type) != expected_types.end()) {
          return event;
        }

        std::cout << "Ignoring libinput event type: " << event_type << '\n';
      }

      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }

    return LibinputEvent {nullptr, destroy_libinput_event};
  }

}  // namespace

TEST_F(LinuxConsumerTest, SdlSeesGenericCanonicalButtons) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  run_sdl_canonical_gamepad_test({
    .profile = lvh::profiles::generic_gamepad(),
    .name_suffix = "SDL Generic Gamepad",
    .stable_id = "libvirtualhid-sdl-gamepad-test",
    .minimum_buttons = 16,
    .minimum_axes = 6,
  });
}

TEST_F(LinuxConsumerTest, SdlSeesXbox360CanonicalButtons) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  run_sdl_canonical_gamepad_test({
    .profile = lvh::profiles::xbox_360(),
    .name_suffix = "SDL Xbox 360",
    .stable_id = "libvirtualhid-sdl-xbox-360-test",
    .minimum_buttons = 15,
    .minimum_axes = 6,
  });
}

TEST_F(LinuxConsumerTest, SdlSeesXboxOneCanonicalButtons) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  run_sdl_canonical_gamepad_test({
    .profile = lvh::profiles::xbox_one(),
    .name_suffix = "SDL Xbox One",
    .stable_id = "libvirtualhid-sdl-xbox-one-test",
    .minimum_buttons = 15,
    .minimum_axes = 6,
  });
}

TEST_F(LinuxConsumerTest, SdlSeesXboxSeriesCanonicalButtons) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  const SdlGamepadConsumerCase test_case {
    .profile = lvh::profiles::xbox_series(),
    .name_suffix = "SDL Xbox Series",
    .stable_id = "libvirtualhid-sdl-xbox-series-test",
    .expected_product_id = 0x0B13,
    .minimum_buttons = 16,
    .minimum_axes = 6,
  };
  run_sdl_canonical_gamepad_test(test_case);
}

TEST_F(LinuxConsumerTest, SdlSeesDualSenseUsbControllerBehavior) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  run_sdl_playstation_controller_test({
    .profile = lvh::profiles::dualsense_usb(),
    .name_suffix = "SDL DualSense USB",
    .stable_id = "02:00:00:00:00:01",
    .minimum_buttons = 10,
    .minimum_axes = 4,
  });
}

TEST_F(LinuxConsumerTest, SdlSeesDualShock4UsbControllerBehavior) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  run_sdl_playstation_controller_test({
    .profile = lvh::profiles::dualshock4_usb(),
    .name_suffix = "SDL DualShock 4 USB",
    .stable_id = "02:00:00:00:00:03",
    .minimum_buttons = 10,
    .minimum_axes = 4,
  });
}

TEST_F(LinuxConsumerTest, SdlSeesDualShock4BluetoothControllerDiscovery) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  run_sdl_playstation_controller_test({
    .profile = lvh::profiles::dualshock4_bluetooth(),
    .name_suffix = "SDL DualShock 4 Bluetooth",
    .stable_id = "02:00:00:00:00:04",
    .minimum_buttons = 10,
    .minimum_axes = 4,
    .expect_live_input = false,
  });
}

TEST_F(LinuxConsumerTest, SdlSeesDualSenseBluetoothControllerDiscovery) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  run_sdl_playstation_controller_test({
    .profile = lvh::profiles::dualsense_bluetooth(),
    .name_suffix = "SDL DualSense Bluetooth",
    .stable_id = "02:00:00:00:00:02",
    .minimum_buttons = 10,
    .minimum_axes = 4,
    .expect_live_input = false,
  });
}

TEST_F(LinuxConsumerTest, LibinputSeesUinputKeyboardKeys) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_TRUE(runtime->capabilities().supports_keyboard);

  lvh::CreateKeyboardOptions options;
  options.profile = lvh::profiles::keyboard();
  options.profile.name = unique_device_name("libinput Keyboard");
  options.stable_id = "libvirtualhid-libinput-keyboard-test";

  auto created = runtime->create_keyboard(options);
  ASSERT_TRUE(created) << created.status.message();

  const auto node = wait_for_readable_event_node(options.profile.name);
  ASSERT_TRUE(node) << "libinput keyboard event node was not readable for " << options.profile.name;

  auto context = create_libinput_context(*node);
  ASSERT_NE(context.get(), nullptr) << "libinput could not open " << node->string();

  auto event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_DEVICE_ADDED});
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_device(event.get()), nullptr);
  EXPECT_TRUE(libinput_device_has_capability(libinput_event_get_device(event.get()), LIBINPUT_DEVICE_CAP_KEYBOARD));

  ASSERT_TRUE(created.keyboard->press(0x41).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_KEYBOARD_KEY});
  ASSERT_NE(event.get(), nullptr);
  auto *keyboard_event = libinput_event_get_keyboard_event(event.get());
  ASSERT_NE(keyboard_event, nullptr);
  EXPECT_EQ(libinput_event_keyboard_get_key(keyboard_event), KEY_A);
  EXPECT_EQ(libinput_event_keyboard_get_key_state(keyboard_event), LIBINPUT_KEY_STATE_PRESSED);

  ASSERT_TRUE(created.keyboard->release(0x41).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_KEYBOARD_KEY});
  ASSERT_NE(event.get(), nullptr);
  keyboard_event = libinput_event_get_keyboard_event(event.get());
  ASSERT_NE(keyboard_event, nullptr);
  EXPECT_EQ(libinput_event_keyboard_get_key(keyboard_event), KEY_A);
  EXPECT_EQ(libinput_event_keyboard_get_key_state(keyboard_event), LIBINPUT_KEY_STATE_RELEASED);
}

TEST_F(LinuxConsumerTest, LibinputSeesUinputMouseMotionAndButtons) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_TRUE(runtime->capabilities().supports_mouse);

  lvh::CreateMouseOptions options;
  options.profile = lvh::profiles::mouse();
  options.profile.name = unique_device_name("libinput Mouse");
  options.stable_id = "libvirtualhid-libinput-mouse-test";

  auto created = runtime->create_mouse(options);
  ASSERT_TRUE(created) << created.status.message();

  const auto node = wait_for_readable_event_node(options.profile.name);
  ASSERT_TRUE(node) << "libinput mouse event node was not readable for " << options.profile.name;

  auto context = create_libinput_context(*node);
  ASSERT_NE(context.get(), nullptr) << "libinput could not open " << node->string();

  auto event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_DEVICE_ADDED});
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_device(event.get()), nullptr);
  EXPECT_TRUE(libinput_device_has_capability(libinput_event_get_device(event.get()), LIBINPUT_DEVICE_CAP_POINTER));

  ASSERT_TRUE(created.mouse->move_relative(25, -10).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_MOTION});
  ASSERT_NE(event.get(), nullptr);
  auto *pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_DOUBLE_EQ(libinput_event_pointer_get_dx_unaccelerated(pointer_event), 25.0);
  EXPECT_DOUBLE_EQ(libinput_event_pointer_get_dy_unaccelerated(pointer_event), -10.0);

  ASSERT_TRUE(created.mouse->button(lvh::MouseButton::left, true).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_BUTTON});
  ASSERT_NE(event.get(), nullptr);
  pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_EQ(libinput_event_pointer_get_button(pointer_event), BTN_LEFT);
  EXPECT_EQ(libinput_event_pointer_get_button_state(pointer_event), LIBINPUT_BUTTON_STATE_PRESSED);

  ASSERT_TRUE(created.mouse->button(lvh::MouseButton::left, false).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_BUTTON});
  ASSERT_NE(event.get(), nullptr);
  pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_EQ(libinput_event_pointer_get_button(pointer_event), BTN_LEFT);
  EXPECT_EQ(libinput_event_pointer_get_button_state(pointer_event), LIBINPUT_BUTTON_STATE_RELEASED);
}

TEST_F(LinuxConsumerTest, LibinputSeesUinputTouchscreenContacts) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_TRUE(runtime->capabilities().supports_touchscreen);

  lvh::CreateTouchscreenOptions options;
  options.profile = lvh::profiles::touchscreen();
  options.profile.name = unique_device_name("libinput Touchscreen");
  options.stable_id = "libvirtualhid-libinput-touchscreen-test";

  auto created = runtime->create_touchscreen(options);
  ASSERT_TRUE(created) << created.status.message();

  const auto node = wait_for_readable_event_node(options.profile.name);
  ASSERT_TRUE(node) << "libinput touchscreen event node was not readable for " << options.profile.name;

  auto context = create_libinput_context(*node);
  ASSERT_NE(context.get(), nullptr) << "libinput could not open " << node->string();

  auto event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_DEVICE_ADDED});
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_device(event.get()), nullptr);
  EXPECT_TRUE(libinput_device_has_capability(libinput_event_get_device(event.get()), LIBINPUT_DEVICE_CAP_TOUCH));

  const lvh::TouchContact contact {.id = 1, .x = 0.25F, .y = 0.5F, .pressure = 1.0F};
  ASSERT_TRUE(created.touchscreen->place_contact(contact).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_TOUCH_DOWN, LIBINPUT_EVENT_TOUCH_MOTION});
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_touch_event(event.get()), nullptr);

  ASSERT_TRUE(created.touchscreen->release_contact(contact.id).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_TOUCH_UP});
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_touch_event(event.get()), nullptr);
}

TEST_F(LinuxConsumerTest, LibinputSeesUinputTrackpadButton) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_TRUE(runtime->capabilities().supports_trackpad);

  lvh::CreateTrackpadOptions options;
  options.profile = lvh::profiles::trackpad();
  options.profile.name = unique_device_name("libinput Trackpad");
  options.stable_id = "libvirtualhid-libinput-trackpad-test";

  auto created = runtime->create_trackpad(options);
  ASSERT_TRUE(created) << created.status.message();

  const auto node = wait_for_readable_event_node(options.profile.name);
  ASSERT_TRUE(node) << "libinput trackpad event node was not readable for " << options.profile.name;

  auto context = create_libinput_context(*node);
  ASSERT_NE(context.get(), nullptr) << "libinput could not open " << node->string();

  auto event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_DEVICE_ADDED});
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_device(event.get()), nullptr);
  EXPECT_TRUE(libinput_device_has_capability(libinput_event_get_device(event.get()), LIBINPUT_DEVICE_CAP_POINTER));

  const lvh::TouchContact contact {.id = 2, .x = 0.5F, .y = 0.5F, .pressure = 1.0F};
  ASSERT_TRUE(created.trackpad->place_contact(contact).ok());
  ASSERT_TRUE(created.trackpad->button(true).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_BUTTON});
  ASSERT_NE(event.get(), nullptr);
  auto *pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_EQ(libinput_event_pointer_get_button(pointer_event), BTN_LEFT);
  EXPECT_EQ(libinput_event_pointer_get_button_state(pointer_event), LIBINPUT_BUTTON_STATE_PRESSED);

  ASSERT_TRUE(created.trackpad->button(false).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_BUTTON});
  ASSERT_NE(event.get(), nullptr);
  pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_EQ(libinput_event_pointer_get_button(pointer_event), BTN_LEFT);
  EXPECT_EQ(libinput_event_pointer_get_button_state(pointer_event), LIBINPUT_BUTTON_STATE_RELEASED);
}

TEST_F(LinuxConsumerTest, LibinputSeesUinputPenTabletTool) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uinput"));

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_TRUE(runtime->capabilities().supports_pen_tablet);

  lvh::CreatePenTabletOptions options;
  options.profile = lvh::profiles::pen_tablet();
  options.profile.name = unique_device_name("libinput Pen Tablet");
  options.stable_id = "libvirtualhid-libinput-pen-tablet-test";

  auto created = runtime->create_pen_tablet(options);
  ASSERT_TRUE(created) << created.status.message();

  const auto node = wait_for_readable_event_node(options.profile.name);
  ASSERT_TRUE(node) << "libinput pen tablet event node was not readable for " << options.profile.name;

  auto context = create_libinput_context(*node);
  ASSERT_NE(context.get(), nullptr) << "libinput could not open " << node->string();

  auto event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_DEVICE_ADDED});
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_device(event.get()), nullptr);
  EXPECT_TRUE(libinput_device_has_capability(libinput_event_get_device(event.get()), LIBINPUT_DEVICE_CAP_TABLET_TOOL));

  const lvh::PenToolState tool {
    .tool = lvh::PenToolType::pen,
    .x = 0.25F,
    .y = 0.75F,
    .pressure = 0.5F,
    .distance = 0.0F,
    .tilt_x = 15.0F,
    .tilt_y = -15.0F,
  };
  ASSERT_TRUE(created.pen_tablet->place_tool(tool).ok());
  event = wait_for_libinput_event(
    context.get(),
    {LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY, LIBINPUT_EVENT_TABLET_TOOL_AXIS, LIBINPUT_EVENT_TABLET_TOOL_TIP}
  );
  ASSERT_NE(event.get(), nullptr);
  ASSERT_NE(libinput_event_get_tablet_tool_event(event.get()), nullptr);
}
