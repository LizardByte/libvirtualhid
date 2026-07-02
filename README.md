<div align="center">
  <img
    src="https://raw.githubusercontent.com/LizardByte/.github/refs/heads/master/branding/logos/logo.svg"
    alt="LizardByte icon"
    width="256"
  />
  <h1 align="center">libvirtualhid</h1>
  <h4 align="center">Cross-platform C++ virtual HID library.</h4>
</div>

<div align="center">
  <a href="https://github.com/LizardByte/libvirtualhid"><img src="https://img.shields.io/github/stars/lizardbyte/libvirtualhid.svg?logo=github&style=for-the-badge" alt="GitHub stars"></a>
  <a href="https://github.com/LizardByte/libvirtualhid/actions/workflows/ci.yml?query=branch%3Amaster"><img src="https://img.shields.io/github/actions/workflow/status/lizardbyte/libvirtualhid/ci.yml.svg?branch=master&label=CI%20build&logo=github&style=for-the-badge" alt="GitHub Workflow Status (CI)"></a>
  <a href="https://codecov.io/gh/LizardByte/libvirtualhid"><img src="https://img.shields.io/endpoint.svg?url=https%3A%2F%2Fapp.lizardbyte.dev%2Fdashboard%2Fshields%2Fcodecov%2Flibvirtualhid.json&style=for-the-badge&logo=codecov" alt="Codecov"></a>
  <a href="https://sonarcloud.io/project/overview?id=LizardByte_libvirtualhid"><img src="https://img.shields.io/sonar/quality_gate/LizardByte_libvirtualhid.svg?server=https%3A%2F%2Fsonarcloud.io&style=for-the-badge&logo=sonarqubecloud&label=sonarcloud" alt="SonarCloud"></a>
</div>

# Overview

## ℹ️ About

`libvirtualhid` is a planned cross-platform C++ library for creating virtual HID
input devices for remote streaming hosts and similar low-latency input
applications.

The primary target is gamepad input. Keyboard and mouse support are secondary
goals once the gamepad model, descriptor handling, and output report plumbing
are stable.

## 🎯 Goals

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

## ❌ Non-goals

- No anti-cheat bypass or stealth device hiding.
- No replication of controller authentication chips or private vendor secrets.
- No Windows kernel-mode driver.
- No built-in network protocol.

## 📚 Reference Projects

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

## 🖥️ Platform Strategy

### Windows

Windows should use a UMDF2 HID minidriver and a C++ client library/backend. The
driver remains user-mode, but it is still a Windows driver package and must be
installed and trusted on the host machine.

That means a consuming application can compile the C++ library as part of its
own build, but compiling the library alone is not enough to create virtual HID
devices on Windows. The project should provide:

- [x] A CMake-built C++ client library for consumers.
- [x] A Windows driver package containing the INF, signed catalog, UMDF driver DLL,
  and any helper/control component needed by the backend.
- [x] Install/uninstall helpers suitable for developer machines and application
  installers.
- [x] A path for projects to either build the driver package themselves with the
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

The current Windows backend selects a UMDF control-channel implementation for
`BackendKind::platform_default`. It always exposes keyboard and mouse through
Win32 `SendInput`, then probes the libvirtualhid control device interface for
descriptor-driven virtual gamepads. It falls back to the legacy fixed
`\\.\LibVirtualHid` and `\\.\Global\LibVirtualHid` links for diagnostics and
older driver builds. It reports `requires_installed_driver = true`, and only
advertises gamepad/output-report support when the driver package is installed
and the control device can be opened. Touchscreen, trackpad, and pen tablet
support are not implemented in the Windows backend yet. The client library
stays buildable with MSVC and MinGW/UCRT64 because the gamepad path talks to the
driver through fixed-size C protocol structures and Win32 `DeviceIoControl`
calls. The default control device path can be overridden for diagnostics with
`LIBVIRTUALHID_WINDOWS_CONTROL_DEVICE`.

