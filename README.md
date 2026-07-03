<div align="center">
  <img
    src="libvirtualhid.svg"
    alt="libvirtualhid icon"
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

`libvirtualhid` provides a platform-neutral C++ API for creating virtual input
devices. The primary target is gamepad input for remote streaming hosts and
other low-latency applications, with keyboard, mouse, touchscreen, trackpad, and
pen tablet support available where the platform backend supports those device
types.

Consumers work with portable concepts such as runtimes, device profiles,
normalized gamepad state, output callbacks, and device nodes. Platform-specific
details such as Linux `uhid`/`uinput` or the Windows UMDF/VHF driver package stay
behind backend implementations.

## 🎮 Capabilities

- Gamepad profiles for generic HID, Xbox 360, Xbox One, Xbox Series,
  DualShock 4, DualSense, and Nintendo Switch Pro-style controllers.
- Descriptor-driven Linux gamepads through `uhid`, plus keyboard, mouse,
  touchscreen, trackpad, and pen tablet devices through `uinput`.
- Windows gamepads through a user-mode UMDF2 control driver backed by Virtual
  HID Framework, with keyboard and mouse support through normal Win32 APIs.
- Output callbacks for profile-specific feedback such as rumble, LEDs,
  adaptive triggers, and raw HID output reports when available.
- CMake consumption through installed packages, vendored source,
  `add_subdirectory`, or `FetchContent`.

## 🚀 Quick Start

```cpp
#include <libvirtualhid/libvirtualhid.hpp>

auto runtime = lvh::Runtime::create();
auto created = runtime->create_gamepad(lvh::profiles::dualsense());
if (!created) {
  return;
}

auto &gamepad = *created.gamepad;
gamepad.set_output_callback([](const lvh::GamepadOutput &output) {
  // Forward rumble, LEDs, adaptive triggers, or raw reports to the client.
});

lvh::GamepadState state;
state.buttons.set(lvh::GamepadButton::a, true);
state.left_stick = {0.25F, -0.5F};
state.right_trigger = 1.0F;

gamepad.submit(state);
```

More complete examples live in `examples/`, including the streaming-host-oriented
`examples/gamepad_adapter.cpp`.

## 📚 Documentation

- [Usage and API](docs/usage.md): CMake consumption, build options, public API
  overview, profiles, and examples.
- [Platform support](docs/platform-support.md): backend capability model,
  Windows, Linux, macOS, and Linux permission setup.
- [Windows driver package](docs/windows-driver.md): UMDF/VHF package build,
  installation, validation, diagnostics, and signing notes.
- [Streaming-host integration](docs/streaming-host-integration.md): integration
  contract and remaining replacement-readiness work for streaming hosts.
- [Development](docs/development.md): local build/test commands, repository
  layout, docs generation, and roadmap.
- [Microsoft Store validation](docs/store-review-validation.md): review notes
  and manual validation for Store submissions.

## 🎯 Scope

`libvirtualhid` owns local virtual device creation and input/output report
translation. It does not provide a network protocol, parse client packets, own a
consumer application's configuration system, bypass anti-cheat systems, hide
devices from the OS, or ship a Windows kernel-mode driver.

## 📌 Status

Linux and Windows are the active backends. Linux uses standard user-space kernel
interfaces. Windows remains user-mode: the C++ library talks to a UMDF2 control
driver, and the driver publishes HID gamepads through VHF. macOS support is not
implemented yet.

The library is designed around gamepad use first because remote streaming hosts
are the first consumer class. Non-gamepad device types are available through the
same API where the backend exposes them.

## 🔁 Alternatives

Alternatives exist if `libvirtualhid` does not meet your needs.

