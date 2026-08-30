/**
 * @file tests/unit/test_linux_consumers.cpp
 * @brief Linux integration tests for SDL2, SDL3, and libinput consumers.
 */

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
#include <sys/stat.h>
#include <sys/wait.h>
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
  constexpr std::string_view playstation_uhid_name = "Wireless Controller";

  /**
   * @brief SDL-visible gamepad case.
   */
  struct SdlGamepadConsumerCase {
    lvh::DeviceProfile profile;
    std::string_view name_suffix;
    std::string_view stable_id;
    std::optional<std::uint16_t> expected_vendor_id;
    std::optional<std::uint16_t> expected_product_id;
    int minimum_buttons = 1;
    int minimum_axes = 2;
    bool require_sdl_rumble = false;
    bool require_trigger_rumble = false;
    bool require_battery = false;
    bool require_motion = false;
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

  bool is_playstation_profile(lvh::GamepadProfileKind kind) {
    return kind == lvh::GamepadProfileKind::dualshock4 || kind == lvh::GamepadProfileKind::dualsense;
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

  bool sdl_joystick_supports_rumble(int index) {
    if (SDL_IsGameController(index) != SDL_TRUE) {
      return false;
    }

    SdlGameController controller {SDL_GameControllerOpen(index), &SDL_GameControllerClose};
    if (!controller) {
      return false;
    }

    auto *joystick = SDL_GameControllerGetJoystick(controller.get());
    return joystick != nullptr && SDL_JoystickHasRumble(joystick) == SDL_TRUE;
  }

  void pump_sdl_events() {
    SDL_JoystickUpdate();

    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
      std::cout << "SDL event type: " << event.type << '\n';
    }
  }

  struct RumbleState {
    std::atomic_uint16_t low_frequency {0};
    std::atomic_uint16_t high_frequency {0};
    std::atomic_uint16_t left_trigger {0};
    std::atomic_uint16_t right_trigger {0};
    std::atomic_bool ordinary_observed {false};
    std::atomic_bool trigger_observed {false};
  };

  std::shared_ptr<RumbleState> observe_rumble(lvh::Gamepad &gamepad) {
    const auto rumble = std::make_shared<RumbleState>();
    gamepad.set_output_callback([rumble](const lvh::GamepadOutput &output) {
      if (
        output.kind == lvh::GamepadOutputKind::rumble &&
        output.low_frequency_rumble > 0 && output.high_frequency_rumble > 0
      ) {
        rumble->low_frequency = output.low_frequency_rumble;
        rumble->high_frequency = output.high_frequency_rumble;
        rumble->ordinary_observed = true;
      } else if (
        output.kind == lvh::GamepadOutputKind::trigger_rumble &&
        output.left_trigger_rumble > 0 && output.right_trigger_rumble > 0
      ) {
        rumble->left_trigger = output.left_trigger_rumble;
        rumble->right_trigger = output.right_trigger_rumble;
        rumble->trigger_observed = true;
      }
    });
    return rumble;
  }

  bool wait_for_rumble(const std::shared_ptr<RumbleState> &rumble, bool pump_sdl, bool trigger = false) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    const auto observed = [&rumble, trigger]() {
      return trigger ? rumble->trigger_observed.load() : rumble->ordinary_observed.load();
    };
    while (std::chrono::steady_clock::now() < deadline && !observed()) {
      if (pump_sdl) {
        pump_sdl_events();
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {20});
    }
    return observed();
  }

  std::optional<std::filesystem::path> wait_for_hidraw_node(const lvh::Gamepad &gamepad) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    while (std::chrono::steady_clock::now() < deadline) {
      for (const auto &node : gamepad.device_nodes()) {
        if (node.kind == lvh::DeviceNodeKind::hidraw) {
          return std::filesystem::path {node.path};
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }
    return std::nullopt;
  }

  std::string describe_device_nodes(const std::vector<lvh::DeviceNode> &nodes) {
    if (nodes.empty()) {
      return "<none>";
    }

    std::ostringstream description;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
      if (index != 0) {
        description << ", ";
      }
      description << nodes[index].path
                  << " (kind=" << static_cast<int>(std::to_underlying(nodes[index].kind)) << ')';
    }
    return description.str();
  }

  std::string errno_message(int error) {
    return std::error_code {error, std::generic_category()}.message();
  }

  std::string describe_node_permissions(const std::filesystem::path &path) {
    struct stat status {};
    if (::stat(path.c_str(), &status) != 0) {
      const auto stat_error = errno;
      return std::format(
        "path={} stat failed: {} (errno={})",
        path.string(),
        errno_message(stat_error),
        stat_error
      );
    }

    return std::format(
      "path={} mode={:04o} uid={} gid={} euid={} egid={}",
      path.string(),
      status.st_mode & 07777,
      status.st_uid,
      status.st_gid,
      ::geteuid(),
      ::getegid()
    );
  }

  int wait_for_write_access(const std::filesystem::path &path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    int access_error = 0;
    while (std::chrono::steady_clock::now() < deadline) {
      if (::access(path.c_str(), W_OK) == 0) {
        return 0;
      }
      access_error = errno;
      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }
    return access_error;
  }

  std::vector<std::uint8_t> playstation_rumble_report(const lvh::DeviceProfile &profile) {
    std::vector<std::uint8_t> report(profile.output_report_size, 0);
    if (profile.gamepad_kind == lvh::GamepadProfileKind::dualshock4 && report.size() >= 32U) {
      report[0] = 0x05;
      report[1] = 0x01;
      report[4] = 0x12;
      report[5] = 0x56;
    } else if (profile.gamepad_kind == lvh::GamepadProfileKind::dualsense && report.size() >= 48U) {
      report[0] = 0x02;
      report[1] = 0x01;
      report[3] = 0x12;
      report[4] = 0x56;
    } else {
      report.clear();
    }
    return report;
  }

  void dump_sdl_joysticks() {
    const auto joystick_count = SDL_NumJoysticks();
    std::cout << "SDL joystick count: " << joystick_count << '\n';
    for (int index = 0; index < joystick_count; ++index) {
      const auto *name = SDL_JoystickNameForIndex(index);
#if SDL_VERSION_ATLEAST(2, 24, 0)
      const auto *path = SDL_JoystickPathForIndex(index);
#endif
      std::cout << "SDL joystick[" << index << "]: " << (name == nullptr ? "<unknown>" : name)
                << " vendor=" << SDL_JoystickGetDeviceVendor(index)
                << " product=" << SDL_JoystickGetDeviceProduct(index)
#if SDL_VERSION_ATLEAST(2, 24, 0)
                << " path=" << (path == nullptr ? "<unknown>" : path)
#endif
                << " rumble=" << sdl_joystick_supports_rumble(index) << '\n';
    }
  }

  int wait_for_sdl_game_controller(const lvh::DeviceProfile &profile, bool require_rumble) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};

    while (std::chrono::steady_clock::now() < deadline) {
      pump_sdl_events();

      const auto joystick_count = SDL_NumJoysticks();
      for (int index = 0; index < joystick_count; ++index) {
        if (
          sdl_joystick_matches_profile(index, profile) &&
          SDL_IsGameController(index) == SDL_TRUE &&
          (!require_rumble || sdl_joystick_supports_rumble(index))
        ) {
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
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_XBOX", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_XBOX_ONE", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS4_RUMBLE", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_PS5_RUMBLE", "1");
    SDL_SetHint("SDL_JOYSTICK_HIDAPI_SWITCH", "1");
  }

  lvh::GamepadCreationResult create_sdl_gamepad(lvh::Runtime &runtime, const SdlGamepadConsumerCase &test_case) {
    lvh::CreateGamepadOptions options;
    options.profile = test_case.profile;
    options.profile.name = unique_device_name(test_case.name_suffix);
    options.metadata.has_battery = test_case.require_battery;
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
      profile.name = is_playstation_profile(profile.gamepad_kind) ?
                       std::string {playstation_uhid_name} :
                       unique_device_name(test_case.name_suffix);
      if (test_case.expected_vendor_id.has_value()) {
        profile.vendor_id = *test_case.expected_vendor_id;
      }
      if (test_case.expected_product_id.has_value()) {
        profile.product_id = *test_case.expected_product_id;
      }
      return profile;
    }();

    auto created = create_sdl_gamepad(*runtime, test_case);
    ASSERT_TRUE(created) << created.status.message();

    if (test_case.require_sdl_rumble) {
      const auto hidraw_node = wait_for_hidraw_node(*created.gamepad);
      ASSERT_TRUE(hidraw_node.has_value())
        << "No hidraw node was discovered. Reported device nodes: "
        << describe_device_nodes(created.gamepad->device_nodes());

      const auto access_error = wait_for_write_access(*hidraw_node);
      ASSERT_EQ(access_error, 0)
        << "hidraw node is not writable: " << errno_message(access_error)
        << " (errno=" << access_error << "); " << describe_node_permissions(*hidraw_node);
      errno = 0;
      const auto read_result = ::access(hidraw_node->c_str(), R_OK);
      const auto read_error = errno;
      ASSERT_EQ(read_result, 0)
        << "hidraw node is not readable: " << errno_message(read_error)
        << " (errno=" << read_error << "); " << describe_node_permissions(*hidraw_node);
    }

    const auto joystick_index = wait_for_sdl_game_controller(expected_profile, test_case.require_sdl_rumble);
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

  void expect_sdl_rumble_callback(
    SDL_GameController *controller,
    lvh::Gamepad &gamepad,
    bool require_trigger_rumble = false
  ) {
    const auto rumble = observe_rumble(gamepad);

    ASSERT_EQ(SDL_GameControllerRumble(controller, 0x5678, 0x1234, 1000), 0) << SDL_GetError();
    EXPECT_TRUE(wait_for_rumble(rumble, true));
    EXPECT_GT(rumble->low_frequency.load(), 0);
    EXPECT_GT(rumble->high_frequency.load(), 0);

    if (require_trigger_rumble) {
      auto *joystick = SDL_GameControllerGetJoystick(controller);
      ASSERT_NE(joystick, nullptr);
      ASSERT_EQ(SDL_JoystickHasRumbleTriggers(joystick), SDL_TRUE)
        << "SDL did not expose the Xbox impulse motors";
      ASSERT_EQ(SDL_GameControllerRumbleTriggers(controller, 0x3456, 0x789A, 1000), 0) << SDL_GetError();
      EXPECT_TRUE(wait_for_rumble(rumble, true, true));
      EXPECT_GT(rumble->left_trigger.load(), 0);
      EXPECT_GT(rumble->right_trigger.load(), 0);
    }
  }

  void expect_sdl_motion_input(SDL_GameController *controller, lvh::Gamepad &gamepad) {
    ASSERT_EQ(SDL_GameControllerHasSensor(controller, SDL_SENSOR_ACCEL), SDL_TRUE);
    ASSERT_EQ(SDL_GameControllerHasSensor(controller, SDL_SENSOR_GYRO), SDL_TRUE);
    ASSERT_EQ(SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_ACCEL, SDL_TRUE), 0) << SDL_GetError();
    ASSERT_EQ(SDL_GameControllerSetSensorEnabled(controller, SDL_SENSOR_GYRO, SDL_TRUE), 0) << SDL_GetError();

    lvh::GamepadState state;
    state.acceleration = lvh::Vector3 {.x = 9.80665F, .y = 19.6133F, .z = -9.80665F};
    state.gyroscope = lvh::Vector3 {.x = 10.0F, .y = 20.0F, .z = -30.0F};
    ASSERT_TRUE(gamepad.submit(state).ok());

    std::array<float, 3> acceleration {};
    std::array<float, 3> gyroscope {};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    while (std::chrono::steady_clock::now() < deadline) {
      SDL_GameControllerUpdate();
      pump_sdl_events();
      ASSERT_EQ(
        SDL_GameControllerGetSensorData(controller, SDL_SENSOR_ACCEL, acceleration.data(), acceleration.size()),
        0
      ) << SDL_GetError();
      ASSERT_EQ(
        SDL_GameControllerGetSensorData(controller, SDL_SENSOR_GYRO, gyroscope.data(), gyroscope.size()),
        0
      ) << SDL_GetError();
      if (std::abs(acceleration[1]) > 15.0F && std::abs(gyroscope[2]) > 0.25F) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds {20});
    }

    ADD_FAILURE() << "SDL motion remained static: accel=" << acceleration[0] << "," << acceleration[1] << ","
                  << acceleration[2] << " gyro=" << gyroscope[0] << "," << gyroscope[1] << "," << gyroscope[2];
  }

  void expect_sdl_battery_input(SDL_Joystick *joystick, lvh::Gamepad &gamepad) {
    lvh::GamepadState state;
    state.battery = lvh::GamepadBattery {
      .state = lvh::GamepadBatteryState::discharging,
      .percentage = 50,
    };
    ASSERT_TRUE(gamepad.submit(state).ok());

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};
    while (
      std::chrono::steady_clock::now() < deadline &&
      SDL_JoystickCurrentPowerLevel(joystick) != SDL_JOYSTICK_POWER_MEDIUM) {
      SDL_GameControllerUpdate();
      pump_sdl_events();
      std::this_thread::sleep_for(std::chrono::milliseconds {20});
    }
    EXPECT_EQ(SDL_JoystickCurrentPowerLevel(joystick), SDL_JOYSTICK_POWER_MEDIUM);
  }

  void expect_hidraw_rumble_callback(const lvh::DeviceProfile &profile, lvh::Gamepad &gamepad) {
    const auto rumble = observe_rumble(gamepad);
    const auto hidraw_node = wait_for_hidraw_node(gamepad);
    ASSERT_TRUE(hidraw_node.has_value())
      << "No hidraw node was discovered. Reported device nodes: "
      << describe_device_nodes(gamepad.device_nodes());

    const auto report = playstation_rumble_report(profile);
    ASSERT_FALSE(report.empty());

    const auto access_error = wait_for_write_access(*hidraw_node);
    ASSERT_EQ(access_error, 0)
      << "hidraw node is not writable: " << errno_message(access_error)
      << " (errno=" << access_error << "); " << describe_node_permissions(*hidraw_node);

    errno = 0;
    const auto descriptor = ::open(hidraw_node->c_str(), O_WRONLY | O_CLOEXEC);
    const auto open_error = errno;
    ASSERT_GE(descriptor, 0)
      << "Failed to open hidraw node: " << errno_message(open_error)
      << " (errno=" << open_error << "); " << describe_node_permissions(*hidraw_node);
    ScopeExit close_descriptor {[descriptor]() {
      static_cast<void>(::close(descriptor));
    }};

    errno = 0;
    const auto bytes_written = ::write(descriptor, report.data(), report.size());
    const auto write_error = errno;
    ASSERT_EQ(bytes_written, static_cast<ssize_t>(report.size()))
      << "Failed to write the complete rumble report: " << errno_message(write_error)
      << " (errno=" << write_error << ")";
    EXPECT_TRUE(wait_for_rumble(rumble, false));
    EXPECT_GT(rumble->low_frequency.load(), 0);
    EXPECT_GT(rumble->high_frequency.load(), 0);
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
    if (test_case.require_sdl_rumble) {
      ASSERT_EQ(SDL_JoystickHasRumble(joystick), SDL_TRUE)
        << "SDL selected a PlayStation transport without rumble support";
    }
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
      if (test_case.require_sdl_rumble) {
        expect_sdl_rumble_callback(controller.get(), gamepad);
        expect_hidraw_rumble_callback(expected_profile, gamepad);
      }
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
      ButtonCase {dpad_up, SDL_CONTROLLER_BUTTON_DPAD_UP},
      ButtonCase {dpad_down, SDL_CONTROLLER_BUTTON_DPAD_DOWN},
      ButtonCase {dpad_left, SDL_CONTROLLER_BUTTON_DPAD_LEFT},
      ButtonCase {dpad_right, SDL_CONTROLLER_BUTTON_DPAD_RIGHT},
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
      SDL_GameControllerGetAxis(controller.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT) < 16000) {
      SDL_GameControllerUpdate();
      pump_sdl_events();
      std::this_thread::sleep_for(std::chrono::milliseconds {20});
    }
    EXPECT_GT(SDL_GameControllerGetAxis(controller.get(), SDL_CONTROLLER_AXIS_TRIGGERLEFT), 0);
    EXPECT_GT(SDL_GameControllerGetAxis(controller.get(), SDL_CONTROLLER_AXIS_TRIGGERRIGHT), 16000);
    if (test_case.require_motion) {
      expect_sdl_motion_input(controller.get(), gamepad);
    }
    if (test_case.require_battery) {
      expect_sdl_battery_input(joystick, gamepad);
    }
    expect_sdl_rumble_callback(controller.get(), gamepad, test_case.require_trigger_rumble);
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

