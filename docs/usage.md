# Usage and API

This page covers how consumers bring `libvirtualhid` into a CMake project and
which public API concepts they should build around.

## CMake Consumption

The library exports `libvirtualhid::libvirtualhid`.

For an installed package, install this project into a prefix and point the
consumer configure at that prefix:

```bash
cmake --install cmake-build-release --prefix /opt/libvirtualhid
cmake -S your-app -B cmake-build-your-app -DCMAKE_PREFIX_PATH=/opt/libvirtualhid
```

Then link the exported package from the consuming project:

```cmake
find_package(libvirtualhid CONFIG REQUIRED)
target_link_libraries(your_app PRIVATE libvirtualhid::libvirtualhid)
```

For a vendored checkout, add the project directly and link the same target:

```cmake
add_subdirectory(third-party/libvirtualhid)
target_link_libraries(your_app PRIVATE libvirtualhid::libvirtualhid)
```

For `FetchContent`, pin a tag or commit and make the project available:

```cmake
include(FetchContent)

FetchContent_Declare(
  libvirtualhid
  GIT_REPOSITORY https://github.com/LizardByte/libvirtualhid.git
  GIT_TAG <tag-or-commit>
)
FetchContent_MakeAvailable(libvirtualhid)

target_link_libraries(your_app PRIVATE libvirtualhid::libvirtualhid)
```

Examples, tests, docs, and the Windows driver package are top-level or opt-in
builds. Normal vendored and `FetchContent` consumers only get the library target
unless they explicitly enable additional options.

## Build Options

- `BUILD_EXAMPLES`: build example executables when this repository is the top
  level project.
- `BUILD_TESTS`: build the GoogleTest suite when this repository is the top
  level project.
- `BUILD_DOCS`: build Doxygen documentation when this repository is the top
  level project.
- `LIBVIRTUALHID_INSTALL`: install targets, headers, and CMake package files.
  This defaults to on for direct builds and off when consumed by another CMake
  project.
- `LIBVIRTUALHID_ENABLE_XTEST`: enable the Linux X11/XTest keyboard and mouse
  fallback.
- `LIBVIRTUALHID_BUILD_WINDOWS_DRIVER`: build the Windows UMDF2 driver package
  with the Microsoft WDK/MSVC toolchain.
- `LIBVIRTUALHID_ENABLE_PACKAGING`: enable CPack package metadata.
- `LIBVIRTUALHID_WARNINGS_AS_ERRORS`: treat project warnings as errors.

Linux consumers need the backend development packages used by the build, such as
`libevdev` and `pkg-config`. Windows consumers can build the normal C++ library
with MSVC or MinGW/UCRT64; the UMDF driver package is a separate WDK/MSVC build
artifact.

## Public API Shape

The API centers on portable device concepts:

- `Runtime`: owns backend discovery, initialization, device creation, and
  shutdown.
- `VirtualDevice`: common lifecycle for created devices.
- `Gamepad`: submits normalized gamepad state and receives output callbacks.
- `Keyboard`: submits key press/release and UTF-8 text input.
- `Mouse`: submits relative motion, absolute motion, buttons, vertical scroll,
  and horizontal scroll.
- `Touchscreen`, `Trackpad`, and `PenTablet`: expose touch and tablet device
  primitives where the backend supports them.
- `DeviceProfile`: describes device identity, HID descriptors, report layout,
  and profile capabilities.
- `DeviceNode`: reports backend-visible device nodes and paths for diagnostics
  or handoff to SDL, libinput, HIDAPI, and similar consumers.
- `BackendCapabilities`: reports runtime/backend limits such as virtual HID,
  output report, keyboard, mouse, XTest fallback, and installed-driver support.

## Gamepad Example

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
    // Route rumble back to the physical client controller.
  }
});

lvh::GamepadState state;
state.buttons.set(lvh::GamepadButton::a, true);
state.left_stick = {0.25F, -0.5F};
state.right_trigger = 1.0F;

gamepad.submit(state);
```

The `examples/gamepad_adapter.cpp` example shows the
streaming-host-oriented adapter path. It maps incremental button, axis, trigger,
touch, motion, battery, feedback, and lifecycle updates onto the platform-neutral
`Runtime` and `Gamepad` APIs.

## Built-In Profiles

Built-in gamepad profiles include:

- Generic HID gamepad.
- Xbox 360.
- Xbox One.
- Xbox Series.
- DualShock 4 USB and Bluetooth.
- DualSense USB and Bluetooth.
- Nintendo Switch Pro.

Profiles advertise support for features such as rumble, trigger rumble, RGB
LEDs, adaptive triggers, motion sensors, touchpads, battery state, profile
specific buttons, and raw output reports. Consumers should query profile and
backend capabilities before warning users about unsupported client features.
