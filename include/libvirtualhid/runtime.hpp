#pragma once

#include <libvirtualhid/types.hpp>

#include <cstddef>
#include <memory>
#include <vector>

namespace lvh {

namespace detail {
struct GamepadDevice;
class RuntimeState;
}  // namespace detail

class VirtualDevice {
public:
  virtual ~VirtualDevice() = default;

  virtual DeviceId device_id() const = 0;
  virtual const DeviceProfile& profile() const = 0;
  virtual bool is_open() const = 0;
  virtual Status close() = 0;
};

class Gamepad final: public VirtualDevice {
public:
  Gamepad(const Gamepad&) = delete;
  Gamepad& operator=(const Gamepad&) = delete;
  Gamepad(Gamepad&&) noexcept;
  Gamepad& operator=(Gamepad&&) noexcept;
  ~Gamepad() override;

  DeviceId device_id() const override;
  const DeviceProfile& profile() const override;
  const GamepadMetadata& metadata() const;
  bool is_open() const override;
  Status close() override;

  Status submit(const GamepadState& state);
  void set_output_callback(OutputCallback callback);
  Status dispatch_output(const GamepadOutput& output);

  GamepadState last_submitted_state() const;
  std::vector<std::uint8_t> last_input_report() const;
  std::size_t submit_count() const;

private:
  friend class Runtime;

  explicit Gamepad(std::shared_ptr<detail::GamepadDevice> device);

  std::shared_ptr<detail::GamepadDevice> device_;
};

struct GamepadCreationResult {
  Status status;
  std::unique_ptr<Gamepad> gamepad;

  explicit operator bool() const {
    return status.ok() && gamepad != nullptr;
  }
};

class Runtime final {
public:
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  Runtime(Runtime&&) noexcept;
  Runtime& operator=(Runtime&&) noexcept;
  ~Runtime();

  static std::unique_ptr<Runtime> create(RuntimeOptions options = {});

  const BackendCapabilities& capabilities() const;
  BackendKind backend_kind() const;
  GamepadCreationResult create_gamepad(const DeviceProfile& profile);
  GamepadCreationResult create_gamepad(const CreateGamepadOptions& options);
  std::size_t active_device_count() const;
  void close_all();

private:
  explicit Runtime(RuntimeOptions options);

  std::shared_ptr<detail::RuntimeState> state_;
};

}  // namespace lvh
