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
- Whether the Linux XTest fallback is active.
- Whether a Windows driver package must be installed.

## Windows

The Windows backend keeps the normal C++ library buildable with MSVC and
MinGW/UCRT64. Keyboard and mouse input use Win32 APIs. Gamepad creation uses a
user-mode UMDF2 control driver and Windows Virtual HID Framework.

The C++ library communicates with the driver through fixed-size protocol
structures and `DeviceIoControl`, not C++ STL types. This keeps the public API
compiler-neutral and preserves the boundary between the MinGW/MSVC client
library and the WDK/MSVC driver package.

When the driver is installed, the backend publishes HID gamepads that standard
HID consumers can enumerate, including SDL/HIDAPI, DirectInput,
Windows.Gaming.Input/GameInput, and browser Gamepad API clients. XInput is not a
direct target of the HID backend.

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

Xbox One and Xbox Series use the native eight-byte PID payload exposed by the
Windows Xbox HID stack. The library applies the actuator-enable mask and
duration field, then reports the body motors as normalized low/high-frequency
rumble and the independent trigger motors as trigger-rumble output. The public
Xbox Series profile remains `0x045E:0x0B12`, while the Windows driver publishes
the `0x045E:0x0B13&IG_00` XInputHID match ID used by the Steam/browser mapping
path that previously worked. The Series Share button may remain unavailable
through that Windows compatibility transport.

The VHF driver answers the calibration, pairing, and firmware feature reports
used to initialize DualShock 4 and DualSense HIDAPI output. It also answers the
Switch Pro USB and subcommand initialization sequence and accepts the native
`0x30` input layout, so descriptor-aware consumers can initialize those
controllers before sending their native output reports.

See [Windows driver package](windows-driver.md) for build, install, validation,
and signing details.

## Linux

The Linux backend uses standard user-space kernel interfaces:

- `uhid` for descriptor-driven HID gamepads.
- `uinput` for Generic, Xbox 360, Xbox One, Xbox Series, and Switch Pro
  gamepads, plus keyboard, mouse, touchscreen, trackpad, and pen tablet
  devices.
- `libevdev` internally for uinput device construction.
- X11/XTest only as a keyboard and mouse fallback when `uinput` cannot be used
  and an X11 session is available.

Gamepad support normally prefers `uhid` because descriptors, raw HID identity,
feature reports, and output reports matter for controller compatibility.
Generic, Xbox-family, and Switch Pro profiles instead use `uinput` so SDL,
Steam, browser Gamepad API implementations, and other evdev consumers receive
canonical Linux gamepad events. Face buttons, shoulders, menu buttons, stick
clicks, and Guide use their native evdev codes; sticks use absolute axes. Every
uinput gamepad exposes its directional pad through `ABS_HAT0X` and `ABS_HAT0Y`.
Generic and Xbox triggers remain independent analog `ABS_Z` and `ABS_RZ` axes.
Switch Pro uses the Nintendo face-button
positions, button events for ZL/ZR, and `BTN_Z` for Capture. Profiles with rumble
support normalize rumble, constant, periodic, and ramp uinput force-feedback
effects back into the public callback. A zero-length effect remains active until
its explicit stop event, matching the infinite-effect contract used by SDL and
Steam. The Linux backend lets a new uinput device settle before reading those
effects so an early poll error cannot disable feedback for the device lifetime.
PlayStation rumble is read from native UHID interrupt-channel output reports.

The Generic profile keeps its public `0x1209:0x0001` identity, USB bus, and
Generic device name at the Linux transport boundary. Its uinput device exposes
both `ABS_HAT0X`/`ABS_HAT0Y` hat axes and semantic `BTN_DPAD_*` events so raw
evdev consumers and higher-level gamepad mappers can observe D-pad movement
without re-identifying the device as Xbox. It uses a compact Generic button
layout rather than the sparse Xbox button slots.

Xbox 360 retains its `0x045E:0x028E` identity, while its Linux uinput device uses
the Bluetooth bus so consumers select the sparse button mapping.
Xbox One and Xbox Series retain their public USB identities, but their Linux
uinput devices use the corresponding Bluetooth product identities (`0x0B20`
and `0x0B13`, respectively), whose standard consumer mappings match the events
that uinput exposes. Those three Xbox profiles preserve the 15-slot
Linux gamepad button sequence: unused `BTN_C`, `BTN_Z`, `BTN_TL2`, and `BTN_TR2`
slots are advertised but never pressed, keeping face buttons, shoulders, menu
buttons, Guide, L3, and R3 at their expected indices. D-pad directions are
reported through the hat axes and exposed as logical buttons by standard
gamepad consumers.

DualShock 4 and DualSense remain on `uhid` so their descriptors, motion,
touchpad, battery, feature reports, and profile-specific output reports stay
available. The backend accepts PlayStation output through both UHID interrupt
and control channels. Numbered control-channel output is normalized before
parsing, whether the kernel includes the report number in the payload or
provides it separately on the UHID event.

Switch Pro keeps its Nintendo identity on the Linux uinput path. This follows
the evdev layout used by Linux-native virtual-controller implementations and
allows standard `FF_RUMBLE` effects without emulating the physical controller's
proprietary initialization handshake.

On descriptor-driven backends, native Switch Pro output reports `0x01` and
`0x10` are decoded into the normalized low- and high-frequency rumble callback.
The original native report remains available in `GamepadOutput::raw_report`.

The optional `virtualhid_control` diagnostic UI uses SDL3 and Dear ImGui through
the repository CPM lockfile. It is intended to stay on the same UI framework for
Windows, Linux, and future macOS support. Static Linux linking is possible only
when the target distribution provides static archives for all selected backend
and UI dependencies, including SDL3, `libevdev`, and any enabled X11/XTest
libraries. Many distro toolchains intentionally omit some static archives, so
release packaging should keep full static linking as a packaging-mode choice
rather than an unconditional default.

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

Consuming applications may also install name-matched rules for stable virtual
device names when generated `hidraw` or `input` nodes must be accessible to the
session user:

```udev
KERNEL=="hidraw*", ATTRS{name}=="Your App Controller*", GROUP="input", MODE="0660", TAG+="uaccess"
SUBSYSTEMS=="input", ATTRS{name}=="Your App Controller*", GROUP="input", MODE="0660", TAG+="uaccess"
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
```

If input still does not work, add the user running the consuming application to
the `input` group, then log out and back in:

```bash
sudo usermod -aG input $USER
```

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
- Mouse relative movement, absolute movement on the main display, left/middle/
  right button transitions, and pixel-based vertical/horizontal scroll.
- Shared keyboard modifier state on mouse events so combinations such as
  shift-click continue to work.

Unsupported macOS capabilities currently return `unsupported_profile`:

- Gamepad devices and output reports.
- Touchscreen, trackpad, and pen tablet devices.
- Keyboard text input through `KeyboardTextEvent`.

Future native virtual HID support may use `IOHIDUserDevice`,
DriverKit/HIDDriverKit, or a combination that preserves the same public API
while documenting any signing, entitlement, and installer requirements.
