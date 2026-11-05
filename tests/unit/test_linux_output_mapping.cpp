/**
 * @file tests/unit/test_linux_output_mapping.cpp
 * @brief Exercise output assignment against an isolated KWin D-Bus service.
 */

// test includes
#include "fixtures/fixtures.hpp"
#include "fixtures/linux_backend_test_hooks.hpp"

// standard includes
#include <algorithm>
#include <array>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// platform includes
#include <linux/input.h>

// lib includes
#include <gio/gio.h>

// local includes
#include "platform/linux/output_mapping.hpp"

namespace {
  /**
   * @brief Properties of one compositor input device.
   */
  struct DeviceMapping {
    std::string output = "HDMI-A-1";  ///< Initially selected output.
    bool workspace = true;  ///< Initial tablet workspace mapping.
    int assignments = 0;  ///< Successful output assignments.
  };

  /**
   * @brief Private bus and compositor service, independent of installed devices.
   */
  class KWinMappingTest: public testing::Test {
  protected:
    /**
     * @brief Start an isolated bus and wait until the compositor service is ready.
     */
    void SetUp() override {
      bus_ = g_test_dbus_new(G_TEST_DBUS_NONE);
      g_test_dbus_up(bus_);
      g_setenv("WAYLAND_DISPLAY", "wayland-test", true);
      context_ = g_main_context_new();
      loop_ = g_main_loop_new(context_, false);
      std::promise<void> ready;
      worker_ = std::jthread {[this, &ready] {
        run_service(ready);
      }};
      ready.get_future().wait();
      ASSERT_TRUE(startup_error_.empty()) << startup_error_;
    }

    /**
     * @brief Stop the service thread and release the isolated bus.
     */
    void TearDown() override {
      g_main_context_invoke(context_, [](gpointer data) {
        g_main_loop_quit(static_cast<GMainLoop *>(data));
        return G_SOURCE_REMOVE;
      },
                            loop_);
      worker_.join();
      g_main_loop_unref(loop_);
      g_main_context_unref(context_);
      g_unsetenv("WAYLAND_DISPLAY");
      g_test_dbus_down(bus_);
      g_object_unref(bus_);
    }

  public:
    /**
     * @brief Register one virtual device in the compositor's event-node namespace.
     * @param name Event node basename.
     */
    void register_device(const char *name) {
      const auto path = std::string {"/org/kde/KWin/InputDevice/"} + name;
      devices_.try_emplace(path);
      static const GDBusInterfaceVTable vtable {
        .method_call = nullptr,
        .get_property = [](GDBusConnection *, const gchar *, const gchar *path, const gchar *, const gchar *property, GError **, gpointer data) {
          const auto &device = static_cast<KWinMappingTest *>(data)->devices_.at(path);
          return std::string_view {property} == "outputName" ? g_variant_new_string(device.output.c_str()) : g_variant_new_boolean(device.workspace);
        },
        // GDBus fixes the callback prefix; forward its property payload to a typed handler.
        .set_property = [](GDBusConnection *, const gchar *, const gchar *path, const gchar *, auto... property_args) {
          return set_device_property(path, property_args...);
        },
        .padding = {},
      };
      registrations_.push_back(g_dbus_connection_register_object(connection_, path.c_str(), introspection_->interfaces[0], &vtable, this, nullptr, nullptr));
    }

    /**
     * @brief Remove the Wayland manager while keeping the compositor service alive.
     */
    void unregister_manager() {
      g_dbus_connection_unregister_object(connection_, manager_id_);
      registrations_.erase(registrations_.begin());
    }