The UMDF driver uses Windows Virtual HID Framework (VHF) for OS-visible gamepad
devices. Create requests start a VHF child device from the requested descriptor,
VID/PID, and version; input reports are submitted with `VhfReadReportSubmit`;
and HID output writes are forwarded back through the existing output-report
callback path. DirectInput, SDL/HIDAPI, Windows.Gaming.Input/GameInput, and the
browser Gamepad API should therefore see standard HID gamepads after the driver
is installed. XInput is not a direct target for this HID-only backend because it
does not emulate the Xbox proprietary bus/API.
The client protocol uses complete HID reports; numbered reports carry the report
ID at byte 0 and unnumbered reports omit it. The UMDF driver passes that
complete report buffer to VHF and also sets `HID_XFER_PACKET.reportId` for
numbered reports. Output reports forwarded by VHF are normalized back to the
same complete-report shape before delivery to the C++ backend. VHF exposes
VID/PID/version, explicit
`HID\VID_....&PID_....` hardware IDs, Xbox
`HID\VID_....&PID_....&IG_00` hardware IDs where applicable, and the report
descriptor for the child HID device so Windows and browser consumers can
identify the selected profile instead of a generic VHF-only device.
The built-in Xbox One and Xbox Series profiles use an XboxGIP-shaped descriptor
and unnumbered 17-byte input reports derived from HIDMaestro's Xbox profiles,
with inputtino's Xbox One VID/PID/version retained for that profile. The
built-in generic and Xbox 360 HID profiles use a standard-gamepad-shaped common
descriptor: 12 one-bit digital buttons, a hat switch for the d-pad, and 8-bit
`X`, `Y`, `Z`, `Rx`, `Ry`, and `Rz` values for sticks and analog triggers.
Keeping the buttons as real HID button caps and the d-pad as a hat switch is
required for DirectInput-style and browser consumers to enumerate the VHF child
as a gamepad.
The UMDF driver opens a separate VHF source target for each virtual gamepad and
parents that target to the control-file handle that created it, so process exits
or crashes clean up any virtual gamepads that were not explicitly destroyed.
During rapid development reinstalls, the fixed global control symbolic link can
outlive the previous root device briefly; the driver treats that collision as
non-fatal so stale object-manager state does not leave the control device in
Code 31. Normal clients discover the PnP control device interface first, so a
stale fixed link does not block the backend from reaching the current device.

Build the UMDF package separately with the Microsoft driver toolchain:

```powershell
cmake -S . -B cmake-build-windows-driver -G "Visual Studio 17 2022" -A x64 `
  -DLIBVIRTUALHID_BUILD_WINDOWS_DRIVER=ON -DLIBVIRTUALHID_ENABLE_PACKAGING=ON `
  -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF
cmake --build cmake-build-windows-driver --config Release --target libvirtualhid_umdf
cmake --build cmake-build-windows-driver --config Release --target libvirtualhid_windows_catalog
cpack -G WIX --config .\cmake-build-windows-driver\CPackConfig.cmake
```

Developer install/uninstall helpers live under `scripts/windows`:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows\install-driver.ps1 `
  -InfPath .\cmake-build-windows-driver\src\platform\windows\driver\package\Release\libvirtualhid.inf `
  -LogPath .\cmake-build-windows-driver\install-driver.log
powershell -ExecutionPolicy Bypass -File .\scripts\windows\test-installed-driver.ps1 `
  -GamepadAdapterPath .\cmake-build-ci\examples\Debug\gamepad_adapter.exe `
  -Profile x360
powershell -ExecutionPolicy Bypass -File .\scripts\windows\uninstall-driver.ps1 `
  -Force -RemoveCertificateSubject "CN=libvirtualhid CI Test Driver Signing"
```

The helper stages the INF with `pnputil`, updates an existing
`ROOT\LIBVIRTUALHID` device when present, and creates that root-enumerated
device when it is missing. It uses SetupAPI/NewDev directly so MSI installs do
not require the WDK tools on the target machine. Existing devices are detected
by matching the `ROOT\LIBVIRTUALHID` hardware ID. The SetupAPI path creates a
root-enumerated instance such as `ROOT\LIBVIRTUALHID\####`.
The install and uninstall helpers also clean up malformed development devices
left by earlier installer revisions, including root instances left in the
failed `HIDClass` package shape. The WiX installer writes the helper transcript
to `C:\ProgramData\libvirtualhid\install-driver.log`.
The test helper fails if the root device is not reported as `Status: Started`,
if `\\.\LibVirtualHid` cannot be opened, or if a held gamepad adapter instance
does not produce a started HID child device such as
`HID\VID_045E&PID_028E&IG_00`. That check is also run by the Windows MSVC pull
request CI leg for every `gamepad_adapter` profile after installing the test
driver package. For manual browser validation, run the same helper or
`examples/gamepad_adapter x360 --hold-seconds 60`, then open
`https://app.lizardbyte.dev/gamepad-tester/` in a normal desktop browser and
press one of the held virtual buttons if the browser needs a gamepad activation
event.

