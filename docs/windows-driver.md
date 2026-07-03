# Windows Driver Package

Windows gamepad support uses a user-mode UMDF2 control driver backed by Virtual
HID Framework. The driver package is separate from the normal C++ library build:
the library remains consumable from MSVC and MinGW/UCRT64, while the driver
package is built with the Microsoft SDK/WDK toolchain.

## Microsoft Store Listing Text

The Windows driver package is not the same product surface as the C++ library,
so Store listing copy should describe the installed driver component.

### Short Description

```text
User-mode virtual HID driver package that enables compatible apps to create virtual gamepads on Windows.
```

### Description

```text
Virtual HID Driver installs the user-mode driver component used by compatible
applications to create virtual HID gamepads on Windows.

The package has no standalone user interface. After installation, compatible
applications can request virtual HID gamepads, and Windows applications that
understand standard HID gamepads can discover those devices.
```

## Architecture

The backend opens the libvirtualhid control device and sends fixed-size C
protocol structures with `DeviceIoControl`. Create requests start a VHF child
device from the requested descriptor, VID/PID, version, and report layout. Input
reports are submitted through VHF, and HID output writes are normalized back to
the C++ output callback path.

The driver opens a separate VHF source target for each virtual gamepad and
parents that target to the control-file handle that created it. If the creating
process exits or crashes, Windows cleans up gamepads that were not explicitly
destroyed.

The backend reports `requires_installed_driver = true` and only advertises
gamepad/output-report support when the control device can be opened. Keyboard
and mouse support do not require the driver package.

## Build

Build the UMDF package with a Visual Studio generator and the WDK installed:

```powershell
cmake -S . -B cmake-build-windows-driver -G "Visual Studio 17 2022" -A x64 `
  -DLIBVIRTUALHID_BUILD_WINDOWS_DRIVER=ON -DLIBVIRTUALHID_ENABLE_PACKAGING=ON `
  -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=ON
cmake --build cmake-build-windows-driver --config Release `
  --target libvirtualhid_windows_catalog gamepad_adapter
cpack -G WIX -C Release --config .\cmake-build-windows-driver\CPackConfig.cmake
```

The package defaults to UMDF 2.15, matching the inbox VHF UMDF source driver
while still exposing the framework APIs used by libvirtualhid. The driver links
the MSVC runtime statically so the UMDF host process does not need VC runtime
DLLs beside the driver.

## Developer Install and Validation

Developer helpers live under `scripts/windows`:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows\install-driver.ps1 `
  -InfPath .\cmake-build-windows-driver\src\platform\windows\driver\package\Release\libvirtualhid.inf `
  -LogPath .\cmake-build-windows-driver\install-driver.log
powershell -ExecutionPolicy Bypass -File .\scripts\windows\test-installed-driver.ps1 `
  -GamepadAdapterPath .\cmake-build-windows-driver\examples\Release\gamepad_adapter.exe `
  -GamepadProfile xseries
powershell -ExecutionPolicy Bypass -File .\scripts\windows\test-browser-gamepad.ps1 `
  -GamepadAdapterPath .\cmake-build-windows-driver\examples\Release\gamepad_adapter.exe `
  -GamepadProfile xseries
powershell -ExecutionPolicy Bypass -File .\scripts\windows\uninstall-driver.ps1 `
  -Force -RemoveCertificateSubject "CN=libvirtualhid CI Test Driver Signing"
```

The WiX installer also places validation files under the default install root,
`C:\Program Files\libvirtualhid`:

- `scripts\windows\test-installed-driver.ps1`
- `scripts\windows\test-browser-gamepad.ps1`
- `tools\windows\gamepad_adapter.exe`

The install helper stages the INF with `pnputil`, updates an existing
`ROOT\LIBVIRTUALHID` device when present, and creates that root-enumerated
device when it is missing. It uses SetupAPI/NewDev directly, so MSI installs do
not require WDK tools on the target machine.

The installed-driver test fails if the root device is not started, if
`\\.\LibVirtualHid` cannot be opened, or if a held `gamepad_adapter` instance
does not produce a started HID child device. The browser helper launches a
desktop browser at `https://hardwaretester.com/gamepad` and validates that the
browser Gamepad API observes the held virtual controller.

For manual browser validation, run the browser helper with `-KeepBrowserOpen`,
or run:

```powershell
tools\windows\gamepad_adapter.exe xseries --hold-seconds 60
```

Then open `https://hardwaretester.com/gamepad` in a normal desktop browser and
press one of the held virtual buttons if the browser requires a gamepad
activation event.

## Installation Notes

The driver binary is a user-mode UMDF DLL installed through the Windows Driver
Store, not a libvirtualhid `.sys` copied into `C:\Windows\System32\drivers`.
Windows still uses its built-in `WUDFRd.sys` and VHF components under
`System32\drivers`.

The libvirtualhid-specific sign that installation completed is the
`ROOT\LIBVIRTUALHID` root device and the `\\.\LibVirtualHid` control device.
Development driver builds write a lightweight UMDF trace to:

```text
C:\Windows\Temp\libvirtualhid-umdf-driver.log
```

During rapid development reinstalls, the fixed global control symbolic link can
briefly outlive the previous root device. The driver treats that collision as
non-fatal, and normal clients discover the PnP control device interface first.

## Profile Compatibility

The Windows backend publishes HID gamepads through VHF. DirectInput, SDL/HIDAPI,
Windows.Gaming.Input/GameInput, and browser Gamepad API clients should see
standard HID devices after the driver is installed.

The built-in Xbox One and Xbox Series profiles use XboxGIP-shaped HID
descriptors. The Xbox Series profile keeps the public physical USB identity
`VID_045E&PID_0B12`; the driver also publishes a compatible driver-matching
hardware ID for Windows binding. The Xbox 360 profile is rejected by the
UMDF/VHF backend because a real Xbox 360 controller is an XUSB device rather
than a VHF HID gamepad.

DualShock 4, DualSense, Switch Pro, and generic HID profiles are descriptor
driven. Consumers that display raw HID strings may still show the Windows VHF
product label because VHF does not provide a product/manufacturer string
callback.

## Signing

Windows driver packages require a signed catalog for normal installation.
Pull-request builds generate a short-lived self-signed test certificate, sign
`libvirtualhid.cat`, bundle the public certificate into the WiX installer, and
import it into local machine trust stores during install.

Release builds must use Azure Trusted Signing for the catalog and generated MSI
and must not ship the local pull-request test certificate.