    /**
     * @brief Run an action on the compositor service thread and wait for completion.
     * @param action Action that reads or changes service state.
     */
    template<typename Action>
    void on_service(Action action) {
      std::packaged_task<void()> task {std::move(action)};
      auto completed = task.get_future();
      g_main_context_invoke(context_, [](gpointer data) {
        (*static_cast<std::packaged_task<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
                            &task);
      completed.get();
    }

    /**
     * @brief Read the observable mapping of one virtual device.
     * @param name Event node basename.
     * @return Snapshot copied on the compositor service thread.
     */
    DeviceMapping device(const char *name) {
      DeviceMapping result;
      on_service([this, &result, name] {
        result = devices_.at(std::string {"/org/kde/KWin/InputDevice/"} + name);
      });
      return result;
    }

    /**
     * @brief Access the output assignment cache under test.
     *
     * @return Mutable mapping instance owned by this fixture.
     */
    lvh::detail::OutputMapping &mapping() {
      return mapping_;
    }

    /**
     * @brief Access the service context when scheduling device registration.
     *
     * @return Borrowed service event context.
     */
    GMainContext *service_context() const {
      return context_;
    }

    /**
     * @brief Access the service bus connection from a service-thread action.
     *
     * @return Borrowed compositor connection.
     */
    GDBusConnection *service_connection() const {
      return connection_;
    }

    /**
     * @brief Configure property write rejection on the service thread.
     *
     * @param property Property to reject, or empty to accept all writes.
     * @param code D-Bus error returned for the rejected property.
     */
    void set_rejection(std::string property = {}, GDBusError code = G_DBUS_ERROR_ACCESS_DENIED) {
      on_service([this, rejected_property = std::move(property), code] {
        reject_property_ = rejected_property;
        reject_code_ = code;
      });
    }

  private:
    /**
     * @brief Serve compositor requests on the worker's thread-default context.
     *
     * @param ready Promise completed after service startup or a startup failure.
     */
    void run_service(std::promise<void> &ready) {
      g_main_context_push_thread_default(context_);
      GError *error = nullptr;
      connection_ = g_dbus_connection_new_for_address_sync(
        g_test_dbus_get_bus_address(bus_),
        GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
        nullptr,
        nullptr,
        &error
      );
      if (!connection_) {
        startup_error_ = error->message;
        g_error_free(error);
        ready.set_value();
        g_main_context_pop_thread_default(context_);
        return;
      }
      g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        connection_,
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "RequestName",
        g_variant_new("(su)", "org.kde.KWin", 0U),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        &error
      );
      if (error) {
        startup_error_ = error->message;
        g_error_free(error);
      }
      introspection_ = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.kde.KWin.InputDevice'>"
        "<property name='outputName' type='s' access='readwrite'/>"
        "<property name='mapToWorkspace' type='b' access='readwrite'/>"
        "</interface></node>",
        nullptr
      );
      g_autoptr(GDBusNodeInfo) manager_info = g_dbus_node_info_new_for_xml(
        "<node><interface name='org.kde.KWin.InputDeviceManager'>"
        "<property name='devicesSysNames' type='as' access='read'/></interface></node>",
        nullptr
      );
      static const GDBusInterfaceVTable manager_vtable {
        .method_call = nullptr,
        .get_property = [](GDBusConnection *, const gchar *, const gchar *, const gchar *, const gchar *, GError **, gpointer) {
          const std::array nodes {"event7", "event8"};
          return g_variant_new_strv(nodes.data(), static_cast<gssize>(nodes.size()));
        },
        .set_property = nullptr,
        .padding = {},
      };
      manager_id_ = g_dbus_connection_register_object(connection_, "/org/kde/KWin/InputDevice", manager_info->interfaces[0], &manager_vtable, this, nullptr, nullptr);
      registrations_.push_back(manager_id_);
      register_device("event7");
      register_device("event8");
      ready.set_value();
      g_main_loop_run(loop_);
      for (const auto id : registrations_) {
        g_dbus_connection_unregister_object(connection_, id);
      }
      g_dbus_node_info_unref(introspection_);
      g_dbus_connection_close_sync(connection_, nullptr, nullptr);
      g_clear_object(&connection_);
      g_main_context_pop_thread_default(context_);
    }

