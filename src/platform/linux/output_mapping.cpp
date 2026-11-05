/**
 * @file src/platform/linux/output_mapping.cpp
 * @brief KDE Wayland output assignment for virtual touch and pen devices.
 */

// standard includes
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

// lib includes
#if defined(LIBVIRTUALHID_HAVE_GIO)
  #include <gio/gio.h>
#endif

// local includes
#include "output_mapping.hpp"

namespace lvh::detail {
#if defined(LIBVIRTUALHID_HAVE_GIO)
  namespace {
    constexpr auto kwin_service = "org.kde.KWin";  ///< Compositor session service.
    constexpr auto device_interface = "org.kde.KWin.InputDevice";  ///< Device properties.
    constexpr int call_timeout_ms = 100;  ///< Bound each compositor request.

    /**
     * @brief Write one KWin property without activating an absent compositor.
     * @param connection Session bus connection.
     * @param path Owned event device object path.
     * @param name Property name.
     * @param value Property value, consumed by the request.
     * @param error Receives a D-Bus error on failure.
     * @return True if KWin accepted the property.
     */
    bool set_property(GDBusConnection *connection, const std::string &path, const char *name, GVariant *value, GError **error) {
      g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        connection,
        kwin_service,
        path.c_str(),
        "org.freedesktop.DBus.Properties",
        "Set",
        g_variant_new("(ssv)", device_interface, name, value),
        nullptr,
        G_DBUS_CALL_FLAGS_NO_AUTO_START,
        call_timeout_ms,
        nullptr,
        error
      );
      return reply != nullptr;
    }

    /**
     * @brief Apply output assignment after KWin registers a newly created device.
     * @param connection Session bus connection.
     * @param path Owned event device object path.
     * @param output_name Connector name.
     * @param tablet Whether to disable workspace-wide tablet mapping.
     * @return Success or the compositor error.
     */
    OperationStatus assign_output(GDBusConnection *connection, const std::string &path, const std::string &output_name, bool tablet) {
      for (int attempt = 0;; ++attempt) {
        g_autoptr(GError) error = nullptr;
        if (set_property(connection, path, "outputName", g_variant_new_string(output_name.c_str()), &error) && (!tablet || set_property(connection, path, "mapToWorkspace", g_variant_new_boolean(false), &error))) {
          break;
        }
        if (const bool registration_pending = g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_OBJECT) || g_error_matches(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD); !registration_pending || attempt == 9) {
          return OperationStatus::failure(ErrorCode::backend_failure, "failed to map virtual input to " + output_name + ": " + error->message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      return OperationStatus::success();
    }

    /**
     * @brief Apply the display hint when the session is managed by KWin Wayland.
     * @param devnode Owned event node.
     * @param output_name Native display connector.
     * @param tablet Whether workspace-wide tablet mapping must be disabled.
     * @return Success for unsupported sessions, or the KDE mapping result.
     */
    OperationStatus assign_if_supported(std::string_view devnode, const std::string &output_name, bool tablet) {
      g_autoptr(GError) error = nullptr;
      g_autoptr(GDBusConnection) connection = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
      if (!connection) {
        return OperationStatus::success();
      }
      // This manager is exposed by KWin's Wayland input backend. Detect it
      // directly because a user service may not inherit WAYLAND_DISPLAY.
      if (g_autoptr(GVariant) manager = g_dbus_connection_call_sync(connection, kwin_service, "/org/kde/KWin/InputDevice", "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", "org.kde.KWin.InputDeviceManager", "devicesSysNames"), G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NO_AUTO_START, call_timeout_ms, nullptr, nullptr); !manager) {
        return OperationStatus::success();
      }
      if (constexpr std::string_view prefix = "/dev/input/event"; !devnode.starts_with(prefix) || devnode.size() == prefix.size() || !std::ranges::all_of(devnode.substr(prefix.size()), [](char c) {
            return c >= '0' && c <= '9';
          }) || !g_utf8_validate(output_name.c_str(), static_cast<gssize>(output_name.size()), nullptr)) {
        return OperationStatus::failure(ErrorCode::invalid_argument, "invalid virtual input device node or display name");
      }
      const auto path = "/org/kde/KWin/InputDevice/" + std::string {devnode.substr(std::string_view {"/dev/input/"}.size())};
      return assign_output(connection, path, output_name, tablet);
    }
  }  // namespace
#endif

  OperationStatus OutputMapping::update(std::string_view devnode, const std::string &output_name, bool tablet) {
    if (output_name.empty() || (devnode == devnode_ && output_name == output_name_)) {
      return OperationStatus::success();
    }
#if defined(LIBVIRTUALHID_HAVE_GIO)
    if (const auto status = assign_if_supported(devnode, output_name, tablet); !status.ok()) {
      return status;
    }
#else
    static_cast<void>(tablet);
#endif
    devnode_ = devnode;
    output_name_ = output_name;
    return OperationStatus::success();
  }
}  // namespace lvh::detail