#if defined(LIBVIRTUALHID_TEST_SDL3_XBOX_CONSUMER_PATH)
  struct ConsumerProcessResult {
    int exit_code = EXIT_FAILURE;
    std::string output;
  };

  ConsumerProcessResult run_sdl3_xbox_consumer(std::string_view profile) {
    std::array<int, 2> output_pipe {-1, -1};
    if (::pipe(output_pipe.data()) != 0) {
      return {.exit_code = EXIT_FAILURE, .output = "Failed to start the SDL3 Xbox consumer probe"};
    }

    const std::string executable {LIBVIRTUALHID_TEST_SDL3_XBOX_CONSUMER_PATH};
    const std::string argument {profile};
    const auto child = ::fork();
    if (child < 0) {
      static_cast<void>(::close(output_pipe[0]));
      static_cast<void>(::close(output_pipe[1]));
      return {.exit_code = EXIT_FAILURE, .output = "Failed to fork the SDL3 Xbox consumer probe"};
    }
    if (child == 0) {
      static_cast<void>(::close(output_pipe[0]));
      static_cast<void>(::dup2(output_pipe[1], STDOUT_FILENO));
      static_cast<void>(::dup2(output_pipe[1], STDERR_FILENO));
      static_cast<void>(::close(output_pipe[1]));
      ::execl(
        executable.c_str(),
        executable.c_str(),
        argument.c_str(),
        static_cast<char *>(nullptr)
      );
      ::_exit(127);
    }

    static_cast<void>(::close(output_pipe[1]));
    ConsumerProcessResult result;
    std::array<char, 4096> buffer {};
    auto count = ::read(output_pipe[0], buffer.data(), buffer.size());
    while (count > 0) {
      result.output.append(buffer.data(), static_cast<std::size_t>(count));
      count = ::read(output_pipe[0], buffer.data(), buffer.size());
    }
    static_cast<void>(::close(output_pipe[0]));

    if (int status = 0; ::waitpid(child, &status, 0) < 0) {
      result.output.append("Consumer probe process could not be reaped\n");
    } else if (WIFEXITED(status)) {
      result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      result.output.append(std::format("Consumer probe terminated by signal {}\n", WTERMSIG(status)));
    } else {
      result.output.append("Consumer probe did not return a decodable process status\n");
    }
    return result;
  }