    /**
     * @brief Apply a property write to one registered compositor device.
     *
     * @param path Event device object path.
     * @param property Property being updated.
     * @param value New property value.
     * @param error Receives a configured rejection error.
     * @param data Fixture owning the registered device.
     * @return Whether the property write succeeded.
     */
    static gboolean set_device_property(const gchar *path, const gchar *property, GVariant *value, GError **error, gpointer data) {
      auto &self = *static_cast<KWinMappingTest *>(data);
      if (self.reject_property_ == property) {
        g_set_error_literal(error, G_DBUS_ERROR, self.reject_code_, "mapping denied");
        return false;
      }
      auto &device = self.devices_.at(path);
      if (std::string_view {property} == "outputName") {
        device.output = g_variant_get_string(value, nullptr);
        ++device.assignments;
      } else {
        device.workspace = g_variant_get_boolean(value);
      }
      return true;
    }

    guint manager_id_ = 0;  ///< Wayland input manager registration.
    lvh::detail::OutputMapping mapping_;  ///< Mapping under test.
    GDBusError reject_code_ = G_DBUS_ERROR_ACCESS_DENIED;  ///< Failure returned for a rejected property.
    std::string reject_property_;  ///< Property rejected by the fake compositor.
    GMainContext *context_ = nullptr;  ///< Service event context.
    GDBusConnection *connection_ = nullptr;  ///< Service bus connection.

    GTestDBus *bus_ = nullptr;  ///< Private D-Bus daemon.
    GMainLoop *loop_ = nullptr;  ///< Service event loop.
    GDBusNodeInfo *introspection_ = nullptr;  ///< Device property interface.
    std::vector<guint> registrations_;  ///< Objects registered in the private bus.
    std::map<std::string, DeviceMapping, std::less<>> devices_;  ///< Compositor device state.
    std::string startup_error_;  ///< Service startup diagnostic.
    std::jthread worker_;  ///< Compositor service worker.
  };

  TEST_F(KWinMappingTest, MapsOnlyTheOwnedTouchscreenAndCachesTheAssignment) {
    ASSERT_TRUE(mapping().update("/dev/input/event7", "DP-1", false).ok());
    EXPECT_EQ(device("event7").output, "DP-1");
    EXPECT_EQ(device("event8").output, "HDMI-A-1");
    EXPECT_TRUE(device("event7").workspace);
    ASSERT_TRUE(mapping().update("/dev/input/event7", "DP-1", false).ok());
    EXPECT_EQ(device("event7").assignments, 1);
    ASSERT_TRUE(mapping().update("/dev/input/event7", "HDMI-A-1", false).ok());
    EXPECT_EQ(device("event7").output, "HDMI-A-1");
    EXPECT_EQ(device("event7").assignments, 2);
    ASSERT_TRUE(mapping().update("/dev/input/event8", "HDMI-A-1", false).ok());
    EXPECT_EQ(device("event8").assignments, 1);
  }

  TEST_F(KWinMappingTest, MapsPenToOneOutputAndDisablesWorkspaceMapping) {
    ASSERT_TRUE(mapping().update("/dev/input/event7", "DP-1", true).ok());
    EXPECT_EQ(device("event7").output, "DP-1");
    EXPECT_FALSE(device("event7").workspace);
    EXPECT_TRUE(device("event8").workspace);
  }

  TEST_F(KWinMappingTest, LeavesEmptyTargetsAndSessionsWithoutInputManagerUnchanged) {
    EXPECT_TRUE(mapping().update("/dev/input/event7", "", false).ok());
    EXPECT_EQ(device("event7").assignments, 0);
    on_service([this] {
      unregister_manager();
    });
    EXPECT_TRUE(mapping().update("/dev/input/event7", "DP-1", false).ok());
    EXPECT_EQ(device("event7").assignments, 0);
  }

  TEST_F(KWinMappingTest, MapsWaylandDeviceWithoutWaylandDisplayEnvironment) {
    g_unsetenv("WAYLAND_DISPLAY");
    EXPECT_TRUE(mapping().update("/dev/input/event7", "DP-1", false).ok());
    EXPECT_EQ(device("event7").output, "DP-1");
  }

