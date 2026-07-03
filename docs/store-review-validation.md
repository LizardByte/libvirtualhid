# Microsoft Store Review Validation

These instructions are intended for Microsoft Store certification review of the
libvirtualhid Windows driver installer. The reviewer does not need to build the
project, install the Windows SDK/WDK, or write a consumer application.

The Windows package installs a user-mode UMDF/VHF virtual HID driver. The
libvirtualhid-specific driver binary is a UMDF DLL installed through the Windows
Driver Store. It is not a kernel-mode `.sys` driver.

## Submission Notes

Paste this into the Partner Center certification notes field:

```text
This package installs the libvirtualhid Windows user-mode UMDF/VHF virtual HID
driver. It has no standalone end-user UI; applications consume it through the
libvirtualhid client API.

A prebuilt validation tool and PowerShell validation scripts are installed by
the MSI so no programming, SDK setup, or driver build environment is required.
Please install the released, production-signed MSI, then run the validation
commands below from PowerShell.

Default install root:
C:\Program Files\libvirtualhid

Installed validation files:
C:\Program Files\libvirtualhid\scripts\windows\test-installed-driver.ps1
C:\Program Files\libvirtualhid\scripts\windows\test-browser-gamepad.ps1
C:\Program Files\libvirtualhid\tools\windows\gamepad_adapter.exe

Required validation:
$installRoot = Join-Path $env:ProgramFiles "libvirtualhid"
powershell -ExecutionPolicy Bypass -File "$installRoot\scripts\windows\test-installed-driver.ps1" `
  -GamepadAdapterPath "$installRoot\tools\windows\gamepad_adapter.exe" `
  -GamepadProfile xseries `
  -Verbose

Expected result:
- The command exits successfully.
- The ROOT\LIBVIRTUALHID control device reports Status: Started.
- The \\.\LibVirtualHid control device opens successfully.
- A virtual HID gamepad child device starts, matching either
  HID\VID_045E&PID_0B12&IG_00 or HID\VID_045E&PID_02FF&IG_00.

Optional browser validation:
$installRoot = Join-Path $env:ProgramFiles "libvirtualhid"
powershell -ExecutionPolicy Bypass -File "$installRoot\scripts\windows\test-browser-gamepad.ps1" `
  -GamepadAdapterPath "$installRoot\tools\windows\gamepad_adapter.exe" `
  -GamepadProfile xseries `
  -KeepBrowserOpen

Expected result:
- Microsoft Edge or Google Chrome opens a gamepad test page.
- The browser Gamepad API sees an Xbox-compatible controller.
- Button and axis values change while the validation adapter is running.

The installed gamepad_adapter.exe is built with the static MSVC runtime, so it
does not require the Visual C++ Redistributable to be installed separately.
```

## Manual Review Steps

1. Install the released, production-signed
   `libvirtualhid-Windows-Driver-installer.msi`.
2. Reboot only if Windows reports that a reboot is required.
3. Open PowerShell.
4. Run the required validation command from the submission notes.
5. Optionally run the browser validation command.

If the default install location was changed during MSI installation, replace
`$env:ProgramFiles\libvirtualhid` with the selected install directory.

The MSI writes the driver-install transcript to:

```text
C:\ProgramData\libvirtualhid\install-driver.log
```

## Scope Notes

The `x360` profile is not used for Store review. The Windows UMDF/VHF backend is
HID-only and intentionally does not emulate the Xbox 360 XUSB stack.

The reviewer-visible success signal is the installed `ROOT\LIBVIRTUALHID`
control device, the `\\.\LibVirtualHid` control path, and a started HID gamepad
child device while `gamepad_adapter.exe` is running.
