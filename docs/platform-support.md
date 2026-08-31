# Platform Support

`libvirtualhid` keeps the public C++ API platform-neutral. Consumers ask the
runtime for capabilities, create devices from profiles, submit normalized state,
and receive output callbacks. Backend-specific virtual HID details stay inside
the platform implementation.

## Capability Model

Backends report what is available at runtime. A backend can be selectable while
still reporting that a specific device type is unavailable because permissions,
kernel modules, driver installation, or platform features are missing. Device
creation then returns an operation status instead of forcing consumers onto
platform-specific probing code.

Use capability queries for behavior such as:

- Whether the backend can create virtual HID devices.
- Whether gamepad output reports are supported.
- Whether keyboard, mouse, touchscreen, trackpad, or pen tablet creation is
  available.
- Whether the X11/XTest keyboard and mouse fallback is active.
- Whether a Windows driver package must be installed.

## Windows

The Windows backend keeps the normal C++ library buildable with MSVC and
MinGW/UCRT64. Gamepad creation, Raw Input-visible keyboard input, and Raw
Input-visible relative mouse input use a user-mode UMDF2 control driver and
Windows Virtual HID Framework. Keyboard text input, absolute mouse input, and
the keyboard and mouse fallbacks use Win32 APIs.

The C++ library communicates with the driver through fixed-size protocol
structures and `DeviceIoControl`, not C++ STL types. This keeps the public API
compiler-neutral and preserves the boundary between the MinGW/MSVC client
library and the WDK/MSVC driver package.

When the driver is installed and licensed, the backend publishes HID gamepads,
keyboards, and mice that standard HID and Raw Input consumers can enumerate.
Gamepad consumers include SDL/HIDAPI, DirectInput,
Windows.Gaming.Input/GameInput, and browser Gamepad API clients. XInput is not
a direct target of the HID backend.

Driver-backed keyboard key transitions use a standard keyboard-page HID report
with modifier state and sixteen simultaneous non-modifier usages. Unicode text
requests and keyboard-page keys that the descriptor cannot represent retain
the Win32 injection path. If driver or license creation is unavailable,
keyboard creation falls back to the existing Win32 implementation. Unexpected
protocol and driver failures remain visible to the caller instead of silently
changing the input path.

The library and driver must use the same Windows control-protocol version. A
descriptor-capacity change therefore increments the protocol version so a
stale installed driver fails creation explicitly instead of misreading the
request.

The built-in Generic profile remains a platform-neutral Game Pad publicly. At
the Windows transport boundary, VHF presents it as a DirectInput-compatible
Joystick with the complete PID output-report contract required by DirectInput.
Constant Force and Sine effects are normalized back into the same rumble
callback used by the other backends; unsupported PID effect payloads are
accepted without producing misleading feedback. Windows also applies
DirectInput's idle-at-maximum Z/Rz trigger polarity. Start delay, duration, and
loop count are honored; finite effects emit a zero-rumble callback when they
expire, while explicit stop commands take effect immediately.

Xbox One uses the native eight-byte PID payload exposed by the Windows Xbox HID
stack. Xbox Series keeps the `0x045E:0x0B12` identity, release `0x0509`, and the
`0x045E:0x0B12&IG_00` XInputHID match ID observed from physical Xbox Series USB
and Xbox Wireless Adapter connections. The VHF device preserves the native
17-byte GIP-shaped input report and native eight-byte four-motor rumble payload.
The public Xbox Series profile remains `0x045E:0x0B12`; the Windows transport
applies the captured release at device creation. Xbox One accepts native HID
rumble writes. The Xbox Series report parser accepts the native eight-byte
four-motor payload when a consumer delivers it, applies its actuator-enable
mask and duration field, and reports the body motors as normalized
low/high-frequency rumble and the independent trigger motors as trigger-rumble
output.

The VHF driver answers the calibration, pairing, and firmware feature reports
used to initialize DualShock 4 and DualSense HIDAPI output. It also answers the
Switch Pro USB and subcommand initialization sequence and accepts the native
`0x30` input layout, so descriptor-aware consumers can initialize those
controllers before sending their native output reports. The Switch Pro profile
uses the `0x0210` hardware revision reported by a physical Nintendo controller,
and the Windows VHF device exposes that revision to HID consumers. Full-state
and subcommand-reply reports use the same per-device packet counter on Windows,
matching the counter that a native controller advances for every input report.
The Windows client backend caches the newest complete Switch Pro state and
streams native `0x30` reports every 15 milliseconds. This coalesces separate
acceleration and gyroscope API updates into the three-sample report cadence used
by a physical USB controller.

