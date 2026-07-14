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

See [Windows driver package](windows-driver.md) for build, install, validation,
and signing details.

## Linux

The Linux backend uses standard user-space kernel interfaces:

- `uhid` for descriptor-driven HID gamepads.
- `uinput` for Xbox Series gamepads, keyboard, mouse, touchscreen, trackpad,
  and pen tablet devices.
- `libevdev` internally for uinput device construction.
- X11/XTest only as a keyboard and mouse fallback when `uinput` cannot be used
  and an X11 session is available.

Gamepad support normally prefers `uhid` because descriptors, raw HID identity,
feature reports, and output reports matter for controller compatibility. Xbox
Series is the exception: on Linux it uses `uinput` so SDL, Steam, and other
evdev consumers receive the native Xbox button codes without interpreting the
descriptor report as Xbox GIP traffic. Face buttons, shoulders, menu buttons,
stick clicks, the guide button, and Share are exposed through their canonical
evdev codes; sticks and the directional pad use absolute axes; triggers remain
independent analog `ABS_Z` and `ABS_RZ` axes. Force-feedback effects are
normalized back into the public rumble callback. The backend advertises the
full 15-slot Xbox Series button range expected by Steam for this controller
identity. Its unused `BTN_C`, `BTN_Z`, `BTN_TL2`, and `BTN_TR2` slots are never
pressed; they keep the active buttons after them at the correct Linux indices.

Other gamepad profiles remain descriptor-driven through `uhid`. The Switch Pro
profile keeps its Nintendo identity but uses the virtual UHID bus on Linux,
preventing `hid-nintendo` from claiming the descriptor-only device and waiting
for physical-controller initialization handshakes.

The optional `virtualhid_control` diagnostic UI uses SDL3 and Dear ImGui through
the repository CPM lockfile. It is intended to stay on the same UI framework for
Windows, Linux, and future macOS support. Static Linux linking is possible only
when the target distribution provides static archives for all selected backend
and UI dependencies, including SDL3, `libevdev`, and any enabled X11/XTest
libraries. Many distro toolchains intentionally omit some static archives, so
release packaging should keep full static linking as a packaging-mode choice
rather than an unconditional default.

### Permissions

Linux deployment is usually a permissions problem, not a kernel-module problem.
Install udev rules such as `/etc/udev/rules.d/60-libvirtualhid.rules`:

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

For descriptor-driven `uhid` gamepad support, install a modules-load entry such as
`/etc/modules-load.d/60-libvirtualhid.conf`:

```text
uhid
```

After installing the rules, load `uhid`, reload udev, and trigger the device
nodes:

```bash
sudo modprobe uhid
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
