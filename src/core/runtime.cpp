/**
 * @file src/core/runtime.cpp
 * @brief Runtime and virtual device handle definitions.
 */

// standard includes
#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

// local includes
#include "core/backend.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>
#include <libvirtualhid/runtime.hpp>

namespace lvh::detail {

  struct GamepadDevice {
    explicit GamepadDevice(
      DeviceId device_id,
      CreateGamepadOptions create_options,
      std::unique_ptr<BackendGamepad> backend_gamepad
    ):
        id {device_id},
        options {std::move(create_options)},
        backend {std::move(backend_gamepad)} {}

    DeviceId id;
    CreateGamepadOptions options;
    std::unique_ptr<BackendGamepad> backend;
    bool open = true;
    GamepadState last_state;
    std::vector<std::uint8_t> last_report;
    std::size_t submitted_reports = 0;
    OutputCallback output_callback;
    mutable std::mutex mutex;
  };

  struct KeyboardDevice {
    explicit KeyboardDevice(
      DeviceId device_id,
      CreateKeyboardOptions create_options,
      std::unique_ptr<BackendKeyboard> backend_keyboard
    ):
        id {device_id},
        options {std::move(create_options)},
        backend {std::move(backend_keyboard)} {}

    DeviceId id;
    CreateKeyboardOptions options;
    std::unique_ptr<BackendKeyboard> backend;
    bool open = true;
    KeyboardEvent last_event;
    KeyboardTextEvent last_text_event;
    std::size_t submitted_events = 0;
    mutable std::mutex mutex;
  };

  struct MouseDevice {
    explicit MouseDevice(DeviceId device_id, CreateMouseOptions create_options, std::unique_ptr<BackendMouse> backend_mouse):
        id {device_id},
        options {std::move(create_options)},
        backend {std::move(backend_mouse)} {}

    DeviceId id;
    CreateMouseOptions options;
    std::unique_ptr<BackendMouse> backend;
    bool open = true;
    MouseEvent last_event;
    std::size_t submitted_events = 0;
    mutable std::mutex mutex;
  };

  class RuntimeState {
  public:
    explicit RuntimeState(RuntimeOptions runtime_options):
        options {runtime_options},
        backend {create_backend(runtime_options.backend)},
        caps {backend->capabilities()} {}

    RuntimeOptions options;
    std::unique_ptr<Backend> backend;
    BackendCapabilities caps;
    DeviceId next_device_id = 1;
    std::vector<std::weak_ptr<GamepadDevice>> gamepads;
    std::vector<std::weak_ptr<KeyboardDevice>> keyboards;
    std::vector<std::weak_ptr<MouseDevice>> mice;
    mutable std::mutex mutex;
  };

}  // namespace lvh::detail

namespace lvh {
  namespace {

    Status validate_gamepad_options(const CreateGamepadOptions &options) {
      if (options.profile.device_type != DeviceType::gamepad) {
        return Status::failure(ErrorCode::unsupported_profile, "device profile is not a gamepad");
      }
      if (options.profile.name.empty()) {
        return Status::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }
      if (options.profile.report_descriptor.empty()) {
        return Status::failure(ErrorCode::invalid_argument, "device profile report descriptor must not be empty");
      }
      if (options.profile.report_id == 0) {
        return Status::failure(ErrorCode::invalid_argument, "device profile report id must not be zero");
      }
      if (options.profile.input_report_size == 0) {
        return Status::failure(ErrorCode::invalid_argument, "device profile input report size must not be zero");
      }

      return Status::success();
    }

    Status validate_keyboard_options(const CreateKeyboardOptions &options) {
      if (options.profile.device_type != DeviceType::keyboard) {
        return Status::failure(ErrorCode::unsupported_profile, "device profile is not a keyboard");
      }
      if (options.profile.name.empty()) {
        return Status::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }

      return Status::success();
    }

    Status validate_mouse_options(const CreateMouseOptions &options) {
      if (options.profile.device_type != DeviceType::mouse) {
        return Status::failure(ErrorCode::unsupported_profile, "device profile is not a mouse");
      }
      if (options.profile.name.empty()) {
        return Status::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }

      return Status::success();
    }