For every gamepad report ID, the VHF driver caches the newest complete input
report and answers synchronous `GetInputReport` requests from that cache. This
lets Windows HID consumers retrieve the current battery state for Xbox One,
Xbox Series, DualShock 4, DualSense, and Switch Pro instead of relying only on
the asynchronous input stream.

That HID report does not change the XInput battery classification of the VHF
device. On a Windows desktop where XInput enumerated the virtual Xbox
controller, `XInputGetBatteryInformation` returned `BATTERY_TYPE_DISCONNECTED`
and `BATTERY_LEVEL_EMPTY` even while its input was available through
`XInputGetState`. Headless Windows CI did not expose an XInput slot for the same
device. Neither path exposes the remote battery through XInput. Consumers that
prefer XInput, including SDL's correlated Windows Xbox path and Windows Game
Bar, therefore do not receive the remote Xbox battery value. DualShock 4,
DualSense, and Switch Pro battery state is independently covered through SDL's
HID path.

The current Steam client displays its controller battery indicator only when it
classifies the device as Bluetooth or wireless. Because VHF exposes a wired
virtual transport, Steam can hide the battery indicator for every Windows
profile even when another HID consumer can retrieve the submitted value.

Windows VHF devices do not expose a Bluetooth transport identity to HIDAPI.
The Windows backend therefore reports DualShock 4 and DualSense requests as
effective USB profiles through `Gamepad::profile()` and uses the matching USB
descriptor, input reports, output reports, and feature-report framing. This
keeps Steam and SDL's transport detection aligned with the reports accepted by
the driver, including rumble and RGB LED output. The DualSense firmware feature
report identifies the base controller's `0x0004` software series and current
`0x0630` device software instead of reporting DualSense Edge series `0x0044`
with the older `0x0154` revision. Linux keeps the Bluetooth defaults described
below.

See [Windows driver package](windows-driver.md) for build, install, validation,
and signing details.

## Linux

The Linux backend uses standard user-space kernel interfaces:

- `uhid` for descriptor-driven PlayStation, Switch Pro, Xbox One, and Xbox
  Series gamepads.
- `uinput` for Generic and Xbox 360 gamepads, for Xbox One and Xbox Series when
  `uhid` is unavailable, and for keyboard, mouse, touchscreen, trackpad, and pen
  tablet devices.
- `libevdev` internally for uinput device construction.
- X11/XTest only as a keyboard and mouse fallback when `uinput` cannot be used
  and an X11 session is available.

The uinput mouse advertises the legacy `REL_WHEEL` and `REL_HWHEEL` axes together
with their high-resolution counterparts when the platform provides them. It
accumulates high-resolution input independently for each axis and emits a legacy
detent for every 120 accumulated units. This follows the Linux input protocol,
lets libinput recognize the device as wheel-capable, and prevents libinput from
reserving the physical middle button for button scrolling.

Gamepad support normally prefers `uhid` because descriptors, raw HID identity,
feature reports, and output reports matter for controller compatibility. Xbox
One and Xbox Series use backend-only Bluetooth identities with a BLE descriptor,
sparse input bitmap, four-motor output framing, and the native report-ID `0x04`
battery notification. When `CreateGamepadOptions::metadata.has_battery` is true,
the descriptor exposes the notification's four categorical wireless charge
levels through a byte-aligned standard HID Battery Strength field and the
backend emits it only when the submitted state contains battery data. Clients
without battery support therefore do not create a phantom Linux power device
or receive a fabricated charge level. The normal input report keeps
the native byte layout used by HIDAPI while advertising `Rx`/`Ry` for the right
stick and `Z`/`Rz` for the triggers, so Linux evdev exposes the canonical
`ABS_RX`/`ABS_RY` and `ABS_Z`/`ABS_RZ` axes expected by Steam. This keeps the bus,
vendor/product identity, descriptor, and reports consistent so Linux input,
Steam, SDL2, and SDL3 select the canonical Xbox mapping and can expose ordinary
and independent trigger rumble. A Bluetooth transport is necessary because
Linux HIDAPI implementations require a physical USB parent for `BUS_USB`
hidraw devices, which a user-space UHID device cannot provide. This transport
override does not change the public Xbox profiles or the platform-neutral API.

