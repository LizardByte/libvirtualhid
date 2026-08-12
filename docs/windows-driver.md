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

The package includes a local diagnostic UI for creating and testing virtual
gamepads. Compatible applications can also request virtual HID gamepads, and
Windows applications that understand standard HID gamepads can discover those
devices.
```

## Architecture

Windows gamepad creation is brokered by `libvirtualhid_broker`. The normal C++
backend asks the broker service to create and destroy gamepads through a local
named pipe, while input reports stay on the direct driver path after creation.
This keeps license and active-device checks outside the input hot path.

The broker pipe explicitly grants local authenticated users generic read access
plus the individual data-write and attribute-write rights needed to exchange
request and response messages in message mode. It does not grant clients the
right to create pipe instances, and it rejects remote clients. This allows a
normal desktop application to use the broker without running as administrator
while keeping broker ownership and privileged device operations in the Windows
service.

The backend sends fixed-size C protocol structures to the broker. A create
request identifies the backend's existing control handle; the broker duplicates
that handle from the named-pipe client process and issues `DeviceIoControl` on
the same file object. This starts a VHF child device from the requested
descriptor, VID/PID, version, and report layout while preserving handle-scoped
output delivery. The driver returns a per-device session token, and
submit/destroy requests include that token so stale or unrelated clients cannot
control devices they did not create. Input reports are submitted through VHF,
and HID output writes are normalized back to the C++ output callback path.

The driver rejects gamepad create and destroy IOCTLs unless the requestor token
contains the `NT SERVICE\libvirtualhid_broker` service SID. On the first boot
after installation, before Windows applies a newly configured service SID to
the process token, the driver instead requires the requestor PID to match the
SCM-registered, currently running broker service. Administrators still control
installation, repair, replacement, and service diagnostics through the normal
Windows service and driver-management tools, but they are not a separate runtime
bypass for creating or destroying virtual devices.

The library and installed driver must use the same control-protocol version.
Protocol version 2 expands the report-descriptor capacity to 2048 bytes for the
complete DirectInput PID descriptor; a version mismatch is rejected rather
than interpreting a differently sized request.

Each backend runtime uses one control-file handle for commands and its pending
output read. Broker protocol version 2 preserves that association by duplicating
the handle only for the authorized create IOCTL. The driver associates output
events with that file object, so feedback from a virtual gamepad is delivered
only to the runtime that created it instead of being consumed by another
libvirtualhid client.

The driver opens a separate VHF source target for each virtual gamepad and
parents that target to the control-file handle that created it. If the creating
process exits or crashes, Windows cleans up gamepads that were not explicitly
destroyed. In brokered driver packages, the broker owns that control-file handle.
The broker tracks the requesting client process for each created device and
destroys broker-owned devices when that client process exits unexpectedly.

The backend reports `requires_installed_driver = true` and only advertises
gamepad/output-report support when the broker is reachable and the control
device can be opened. Keyboard and mouse support do not require the driver
package.

## Build

Build the UMDF package with a Visual Studio generator and the WDK installed:

```powershell
cmake -S . -B cmake-build-windows-driver -G "Visual Studio 17 2022" -A x64 `
  -DLIBVIRTUALHID_BUILD_WINDOWS_DRIVER=ON -DLIBVIRTUALHID_ENABLE_PACKAGING=ON `
  -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=ON -DLIBVIRTUALHID_BUILD_TOOLS=ON
cmake --build cmake-build-windows-driver --config Release `
  --target libvirtualhid_windows_catalog libvirtualhid_broker gamepad_adapter virtualhid_control
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
  -BrokerPath .\cmake-build-windows-driver\src\platform\windows\broker\Release\libvirtualhid_broker.exe `
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

- `tools\windows\gamepad_adapter.exe`
- `tools\windows\virtualhid_control.exe`
- `services\windows\libvirtualhid_broker.exe`

The source-tree validation scripts remain developer and CI helpers. They are not
packaged as reviewer-facing MSI validation scripts because the native
`virtualhid_control.exe` tool can create, exercise, and inspect virtual
gamepads interactively.

