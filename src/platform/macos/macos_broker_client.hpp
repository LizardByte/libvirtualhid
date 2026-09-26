/**
 * @file src/platform/macos/macos_broker_client.hpp
 * @brief Internal macOS broker gamepad creation entry point.
 */
#pragma once

#include "core/backend.hpp"

namespace lvh::detail {
  BackendGamepadCreationResult create_macos_brokered_gamepad(DeviceId id, const CreateGamepadOptions &options);
}
