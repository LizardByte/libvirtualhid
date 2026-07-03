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

More complete examples live in [examples](examples), including a
streaming-host-oriented [gamepad adapter](examples/gamepad_adapter.cpp).

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

## 📄 License

The cross-platform `libvirtualhid` library is licensed under the
[MIT License](LICENSE). The Windows UMDF driver source and generated Windows
driver package artifacts, including the driver MSI, are licensed under the
[LizardByte Source-Available License 1.0](LICENSES/LicenseRef-LizardByte-SAL-1.0.md)
(LB-SAL 1.0). See the [license map](LICENSES/README.md) for the full repository
split.