The install helper stages the INF with `pnputil`, updates an existing
`ROOT\LIBVIRTUALHID` device when present, and creates that root-enumerated
device when it is missing. It uses SetupAPI/NewDev directly, so MSI installs do
not require WDK tools on the target machine. When a broker executable is present,
the helper also installs and starts the `libvirtualhid_broker` Windows service
with a service SID. The service `ImagePath` is stored as a literal quoted path,
and installation fails if the registry value is not safely quoted. This avoids
CWE-428 unquoted-service-path escalation when the install root contains spaces.
The install helper also clears any legacy broker service `Environment` value so
licensing configuration cannot be overridden on the user's machine. The uninstall
helper stops and deletes that service before removing the driver package.

The installed-driver test fails if the root device is not started, if
`\\.\LibVirtualHid` cannot be opened, or if a held `gamepad_adapter` instance
does not produce a started HID child device. The browser helper launches a
desktop browser at `https://hardwaretester.com/gamepad` and validates that the
browser Gamepad API observes the held virtual controller.

For manual browser validation, run the browser helper with `-KeepBrowserOpen`,
run the interactive UI, or run:

```powershell
tools\windows\gamepad_adapter.exe xseries --hold-seconds 60
```

Then open `https://hardwaretester.com/gamepad` in a normal desktop browser and
press one of the held virtual buttons if the browser requires a gamepad
activation event.

For interactive local validation, run:

```powershell
tools\windows\virtualhid_control.exe
```

The native UI can create, remove, control, and monitor gamepads that it owns.
Buttons are momentary by default, with an explicit lock mode for held inputs.
The UI also shows supported profile features, battery input state, device nodes,
and normalized feedback reports such as rumble, RGB LED, adaptive trigger, and
raw output events. Devices created by another process are not listed yet; that
requires a future Windows control-protocol extension for cross-process
diagnostics.

On Windows, the UI also shows broker license status. It can activate a license
key, refresh validation, deactivate the current machine, and open
compiled purchase or account-management URLs.

## Installation Notes

The driver binary is a user-mode UMDF DLL installed through the Windows Driver
Store, not a libvirtualhid `.sys` copied into `C:\Windows\System32\drivers`.
Windows still uses its built-in `WUDFRd.sys` and VHF components under
`System32\drivers`.

The libvirtualhid-specific sign that installation completed is the
`ROOT\LIBVIRTUALHID` root device, the `\\.\LibVirtualHid` control device, and
the running `libvirtualhid_broker` service.

Host applications can present the same license workflow through the installed
public C++ API. Include `libvirtualhid/license.hpp` (or the aggregate
`libvirtualhid/libvirtualhid.hpp`) and call `get_license_status`,
`activate_license`, `validate_license`, or `deactivate_license`. The API uses
provider-neutral types, sends activation keys directly to the local broker,
and returns purchase and account-management URLs with the status. Applications
must treat activation keys as transient secrets and must not persist or log
them.

### Driver Diagnostic Logs

The UMDF driver writes lifecycle events and operational failures to the
following path (normally `C:\Windows\Temp`):

```text
%WINDIR%\Temp\libvirtualhid-umdf-driver.log
```

Successful input reports are deliberately excluded because they are the
latency-sensitive hot path. When the active log would exceed 5 MiB, the driver
rotates it before writing the next entry. Five previous logs are retained as
`libvirtualhid-umdf-driver.log.1` through
`libvirtualhid-umdf-driver.log.5`; `.1` is the newest backup. The active log
and all numbered backups use at most approximately 30 MiB in total. Include
the active log and any numbered backups when reporting a driver installation,
device-lifecycle, authorization, or input-submission problem.

During rapid development reinstalls, the fixed global control symbolic link can
briefly outlive the previous root device. The driver treats that collision as
non-fatal, and normal clients discover the PnP control device interface first.

The broker stores machine-scoped license state in:

```text
C:\ProgramData\libvirtualhid\license.dat
```

