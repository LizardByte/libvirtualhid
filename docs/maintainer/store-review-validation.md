# Microsoft Store review validation

Use these steps for certification review of the Windows driver MSI. Reviewers
do not need the Windows SDK/WDK or a consuming application. Supply an active
review license key separately through Partner Center; do not embed it in the
package or this document.

## Partner Center notes

```text
Install the production-signed libvirtualhid Windows AMD64 driver MSI. Reboot
only if Windows requests it.

Open PowerShell and run:
$installRoot = Join-Path $env:ProgramFiles "libvirtualhid"
Start-Process "$installRoot\tools\windows\virtualhid_control.exe"

In Virtual HID Control, paste the supplied review key and click Activate
license. Confirm the status is Licensed. Leave Xbox Series selected and click
Create. Use the UI to press buttons and move axes. Then select Mouse, click
Create, and use Tab or the arrow keys to highlight a mouse control. Press
Space or Enter to test movement, buttons, and scrolling.

Expected: the broker is running; gamepad and mouse creation succeed; the
controller appears in the device list; button and axis values change; the
mouse pointer, buttons, and wheel respond.

Optional browser test: with the Xbox Series gamepad created, open
https://hardwaretester.com/gamepad and use the UI to press a button. The
browser should detect the controller and show changing input values.
```

If the MSI was installed elsewhere, replace `$env:ProgramFiles\libvirtualhid`
with the chosen installation directory. The installed UI can manage the
machine license and exercise devices without administrator privileges.

## Release scope and diagnostics

The default review flow exercises Xbox Series. Before claiming Xbox 360
compatibility for a release, also run
`scripts/windows/test-installed-driver.ps1 -GamepadProfile x360` and the
installed-driver `Xbox360PublishesXInputStateAndRumble` integration test.

The MSI writes its install log to
`C:\ProgramData\libvirtualhid\install-driver.log`. The driver log is
`%WINDIR%\Temp\libvirtualhid-umdf-driver.log`. Include these logs when
reporting installation or device-creation failures.