The driver binary is a UMDF DLL installed through the Windows Driver Store, not
a libvirtualhid `.sys` copied into `C:\Windows\System32\drivers`. Windows still
uses its built-in `WUDFRd.sys` and VHF components under `System32\drivers`; the
libvirtualhid-specific sign that installation completed is the
`ROOT\LIBVIRTUALHID` device and the `\\.\LibVirtualHid` control device. The INF
includes the built-in `WUDFRd` install sections for the root `System` control
device, appends the VHF lower filter, sets `VhfMode=1` for the UMDF VHF source
stack, grants non-admin user-mode clients read/write access to the control
device, and disables UMDF host-process sharing so driver updates do not keep
using an older in-process UMDF module during development. The installer also
writes `VhfMode=1` onto the
root device before starting the driver so root-enumerated development installs
get the same VHF source mode as the INF hardware section. The UMDF control
device is restarted after install or update so same-version development builds
load the current UMDF module; if Windows cannot unload the old host, the
installer reports the reboot requirement. The UMDF control device starts
without opening VHF; each gamepad creation opens its own VHF target from the
creating file handle so target-open failures are reported through the
create-device response instead of making `\\.\LibVirtualHid` unavailable. The
generated INF uses the same UMDF
library version as the WDF headers and stub library selected by CMake. The
package defaults to UMDF 2.15, matching the inbox VHF UMDF source driver while
still exposing the framework APIs used by libvirtualhid. The driver target links
the MSVC runtime statically to avoid requiring VC runtime DLLs in the UMDF host
process. Development driver builds write a lightweight UMDF trace to
`C:\Windows\Temp\libvirtualhid-umdf-driver.log`.

Windows driver packages require a signed catalog for normal installation. Pull
request builds generate a short-lived self-signed test certificate, sign
`libvirtualhid.cat`, bundle the public `.cer` into the WiX installer, and import
that certificate into the local machine root and trusted-publisher stores during
install. The uninstall helper removes certificates matching
`CN=libvirtualhid CI Test Driver Signing`. Push/release builds must use Azure
Trusted Signing for the catalog and generated MSI, matching Sunshine's Windows
signing model, and must not ship the local PR test certificate.

### Linux

Linux should compile directly into the consuming project and use standard kernel
user-space interfaces:

- `uhid` for descriptor-driven HID gamepads where the raw HID identity and
  output reports matter.
- `uinput` for keyboard, mouse, and simpler evdev-style devices.
- `libevdev` for uinput device construction where it reduces direct ioctl
  handling and preserves the same public API.
- X11/XTest as a last-resort keyboard and mouse fallback when `uinput` cannot
  be opened and an X11 session is available.

Linux deployment should be documentation and permissions focused: users need
access to `/dev/uinput` and/or `/dev/uhid`, usually through udev rules or group
membership. No out-of-tree kernel module should be required.

The current Linux MVP uses `uhid` and `uinput` for
`BackendKind::platform_default`. When `/dev/uhid` is readable and writable, the
backend reports gamepad and output-report support. When `/dev/uinput` is
readable and writable, it reports keyboard and mouse support. When a required
node is missing or permission is denied, the same backend remains selectable
but reports the affected capability as unavailable and returns
`backend_unavailable` from that device creation path.

The Linux backend uses `libevdev` internally to construct uinput keyboard,
mouse, touchscreen, trackpad, and pen tablet devices. Consumers still use the
same platform-neutral C++ API; `libevdev` is a Linux build dependency, not a
public API dependency. UTF-8 keyboard text submission is supported through the
same Unicode compose sequence for both uinput keyboards and the XTest fallback.

