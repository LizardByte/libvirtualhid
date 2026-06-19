/**
 * @file src/core/backend.cpp
 * @brief Internal fake backend and backend selection definitions.
 */

// standard includes
#include <memory>
#include <utility>
#include <vector>

// local includes
#include "core/backend.hpp"

namespace lvh::detail {
  namespace {

    /**
     * @brief In-memory gamepad backend used for portable tests.
     */
    class FakeGamepad final: public BackendGamepad {
    public:
      OperationStatus submit(const std::vector<std::uint8_t> & /*report*/) override {
        return OperationStatus::success();
      }

      void set_output_callback(OutputCallback callback) override {
        output_callback_ = std::move(callback);
      }

      OperationStatus close() override {
        return OperationStatus::success();
      }

    private:
      OutputCallback output_callback_;
    };

    /**
     * @brief In-memory keyboard backend used for portable tests.
     */
    class FakeKeyboard final: public BackendKeyboard {
    public:
      OperationStatus submit(const KeyboardEvent & /*event*/) override {
        return OperationStatus::success();
      }

      OperationStatus type_text(const KeyboardTextEvent & /*event*/) override {
        return OperationStatus::success();
      }

      OperationStatus close() override {
        return OperationStatus::success();
      }
    };

    /**
     * @brief In-memory mouse backend used for portable tests.
     */
    class FakeMouse final: public BackendMouse {
    public:
      OperationStatus submit(const MouseEvent & /*event*/) override {
        return OperationStatus::success();
      }

      OperationStatus close() override {
        return OperationStatus::success();
      }
    };

    /**
     * @brief In-memory backend used by default for API validation.
     */
    class FakeBackend final: public Backend {
    public:
      FakeBackend() {
        capabilities_.backend_name = "fake";
        capabilities_.supports_gamepad = true;
        capabilities_.supports_keyboard = true;
        capabilities_.supports_mouse = true;
        capabilities_.supports_output_reports = true;
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId /*id*/, const CreateGamepadOptions & /*options*/) override {
        return {OperationStatus::success(), std::make_unique<FakeGamepad>()};
      }

      BackendKeyboardCreationResult create_keyboard(
        DeviceId /*id*/,
        const CreateKeyboardOptions & /*options*/
      ) override {
        return {OperationStatus::success(), std::make_unique<FakeKeyboard>()};
      }

      BackendMouseCreationResult create_mouse(DeviceId /*id*/, const CreateMouseOptions & /*options*/) override {
        return {OperationStatus::success(), std::make_unique<FakeMouse>()};
      }

    private:
      BackendCapabilities capabilities_;
    };

  }  // namespace

  std::unique_ptr<Backend> create_backend(BackendKind kind) {
    if (kind == BackendKind::fake) {
      return std::make_unique<FakeBackend>();
    }

    return create_platform_backend();
  }

}  // namespace lvh::detail