#endif

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
    .expected_vendor_id = 0x1209,
    .expected_product_id = 0x0001,
    .minimum_buttons = 12,  // D-pad directions are exposed through the hat axes.
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
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  run_sdl_canonical_gamepad_test({
    .profile = lvh::profiles::xbox_one(),
    .name_suffix = "SDL Xbox One",
    .stable_id = "libvirtualhid-sdl-xbox-one-test",
    .expected_product_id = 0x0B20,
    .minimum_buttons = 15,
    .minimum_axes = 6,
    .require_sdl_rumble = true,
    .require_trigger_rumble = true,
    .require_battery = true,
  });
}

TEST_F(LinuxConsumerTest, SdlSeesXboxSeriesCanonicalButtons) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  const SdlGamepadConsumerCase test_case {
    .profile = lvh::profiles::xbox_series(),
    .name_suffix = "SDL Xbox Series",
    .stable_id = "libvirtualhid-sdl-xbox-series-test",
    .expected_product_id = 0x0B13,
    .minimum_buttons = 16,
    .minimum_axes = 6,
    .require_sdl_rumble = true,
    .require_trigger_rumble = true,
    .require_battery = true,
  };
  run_sdl_canonical_gamepad_test(test_case);
}

#if defined(LIBVIRTUALHID_TEST_SDL3_XBOX_CONSUMER_PATH)
TEST_F(LinuxConsumerTest, Sdl3SeesXboxOneIdentityMappingsAndFourMotorRumble) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));
  const auto result = run_sdl3_xbox_consumer("xone");
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