The Linux packaging model needs `/dev/uinput` and `/dev/uhid` access. Install a udev rules file such
as `/etc/udev/rules.d/60-libvirtualhid.rules` with:

```udev
# Allows libvirtualhid consumers to access /dev/uinput
KERNEL=="uinput", SUBSYSTEM=="misc", OPTIONS+="static_node=uinput", GROUP="input", MODE="0660", TAG+="uaccess"

# Allows libvirtualhid consumers to access /dev/uhid
KERNEL=="uhid", GROUP="input", MODE="0660", TAG+="uaccess"
```

Consuming applications may also install name-matched rules for their stable
virtual device names when generated `hidraw` or `input` nodes must be
accessible to the session user:

```udev
KERNEL=="hidraw*", ATTRS{name}=="Your App Controller*", GROUP="input", MODE="0660", TAG+="uaccess"
SUBSYSTEMS=="input", ATTRS{name}=="Your App Controller*", GROUP="input", MODE="0660", TAG+="uaccess"
```

For `uhid` gamepad support, install a modules-load entry such as
`/etc/modules-load.d/60-libvirtualhid.conf` containing:

```text
uhid
```

After installing the rules, load `uhid`, reload udev, and trigger the device
nodes:

```bash
sudo modprobe uhid
sudo udevadm control --reload-rules
sudo udevadm trigger --property-match=DEVNAME=/dev/uinput
sudo udevadm trigger --property-match=DEVNAME=/dev/uhid
```

If input still does not work, add the user running the consuming application to
the `input` group, then log out and back in:

```bash
sudo usermod -aG input $USER
```

The Linux UHID smoke test creates a real virtual gamepad and fails when the
current user cannot open `/dev/uhid`.

The Linux uinput smoke test creates real keyboard and mouse devices and fails
when the current user cannot open `/dev/uinput`.

The Linux consumer integration tests create real virtual devices and validate
them through in-process consumer libraries. SDL2 must see the UHID gamepad and
observe button/axis input. libinput must see the uinput keyboard and mouse and
observe key, pointer motion, and button events. These tests fail when the Linux
device nodes or consumer development libraries are unavailable.

The XTest fallback should not be treated as a gamepad backend. It can cover
keyboard and mouse injection on X11, but it does not create virtual HID devices,
does not help on Wayland, and should not replace `uhid`/`uinput` for gamepads.
It is enabled automatically when `LIBVIRTUALHID_ENABLE_XTEST` is `ON` and CMake
finds X11/XTest development files.
commit `8227e8f8` added the XTest input fallback, and commit `f57aee90` removed
`src/platform/linux/input/legacy_input.cpp` when Sunshine moved fully to
inputtino.

### macOS

macOS is a later target. The first planning milestone is to validate whether the
backend should use `IOHIDUserDevice`, DriverKit/HIDDriverKit, or a combination
of both, then document the entitlement, signing, and distribution requirements.
The public API should already be shaped so the macOS backend can plug in without
breaking Windows or Linux consumers.

## 🛠️ CMake Consumption

All consumption modes expose the same CMake target:
`libvirtualhid::libvirtualhid`.

For an installed package, install the project into a prefix and point consumer
configures at that prefix:

```bash
cmake --install cmake-build-release --prefix /opt/libvirtualhid
cmake -S your-app -B cmake-build-your-app -DCMAKE_PREFIX_PATH=/opt/libvirtualhid
```

Then link the exported config package from the consuming project:

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

Tests, examples, docs, and the Windows driver package are top-level or opt-in
builds, so normal vendored and `FetchContent` consumers only get the library
target unless they explicitly enable additional options. Linux consumers still
need the development packages used by the backend, such as `libevdev` and
`pkg-config`.

## 📖 Proposed Public API Shape

The exact names may change during implementation, but the API should center on
portable concepts instead of platform concepts:

```cpp
#include <libvirtualhid/libvirtualhid.hpp>

auto runtime = lvh::Runtime::create();

auto created = runtime->create_gamepad(lvh::profiles::xbox_360());
if (!created) {
  return;
}

auto &gamepad = *created.gamepad;
gamepad.set_output_callback([](const lvh::GamepadOutput &output) {
  // Route rumble, LED, or trigger feedback back to the physical controller.
});

lvh::GamepadState state;
state.buttons.set(lvh::GamepadButton::a, true);
state.left_stick = {0.25f, -0.5f};
state.right_trigger = 1.0f;

gamepad.submit(state);
```