The file is protected with Windows DPAPI local-machine scope. GitHub Actions
evaluation timing is stored separately with the same protection in
`C:\ProgramData\libvirtualhid\github-actions-evaluation.dat`. Broker entitlement
configuration is compiled into the Windows broker and diagnostic UI. Update
`src/platform/windows/shared/lvh_windows_broker_config.hpp` when the Polar
organization ID, allowed license-key benefit IDs, Checkout Links, customer
portal URL changes, then rebuild the Windows package. No Polar access token or
webhook secret is compiled into the client:
activation, validation, and deactivation use Polar's
[public customer license-key API](https://polar.sh/docs/features/benefits/license-keys).

The production configuration accepts organization
`3db9f05a-44d7-42f1-ba7c-a0f198235fb7` with yearly license-key benefit
`eb316dac-bf6a-4359-95a2-86c299d48ecc` or lifetime license-key benefit
`157374cb-f526-4154-81ba-9f2c92a053ca`. Polar's public response identifies the
benefit rather than the purchased product, so the broker fails closed unless the
returned organization and benefit are both allow-listed. The purchase button
opens the shared persistent Polar Checkout Link. Account management opens the
[LizardByte LLC Polar customer portal](https://polar.sh/lizardbyte-llc/portal),
where customers can manage their five allowed machine activations.

Normal Windows UMDF gamepad creation requires a current successful license
validation response before the broker calls the driver. The sole exception is
for CI runners where the broker service itself has the `GITHUB_ACTIONS`
environment marker. That environment receives one machine-scoped five-minute
evaluation window beginning with its first unlicensed creation attempt. The
start survives broker restarts, clock rollback expires the window, and the
broker destroys evaluation-created devices when the deadline is reached.
Setting `GITHUB_ACTIONS` only in a consuming application does not affect the
separately running service.

Polar's `limit_activations` value is the machine limit and is configured as `5`
on both license-key benefits. The broker gives yearly and lifetime licenses the
same full local access when the provider reports the key status as `granted`. Polar
revokes a subscription benefit when its entitlement ends. Licensed access has
no local active-device cap, and there is no production offline grace period.

## Profile Compatibility

The Windows backend publishes HID gamepads through VHF. DirectInput, SDL/HIDAPI,
Windows.Gaming.Input/GameInput, and browser Gamepad API clients should see
standard HID devices after the driver is installed.

The built-in Xbox One profile uses its XboxGIP-shaped HID descriptor. The public
Xbox Series profile remains `VID_045E&PID_0B12`; the Windows transport presents
it with release `0x0509` and the `VID_045E&PID_0B12&IG_00` XInputHID match ID
observed from physical Xbox Series USB and Xbox Wireless Adapter connections.
The VHF child preserves the native 17-byte GIP-shaped input report, including
Share/Misc as button bit 12, and the report parser accepts the native eight-byte
four-motor Xbox payload when a consumer delivers it. Physical Xbox Series USB,
Bluetooth, and Xbox Wireless Adapter transports register in Steam through the
Xbox HIDAPI path with Share mapped as `misc1:b11`; the VHF child does not follow
that same consumer path or guarantee registration as an XInput slot. A
Steam-visible Xbox Series Share button on Windows requires a non-VHF Xbox
HIDAPI/GIP transport. The Xbox 360 profile is rejected by the UMDF/VHF backend
because a real Xbox 360 controller is an XUSB device rather than a VHF HID
gamepad.

DualShock 4 and DualSense answer the calibration, pairing, and firmware feature
requests used by their Windows HIDAPI initialization paths. Switch Pro answers
the native USB and subcommand handshake and submits native `0x30` input reports.
The built-in Generic profile is presented to Windows as a DirectInput PID
Joystick with the complete output-report set required for DirectInput
enumeration. Constant Force and Sine output is normalized to the portable
gamepad rumble callback; other declared effect payloads are ignored safely. The
backend honors PID start delay, duration, and loop count, and automatically
stops finite effects. These changes remain private to the Windows transport and
do not alter the public platform-neutral profile API.

Consumers that display raw HID strings may still show the Windows VHF product
label because VHF does not provide a product/manufacturer string callback.

## Signing

Windows driver packages require a signed catalog for normal installation.
Pull-request builds generate a short-lived self-signed test certificate, sign
`libvirtualhid.cat`, bundle the public certificate into the WiX installer, and
import it into local machine trust stores during install.

Release builds must use Azure Trusted Signing for the catalog and generated MSI
and must not ship the local pull-request test certificate.

## License

The Windows UMDF driver source and generated Windows driver package artifacts,
including the driver MSI, are licensed under the LizardByte Source-Available
License 1.0 (LB-SAL 1.0). See the [license map](../LICENSES/README.md) for the
full repository license split. The MSI may also include MIT-licensed helper
components from this repository, so packaged installs include both license
texts.