| Feature                           | `libvirtualhid`                                  | [ViGEmBus](https://github.com/nefarius/ViGEmBus) | [HIDMaestro](https://github.com/hifihedgehog/HIDMaestro) | [inputtino](https://github.com/games-on-whales/inputtino) | [WinUHid](https://github.com/cgutman/WinUHid)    |
|-----------------------------------|--------------------------------------------------|--------------------------------------------------|----------------------------------------------------------|-----------------------------------------------------------|--------------------------------------------------|
| Windows support                   | ✅                                                | ✅                                                | ✅                                                        | ❌                                                         | ✅                                                |
| Linux support                     | ✅                                                | ❌                                                | ❌                                                        | ✅                                                         | ❌                                                |
| Windows AMD64 support             | ✅                                                | ✅                                                | ✅                                                        | -                                                         | ✅                                                |
| Windows ARM64 support             | ❌<sup><a href="#alternatives-note-5">5</a></sup> | ✅                                                | ❌<sup><a href="#alternatives-note-6">6</a></sup>         | -                                                         | ✅                                                |
| Windows user-mode driver          | ✅                                                | ❌                                                | ✅                                                        | -                                                         | ✅                                                |
| No Windows kernel-mode driver     | ✅                                                | ❌                                                | ✅                                                        | -                                                         | ✅                                                |
| Descriptor-defined HID devices    | ✅                                                | ❌                                                | ✅                                                        | ✅<sup><a href="#alternatives-note-1">1</a></sup>          | ✅                                                |
| Platform-neutral C++ API          | ✅                                                | ❌<sup><a href="#alternatives-note-2">2</a></sup> | ❌<sup><a href="#alternatives-note-2">2</a></sup>         | ❌<sup><a href="#alternatives-note-3">3</a></sup>          | ❌<sup><a href="#alternatives-note-2">2</a></sup> |
| Keyboard                          | ✅                                                | ❌                                                | ❌                                                        | ✅                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Mouse                             | ✅                                                | ❌                                                | ❌                                                        | ✅                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Touchscreen, trackpad, or pen     | ✅                                                | ❌                                                | ❌                                                        | ✅                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Generic HID gamepad               | ✅                                                | ❌                                                | ✅                                                        | ❌                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Xbox 360 gamepad                  | ✅                                                | ✅                                                | ✅                                                        | ❌                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Xbox One gamepad                  | ✅                                                | ❌                                                | ✅                                                        | ✅                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Xbox Series gamepad               | ✅                                                | ❌                                                | ✅                                                        | ❌                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| DualShock 4 gamepad               | ✅                                                | ✅                                                | ✅                                                        | ❌                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| DualSense gamepad                 | ✅                                                | ❌                                                | ✅                                                        | ✅                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Nintendo Switch Pro-style gamepad | ✅                                                | ❌                                                | ✅                                                        | ✅                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Rumble or output callbacks        | ✅                                                | ❌                                                | ✅                                                        | ✅                                                         | ✅<sup><a href="#alternatives-note-4">4</a></sup> |
| Data-driven profiles              | ❌                                                | ❌                                                | ✅                                                        | ❌                                                         | ❌                                                |
| Actively developed                | ✅                                                | ❌                                                | ✅                                                        | ✅                                                         | ✅                                                |

<a id="alternatives-note-1"></a><sup>1</sup> inputtino uses `uhid` for
virtual joypads; its other listed device types use Linux input interfaces rather
than generic HID descriptors.

<a id="alternatives-note-2"></a><sup>2</sup> Windows-only project, so it does
not provide a platform-neutral API surface.

<a id="alternatives-note-3"></a><sup>3</sup> Linux-only project, so it does not
provide a platform-neutral API surface.

<a id="alternatives-note-4"></a><sup>4</sup> WinUHid is a framework-level
virtual HID target; support requires a custom HID descriptor and matching report
handling rather than a ready-made device profile.

<a id="alternatives-note-5"></a><sup>5</sup> libvirtualhid has
architecture-aware WiX packaging, but the built Windows
driver package target is AMD64 today. Windows ARM64 release driver packages
require a Microsoft dashboard signing path, such as WHQL/Hardware Dev Center
signing; the current Azure Trusted Signing catalog/MSI flow is not sufficient
for ARM64 release driver package installation.

<a id="alternatives-note-6"></a><sup>6</sup> HIDMaestro documents a `win-x64`
test app path and does not currently advertise an ARM64 build.

## 📄 License

The cross-platform `libvirtualhid` library is licensed under the MIT License.
The Windows UMDF driver source and generated Windows
driver package artifacts, including the driver MSI, are licensed under the
[LizardByte Source-Available License 1.0](LICENSES/LicenseRef-LizardByte-SAL-1.0.md)
(LB-SAL 1.0). See the [license map](LICENSES/README.md) for the full repository
split.
