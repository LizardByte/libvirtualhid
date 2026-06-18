# libvirtualhid

`libvirtualhid` is a planned cross-platform C++ library for creating virtual HID
input devices for remote streaming hosts and similar low-latency input
applications.

The primary target is gamepad input. Keyboard and mouse support are secondary
goals once the gamepad model, descriptor handling, and output report plumbing
are stable.

## Goals

- Provide the same public C++ API on Windows, Linux, and eventually macOS.
- Hide platform-specific virtual HID details behind backend implementations.
- Prefer user-mode platform facilities and avoid custom kernel-mode drivers.
- Build with CMake and support direct consumption through `add_subdirectory`,
  `FetchContent`, installed CMake packages, or vendored source.
- Keep Sunshine as the first target consumer and validate the library against
  Sunshine's current input lifecycle, controller profiles, and packaging needs.
- Keep network transport out of scope. Consumers such as streaming hosts own
  network input collection and feed local reports into this library.
- Use the MIT license.

## Non-goals

- No anti-cheat bypass or stealth device hiding.
- No replication of controller authentication chips or private vendor secrets.
- No Windows kernel-mode driver.
- No built-in network protocol.

## Reference Projects

The initial design is informed by these projects:

- [cgutman/WinUHid](https://github.com/cgutman/WinUHid): Windows virtual HID
  device emulation with a UMDF-oriented driver/package shape.
- [hifihedgehog/HIDMaestro](https://github.com/hifihedgehog/HIDMaestro):
  Windows user-mode UMDF2 game controller emulation, profile-driven controller
  identity, output callbacks, hot-plug behavior, and no custom kernel driver.
- [games-on-whales/inputtino](https://github.com/games-on-whales/inputtino):
  Linux C++ virtual input library built around `uinput`, `evdev`, and `uhid`,
  including gamepad, keyboard, mouse, and output-event handling.
- LizardByte C++ project structure references:
  [tray](https://github.com/LizardByte/tray) and
  [libdisplaydevice](https://github.com/LizardByte/libdisplaydevice), especially
  their CMake option shape, top-level-only test/doc setup, `third-party`
  submodule layout, and GoogleTest wiring.

## Platform Strategy

### Windows

Windows should use a UMDF2 HID minidriver and a C++ client library/backend. The
driver remains user-mode, but it is still a Windows driver package and must be
installed and trusted on the host machine.

That means a consuming application can compile the C++ library as part of its
own build, but compiling the library alone is not enough to create virtual HID
devices on Windows. The project should provide:

- A CMake-built C++ client library for consumers.
- A Windows driver package containing the INF, signed catalog, UMDF driver DLL,
  and any helper/control component needed by the backend.
- Install/uninstall helpers suitable for developer machines and application
  installers.
- A path for projects to either build the driver package themselves with the
  Windows SDK/WDK or redistribute an official prebuilt, signed package.

The public API should not expose these details. Consumers should create a
runtime, create devices, submit input state, and receive output reports the same
way they do on Linux.

The Windows C++ client library should support both MSVC and MinGW/UCRT64 where
the code only depends on normal Win32 or C++ APIs. MinGW support matters for
consumers that already build their application with that toolchain. The UMDF2
driver package is different: it should be treated as a Windows SDK/WDK build
artifact and built with the Microsoft driver toolchain, such as Visual Studio,
MSBuild, or EWDK. The boundary between the library and driver should therefore
be compiler-neutral: prefer a stable C ABI, named pipe, device interface IOCTL,
or similar control channel over passing C++ STL types across that boundary.

### Linux

Linux should compile directly into the consuming project and use standard kernel
user-space interfaces:

- `uhid` for descriptor-driven HID gamepads where the raw HID identity and
  output reports matter.
- `uinput` for keyboard, mouse, and simpler evdev-style devices.
- `evdev` or `libevdev` where it meaningfully reduces direct ioctl handling.
- X11/XTest as a last-resort keyboard and mouse fallback when `uinput` cannot
  be opened and an X11 session is available.

Linux deployment should be documentation and permissions focused: users need
access to `/dev/uinput` and/or `/dev/uhid`, usually through udev rules or group
membership. No out-of-tree kernel module should be required.

The XTest fallback should not be treated as a gamepad backend. It can cover
keyboard and mouse injection on X11, but it does not create virtual HID devices,
does not help on Wayland, and should not replace `uhid`/`uinput` for gamepads.
Sunshine's removed legacy implementation is the first reference for this path:
commit `8227e8f8` added the XTest input fallback, and commit `f57aee90` removed
`src/platform/linux/input/legacy_input.cpp` when Sunshine moved fully to
inputtino.

### macOS

macOS is a later target. The first planning milestone is to validate whether the
backend should use `IOHIDUserDevice`, DriverKit/HIDDriverKit, or a combination
of both, then document the entitlement, signing, and distribution requirements.
The public API should already be shaped so the macOS backend can plug in without
breaking Windows or Linux consumers.

## Proposed Public API Shape

The exact names may change during implementation, but the API should center on
portable concepts instead of platform concepts:

```cpp
#include <libvirtualhid/libvirtualhid.hpp>

auto runtime = lvh::Runtime::create();

auto gamepad = runtime->create_gamepad(lvh::profiles::xbox_360());

gamepad->set_output_callback([](const lvh::GamepadOutput& output) {
  // Route rumble, LED, or trigger feedback back to the physical controller.
});

lvh::GamepadState state;
state.buttons.set(lvh::GamepadButton::A, true);
state.left_stick = {0.25f, -0.5f};
state.right_trigger = 1.0f;

gamepad->submit(state);
```

Expected core types:

- `Runtime`: owns platform backend discovery, initialization, and shutdown.
- `VirtualDevice`: common lifecycle for create, destroy, and hot-plug.
- `Gamepad`: gamepad-specific state submission and output callbacks.
- `Keyboard` and `Mouse`: later secondary device types.
- `DeviceProfile`: VID/PID, product strings, bus type, HID descriptor, report
  layout, and platform capability metadata.
- `GamepadState`: normalized buttons, axes, triggers, hats, motion sensors, and
  optional touchpad data.
- `GamepadOutput`: normalized rumble, haptics, LEDs, adaptive triggers, and raw
  output reports when a profile needs them.
- `BackendCapabilities`: runtime capability query for platform/backend limits,
  such as `supports_virtual_hid`, `supports_output_reports`,
  `supports_keyboard`, `supports_mouse`, `supports_xtest_fallback`, and
  `requires_installed_driver`.

## Sunshine Integration Requirements

Sunshine is the first consumer to design against. The initial implementation
should cover Sunshine's active input behavior before optimizing for unrelated
consumers:

- CMake consumption must work as a vendored dependency under Sunshine's
  `third-party` tree.
- The API must support multiple client-relative and global gamepad indexes so
  Sunshine can preserve stable controller lifecycles across arrival, update,
  feedback, and removal events.
- Built-in profiles should cover Sunshine's current gamepad choices: automatic
  selection, Xbox One-style, DualSense-style, and Switch Pro-style devices. Xbox
  360 can remain useful as a compatibility profile and test target.
- Controller metadata must be rich enough for Sunshine's selection rules:
  client controller type, motion sensor capability, touchpad capability, RGB LED
  support, battery state, and per-controller identity data.
- Output callbacks must carry rumble first, then RGB LED, adaptive trigger,
  motion activation, and raw output report data where the selected profile
  supports it.
- Keyboard and mouse APIs should map cleanly to Sunshine's current relative
  mouse, absolute mouse, buttons, scroll, horizontal scroll, keyboard scancode,
  and Unicode paths.
- Linux fallback behavior should match Sunshine's operational expectation:
  prefer real virtual devices through `uhid`/`uinput`; only use XTest for
  keyboard/mouse when virtual device creation fails and X11 is available.
- The library must not own Sunshine's network protocol, Moonlight packet
  parsing, configuration system, or feedback queue. It should expose the device
  primitives Sunshine needs to keep that ownership in Sunshine.

## Tooling and Dependency Plan

- Use CMake as the only build system for the core library.
- Follow the LizardByte `tray` and `libdisplaydevice` pattern: top-level-only
  `BUILD_TESTS` and `BUILD_DOCS` options, reusable library targets, and tests
  that do not force themselves on parent projects.
- Put all submodules under `third-party`.
- Add GoogleTest as a submodule at `third-party/googletest`; do not download it
  during configure.
- Expose `libvirtualhid::libvirtualhid` as the main CMake target.
- Keep the public headers under `include/libvirtualhid` and the implementation
  split into shared core code plus platform-specific backends.
- Add Windows CI coverage for the client library with MSVC and MinGW/UCRT64.
  Add separate WDK/MSVC validation for the driver package once driver sources
  exist.
- Add Linux CI coverage for GCC and Clang, with integration tests gated behind
  explicit availability of `/dev/uinput`, `/dev/uhid`, or X11/XTest.

## Repository Plan

The intended project layout is:

```text
include/libvirtualhid/        Public C++ headers
src/core/                     Shared profile, descriptor, and report logic
src/platform/windows/         Windows client backend and UMDF control channel
src/platform/linux/           Linux uhid/uinput backend
src/platform/macos/           Future macOS backend
drivers/windows/              UMDF2 driver package sources
profiles/                     Built-in gamepad profiles
examples/                     Minimal consumers and platform smoke tests
tests/                        Unit and integration tests
cmake/                        Package config and helper modules
third-party/googletest/       GoogleTest submodule
```

## Implementation Plan

### Phase 1: Project Foundation

- Add CMake project scaffolding and exported target
  `libvirtualhid::libvirtualhid`.
- Define the public C++ API, error model, device lifecycle, and ownership rules.
- Add a fake in-memory backend so API tests can run on every platform.
- Add GoogleTest as a submodule under `third-party/googletest` and wire tests
  using the same top-level-only pattern as `tray` and `libdisplaydevice`.
- Add CI using the `libdisplaydevice` workflow pattern for Linux GCC, Linux
  Clang, macOS, Windows MinGW/UCRT64, and Windows MSVC configure/build/test
  coverage.
- Add descriptor/profile models for at least Xbox 360, Xbox Series, DualSense,
  and a generic HID gamepad.
- Add unit tests for state normalization and HID report packing.
- Add a Sunshine-oriented example or adapter test that exercises controller
  arrival, state updates, output feedback, and removal without depending on
  Sunshine internals.

### Phase 2: Linux MVP

- Implement gamepad creation over `uhid` for descriptor-driven controllers.
- Add `uinput` support for keyboard and mouse once the gamepad path is stable.
- Support output report callbacks for rumble and profile-specific feedback.
- Add X11/XTest fallback support for keyboard and mouse only, using Sunshine's
  historical legacy input implementation as the reference point.
- Add examples and integration tests that validate SDL/HIDAPI discovery where
  available.
- Document required Linux permissions and sample udev rules.

### Phase 3: Windows MVP

- Build a UMDF2 HID minidriver package with CMake/WDK integration.
- Implement the Windows backend and control channel between the C++ library and
  the UMDF driver.
- Keep the client library buildable with MSVC and MinGW/UCRT64. Keep the driver
  package on the Microsoft WDK toolchain.
- Add install/uninstall tooling for developer workflows.
- Support hot-plug, multi-controller instances, and output report callbacks.
- Validate visibility through DirectInput, XInput where applicable, SDL/HIDAPI,
  Windows.Gaming.Input/GameInput, and browser Gamepad API.

### Phase 4: API Parity and Packaging

- Keep one API surface across Windows and Linux, with capability queries for
  platform limitations instead of platform-specific methods.
- Add installed CMake package support and `FetchContent` documentation.
- Add CI for formatting, static analysis, CMake configure/build, unit tests, and
  platform smoke tests.
- Decide whether official Windows releases should ship signed driver packages
  in addition to source.

### Phase 5: macOS Research and Backend

- Prototype macOS virtual HID creation and report submission.
- Document signing, entitlement, and installer constraints.
- Add macOS backend behind the existing public API.
- Add macOS discovery and smoke-test coverage.

## Testing Plan

- Unit test descriptor generation, report packing, axis scaling, button mapping,
  and output report parsing.
- Run lifecycle tests for create, submit, output callback, destroy, repeated
  hot-plug, and process shutdown cleanup.
- Validate multi-controller behavior and stable ordering.
- Test against real consumers where practical: SDL, HIDAPI, browser Gamepad API,
  DirectInput/XInput/GameInput on Windows, and evdev/libinput tooling on Linux.

## License

`libvirtualhid` is licensed under the MIT License. See [LICENSE](LICENSE).