Generic and Xbox 360 profiles use `uinput` so SDL, Steam, browser Gamepad API
implementations, and other evdev consumers receive canonical Linux gamepad
events. Xbox One and Xbox Series use the same path only as a fallback when
`/dev/uhid` cannot be opened or initialized. Face buttons, shoulders, menu
buttons, stick clicks, and Guide use their native evdev codes; sticks use
absolute axes. Every uinput gamepad exposes its directional pad through
`ABS_HAT0X` and `ABS_HAT0Y`. Generic and Xbox triggers remain independent analog
`ABS_Z` and `ABS_RZ` axes. Profiles with rumble support normalize rumble,
constant, periodic, and ramp uinput force-feedback effects back into the public
callback. Each requested playback repetition restarts the effect's ramp and
envelope timing. A zero-length effect remains active until its explicit stop
event, matching the infinite-effect contract used by SDL and Steam. The Linux
backend lets a new uinput device settle before reading those effects, so an
early poll error cannot disable feedback for the device lifetime.

Generated UHID nodes are correlated by stable physical and unique identifiers
when available, with device-name matching used only as a fallback. UHID
identities include the virtual profile's vendor and product IDs so applications
do not reuse metadata from another profile after the same virtual slot changes
profiles. PlayStation rumble is read from native UHID interrupt-channel output
reports.

The Generic profile keeps its public `0x1209:0x0001` identity, USB bus, and
Generic device name at the Linux transport boundary. Its uinput device exposes
D-pad directions once through the standard `ABS_HAT0X` and `ABS_HAT0Y` axes,
which avoids changing the raw button capability surface. It uses a compact
Generic button layout rather than the sparse Xbox button slots.

Xbox 360 retains its `0x045E:0x028E` identity, while its Linux uinput device uses
the Bluetooth bus, so consumers select the sparse button mapping. The Xbox One
and Xbox Series UHID transports use the native Bluetooth product identities
`0x045E:0x0B20` and `0x045E:0x0B13`, respectively. Their Bluetooth HID reports
carry canonical gamepad input, coarse battery levels, and four-motor output,
which the backend decodes into ordinary and independent trigger-rumble
callbacks. The backend maps the continuous percentage to the nearest native
Xbox level exposed by SDL: 10, 40, 70, or 100 percent.

If UHID is unavailable, the Xbox One and Xbox Series uinput fallbacks use the
corresponding Bluetooth product identities (`0x0B20` and `0x0B13`, respectively),
whose standard consumer mappings match the events that uinput exposes. The Xbox
uinput profiles preserve the 15-slot Linux gamepad button sequence: unused
`BTN_C`, `BTN_Z`, `BTN_TL2`, and `BTN_TR2` slots are advertised but never
pressed, keeping face buttons, shoulders, menu buttons, Guide, L3, and R3 at
their expected indices. D-pad directions are reported through the hat axes and
exposed as logical buttons by standard gamepad consumers. The fallback retains
all of those controls, analog trigger input, and ordinary force feedback, but
Linux uinput cannot expose independent trigger motors or native Xbox battery
notifications, so its effective profile clears trigger-rumble and battery
support.

DualShock 4 and DualSense remain on `uhid` so their descriptors, motion,
touchpad, battery, feature reports, and profile-specific output reports stay
available. The backend accepts PlayStation output through both UHID interrupt
and control channels. Numbered control-channel output is normalized before
parsing, whether the kernel includes the report number in the payload or
provides it separately on the UHID event.

The default DualShock 4 and DualSense profiles use Bluetooth framing, avoiding
the parent-USB checks that can make virtual USB devices appear late in Steam.
Explicit USB and Bluetooth factories remain available for consumers that
require a particular transport. DualShock 4 Bluetooth input reports set the
HID-present header flag required by HIDAPI consumers and include the transport
CRC, so a running consumer can accept live input after hotplug. DualSense motion
packing preserves the public meters-per-second-squared and degrees-per-second
units while applying the same
raw sensor calibration used by Inputtino. Periodic PlayStation reports are
repacked at 100 Hz so their sequence number and sensor timestamp continue to
advance even when controller state is unchanged. Periodic and application
submissions are serialized so a repeated report cannot restore stale motion
state after a newer application report.

The backend opens `/dev/uhid` in nonblocking mode, matching the original
asynchronous gamepad registration path. Its event reader is active before
device registration begins, and creation does not report success until the
kernel returns `UHID_START`. This keeps control-channel initialization
available throughout registration and prevents streaming hosts from publishing
a controller before its kernel HID device has started.

