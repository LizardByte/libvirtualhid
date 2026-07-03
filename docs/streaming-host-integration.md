# Streaming-Host Integration

Remote streaming hosts are the first consumer class for `libvirtualhid`.
Integration should preserve a host application's existing network protocol,
client input parsing, configuration, feedback queue, and device lifecycle while
moving local virtual device creation behind the `libvirtualhid` API.

## Integration Contract

A streaming host should be able to:

- Create stable per-client gamepad handles with both client-relative and global
  indexes.
- Submit incremental button, axis, trigger, touchpad, motion, and battery
  updates without recreating a device.
- Receive output callbacks for rumble, LEDs, adaptive triggers, trigger rumble,
  and raw output reports where the selected profile supports them.
- Query profile and backend capabilities before warning users about unsupported
  client features.
- Read device nodes and platform paths when a downstream consumer or diagnostic
  needs to inspect SDL, HIDAPI, libinput, `hidraw`, or system device state.
- Use keyboard and mouse APIs for relative mouse, absolute mouse, buttons,
  wheel, horizontal wheel, key events, and Unicode text input.

`libvirtualhid` should not own the host application's network transport, packet
schema, configuration model, controller assignment policy, or status API.

## Adapter Pattern

The [gamepad adapter example](../examples/gamepad_adapter.cpp) demonstrates the
intended shape:

- Choose a built-in `DeviceProfile` from a host-facing profile name.
- Fill `CreateGamepadOptions` with stable controller metadata.
- Create a `GamepadStateAdapter` from a `Runtime`.
- Cache state inside the adapter as separate input events arrive.
- Submit an initial neutral report so operating-system consumers can enumerate
  the virtual controller before the first client input packet.
- Forward output callbacks back to the physical client controller or feedback
  queue.

This keeps one public code path for Linux, Windows, and future platforms while
still letting each backend report real capability limits.

## Current Readiness

The core API and adapter shape cover the major streaming-host requirements:

- Multiple controller lifecycles.
- Built-in profiles for common controller classes.
- Rich controller metadata.
- Gamepad output callbacks.
- Keyboard and mouse input paths.
- Linux `uhid` gamepads and `uinput` keyboard/pointer devices.
- Linux DualSense and DualShock 4 USB/Bluetooth report handling.
- Linux touchscreen, trackpad, and pen tablet device types.
- Windows UMDF/VHF gamepad creation through an installed driver package.

Remaining replacement work is validation and packaging, not broad API shape:

- Validate Windows UMDF/VHF gamepads against the same application classes that
  previously relied on ViGEmBus, including XInput-only compatibility decisions.
- Validate DualShock 4 and Xbox behavior through the intended Windows streaming
  host path.
- Replace any consumer-specific ViGEmBus installer, status, and diagnostics with
  libvirtualhid driver-package checks.
- Add and validate the Linux host adapter for the selected controller profile
  names used by the consuming application.
- Define the FreeBSD backend subset explicitly instead of assuming Linux
  `uhid` behavior applies.
- Implement and validate a macOS backend before advertising macOS runtime
  support.
