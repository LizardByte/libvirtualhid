#include <libvirtualhid/runtime.hpp>

#include <libvirtualhid/report.hpp>

#include <algorithm>
#include <mutex>
#include <utility>

namespace lvh::detail {

struct GamepadDevice {
  explicit GamepadDevice(DeviceId device_id, CreateGamepadOptions create_options):
      id {device_id},
      options {std::move(create_options)} {}

  DeviceId id;
  CreateGamepadOptions options;
  bool open = true;
  GamepadState last_state;
  std::vector<std::uint8_t> last_report;
  std::size_t submitted_reports = 0;
  OutputCallback output_callback;
  mutable std::mutex mutex;
};

class RuntimeState {
public:
  explicit RuntimeState(RuntimeOptions runtime_options):
      options {runtime_options},
      caps {make_capabilities(runtime_options.backend)} {}

  static BackendCapabilities make_capabilities(BackendKind kind) {
    BackendCapabilities capabilities;
    if(kind == BackendKind::fake) {
      capabilities.backend_name = "fake";
      capabilities.supports_gamepad = true;
      capabilities.supports_output_reports = true;
    }
    else {
      capabilities.backend_name = "platform-default-unimplemented";
    }
    return capabilities;
  }

  RuntimeOptions options;
  BackendCapabilities caps;
  DeviceId next_device_id = 1;
  std::vector<std::weak_ptr<GamepadDevice>> gamepads;
  mutable std::mutex mutex;
};

}  // namespace lvh::detail

namespace lvh {
namespace {

Status validate_gamepad_options(const CreateGamepadOptions& options) {
  if(options.profile.device_type != DeviceType::gamepad) {
    return Status::failure(ErrorCode::unsupported_profile, "device profile is not a gamepad");
  }
  if(options.profile.name.empty()) {
    return Status::failure(ErrorCode::invalid_argument, "device profile name must not be empty");
  }
  if(options.profile.report_descriptor.empty()) {
    return Status::failure(ErrorCode::invalid_argument, "device profile report descriptor must not be empty");
  }
  if(options.profile.report_id == 0) {
    return Status::failure(ErrorCode::invalid_argument, "device profile report id must not be zero");
  }
  if(options.profile.input_report_size == 0) {
    return Status::failure(ErrorCode::invalid_argument, "device profile input report size must not be zero");
  }

  return Status::success();
}

template<class Func>
auto with_device(const std::shared_ptr<detail::GamepadDevice>& device, Func&& func) {
  std::lock_guard lock {device->mutex};
  return func(*device);
}

}  // namespace

Gamepad::Gamepad(std::shared_ptr<detail::GamepadDevice> device):
    device_ {std::move(device)} {}

Gamepad::Gamepad(Gamepad&&) noexcept = default;
Gamepad& Gamepad::operator=(Gamepad&&) noexcept = default;
Gamepad::~Gamepad() = default;

DeviceId Gamepad::device_id() const {
  return device_->id;
}

const DeviceProfile& Gamepad::profile() const {
  return device_->options.profile;
}

const GamepadMetadata& Gamepad::metadata() const {
  return device_->options.metadata;
}

bool Gamepad::is_open() const {
  return with_device(device_, [](const auto& device) {
    return device.open;
  });
}

Status Gamepad::close() {
  return with_device(device_, [](auto& device) {
    if(!device.open) {
      return Status::success();
    }
    device.open = false;
    return Status::success();
  });
}

Status Gamepad::submit(const GamepadState& state) {
  return with_device(device_, [&state](auto& device) {
    if(!device.open) {
      return Status::failure(ErrorCode::device_closed, "gamepad is closed");
    }

    auto report = reports::pack_input_report(device.options.profile, state);
    if(report.empty()) {
      return Status::failure(ErrorCode::backend_failure, "failed to pack gamepad input report");
    }

    device.last_state = reports::normalize_state(state);
    device.last_report = std::move(report);
    ++device.submitted_reports;
    return Status::success();
  });
}

void Gamepad::set_output_callback(OutputCallback callback) {
  with_device(device_, [&callback](auto& device) {
    device.output_callback = std::move(callback);
    return 0;
  });
}

Status Gamepad::dispatch_output(const GamepadOutput& output) {
  OutputCallback callback;
  const auto status = with_device(device_, [&callback](auto& device) {
    if(!device.open) {
      return Status::failure(ErrorCode::device_closed, "gamepad is closed");
    }
    callback = device.output_callback;
    return Status::success();
  });

  if(!status.ok()) {
    return status;
  }
  if(callback) {
    callback(output);
  }
  return Status::success();
}

GamepadState Gamepad::last_submitted_state() const {
  return with_device(device_, [](const auto& device) {
    return device.last_state;
  });
}

std::vector<std::uint8_t> Gamepad::last_input_report() const {
  return with_device(device_, [](const auto& device) {
    return device.last_report;
  });
}

std::size_t Gamepad::submit_count() const {
  return with_device(device_, [](const auto& device) {
    return device.submitted_reports;
  });
}

Runtime::Runtime(RuntimeOptions options):
    state_ {std::make_shared<detail::RuntimeState>(options)} {}

Runtime::Runtime(Runtime&&) noexcept = default;
Runtime& Runtime::operator=(Runtime&&) noexcept = default;
Runtime::~Runtime() = default;

std::unique_ptr<Runtime> Runtime::create(RuntimeOptions options) {
  return std::unique_ptr<Runtime> {new Runtime {options}};
}

const BackendCapabilities& Runtime::capabilities() const {
  return state_->caps;
}

BackendKind Runtime::backend_kind() const {
  return state_->options.backend;
}

GamepadCreationResult Runtime::create_gamepad(const DeviceProfile& profile) {
  CreateGamepadOptions options;
  options.profile = profile;
  return create_gamepad(options);
}

GamepadCreationResult Runtime::create_gamepad(const CreateGamepadOptions& options) {
  if(state_->options.backend != BackendKind::fake) {
    return {Status::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
  }

  if(const auto validation = validate_gamepad_options(options); !validation.ok()) {
    return {validation, nullptr};
  }

  std::lock_guard lock {state_->mutex};
  const auto id = state_->next_device_id++;
  auto device = std::make_shared<detail::GamepadDevice>(id, options);
  state_->gamepads.emplace_back(device);
  return {Status::success(), std::unique_ptr<Gamepad> {new Gamepad {std::move(device)}}};
}

std::size_t Runtime::active_device_count() const {
  std::lock_guard lock {state_->mutex};
  std::size_t count = 0;
  for(const auto& weak_device : state_->gamepads) {
    if(const auto device = weak_device.lock()) {
      if(device->open) {
        ++count;
      }
    }
  }
  return count;
}

void Runtime::close_all() {
  std::lock_guard lock {state_->mutex};
  for(const auto& weak_device : state_->gamepads) {
    if(const auto device = weak_device.lock()) {
      std::lock_guard device_lock {device->mutex};
      device->open = false;
    }
  }
}

}  // namespace lvh