On Linux, DualShock 4 and DualSense emit Sony's native `Wireless Controller`
product name for Steam HID discovery. The requested USB or Bluetooth bus,
descriptor, and report framing remain unchanged. This transport-only name is
confined to the Linux backend; public profile names, Windows names, and VHF
behavior are unchanged.

Switch Pro uses Linux `uhid` with its native Nintendo descriptor and identity.
Its backend-only UHID identity advertises Bluetooth transport because SDL2's
Linux HIDAPI rejects virtual `BUS_USB` HIDRAW devices without a physical USB
parent in sysfs. The public profile remains USB and its report framing is
unchanged. The backend answers Nintendo subcommand initialization reports, and
native `0x30` input reports carry buttons, sticks, battery state, and three live
IMU samples.
The public acceleration and gyroscope units remain meters per second squared
and degrees per second; the packer converts them to Nintendo's coordinate
system and sensor scales.

Linux touchscreen and trackpad contacts use the lowest available multitouch
slot while they are active. A newly placed contact receives a new tracking ID,
including when it reuses a slot released by another contact, so replacing one
finger cannot overwrite another active finger in standard evdev consumers.

On descriptor-driven backends, native Switch Pro output reports `0x01` and
`0x10` are decoded into the normalized low- and high-frequency rumble callback.
Set Player Lights subcommand `0x30` additionally produces a `player_leds`
callback with separate solid and flashing states for the four indicators. The
Set HOME Light subcommand `0x38` produces a grayscale `rgb_led` callback whose
equal channels preserve the requested monochrome intensity. The original native
report remains available in `GamepadOutput::raw_report`.

The optional `virtualhid_control` diagnostic UI uses SDL3 and Dear ImGui through
the repository CPM lockfile. It is intended to stay on the same UI framework for
Windows, Linux, and future macOS support. Static Linux linking is possible only
when the target distribution provides static archives for all selected backend
and UI dependencies, including SDL3, `libevdev`, and any enabled X11/XTest
libraries. Many distro toolchains intentionally omit some static archives, so
release packaging should keep full static linking as a packaging-mode choice
rather than an unconditional default.

The UI can create and exercise both gamepads and mice. Its mouse movement,
button, and wheel controls participate in Dear ImGui keyboard navigation; use
Tab or the arrow keys to highlight them and Space or Enter to activate them.
Mouse buttons are momentary. A delayed browser-test mode queues an action long
enough to switch focus to an external event tester, sending button actions as a
single press-and-release click.

### Permissions

Linux deployment requires both device-node permissions and the kernel modules
for the selected virtual-controller path. Install udev rules such as
`/etc/udev/rules.d/60-libvirtualhid.rules`:

```udev
# Allows libvirtualhid consumers to access /dev/uinput
KERNEL=="uinput", SUBSYSTEM=="misc", OPTIONS+="static_node=uinput", GROUP="input", MODE="0660", TAG+="uaccess"

# Allows libvirtualhid consumers to access /dev/uhid
KERNEL=="uhid", GROUP="input", MODE="0660", TAG+="uaccess"
```

UHID gamepads use a stable `libvirtualhid/uhid/*` physical path even when the
library is compiled directly into a consuming application. Match that path for
generated `hidraw` and input event nodes because native profiles such as
DualShock 4 and DualSense intentionally do not retain the application's product
name. For `hidraw`, Linux exposes `HID_PHYS` and `HID_NAME` as uevent properties
on the HID parent rather than as sysfs attributes. Import those parent
properties before matching them:

```udev
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", IMPORT{parent}="HID_*"
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", ENV{HID_PHYS}=="libvirtualhid/uhid/*", GROUP="input", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{phys}=="libvirtualhid/uhid/*", GROUP="input", MODE="0660", TAG+="uaccess"
```

Do not replace the `hidraw` import and `ENV{HID_PHYS}` match with
`ATTRS{phys}`. `udevadm verify` validates rule syntax, but it does not prove
that the matched sysfs attribute exists on the device's parent chain.

Consuming applications may additionally install name-matched rules for stable
virtual device names, including uinput-backed gamepads. The `hidraw` rule below
uses `HID_NAME` imported by the preceding `IMPORT{parent}` rule:

```udev
SUBSYSTEM=="hidraw", KERNEL=="hidraw*", ENV{HID_NAME}=="Your App Controller*", GROUP="input", MODE="0660", TAG+="uaccess"
SUBSYSTEM=="input", KERNEL=="event*", ATTRS{name}=="Your App Controller*", GROUP="input", MODE="0660", TAG+="uaccess"
```

