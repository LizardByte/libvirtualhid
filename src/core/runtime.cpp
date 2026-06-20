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

  struct TouchscreenDevice {
    explicit TouchscreenDevice(
      DeviceId device_id,
      CreateTouchscreenOptions create_options,
      std::unique_ptr<BackendTouchscreen> backend_touchscreen
    ):
        id {device_id},
        options {std::move(create_options)},
        backend {std::move(backend_touchscreen)} {}

    DeviceId id;
    CreateTouchscreenOptions options;
    std::unique_ptr<BackendTouchscreen> backend;
    bool open = true;
    TouchContact last_contact;
    std::size_t submitted_events = 0;
    mutable std::mutex mutex;
  };

  struct TrackpadDevice {
    explicit TrackpadDevice(
      DeviceId device_id,
      CreateTrackpadOptions create_options,
      std::unique_ptr<BackendTrackpad> backend_trackpad
    ):
        id {device_id},
        options {std::move(create_options)},
        backend {std::move(backend_trackpad)} {}

    DeviceId id;
    CreateTrackpadOptions options;
    std::unique_ptr<BackendTrackpad> backend;
    bool open = true;
    TouchContact last_contact;
    std::size_t submitted_events = 0;
    mutable std::mutex mutex;
  };

  struct PenTabletDevice {
    explicit PenTabletDevice(
      DeviceId device_id,
      CreatePenTabletOptions create_options,
      std::unique_ptr<BackendPenTablet> backend_pen_tablet
    ):
        id {device_id},
        options {std::move(create_options)},
        backend {std::move(backend_pen_tablet)} {}

    DeviceId id;
    CreatePenTabletOptions options;
    std::unique_ptr<BackendPenTablet> backend;
    bool open = true;
    PenToolState last_tool;
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
    std::vector<std::weak_ptr<TouchscreenDevice>> touchscreens;
    std::vector<std::weak_ptr<TrackpadDevice>> trackpads;
    std::vector<std::weak_ptr<PenTabletDevice>> pen_tablets;
    mutable std::mutex mutex;
  };

}  // namespace lvh::detail

namespace lvh {
  namespace {

