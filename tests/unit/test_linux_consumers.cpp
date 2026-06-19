/**
 * @file tests/unit/test_linux_consumers.cpp
 * @brief Linux integration tests for SDL2 and libinput consumers.
 */

// standard includes
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
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
#if defined(__linux__)
  #include <cerrno>
  #include <fcntl.h>
  #include <linux/input.h>
  #include <unistd.h>
#endif

// lib includes
#if defined(__linux__)
  #include <libinput.h>
  #include <SDL.h>
#endif
#include <libvirtualhid/libvirtualhid.hpp>

// local includes
#include "fixtures/fixtures.hpp"

/**
 * @brief Test fixture for Linux consumer input libraries.
 */
class LinuxConsumerTest: public LinuxTest {};

namespace {

#if defined(__linux__)
  using LibinputContext = std::unique_ptr<libinput, void (*)(libinput *)>;
  using LibinputEvent = std::unique_ptr<libinput_event, void (*)(libinput_event *)>;
  using SdlJoystick = std::unique_ptr<SDL_Joystick, void (*)(SDL_Joystick *)>;

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
    ~ScopeExit() {
      function_();
    }

    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;
    ScopeExit(ScopeExit &&) noexcept = delete;
    ScopeExit &operator=(ScopeExit &&) noexcept = delete;

  private:
    std::function<void()> function_;
  };

  std::string unique_device_name(std::string_view suffix) {
    return "libvirtualhid " + std::string {suffix} + " " + std::to_string(::getpid());
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
    const auto *name = SDL_JoystickNameForIndex(index);
    if (name != nullptr && profile.name == name) {
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

  bool wait_for_sdl_gamepad_input(SDL_Joystick *joystick) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds {3};

    while (std::chrono::steady_clock::now() < deadline) {
      pump_sdl_events();

      const auto button_pressed = SDL_JoystickNumButtons(joystick) > 0 && SDL_JoystickGetButton(joystick, 0) != 0;
      bool axis_moved = false;
      for (int axis = 0; axis < SDL_JoystickNumAxes(joystick); ++axis) {
        if (std::abs(static_cast<int>(SDL_JoystickGetAxis(joystick, axis))) > 8000) {
          axis_moved = true;
          break;
        }
      }

      if (button_pressed && axis_moved) {
        return true;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }

    return false;
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

  int open_restricted(const char *path, int flags, void *user_data) {
    static_cast<void>(user_data);

    const auto fd = ::open(path, flags);
    return fd < 0 ? -errno : fd;
  }

  void close_restricted(int fd, void *user_data) {
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
        if (std::find(expected_types.begin(), expected_types.end(), event_type) != expected_types.end()) {
          return event;
        }

        std::cout << "Ignoring libinput event type: " << event_type << '\n';
      }

      std::this_thread::sleep_for(std::chrono::milliseconds {50});
    }

    return LibinputEvent {nullptr, destroy_libinput_event};
  }

#endif  // defined(__linux__)

}  // namespace

#if defined(__linux__)
TEST_F(LinuxConsumerTest, SdlSeesUhidGamepadButtonAndAxisInput) {
  ASSERT_TRUE(HasReadableWritableDeviceNode("/dev/uhid"));

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  ASSERT_EQ(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS), 0) << SDL_GetError();
  ScopeExit sdl_quit {[]() {
    SDL_Quit();
  }};

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  ASSERT_TRUE(runtime->capabilities().supports_gamepad);

  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::generic_gamepad();
  options.profile.name = unique_device_name("SDL Gamepad");
  options.metadata.stable_id = "libvirtualhid-sdl-gamepad-test";

  auto created = runtime->create_gamepad(options);
  ASSERT_TRUE(created) << created.status.message();

  const auto joystick_index = wait_for_sdl_joystick(options.profile);
  ASSERT_GE(joystick_index, 0);

  SdlJoystick joystick {SDL_JoystickOpen(joystick_index), SDL_JoystickClose};
  ASSERT_NE(joystick.get(), nullptr) << SDL_GetError();
  EXPECT_GE(SDL_JoystickNumButtons(joystick.get()), 1);
  EXPECT_GE(SDL_JoystickNumAxes(joystick.get()), 2);

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.left_stick = {0.75F, -0.5F};
  ASSERT_TRUE(created.gamepad->submit(state).ok());

  EXPECT_TRUE(wait_for_sdl_gamepad_input(joystick.get())) << describe_sdl_state(joystick.get());
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
#else
TEST_F(LinuxConsumerTest, SdlSeesUhidGamepadButtonAndAxisInput) {}

TEST_F(LinuxConsumerTest, LibinputSeesUinputKeyboardKeys) {}

TEST_F(LinuxConsumerTest, LibinputSeesUinputMouseMotionAndButtons) {}

TEST_F(LinuxConsumerTest, LibinputSeesUinputTouchscreenContacts) {}

TEST_F(LinuxConsumerTest, LibinputSeesUinputTrackpadButton) {}

TEST_F(LinuxConsumerTest, LibinputSeesUinputPenTabletTool) {}
#endif
