/**
 * @file src/platform/linux/output_mapping.hpp
 * @brief Compositor output assignment for virtual absolute input devices.
 */
#pragma once

// standard includes
#include <string>
#include <string_view>

// lib includes
#include <libvirtualhid/types.hpp>

namespace lvh::detail {
  /**
   * @brief Cache output assignment for one owned Linux event device.
   */
  class OutputMapping {
  public:
    /**
     * @brief Assign an absolute input device to a native display when supported.
     *
     * KDE Wayland exposes output assignment through its session D-Bus service.
     * Other compositors and builds without GIO preserve their existing mapping.
     * Successful assignments are cached; failed assignments can be retried.
     *
     * @param devnode Event device owned by the caller, such as `/dev/input/event7`.
     * @param output_name Native connector name, or empty to preserve defaults.
     * @param tablet Whether workspace-wide tablet mapping must be disabled.
     * @return Success, or a failed KDE assignment before input is submitted.
     */
    OperationStatus update(std::string_view devnode, const std::string &output_name, bool tablet);

  private:
    std::string devnode_;  ///< Event node whose mapping was last applied.
    std::string output_name_;  ///< Last successfully requested output.
  };
}  // namespace lvh::detail