    Status validate_keyboard_event(const KeyboardEvent &event) {
      if (event.key_code == 0) {
        return Status::failure(ErrorCode::invalid_argument, "keyboard key code must not be zero");
      }

      return Status::success();
    }

    Status validate_mouse_event(const MouseEvent &event) {
      if (event.kind == MouseEventKind::absolute_motion && (event.width <= 0 || event.height <= 0)) {
        return Status::failure(ErrorCode::invalid_argument, "absolute mouse movement requires positive dimensions");
      }

      return Status::success();
    }

    template<class Func>
    auto with_device(const auto &device, Func &&func) {
      std::lock_guard lock {device->mutex};
      return func(*device);
    }

    template<class DeviceList>
    std::size_t count_open_devices(const DeviceList &devices) {
      std::size_t count = 0;
      for (const auto &weak_device : devices) {
        if (const auto device = weak_device.lock()) {
          if (device->open) {
            ++count;
          }
        }
      }
      return count;
    }

    template<class DeviceList>
    void close_devices(DeviceList &devices) {
      for (const auto &weak_device : devices) {
        if (const auto device = weak_device.lock()) {
          std::lock_guard device_lock {device->mutex};
          if (device->backend) {
            static_cast<void>(device->backend->close());
          }
          device->open = false;
        }
      }
    }

  }  // namespace

  Gamepad::Gamepad(std::shared_ptr<detail::GamepadDevice> device):
      device_ {std::move(device)} {}

  Gamepad::Gamepad(Gamepad &&) noexcept = default;
  Gamepad &Gamepad::operator=(Gamepad &&) noexcept = default;
  Gamepad::~Gamepad() = default;

  DeviceId Gamepad::device_id() const {
    return device_->id;
  }

  const DeviceProfile &Gamepad::profile() const {
    return device_->options.profile;
  }

  const GamepadMetadata &Gamepad::metadata() const {
    return device_->options.metadata;
  }

  bool Gamepad::is_open() const {
    return with_device(device_, [](const auto &device) {
      return device.open;
    });
  }