TEST_F(LinuxConsumerTest, Sdl3SeesXboxSeriesIdentityMappingsAndFourMotorRumble) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));
  const auto result = run_sdl3_xbox_consumer("xseries");
  EXPECT_EQ(result.exit_code, 0) << result.output;
}
#endif

TEST_F(LinuxConsumerTest, SdlSeesSwitchProCanonicalButtons) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  run_sdl_canonical_gamepad_test({
    .profile = lvh::profiles::switch_pro(),
    .name_suffix = "SDL Switch Pro",
    .stable_id = "02:00:00:00:00:05",
    .minimum_buttons = 14,
    .minimum_axes = 4,
    .require_sdl_rumble = true,
    .require_motion = true,
  });
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

TEST_F(LinuxConsumerTest, SdlSeesDualShock4BluetoothControllerBehavior) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  run_sdl_playstation_controller_test({
    .profile = lvh::profiles::dualshock4_bluetooth(),
    .name_suffix = "SDL DualShock 4 Bluetooth",
    .stable_id = "02:00:00:00:00:04",
    .minimum_buttons = 10,
    .minimum_axes = 4,
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
  auto *device = libinput_event_get_device(event.get());
  ASSERT_NE(device, nullptr);
  EXPECT_TRUE(libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER));
  EXPECT_EQ(libinput_device_config_scroll_get_method(device), LIBINPUT_CONFIG_SCROLL_NO_SCROLL);

  ASSERT_TRUE(created.mouse->move_relative(25, -10).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_MOTION});
  ASSERT_NE(event.get(), nullptr);
  auto *pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_DOUBLE_EQ(libinput_event_pointer_get_dx_unaccelerated(pointer_event), 25.0);
  EXPECT_DOUBLE_EQ(libinput_event_pointer_get_dy_unaccelerated(pointer_event), -10.0);

  ASSERT_TRUE(created.mouse->button(lvh::MouseButton::middle, true).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_BUTTON});
  ASSERT_NE(event.get(), nullptr);
  pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_EQ(libinput_event_pointer_get_button(pointer_event), BTN_MIDDLE);
  EXPECT_EQ(libinput_event_pointer_get_button_state(pointer_event), LIBINPUT_BUTTON_STATE_PRESSED);

  ASSERT_TRUE(created.mouse->button(lvh::MouseButton::middle, false).ok());
  event = wait_for_libinput_event(context.get(), {LIBINPUT_EVENT_POINTER_BUTTON});
  ASSERT_NE(event.get(), nullptr);
  pointer_event = libinput_event_get_pointer_event(event.get());
  ASSERT_NE(pointer_event, nullptr);
  EXPECT_EQ(libinput_event_pointer_get_button(pointer_event), BTN_MIDDLE);
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