  TEST_F(KWinMappingTest, LeavesOtherCompositorsUnchanged) {
    on_service([this] {
      g_autoptr(GVariant) reply = g_dbus_connection_call_sync(
        service_connection(),
        "org.freedesktop.DBus",
        "/org/freedesktop/DBus",
        "org.freedesktop.DBus",
        "ReleaseName",
        g_variant_new("(s)", "org.kde.KWin"),
        nullptr,
        G_DBUS_CALL_FLAGS_NONE,
        1000,
        nullptr,
        nullptr
      );
    });
    EXPECT_TRUE(mapping().update("/dev/input/event7", "DP-1", false).ok());
    EXPECT_EQ(device("event7").assignments, 0);
  }

  TEST_F(KWinMappingTest, RejectsInvalidDeviceNodesAndDisplayNames) {
    for (const auto *node : {"", "/dev/input/event", "/dev/input/event7/other", "/not-an-input-device/event7", "/dev/input/event-1", "/dev/input/eventz"}) {
      EXPECT_EQ(mapping().update(node, "DP-1", false).code(), lvh::ErrorCode::invalid_argument) << node;
    }
    EXPECT_EQ(mapping().update("/dev/input/event7", std::string(1, '\xff'), false).code(), lvh::ErrorCode::invalid_argument);
    EXPECT_EQ(mapping().update("/dev/input/event7", std::string {"DP-1\0x", 6}, false).code(), lvh::ErrorCode::invalid_argument);
    EXPECT_EQ(device("event7").assignments, 0);
  }

  TEST_F(KWinMappingTest, RetriesFailedAssignments) {
    set_rejection("outputName");
    auto status = mapping().update("/dev/input/event7", "DP-1", false);
    EXPECT_EQ(status.code(), lvh::ErrorCode::backend_failure);
    EXPECT_NE(status.message().find("mapping denied"), std::string::npos);
    EXPECT_EQ(device("event7").output, "HDMI-A-1");
    set_rejection();
    EXPECT_TRUE(mapping().update("/dev/input/event7", "DP-1", false).ok());
    EXPECT_EQ(device("event7").output, "DP-1");
  }

  TEST_F(KWinMappingTest, ReportsTabletWorkspaceMappingFailureAndRetries) {
    set_rejection("mapToWorkspace");
    EXPECT_EQ(mapping().update("/dev/input/event7", "DP-1", true).code(), lvh::ErrorCode::backend_failure);
    EXPECT_TRUE(device("event7").workspace);
    set_rejection();
    EXPECT_TRUE(mapping().update("/dev/input/event7", "DP-1", true).ok());
    EXPECT_FALSE(device("event7").workspace);
  }

  TEST_F(KWinMappingTest, WaitsForNewDeviceRegistration) {
    GSource *source = g_timeout_source_new(15);
    g_source_set_callback(source, [](gpointer data) {
      static_cast<KWinMappingTest *>(data)->register_device("event9");
      return G_SOURCE_REMOVE;
    },
                          this,
                          nullptr);
    g_source_attach(source, service_context());
    g_source_unref(source);
    EXPECT_TRUE(mapping().update("/dev/input/event9", "DP-1", false).ok());
    EXPECT_EQ(device("event9").output, "DP-1");
  }

  TEST_F(KWinMappingTest, BoundsTheWaitWhenCompositorReportsUnknownObject) {
    set_rejection("outputName", G_DBUS_ERROR_UNKNOWN_OBJECT);
    EXPECT_EQ(mapping().update("/dev/input/event7", "DP-1", false).code(), lvh::ErrorCode::backend_failure);
    EXPECT_EQ(device("event7").assignments, 0);
  }

  TEST_F(KWinMappingTest, BoundsTheWaitForAnUnregisteredDevice) {
    EXPECT_EQ(mapping().update("/dev/input/event404", "DP-1", false).code(), lvh::ErrorCode::backend_failure);
    EXPECT_EQ(device("event7").assignments, 0);
  }