  Status Gamepad::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return Status::success();
      }

      auto status = Status::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  Status Gamepad::submit(const GamepadState &state) {
    return with_device(device_, [&state](auto &device) {
      if (!device.open) {
        return Status::failure(ErrorCode::device_closed, "gamepad is closed");
      }

      auto report = reports::pack_input_report(device.options.profile, state);
      if (report.empty()) {
        return Status::failure(ErrorCode::backend_failure, "failed to pack gamepad input report");
      }

      if (device.backend) {
        if (const auto status = device.backend->submit(report); !status.ok()) {
          return status;
        }
      }

      device.last_state = reports::normalize_state(state);
      device.last_report = std::move(report);
      ++device.submitted_reports;
      return Status::success();
    });
  }

  void Gamepad::set_output_callback(OutputCallback callback) {
    with_device(device_, [&callback](auto &device) {
      device.output_callback = std::move(callback);
      if (device.backend) {
        device.backend->set_output_callback(device.output_callback);
      }
      return 0;
    });
  }

  Status Gamepad::dispatch_output(const GamepadOutput &output) {
    OutputCallback callback;
    const auto status = with_device(device_, [&callback](auto &device) {
      if (!device.open) {
        return Status::failure(ErrorCode::device_closed, "gamepad is closed");
      }
      callback = device.output_callback;
      return Status::success();
    });

    if (!status.ok()) {
      return status;
    }
    if (callback) {
      callback(output);
    }
    return Status::success();
  }

  GamepadState Gamepad::last_submitted_state() const {
    return with_device(device_, [](const auto &device) {
      return device.last_state;
    });
  }

  std::vector<std::uint8_t> Gamepad::last_input_report() const {
    return with_device(device_, [](const auto &device) {
      return device.last_report;
    });
  }

  std::size_t Gamepad::submit_count() const {
    return with_device(device_, [](const auto &device) {
      return device.submitted_reports;
    });
  }

  Keyboard::Keyboard(std::shared_ptr<detail::KeyboardDevice> device):
      device_ {std::move(device)} {}

  Keyboard::Keyboard(Keyboard &&) noexcept = default;
  Keyboard &Keyboard::operator=(Keyboard &&) noexcept = default;
  Keyboard::~Keyboard() = default;

  DeviceId Keyboard::device_id() const {
    return device_->id;
  }

  const DeviceProfile &Keyboard::profile() const {
    return device_->options.profile;
  }

  bool Keyboard::is_open() const {
    return with_device(device_, [](const auto &device) {
      return device.open;
    });
  }

  Status Keyboard::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return Status::success();
      }

      auto status = Status::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  Status Keyboard::submit(const KeyboardEvent &event) {
    if (const auto validation = validate_keyboard_event(event); !validation.ok()) {
      return validation;
    }

    return with_device(device_, [&event](auto &device) {
      if (!device.open) {
        return Status::failure(ErrorCode::device_closed, "keyboard is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->submit(event); !status.ok()) {
          return status;
        }
      }

      device.last_event = event;
      ++device.submitted_events;
      return Status::success();
    });
  }

  Status Keyboard::press(KeyboardKeyCode key_code) {
    return submit({.key_code = key_code, .pressed = true});
  }

  Status Keyboard::release(KeyboardKeyCode key_code) {
    return submit({.key_code = key_code, .pressed = false});
  }

  Status Keyboard::type_text(const KeyboardTextEvent &event) {
    return with_device(device_, [&event](auto &device) {
      if (!device.open) {
        return Status::failure(ErrorCode::device_closed, "keyboard is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->type_text(event); !status.ok()) {
          return status;
        }
      }

      device.last_text_event = event;
      ++device.submitted_events;
      return Status::success();
    });
  }

  KeyboardEvent Keyboard::last_submitted_event() const {
    return with_device(device_, [](const auto &device) {
      return device.last_event;
    });
  }

  std::size_t Keyboard::submit_count() const {
    return with_device(device_, [](const auto &device) {
      return device.submitted_events;
    });
  }

  Mouse::Mouse(std::shared_ptr<detail::MouseDevice> device):
      device_ {std::move(device)} {}

  Mouse::Mouse(Mouse &&) noexcept = default;
  Mouse &Mouse::operator=(Mouse &&) noexcept = default;
  Mouse::~Mouse() = default;

  DeviceId Mouse::device_id() const {
    return device_->id;
  }

  const DeviceProfile &Mouse::profile() const {
    return device_->options.profile;
  }

  bool Mouse::is_open() const {
    return with_device(device_, [](const auto &device) {
      return device.open;
    });
  }

  Status Mouse::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return Status::success();
      }

      auto status = Status::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  Status Mouse::submit(const MouseEvent &event) {
    if (const auto validation = validate_mouse_event(event); !validation.ok()) {
      return validation;
    }

    return with_device(device_, [&event](auto &device) {
      if (!device.open) {
        return Status::failure(ErrorCode::device_closed, "mouse is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->submit(event); !status.ok()) {
          return status;
        }
      }

      device.last_event = event;
      ++device.submitted_events;
      return Status::success();
    });
  }

  Status Mouse::move_relative(std::int32_t delta_x, std::int32_t delta_y) {
    return submit({.kind = MouseEventKind::relative_motion, .x = delta_x, .y = delta_y});
  }

  Status Mouse::move_absolute(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    return submit({.kind = MouseEventKind::absolute_motion, .x = x, .y = y, .width = width, .height = height});
  }

  Status Mouse::button(MouseButton button, bool pressed) {
    MouseEvent event;
    event.kind = MouseEventKind::button;
    event.button = button;
    event.pressed = pressed;
    return submit(event);
  }

  Status Mouse::vertical_scroll(std::int32_t distance) {
    MouseEvent event;
    event.kind = MouseEventKind::vertical_scroll;
    event.high_resolution_scroll = distance;
    return submit(event);
  }

  Status Mouse::horizontal_scroll(std::int32_t distance) {
    MouseEvent event;
    event.kind = MouseEventKind::horizontal_scroll;
    event.high_resolution_scroll = distance;
    return submit(event);
  }

  MouseEvent Mouse::last_submitted_event() const {
    return with_device(device_, [](const auto &device) {
      return device.last_event;
    });
  }

  std::size_t Mouse::submit_count() const {
    return with_device(device_, [](const auto &device) {
      return device.submitted_events;
    });
  }

  Runtime::Runtime(RuntimeOptions options):
      state_ {std::make_shared<detail::RuntimeState>(options)} {}

  Runtime::Runtime(Runtime &&) noexcept = default;
  Runtime &Runtime::operator=(Runtime &&) noexcept = default;

  Runtime::~Runtime() {
    if (state_) {
      close_all();
    }
  }

  std::unique_ptr<Runtime> Runtime::create(RuntimeOptions options) {
    return std::unique_ptr<Runtime> {new Runtime {options}};
  }

  const BackendCapabilities &Runtime::capabilities() const {
    return state_->caps;
  }

  BackendKind Runtime::backend_kind() const {
    return state_->options.backend;
  }

  GamepadCreationResult Runtime::create_gamepad(const DeviceProfile &profile) {
    CreateGamepadOptions options;
    options.profile = profile;
    return create_gamepad(options);
  }

  GamepadCreationResult Runtime::create_gamepad(const CreateGamepadOptions &options) {
    if (const auto validation = validate_gamepad_options(options); !validation.ok()) {
      return {validation, nullptr};
    }

    DeviceId id;
    {
      std::lock_guard lock {state_->mutex};
      id = state_->next_device_id++;
    }

    auto backend_result = state_->backend->create_gamepad(id, options);
    if (!backend_result) {
      return {std::move(backend_result.status), nullptr};
    }

    auto device = std::make_shared<detail::GamepadDevice>(id, options, std::move(backend_result.gamepad));
    {
      std::lock_guard lock {state_->mutex};
      state_->gamepads.emplace_back(device);
    }

    return {Status::success(), std::unique_ptr<Gamepad> {new Gamepad {std::move(device)}}};
  }

  KeyboardCreationResult Runtime::create_keyboard() {
    CreateKeyboardOptions options;
    options.profile = profiles::keyboard();
    return create_keyboard(options);
  }

  KeyboardCreationResult Runtime::create_keyboard(const CreateKeyboardOptions &options) {
    if (const auto validation = validate_keyboard_options(options); !validation.ok()) {
      return {validation, nullptr};
    }

    DeviceId id;
    {
      std::lock_guard lock {state_->mutex};
      id = state_->next_device_id++;
    }

    auto backend_result = state_->backend->create_keyboard(id, options);
    if (!backend_result) {
      return {std::move(backend_result.status), nullptr};
    }

    auto device = std::make_shared<detail::KeyboardDevice>(id, options, std::move(backend_result.keyboard));
    {
      std::lock_guard lock {state_->mutex};
      state_->keyboards.emplace_back(device);
    }

    return {Status::success(), std::unique_ptr<Keyboard> {new Keyboard {std::move(device)}}};
  }

  MouseCreationResult Runtime::create_mouse() {
    CreateMouseOptions options;
    options.profile = profiles::mouse();
    return create_mouse(options);
  }

  MouseCreationResult Runtime::create_mouse(const CreateMouseOptions &options) {
    if (const auto validation = validate_mouse_options(options); !validation.ok()) {
      return {validation, nullptr};
    }

    DeviceId id;
    {
      std::lock_guard lock {state_->mutex};
      id = state_->next_device_id++;
    }

    auto backend_result = state_->backend->create_mouse(id, options);
    if (!backend_result) {
      return {std::move(backend_result.status), nullptr};
    }

    auto device = std::make_shared<detail::MouseDevice>(id, options, std::move(backend_result.mouse));
    {
      std::lock_guard lock {state_->mutex};
      state_->mice.emplace_back(device);
    }

    return {Status::success(), std::unique_ptr<Mouse> {new Mouse {std::move(device)}}};
  }

  std::size_t Runtime::active_device_count() const {
    std::lock_guard lock {state_->mutex};
    return count_open_devices(state_->gamepads) + count_open_devices(state_->keyboards) + count_open_devices(state_->mice);
  }

  void Runtime::close_all() {
    std::lock_guard lock {state_->mutex};
    close_devices(state_->gamepads);
    close_devices(state_->keyboards);
    close_devices(state_->mice);
  }

}  // namespace lvh
