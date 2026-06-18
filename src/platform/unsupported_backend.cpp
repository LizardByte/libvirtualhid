/**
 * @file src/platform/unsupported_backend.cpp
 * @brief Unsupported platform backend definitions.
 */

// standard includes
#include <memory>

// local includes
#include "core/backend.hpp"

namespace lvh::detail {
  namespace {

    /**
     * @brief Platform backend used when no native implementation exists.
     */
    class UnsupportedBackend final: public Backend {
    public:
      UnsupportedBackend() {
        capabilities_.backend_name = "platform-default-unimplemented";
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId /*id*/, const CreateGamepadOptions & /*options*/) override {
        return {Status::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

      BackendKeyboardCreationResult create_keyboard(
        DeviceId /*id*/,
        const CreateKeyboardOptions & /*options*/
      ) override {
        return {Status::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

      BackendMouseCreationResult create_mouse(DeviceId /*id*/, const CreateMouseOptions & /*options*/) override {
        return {Status::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

    private:
      BackendCapabilities capabilities_;
    };

  }  // namespace

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<UnsupportedBackend>();
  }

}  // namespace lvh::detail