Expected core types:

- `Runtime`: owns platform backend discovery, initialization, and shutdown.
- `VirtualDevice`: common lifecycle for create, destroy, and hot-plug.
- `Gamepad`: gamepad-specific state submission and output callbacks.
- `Keyboard`: key press/release and UTF-8 text submission.
- `Mouse`: relative motion, absolute motion, button, vertical scroll, and
  horizontal scroll submission.
- `Touchscreen`: direct multi-touch contacts for touch displays.
- `Trackpad`: indirect multi-touch contacts and click state for touchpads.
- `PenTablet`: tablet tool, pressure, distance, tilt, and pen button state.
- `DeviceProfile`: VID/PID, product strings, bus type, HID descriptor, report
  layout, and platform capability metadata.
- `DeviceNode`: platform-reported device nodes and sysfs paths for consumers
  that must hand created devices to SDL, libinput, HIDAPI, or diagnostics.
- `GamepadState`: normalized buttons, axes, triggers, hats, motion sensors, and
  optional touchpad data.
- `GamepadOutput`: normalized rumble, haptics, LEDs, adaptive triggers, and raw
  output reports when a profile needs them.
- `BackendCapabilities`: runtime capability query for platform/backend limits,
  such as `supports_virtual_hid`, `supports_output_reports`,
  `supports_keyboard`, `supports_mouse`, `supports_xtest_fallback`, and
  `requires_installed_driver`.

## 📡 Streaming Host Integration Requirements

Streaming hosts are the first consumer class to design against. The initial
implementation should cover the behavior Sunshine needs first, while keeping
the requirements expressed in terms that apply to other consumers:

- [x] CMake consumption must work as a vendored dependency under a consuming
  project's `third-party` tree.
- [x] The API must support multiple client-relative and global gamepad indexes so
  streaming hosts can preserve stable controller lifecycles across arrival,
  update, feedback, and removal events.
- [x] Built-in profiles should cover common streaming controller choices:
  automatic selection, Xbox One-style, DualShock 4-style, DualSense-style, and
  Switch Pro-style devices. Xbox 360 can remain useful as a compatibility
  profile and test target.
- [x] Controller metadata must be rich enough for streaming-host selection rules:
  client controller type, motion sensor capability, touchpad capability, RGB LED
  support, battery state, and per-controller identity data.
- [x] Output callbacks must carry rumble first, then RGB LED, adaptive trigger,
  and raw output report data where the selected profile supports it.
- [x] Keyboard and mouse APIs should map cleanly to common relative mouse,
  absolute mouse, buttons, scroll, horizontal scroll, keyboard scancode, and
  Unicode paths.
- [x] Linux keyboard support must include configurable auto-repeat for held keys
  so streaming hosts can preserve input behavior previously covered by
  inputtino.
- [x] Linux devices must expose created device nodes and relevant sysfs paths
  for consumers and diagnostics that need to inspect or pass those paths onward.
- [x] Linux fallback behavior should match streaming-host operational
  expectations:
  prefer real virtual devices through `uhid`/`uinput`; only use XTest for
  keyboard/mouse when virtual device creation fails and X11 is available.
- [x] Linux gamepad support must reach inputtino parity before replacement:
  real DualSense UHID descriptors, GET_REPORT replies, periodic input reports,
  touchpad, motion, battery, RGB LED, adaptive trigger callbacks, CRC handling,
  and equivalent output-report feedback behavior for the UHID gamepad path.
- [x] Linux pointer support must cover touchscreen, trackpad, and pen tablet
  virtual devices with libinput-observable behavior.
- [x] The library must not own a consumer's network protocol, client packet
  parsing, configuration system, or feedback queue. It should expose the device
  primitives consumers need to keep that ownership in their applications.

## 🛠️ Tooling and Dependency Plan

- [x] Use CMake as the only build system for the core library.
- [x] Follow the LizardByte `tray` and `libdisplaydevice` pattern: top-level-only
  `BUILD_TESTS` and `BUILD_DOCS` options, reusable library targets, and tests
  that do not force themselves on parent projects.
