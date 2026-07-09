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
This package installs the libvirtualhid Windows user-mode UMDF/VHF virtual HID driver and local broker service. Applications consume it through the libvirtualhid client API, and the MSI includes a native diagnostic UI for local validation.

Every virtual gamepad creation requires an active license. A currently granted review license key with an available device activation is supplied separately in the Partner Center certification credentials or notes. The key is not embedded in the package or this document.

Launch the validation tool below.

Default install root:
C:\Program Files\libvirtualhid

Installed validation files:
C:\Program Files\libvirtualhid\tools\windows\virtualhid_control.exe
C:\Program Files\libvirtualhid\tools\windows\gamepad_adapter.exe
C:\Program Files\libvirtualhid\services\windows\libvirtualhid_broker.exe

Required validation:
$installRoot = Join-Path $env:ProgramFiles "libvirtualhid"
& "$installRoot\tools\windows\virtualhid_control.exe"

In the libvirtualhid control window, paste the supplied review key into the License key field and click Activate license. Confirm the status changes to Licensed. Then leave the default Xbox Series profile selected and click Create. Use the button and axis controls in the UI to submit input to the virtual controller.

Expected result:
- The backend status reports windows-umdf with gamepad support available
- The libvirtualhid_broker service is running
- License validation succeeds and the license status reports Licensed
- A virtual HID gamepad is created and appears in the device list
- A virtual HID gamepad child device starts with the Xbox Series HID ID
  HID\VID_045E&PID_0B12&IG_00
- Button, axis, and Share values in the UI can be pressed or moved without
  errors

Optional browser validation:
$installRoot = Join-Path $env:ProgramFiles "libvirtualhid"
& "$installRoot\tools\windows\virtualhid_control.exe"

Create the default Xbox Series gamepad, then open:
https://hardwaretester.com/gamepad

Use the libvirtualhid control window to press buttons or move axes while the browser page is open.

Expected result:
- The browser Gamepad API sees an Xbox-compatible controller
- Button and axis values change while controls are used in the validation UI
```

## Manual Review Steps

1. Install the released, production-signed
   `libvirtualhid-Windows-Driver-installer.msi`.
2. Reboot only if Windows reports that a reboot is required.
3. Open PowerShell.
4. Run the required validation tool from the submission notes.
5. Activate the review key supplied through Partner Center.
6. Create the default gamepad and exercise its controls.
7. Optionally run the browser validation steps.

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
control device, the `\\.\LibVirtualHid` control path, the running
`libvirtualhid_broker` service, and a started HID gamepad child device while
`virtualhid_control.exe` has a gamepad created.