    OperationStatus validate_gamepad_options(const CreateGamepadOptions &options) {
      using enum ErrorCode;

      if (options.profile.device_type != DeviceType::gamepad) {
        return OperationStatus::failure(unsupported_profile, "device profile is not a gamepad");
      }
      if (options.profile.name.empty()) {
        return OperationStatus::failure(invalid_argument, "device profile name must not be empty");
      }
      if (options.profile.report_descriptor.empty()) {
        return OperationStatus::failure(invalid_argument, "device profile report descriptor must not be empty");
      }
      if (options.profile.report_id == 0) {
        return OperationStatus::failure(invalid_argument, "device profile report id must not be zero");
      }
      if (options.profile.input_report_size == 0) {
        return OperationStatus::failure(invalid_argument, "device profile input report size must not be zero");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_keyboard_options(const CreateKeyboardOptions &options) {
      if (options.profile.device_type != DeviceType::keyboard) {
        return OperationStatus::failure(ErrorCode::unsupported_profile, "device profile is not a keyboard");
      }
      if (options.profile.name.empty()) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_mouse_options(const CreateMouseOptions &options) {
      if (options.profile.device_type != DeviceType::mouse) {
        return OperationStatus::failure(ErrorCode::unsupported_profile, "device profile is not a mouse");
      }
      if (options.profile.name.empty()) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_touchscreen_options(const CreateTouchscreenOptions &options) {
      if (options.profile.device_type != DeviceType::touchscreen) {
        return OperationStatus::failure(ErrorCode::unsupported_profile, "device profile is not a touchscreen");
      }
      if (options.profile.name.empty()) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_trackpad_options(const CreateTrackpadOptions &options) {
      if (options.profile.device_type != DeviceType::trackpad) {
        return OperationStatus::failure(ErrorCode::unsupported_profile, "device profile is not a trackpad");
      }
      if (options.profile.name.empty()) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_pen_tablet_options(const CreatePenTabletOptions &options) {
      if (options.profile.device_type != DeviceType::pen_tablet) {
        return OperationStatus::failure(ErrorCode::unsupported_profile, "device profile is not a pen tablet");
      }
      if (options.profile.name.empty()) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_keyboard_event(const KeyboardEvent &event) {
      if (event.key_code == 0) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "keyboard key code must not be zero");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_mouse_event(const MouseEvent &event) {
      if (event.kind == MouseEventKind::absolute_motion && (event.width <= 0 || event.height <= 0)) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "absolute mouse movement requires positive dimensions");
      }

      return OperationStatus::success();
    }

    OperationStatus validate_touch_contact(const TouchContact &contact) {
      if (contact.id < 0) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "touch contact id must not be negative");
      }

      return OperationStatus::success();
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

  std::vector<DeviceNode> Gamepad::device_nodes() const {
    return with_device(device_, [](const auto &device) {
      if (device.backend) {
        return device.backend->device_nodes();
      }
      return std::vector<DeviceNode> {};
    });
  }

  OperationStatus Gamepad::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return OperationStatus::success();
      }

      auto status = OperationStatus::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  OperationStatus Gamepad::submit(const GamepadState &state) {
    return with_device(device_, [&state](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "gamepad is closed");
      }

      auto report = reports::pack_input_report(device.options.profile, state);
      if (report.empty()) {
        return OperationStatus::failure(ErrorCode::backend_failure, "failed to pack gamepad input report");
      }

      if (device.backend) {
        if (const auto status = device.backend->submit(report); !status.ok()) {
          return status;
        }
      }

      device.last_state = reports::normalize_state(state);
      device.last_report = std::move(report);
      ++device.submitted_reports;
      return OperationStatus::success();
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

  OperationStatus Gamepad::dispatch_output(const GamepadOutput &output) {
    OutputCallback callback;
    const auto status = with_device(device_, [&callback](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "gamepad is closed");
      }
      callback = device.output_callback;
      return OperationStatus::success();
    });

    if (!status.ok()) {
      return status;
    }
    if (callback) {
      callback(output);
    }
    return OperationStatus::success();
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

  std::vector<DeviceNode> Keyboard::device_nodes() const {
    return with_device(device_, [](const auto &device) {
      if (device.backend) {
        return device.backend->device_nodes();
      }
      return std::vector<DeviceNode> {};
    });
  }

  OperationStatus Keyboard::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return OperationStatus::success();
      }

      auto status = OperationStatus::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  OperationStatus Keyboard::submit(const KeyboardEvent &event) {
    if (const auto validation = validate_keyboard_event(event); !validation.ok()) {
      return validation;
    }

    return with_device(device_, [&event](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "keyboard is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->submit(event); !status.ok()) {
          return status;
        }
      }

      device.last_event = event;
      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  OperationStatus Keyboard::press(KeyboardKeyCode key_code) {
    return submit({.key_code = key_code, .pressed = true});
  }

  OperationStatus Keyboard::release(KeyboardKeyCode key_code) {
    return submit({.key_code = key_code, .pressed = false});
  }

  OperationStatus Keyboard::type_text(const KeyboardTextEvent &event) {
    return with_device(device_, [&event](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "keyboard is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->type_text(event); !status.ok()) {
          return status;
        }
      }

      device.last_text_event = event;
      ++device.submitted_events;
      return OperationStatus::success();
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

  std::vector<DeviceNode> Mouse::device_nodes() const {
    return with_device(device_, [](const auto &device) {
      if (device.backend) {
        return device.backend->device_nodes();
      }
      return std::vector<DeviceNode> {};
    });
  }

  OperationStatus Mouse::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return OperationStatus::success();
      }

      auto status = OperationStatus::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  OperationStatus Mouse::submit(const MouseEvent &event) {
    if (const auto validation = validate_mouse_event(event); !validation.ok()) {
      return validation;
    }

    return with_device(device_, [&event](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "mouse is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->submit(event); !status.ok()) {
          return status;
        }
      }

      device.last_event = event;
      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  OperationStatus Mouse::move_relative(std::int32_t delta_x, std::int32_t delta_y) {
    return submit({.kind = MouseEventKind::relative_motion, .x = delta_x, .y = delta_y});
  }

  OperationStatus Mouse::move_absolute(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    return submit({.kind = MouseEventKind::absolute_motion, .x = x, .y = y, .width = width, .height = height});
  }

  OperationStatus Mouse::button(MouseButton button, bool pressed) {
    MouseEvent event;
    event.kind = MouseEventKind::button;
    event.button = button;
    event.pressed = pressed;
    return submit(event);
  }

  OperationStatus Mouse::vertical_scroll(std::int32_t distance) {
    MouseEvent event;
    event.kind = MouseEventKind::vertical_scroll;
    event.high_resolution_scroll = distance;
    return submit(event);
  }

  OperationStatus Mouse::horizontal_scroll(std::int32_t distance) {
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

  Touchscreen::Touchscreen(std::shared_ptr<detail::TouchscreenDevice> device):
      device_ {std::move(device)} {}

  Touchscreen::Touchscreen(Touchscreen &&) noexcept = default;
  Touchscreen &Touchscreen::operator=(Touchscreen &&) noexcept = default;
  Touchscreen::~Touchscreen() = default;

  DeviceId Touchscreen::device_id() const {
    return device_->id;
  }

  const DeviceProfile &Touchscreen::profile() const {
    return device_->options.profile;
  }

  bool Touchscreen::is_open() const {
    return with_device(device_, [](const auto &device) {
      return device.open;
    });
  }

  std::vector<DeviceNode> Touchscreen::device_nodes() const {
    return with_device(device_, [](const auto &device) {
      if (device.backend) {
        return device.backend->device_nodes();
      }
      return std::vector<DeviceNode> {};
    });
  }

  OperationStatus Touchscreen::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return OperationStatus::success();
      }

      auto status = OperationStatus::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  OperationStatus Touchscreen::place_contact(const TouchContact &contact) {
    if (const auto validation = validate_touch_contact(contact); !validation.ok()) {
      return validation;
    }

    return with_device(device_, [&contact](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "touchscreen is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->place_contact(contact); !status.ok()) {
          return status;
        }
      }

      device.last_contact = contact;
      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  OperationStatus Touchscreen::release_contact(std::int32_t contact_id) {
    if (contact_id < 0) {
      return OperationStatus::failure(ErrorCode::invalid_argument, "touch contact id must not be negative");
    }

    return with_device(device_, [contact_id](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "touchscreen is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->release_contact(contact_id); !status.ok()) {
          return status;
        }
      }

      device.last_contact.id = contact_id;
      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  TouchContact Touchscreen::last_submitted_contact() const {
    return with_device(device_, [](const auto &device) {
      return device.last_contact;
    });
  }

  std::size_t Touchscreen::submit_count() const {
    return with_device(device_, [](const auto &device) {
      return device.submitted_events;
    });
  }

  Trackpad::Trackpad(std::shared_ptr<detail::TrackpadDevice> device):
      device_ {std::move(device)} {}

  Trackpad::Trackpad(Trackpad &&) noexcept = default;
  Trackpad &Trackpad::operator=(Trackpad &&) noexcept = default;
  Trackpad::~Trackpad() = default;

  DeviceId Trackpad::device_id() const {
    return device_->id;
  }

  const DeviceProfile &Trackpad::profile() const {
    return device_->options.profile;
  }

  bool Trackpad::is_open() const {
    return with_device(device_, [](const auto &device) {
      return device.open;
    });
  }

  std::vector<DeviceNode> Trackpad::device_nodes() const {
    return with_device(device_, [](const auto &device) {
      if (device.backend) {
        return device.backend->device_nodes();
      }
      return std::vector<DeviceNode> {};
    });
  }

  OperationStatus Trackpad::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return OperationStatus::success();
      }

      auto status = OperationStatus::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  OperationStatus Trackpad::place_contact(const TouchContact &contact) {
    if (const auto validation = validate_touch_contact(contact); !validation.ok()) {
      return validation;
    }

    return with_device(device_, [&contact](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "trackpad is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->place_contact(contact); !status.ok()) {
          return status;
        }
      }

      device.last_contact = contact;
      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  OperationStatus Trackpad::release_contact(std::int32_t contact_id) {
    if (contact_id < 0) {
      return OperationStatus::failure(ErrorCode::invalid_argument, "touch contact id must not be negative");
    }

    return with_device(device_, [contact_id](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "trackpad is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->release_contact(contact_id); !status.ok()) {
          return status;
        }
      }

      device.last_contact.id = contact_id;
      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  OperationStatus Trackpad::button(bool pressed) {
    return with_device(device_, [pressed](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "trackpad is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->button(pressed); !status.ok()) {
          return status;
        }
      }

      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  TouchContact Trackpad::last_submitted_contact() const {
    return with_device(device_, [](const auto &device) {
      return device.last_contact;
    });
  }

  std::size_t Trackpad::submit_count() const {
    return with_device(device_, [](const auto &device) {
      return device.submitted_events;
    });
  }

  PenTablet::PenTablet(std::shared_ptr<detail::PenTabletDevice> device):
      device_ {std::move(device)} {}

  PenTablet::PenTablet(PenTablet &&) noexcept = default;
  PenTablet &PenTablet::operator=(PenTablet &&) noexcept = default;
  PenTablet::~PenTablet() = default;

  DeviceId PenTablet::device_id() const {
    return device_->id;
  }

  const DeviceProfile &PenTablet::profile() const {
    return device_->options.profile;
  }

  bool PenTablet::is_open() const {
    return with_device(device_, [](const auto &device) {
      return device.open;
    });
  }

  std::vector<DeviceNode> PenTablet::device_nodes() const {
    return with_device(device_, [](const auto &device) {
      if (device.backend) {
        return device.backend->device_nodes();
      }
      return std::vector<DeviceNode> {};
    });
  }

  OperationStatus PenTablet::close() {
    return with_device(device_, [](auto &device) {
      if (!device.open) {
        return OperationStatus::success();
      }

      auto status = OperationStatus::success();
      if (device.backend) {
        status = device.backend->close();
      }

      device.open = false;
      return status;
    });
  }

  OperationStatus PenTablet::place_tool(const PenToolState &state) {
    return with_device(device_, [&state](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "pen tablet is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->place_tool(state); !status.ok()) {
          return status;
        }
      }

      device.last_tool = state;
      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  OperationStatus PenTablet::button(PenButton button, bool pressed) {
    return with_device(device_, [button, pressed](auto &device) {
      if (!device.open) {
        return OperationStatus::failure(ErrorCode::device_closed, "pen tablet is closed");
      }

      if (device.backend) {
        if (const auto status = device.backend->button(button, pressed); !status.ok()) {
          return status;
        }
      }

      ++device.submitted_events;
      return OperationStatus::success();
    });
  }

  PenToolState PenTablet::last_submitted_tool() const {
    return with_device(device_, [](const auto &device) {
      return device.last_tool;
    });
  }

  std::size_t PenTablet::submit_count() const {
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

    return {OperationStatus::success(), std::unique_ptr<Gamepad> {new Gamepad {std::move(device)}}};
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

    return {OperationStatus::success(), std::unique_ptr<Keyboard> {new Keyboard {std::move(device)}}};
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

    return {OperationStatus::success(), std::unique_ptr<Mouse> {new Mouse {std::move(device)}}};
  }

  TouchscreenCreationResult Runtime::create_touchscreen() {
    CreateTouchscreenOptions options;
    options.profile = profiles::touchscreen();
    return create_touchscreen(options);
  }

  TouchscreenCreationResult Runtime::create_touchscreen(const CreateTouchscreenOptions &options) {
    if (const auto validation = validate_touchscreen_options(options); !validation.ok()) {
      return {validation, nullptr};
    }

    DeviceId id;
    {
      std::lock_guard lock {state_->mutex};
      id = state_->next_device_id++;
    }

    auto backend_result = state_->backend->create_touchscreen(id, options);
    if (!backend_result) {
      return {std::move(backend_result.status), nullptr};
    }

    auto device = std::make_shared<detail::TouchscreenDevice>(id, options, std::move(backend_result.touchscreen));
    {
      std::lock_guard lock {state_->mutex};
      state_->touchscreens.emplace_back(device);
    }

    return {OperationStatus::success(), std::unique_ptr<Touchscreen> {new Touchscreen {std::move(device)}}};
  }

  TrackpadCreationResult Runtime::create_trackpad() {
    CreateTrackpadOptions options;
    options.profile = profiles::trackpad();
    return create_trackpad(options);
  }

  TrackpadCreationResult Runtime::create_trackpad(const CreateTrackpadOptions &options) {
    if (const auto validation = validate_trackpad_options(options); !validation.ok()) {
      return {validation, nullptr};
    }

    DeviceId id;
    {
      std::lock_guard lock {state_->mutex};
      id = state_->next_device_id++;
    }

    auto backend_result = state_->backend->create_trackpad(id, options);
    if (!backend_result) {
      return {std::move(backend_result.status), nullptr};
    }

    auto device = std::make_shared<detail::TrackpadDevice>(id, options, std::move(backend_result.trackpad));
    {
      std::lock_guard lock {state_->mutex};
      state_->trackpads.emplace_back(device);
    }

    return {OperationStatus::success(), std::unique_ptr<Trackpad> {new Trackpad {std::move(device)}}};
  }

  PenTabletCreationResult Runtime::create_pen_tablet() {
    CreatePenTabletOptions options;
    options.profile = profiles::pen_tablet();
    return create_pen_tablet(options);
  }

  PenTabletCreationResult Runtime::create_pen_tablet(const CreatePenTabletOptions &options) {
    if (const auto validation = validate_pen_tablet_options(options); !validation.ok()) {
      return {validation, nullptr};
    }

    DeviceId id;
    {
      std::lock_guard lock {state_->mutex};
      id = state_->next_device_id++;
    }

    auto backend_result = state_->backend->create_pen_tablet(id, options);
    if (!backend_result) {
      return {std::move(backend_result.status), nullptr};
    }

    auto device = std::make_shared<detail::PenTabletDevice>(id, options, std::move(backend_result.pen_tablet));
    {
      std::lock_guard lock {state_->mutex};
      state_->pen_tablets.emplace_back(device);
    }

    return {OperationStatus::success(), std::unique_ptr<PenTablet> {new PenTablet {std::move(device)}}};
  }

  std::size_t Runtime::active_device_count() const {
    std::lock_guard lock {state_->mutex};
    return count_open_devices(state_->gamepads) + count_open_devices(state_->keyboards) + count_open_devices(state_->mice) +
           count_open_devices(state_->touchscreens) + count_open_devices(state_->trackpads) +
           count_open_devices(state_->pen_tablets);
  }

  void Runtime::close_all() {
    std::lock_guard lock {state_->mutex};
    close_devices(state_->gamepads);
    close_devices(state_->keyboards);
    close_devices(state_->mice);
    close_devices(state_->touchscreens);
    close_devices(state_->trackpads);
    close_devices(state_->pen_tablets);
  }

}  // namespace lvh