- [x] Put all submodules under `third-party`.
- [x] Add GoogleTest as a submodule at `third-party/googletest`; do not download it
  during configure.
- [x] Add the LizardByte Doxygen configuration as a submodule at
  `third-party/doxyconfig` and use it for local docs and Read the Docs builds.
- [x] Expose `libvirtualhid::libvirtualhid` as the main CMake target.
- [x] Keep the public headers under `src/include/libvirtualhid` and the implementation
  split into shared core code plus platform-specific backends.
- [x] Add Windows CI coverage for the client library with MSVC and MinGW/UCRT64.
- [x] Add Linux CI coverage for GCC and Clang, with integration tests requiring
  `/dev/uinput`, `/dev/uhid`, SDL2, libinput, and X11/XTest where applicable.
- [ ] Add separate WDK/MSVC validation for the driver package once driver sources
  exist.

## 📒 Repository Plan

The intended project layout is:

```text
src/include/libvirtualhid/    Public C++ headers
src/core/                     Shared profile, descriptor, and report logic
src/platform/windows/         Windows client backend and UMDF control channel
src/platform/windows/driver/  Windows UMDF2 driver package sources
src/platform/linux/           Linux uhid/uinput backend
src/platform/macos/           Future macOS backend
profiles/                     Built-in gamepad profiles
examples/                     Minimal consumers and platform smoke tests
tests/                        Unit and integration tests
cmake/                        Package config and helper modules
docs/                         Project Doxygen configuration
third-party/doxyconfig/       LizardByte Doxygen configuration submodule
third-party/googletest/       GoogleTest submodule
```

## 📋 Implementation Plan

### Phase 1: Project Foundation

- [x] Add CMake project scaffolding and exported target
  `libvirtualhid::libvirtualhid`.
- [x] Define the public C++ API, error model, device lifecycle, and ownership rules.
- [x] Add a fake in-memory backend so API tests can run on every platform.
- [x] Add GoogleTest as a submodule under `third-party/googletest` and wire tests
  using the same top-level-only pattern as `tray` and `libdisplaydevice`.
- [x] Add Doxygen documentation wiring with `third-party/doxyconfig`, a project
  `docs/Doxyfile`, and Read the Docs configuration.
- [x] Add CI using the `libdisplaydevice` workflow pattern for Linux GCC, Linux
  Clang, macOS, Windows MinGW/UCRT64, and Windows MSVC configure/build/test
  coverage.
- [x] Add descriptor/profile models for at least Xbox 360, Xbox Series,
  DualShock 4, DualSense, and a generic HID gamepad.
- [x] Add unit tests for state normalization and HID report packing.
- [x] Add a streaming-host-oriented example or adapter test that exercises
  controller arrival, state updates, output feedback, and removal without
  depending on consumer internals.

### Phase 2: Linux MVP

- [x] Implement gamepad creation over `uhid` for descriptor-driven controllers.
- [x] Add `uinput` support for keyboard and mouse once the gamepad path is stable.
- [x] Support output report callbacks for rumble and profile-specific feedback.
- [x] Add X11/XTest fallback support for keyboard and mouse only.
- [x] Add examples and integration tests that validate virtual device visibility
  through SDL2 for generic gamepad input, DualShock 4 USB input, DualShock 4
  Bluetooth controller discovery, DualSense USB input, and DualSense Bluetooth
  controller discovery, plus libinput for keyboard/mouse.
- [x] Document required Linux permissions and sample udev rules.

### Phase 2B: Linux inputtino Parity

- [x] Replace the generic DualSense USB profile behavior with a descriptor-driven
  DualSense report descriptor and 64-byte input report packing.
- [x] Add Bluetooth DualSense descriptor parity, CRC handling, and Bluetooth input
  report framing.
- [x] Add DualSense UHID GET_REPORT replies for calibration, pairing, and firmware
  reports, including MAC/uniq identity handling.
- [x] Add periodic DualSense input reports for consumers that expect steady sensor
  and touchpad updates.
- [x] Add DualSense input state for motion sensors, touchpad contacts, battery
  state, and profile-specific buttons without leaking Linux-specific details
  into consumers.
