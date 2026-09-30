# Windows driver package

The Windows package lets compatible applications create virtual gamepads,
Raw Input-visible keyboards, and relative mice. It uses a user-mode UMDF2
driver and a local broker service. Xbox 360 uses an XUSB companion for XInput;
other profiles use virtual HID. No libvirtualhid kernel-mode driver is
installed.

The released MSI targets Windows 11 version 21H2 or later on AMD64. Windows 10
version 2004 or later has a best-effort compatibility path. Windows ARM64
release packages are not yet available.

## Install and use

Install the production-signed MSI from the
[libvirtualhid releases](https://github.com/LizardByte/libvirtualhid/releases).
Restart Windows if the installer requests it. The package installs the
`libvirtualhid_broker` service and a diagnostic tool at
`C:\Program Files\libvirtualhid\tools\windows\virtualhid_control.exe` by
default. Applications can then use the normal C++ API without administrator
privileges.

A machine license is required to create a driver-backed device. Open
`virtualhid_control.exe` to activate or refresh a license, select a gamepad
profile, and create a test controller. The tool can submit buttons, axes,
triggers, and mouse actions and show supported feedback. It lists devices
created by the tool; controllers owned by another application are not listed.

Applications may use the public `get_license_status`, `activate_license`,
`validate_license`, and `deactivate_license` APIs to offer the same workflow.
Do not log or store activation keys. A machine activation covers licensed
gamepads, keyboards, and mice on that machine. Successful licensed gamepad
creations are reported as usage on a later validation; routine license checks
do not count as gamepad creation.

The broker validates the license at startup and every 24 hours, retrying
temporary failures about once a minute. While Polar validation is unavailable,
new driver-backed creation is limited to one active licensed device in total,
whether gamepad, keyboard, or mouse. Existing licensed devices remain for up
to one hour; then the broker removes all but one. A yearly license must
validate within 25 hours of its last successful validation or the remaining
device is removed. After Windows restarts, a yearly license needs online
validation before device creation. A previously activated lifetime license
can keep or create one licensed device while Polar is unreachable, with no
offline time limit. The broker keeps retrying validation about once a minute
and returns to the normal 24-hour schedule after success. Confirmed revocation
or deactivation removes existing licensed devices.

Keyboard and mouse have Win32 fallbacks when the driver or license is
unavailable. Those fallback inputs are not Raw Input-visible virtual HID
devices. Unicode text and absolute mouse positioning always use the Win32
path.

## Check an installation

The broker service should be running, and a gamepad created by
`virtualhid_control.exe` should appear in Windows device tools. Use
`joy.cpl` to check buttons and axes. Steam, browsers, and games may use
different input APIs, so test with the intended consumer too. If the
application cannot create a controller, check its log and the license status
in `virtualhid_control.exe`.

Installation and uninstall logs are under `C:\ProgramData\libvirtualhid`.
The UMDF driver log is at
`%WINDIR%\Temp\libvirtualhid-umdf-driver.log`; include it when reporting
driver or device-creation failures. A broker restart removes its existing
virtual devices, so reconnect the application afterward.

## Build the driver package

Contributors need Visual Studio 2022 and the Windows SDK/WDK. This is a
separate MSVC build from the normal library's MSYS2/UCRT64 build:

```powershell
cmake -S . -B cmake-build-windows-driver -G "Visual Studio 17 2022" -A x64 `
  -DLIBVIRTUALHID_BUILD_WINDOWS_DRIVER=ON -DLIBVIRTUALHID_ENABLE_PACKAGING=ON `
  -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=ON -DLIBVIRTUALHID_BUILD_TOOLS=ON
cmake --build cmake-build-windows-driver --config Release `
  --target libvirtualhid_windows_catalog libvirtualhid_driver_setup libvirtualhid_broker gamepad_adapter virtualhid_control
cpack -G WIX -C Release --config .\cmake-build-windows-driver\CPackConfig.cmake
```

Developer install and installed-driver validation helpers are in
`scripts/windows/`. A signed installer and installed consumer tests are needed
to verify real Windows behavior; a library-only build does not do so.