For gamepad support, install a modules-load entry such as
`/etc/modules-load.d/60-libvirtualhid.conf`. `hid_playstation` enables the
kernel force-feedback path used by virtual DualShock 4 and DualSense
controllers; descriptor-aware HIDAPI clients can also write their native
output reports through `hidraw`:

```text
uhid
uinput
hid_playstation
```

After installing the rules, load the modules, reload udev, and trigger the
device nodes:

```bash
sudo modprobe uhid
sudo modprobe uinput
sudo modprobe hid_playstation
sudo udevadm control --reload-rules
sudo udevadm trigger --property-match=DEVNAME=/dev/uinput
sudo udevadm trigger --property-match=DEVNAME=/dev/uhid
sudo udevadm trigger --subsystem-match=hidraw
sudo udevadm trigger --subsystem-match=input
```

UHID gamepad nodes are recreated whenever a consumer destroys and recreates a
virtual controller. Manual `chmod` or `setfacl` changes apply only to the
current node and disappear after recreation; install a matching udev rule for
persistent access.

If input still does not work, add the user running the consuming application to
the `input` group, then log out and back in:

```bash
sudo usermod -aG input $USER
```

## FreeBSD

The FreeBSD backend uses the native evdev compatibility stack through
`libevdev` and uinput. It accepts both `/dev/input/uinput`, which is the native
FreeBSD path, and `/dev/uinput` for environments that provide the Linux-style
alias. It supports the same uinput device categories as the Linux backend:

- Generic, Xbox 360, Xbox One, Xbox Series, DualShock 4, DualSense, and Switch
  Pro gamepads.
- Keyboard and mouse devices, with X11/XTest available as a fallback.
- Touchscreen, trackpad, and pen tablet devices.

FreeBSD's [uhid(4)](https://man.freebsd.org/cgi/man.cgi?query=uhid&sektion=4)
is not the Linux UHID transport. It exposes an existing physical USB HID
interface through `/dev/uhid?`; it does not let a process register a new device
with the kernel HID bus. FreeBSD CUSE applications such as
[uhidd(8)](https://man.freebsd.org/cgi/man.cgi?query=uhidd&sektion=8) can emulate
a `uhid(4)`-compatible character device for direct consumers, but that is a
different integration surface and is not used by the current backend.

Generic, Xbox-family, Switch Pro, DualShock 4, and DualSense behavior therefore
uses uinput. Ordinary buttons, sticks, analog triggers, and rumble are available,
but raw HID reports and descriptor-driven features are not.

For each created gamepad, `Gamepad::profile()` reports the effective FreeBSD
uinput capability subset. Motion, touchpad contacts and click, battery state,
RGB LED output, adaptive-trigger output, and raw HID output reports are disabled.
This includes Switch Pro motion and battery state as well as the
PlayStation-specific features. Streaming-host adapters can reject those
operations instead of silently accepting state that uinput cannot expose.

The `uinput` kernel module and a writable uinput device node are required.

## macOS

The macOS backend currently uses CoreGraphics event injection for keyboard and
mouse input. It keeps the same public device model as the other backends:
consumers create keyboard and mouse devices through the runtime and submit the
same normalized event types. Platform details such as macOS virtual key-code
translation, modifier flag tracking, display coordinate scaling, scroll-wheel
preference handling, and CoreGraphics event posting stay inside the backend.

This first backend is not a virtual HID implementation. It does not require a
driver package, but consuming applications still need the normal macOS
permission path for synthetic input, such as Accessibility/Input Monitoring
approval when the host environment enforces it.

Current macOS capabilities:

- Keyboard key press and release using the existing Windows-style portable key
  codes.
- UTF-8 keyboard text input, converted to the UTF-16 strings expected by
  CoreGraphics keyboard events.
- Mouse relative movement, absolute movement on the main display, left/middle/
  right button transitions, and pixel-based vertical/horizontal scroll.
- Shared keyboard modifier state on mouse events, so combinations such as
  shift-click continue to work.

Unsupported macOS capabilities currently return `unsupported_profile`:

- Gamepad devices and output reports.
- Touchscreen, trackpad, and pen tablet devices.

Native macOS virtual-HID gamepad support is planned. A future backend may use
`IOHIDUserDevice`, DriverKit/HIDDriverKit, or a combination that preserves the
same public API while documenting any signing, entitlement, and installer
requirements.