- [x] Parse DualSense output reports into rumble, RGB LED, adaptive trigger, and
  raw-report callbacks.
- [x] Add DualShock 4 USB and Bluetooth profiles with descriptor-driven input
  reports, touchpad click, touch contacts, motion sensors, battery state,
  lightbar feedback, rumble callbacks, Bluetooth CRC handling, GET_REPORT
  replies, stable MAC identity, periodic reports, and generated sensor
  timestamps.
- [x] Expose created device nodes and sysfs paths through the platform-neutral
  public API.
- [x] Add configurable keyboard auto-repeat for held keys.
- [x] Add touchscreen, trackpad, and pen tablet public device types and Linux
  direct-uinput backend implementations.
- [x] Keep gamepad feedback on UHID output reports. There is no uinput-backed
  gamepad path in this library; if one is added later, it must implement Linux
  force-feedback upload, erase, playback, and gain handling.
- [x] Expand Linux consumer tests so SDL2 validates generic joystick input,
  DualShock 4 USB controller input, DualShock 4 Bluetooth controller discovery,
  DualSense USB controller input, and DualSense Bluetooth controller discovery,
  and libinput validates keyboard, mouse, touchscreen, trackpad, and pen tablet
  events.

### Phase 2C: Linux uinput Hardening

- [x] Prefer libevdev for uinput device construction where it removes fragile
  direct ioctl setup, while keeping the public API unchanged.

### Phase 3: Windows MVP

- [x] Add CMake/WDK integration for the UMDF2 driver package.
- [x] Implement the Windows backend and control channel between the C++ library and
  the UMDF driver.
- [x] Keep the client library buildable with MSVC and MinGW/UCRT64. Keep the driver
  package on the Microsoft WDK toolchain.
- [x] Add install/uninstall tooling for developer workflows.
- [x] Support backend hot-plug, multi-controller instances, and output report callbacks
  through the Windows control protocol.
- [x] Publish Windows gamepads through VHF so DirectInput, SDL/HIDAPI,
  Windows.Gaming.Input/GameInput, and browser Gamepad API can enumerate standard
  HID gamepads. XInput is not applicable to the HID-only backend without a
  consumer-side mapping layer.

### Phase 4: API Parity and Packaging

- [x] Keep one API surface across Windows and Linux, with capability queries for
  platform limitations instead of platform-specific methods.
- [x] Add installed CMake package support and `FetchContent` documentation.
- [x] Add CI for formatting, static analysis, CMake configure/build, unit tests, and
  platform smoke tests.
- [x] Defer C, Python, and Rust bindings until after the platform API is stable,
  likely after macOS support lands.
- [x] Decide whether official Windows releases should ship signed driver packages
  in addition to source.

### Phase 5: Sunshine Replacement Readiness

The Windows driver package is part of the intended design, similar in role to
ViGEmBus as an installable user-mode virtual device component. The remaining
replacement blockers are compatibility, packaging integration, and feature
parity with Sunshine's current ViGEmBus and inputtino behavior, while keeping
one public API across supported operating systems. The API may expose a richer
cross-platform model than any one backend can implement, but backends must report
unsupported features through capabilities instead of forcing consumers onto
platform-specific calls.

#### Phase 5A: Shared Sunshine Adapter

- [x] Add a Sunshine-oriented adapter example or test that maps controller
  arrival, state updates, touchpad contacts, motion samples, battery reports,
  feedback callbacks, and removal through the existing platform-neutral
  `Runtime` and `Gamepad` APIs.
- [x] Preserve Sunshine's asynchronous event shape by caching per-controller
  `GamepadState` and resubmitting after separate button, axis, trigger, touch,
  motion, and battery updates.
- [x] Expand or formally map the public button model so Sunshine's full
  controller flag set is preserved, including guide/home, profile-specific
  misc/share, and rear paddles where the emulated profile can expose them.
- [x] Add profile capability checks for rumble, trigger rumble, RGB LED,
  adaptive triggers, motion, touchpad, battery, and profile-specific buttons so
  Sunshine can keep one code path while warning only when a selected profile
  cannot expose a client-reported feature.