  TEST_F(KWinMappingTest, TouchAndPenKeepMonitorLocalCoordinatesAfterAssignment) {
    lvh::TouchContact contact;
    contact.id = 1;
    contact.x = 0.5F;
    contact.y = 0.25F;
    contact.viewport = {.offset_x = 1920, .width = 1920, .height = 1080, .output_name = "DP-1"};
    const auto touch = lvh::detail::test::linux_uinput_mapped_contact(contact);
    ASSERT_TRUE(touch.status.ok()) << touch.status.message();
    EXPECT_EQ(device("event7").output, "DP-1");
    EXPECT_TRUE(std::ranges::any_of(touch.events, [](const auto &event) {
      return event.type == EV_ABS && event.code == ABS_MT_POSITION_X && event.value == 9600;
    }));
    EXPECT_TRUE(std::ranges::any_of(touch.events, [](const auto &event) {
      return event.type == EV_ABS && event.code == ABS_MT_POSITION_Y && event.value == 2700;
    }));

    lvh::PenToolState tool;
    tool.x = 0.5F;
    tool.y = 0.25F;
    tool.viewport = contact.viewport;
    const auto pen = lvh::detail::test::linux_uinput_mapped_tool(tool);
    ASSERT_TRUE(pen.status.ok()) << pen.status.message();
    EXPECT_FALSE(device("event7").workspace);
    EXPECT_TRUE(std::ranges::any_of(pen.events, [](const auto &event) {
      return event.type == EV_ABS && event.code == ABS_X && event.value == 9600;
    }));
    EXPECT_TRUE(std::ranges::any_of(pen.events, [](const auto &event) {
      return event.type == EV_ABS && event.code == ABS_Y && event.value == 2700;
    }));
  }

  TEST_F(KWinMappingTest, FailedAssignmentEmitsNoTouchOrPenInput) {
    set_rejection("outputName");
    lvh::TouchContact contact;
    contact.viewport.output_name = "DP-1";
    const auto touch = lvh::detail::test::linux_uinput_mapped_contact(contact);
    EXPECT_EQ(touch.status.code(), lvh::ErrorCode::backend_failure);
    EXPECT_TRUE(touch.events.empty());
    lvh::PenToolState tool;
    tool.viewport.output_name = "DP-1";
    const auto pen = lvh::detail::test::linux_uinput_mapped_tool(tool);
    EXPECT_EQ(pen.status.code(), lvh::ErrorCode::backend_failure);
    EXPECT_TRUE(pen.events.empty());
  }

  TEST_F(KWinMappingTest, UnresolvedOwnedDeviceCannotSendInputToAnotherDisplay) {
    lvh::TouchContact contact;
    contact.viewport.output_name = "DP-1";
    const auto touch = lvh::detail::test::linux_uinput_touchscreen_contact_pipe(contact);
    EXPECT_EQ(touch.status.code(), lvh::ErrorCode::invalid_argument);
    EXPECT_TRUE(touch.events.empty());
    lvh::PenToolState tool;
    tool.viewport.output_name = "DP-1";
    const auto pen = lvh::detail::test::linux_uinput_pen_tablet_tool_pipe(tool);
    EXPECT_EQ(pen.status.code(), lvh::ErrorCode::invalid_argument);
    EXPECT_TRUE(pen.events.empty());
  }

  TEST(OutputMappingFallbackTest, LeavesASessionWithoutDBusUsable) {
    const auto *old_address = g_getenv("DBUS_SESSION_BUS_ADDRESS");
    const std::string saved_address = old_address ? old_address : "";
    g_setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/tmp/libvirtualhid-no-such-session-bus", true);
    g_setenv("WAYLAND_DISPLAY", "wayland-test", true);
    lvh::detail::OutputMapping mapping;
    EXPECT_TRUE(mapping.update("/dev/input/event7", "DP-1", false).ok());
    if (saved_address.empty()) {
      g_unsetenv("DBUS_SESSION_BUS_ADDRESS");
    } else {
      g_setenv("DBUS_SESSION_BUS_ADDRESS", saved_address.c_str(), true);
    }
    g_unsetenv("WAYLAND_DISPLAY");
  }
}  // namespace
