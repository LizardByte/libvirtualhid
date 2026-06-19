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
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

      BackendKeyboardCreationResult create_keyboard(
        DeviceId /*id*/,
        const CreateKeyboardOptions & /*options*/
      ) override {
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

      BackendMouseCreationResult create_mouse(DeviceId /*id*/, const CreateMouseOptions & /*options*/) override {
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

      BackendTouchscreenCreationResult create_touchscreen(
        DeviceId /*id*/,
        const CreateTouchscreenOptions & /*options*/
      ) override {
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

      BackendTrackpadCreationResult create_trackpad(DeviceId /*id*/, const CreateTrackpadOptions & /*options*/) override {
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

      BackendPenTabletCreationResult create_pen_tablet(
        DeviceId /*id*/,
        const CreatePenTabletOptions & /*options*/
      ) override {
        return {OperationStatus::failure(ErrorCode::backend_unavailable, "platform backend is not implemented yet"), nullptr};
      }

    private:
      BackendCapabilities capabilities_;
    };

  }  // namespace

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<UnsupportedBackend>();
  }

}  // namespace lvh::detail