#### Phase 5B: Windows ViGEmBus Parity

- [ ] Validate the UMDF/VHF backend as Sunshine's Windows gamepad replacement
  against the consumers that currently work through ViGEmBus, including SDL,
  HIDAPI, browser Gamepad API, DirectInput, GameInput, and games or clients that
  rely on the current Xbox controller behavior.
- [ ] Decide and document the Windows Xbox compatibility story before replacing
  ViGEmBus. If the HID backend is not accepted by XInput-only consumers, keep a
  compatibility layer, consumer-side mapping, or a retained ViGEmBus path for
  that class of application.
- [x] Add a DualShock 4 compatible profile for Sunshine's current DS4 mode,
  including touchpad click, touch contacts, motion sensors, battery state,
  lightbar feedback, rumble feedback, Bluetooth CRC handling, and timestamp
  behavior.
- [ ] Validate the DualShock 4 profile through Sunshine's Windows path against
  the same applications currently covered by ViGEmBus DS4 mode.
- [ ] Replace Sunshine's ViGEmBus installer, status API, and diagnostics with
  equivalent libvirtualhid driver package checks, install/uninstall flows, and
  signed release packaging.
- [ ] Run Windows lifecycle and multi-controller validation through Sunshine with
  the installed driver package, including hot-plug, output-report callbacks,
  process shutdown cleanup, and simultaneous controllers.

#### Phase 5C: Linux and FreeBSD inputtino Parity

- [ ] Implement a Sunshine Linux adapter that preserves the current `xone`,
  `ds5`, `switch`, and `auto` selection behavior while using the same
  platform-neutral libvirtualhid API as Windows.
- [ ] Validate Linux DualSense parity against inputtino's UHID path: USB and
  Bluetooth reports, calibration/pairing/firmware feature replies, periodic
  input reports, touchpad, motion, battery, rumble, RGB LED, and adaptive trigger
  feedback.
- [ ] Validate Linux DualShock 4 parity against Sunshine's former Windows DS4
  behavior and Linux consumers: USB and Bluetooth reports,
  calibration/pairing/firmware feature replies, periodic input reports, sensor
  timestamps, touchpad, motion, battery, rumble, and RGB LED feedback.
- [ ] Validate Xbox One and Switch Pro behavior through SDL and evdev consumers,
  including d-pad, sticks, triggers, guide/misc buttons, rumble, device names,
  VID/PID/version identity, and stable device-node discovery.
- [ ] Add a FreeBSD backend plan instead of assuming the Linux backend applies
  unchanged. Sunshine disables inputtino `USE_UHID` on FreeBSD, so the current
  FreeBSD path uses the uinput/libevdev-style fallback and does not get the
  UHID-only DualSense features.
- [ ] Keep the FreeBSD API surface identical to Linux and Windows, but report
  FreeBSD's real backend capabilities separately. At minimum, validate basic
  Xbox One, Switch Pro, and uinput-backed PS5-style gamepad creation; only mark
  DualSense touch, motion, battery, RGB LED, adaptive triggers, and Bluetooth
  identity as supported if a FreeBSD backend can actually deliver them.
- [ ] Add FreeBSD configure/build coverage and a smoke-test strategy for the
  supported subset, with explicit documentation for required device nodes,
  permissions, and any FreeBSD-specific package dependencies.

### Phase 6: macOS Research and Backend

- [ ] Prototype macOS virtual HID creation and report submission.
- [ ] Document signing, entitlement, and installer constraints.
- [ ] Add macOS backend behind the existing public API.
- [ ] Add macOS discovery and smoke-test coverage.

## 🧪 Testing Plan

- [ ] Unit test descriptor generation, report packing, axis scaling, button mapping,
  and output report parsing.
- [ ] Run lifecycle tests for create, submit, output callback, destroy, repeated
  hot-plug, and process shutdown cleanup.
- [ ] Validate multi-controller behavior and stable ordering.
- [ ] Test against real consumers where practical: Sunshine, SDL, HIDAPI, browser
  Gamepad API, DirectInput/XInput/GameInput on Windows, and evdev/libinput
  libraries on Linux.

## 📄 License

`libvirtualhid` is licensed under the MIT License. See [LICENSE](LICENSE).
