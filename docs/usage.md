# Usage and API

This page is for developers embedding `libvirtualhid`. If you use Sunshine,
start with the [end-user guide](end-user-gamepad-guide.md).

## Add the CMake target

An installed package exports `libvirtualhid::libvirtualhid`:

```cmake
find_package(libvirtualhid CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE libvirtualhid::libvirtualhid)
```

Point `CMAKE_PREFIX_PATH` at the installation prefix if CMake cannot find it.
For a vendored checkout, use:

```cmake
add_subdirectory(third-party/libvirtualhid)
target_link_libraries(your_app PRIVATE libvirtualhid::libvirtualhid)
```

`FetchContent` consumers can use the same target after pinning a release tag or
commit. Tests, examples, documentation, and platform packages are top-level or
opt-in builds; ordinary subdirectory consumers get the library.

## Create and update a gamepad

```cpp
#include <libvirtualhid/libvirtualhid.hpp>

auto runtime = lvh::Runtime::create();
auto created = runtime->create_gamepad(lvh::profiles::xbox_series());
if (!created) {
  return;
}

auto &gamepad = *created.gamepad;
gamepad.set_output_callback([](const lvh::GamepadOutput &output) {
  if (output.kind == lvh::GamepadOutputKind::rumble) {
    // Forward feedback to the client controller.
  }
});

lvh::GamepadState state;
state.buttons.set(lvh::GamepadButton::a, true);
state.left_stick = {0.25F, -0.5F};
state.right_trigger = 1.0F;
gamepad.submit(state);
```

Keep the `Gamepad` alive for the session and destroy it when the client
disconnects. Query runtime and effective profile capabilities before exposing
optional inputs or feedback. The
[gamepad adapter example](../examples/gamepad_adapter.cpp) shows incremental
updates, metadata, output callbacks, and lifecycle handling.

Built-in profiles cover Generic HID, Xbox 360, Xbox One, Xbox Series,
DualShock 4, DualSense, and Switch Pro. PlayStation profiles offer explicit USB
and Bluetooth variants. Device names can be customized through
`DeviceProfile::name`.

The API also exposes `Keyboard`, `Mouse`, `Touchscreen`, `Trackpad`, and
`PenTablet` where supported. `DeviceNode` provides paths for diagnostics or
handoff to another local input consumer.

## Diagnostics

Hosts can route libvirtualhid diagnostics into their own logging system by
installing `RuntimeOptions::log_callback` before creating the runtime:

```cpp
lvh::RuntimeOptions runtime_options;
runtime_options.backend = lvh::BackendKind::platform_default;
runtime_options.log_callback = [](lvh::LogLevel level, const std::string &message) {
  host_log(level, message);
};
auto runtime = lvh::Runtime::create(runtime_options);
```

The callback receives runtime and device lifecycle messages, operation failures,
and debug-level mouse coordinate diagnostics. It runs synchronously on the
calling thread. If a consumer callback throws, libvirtualhid disables it for
subsequent messages so it cannot interrupt input delivery.

## Absolute Mouse Viewports

Absolute mouse coordinates can target one monitor inside a larger virtual
desktop. Supply both the desktop bounds and the selected viewport in native
desktop pixels when creating the mouse:

```cpp
lvh::CreateMouseOptions mouse_options;
mouse_options.profile = lvh::profiles::mouse();
mouse_options.desktop = {.offset_x = -1920, .offset_y = 0, .width = 3840, .height = 1080};
mouse_options.viewport = {.offset_x = 0, .offset_y = 0, .width = 1920, .height = 1080};
auto created = runtime->create_mouse(mouse_options);
```

`Mouse::move_absolute()` coordinates are scaled from their supplied source
dimensions into the target viewport, then normalized against the virtual
desktop where the platform input API requires it. This contract covers
CoreGraphics on macOS, `SendInput` on Windows, and the XTest or `uinput` path on
Linux and FreeBSD, including virtual desktops whose origin is negative. Leave
both viewport dimensions at zero to retain the platform-default pointer area
(the main display on macOS and the virtual desktop on other current backends).
A configured target viewport must be fully contained by its desktop.

## License and diagnostic tool

On Windows and macOS, `get_license_status`, `activate_license`,
`validate_license`, and `deactivate_license` manage the machine license through
the installed broker. Treat activation keys as transient secrets; do not log or
persist them. The broker, not the application, maintains the activation.

The optional `virtualhid_control` tool creates test devices, displays
capabilities and feedback, and manages the broker license on Windows and
macOS. It lists devices created by that tool, not devices owned by other
processes.

See [platform support](platform-support.md) for availability and
[development](development.md) for build commands and optional targets.
