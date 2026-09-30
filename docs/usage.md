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
