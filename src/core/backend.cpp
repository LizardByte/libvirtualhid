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
      Status submit(const std::vector<std::uint8_t> & /*report*/) override {
        return Status::success();
      }

      void set_output_callback(OutputCallback callback) override {
        output_callback_ = std::move(callback);
      }

      Status close() override {
        return Status::success();
      }

    private:
      OutputCallback output_callback_;
    };

    /**
     * @brief In-memory backend used by default for API validation.
     */
    class FakeBackend final: public Backend {
    public:
      FakeBackend() {
        capabilities_.backend_name = "fake";
        capabilities_.supports_gamepad = true;
        capabilities_.supports_output_reports = true;
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId /*id*/, const CreateGamepadOptions & /*options*/) override {
        return {Status::success(), std::make_unique<FakeGamepad>()};
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
