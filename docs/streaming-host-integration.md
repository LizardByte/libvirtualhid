# Streaming-host integration

`libvirtualhid` creates local virtual devices for a streaming host. The host
keeps its network protocol, client input mapping, controller assignment,
configuration, and feedback transport.

Use [`GamepadStateAdapter` in the example](../examples/gamepad_adapter.cpp)
as the integration pattern:

1. Choose a built-in profile and stable controller metadata.
2. Create one gamepad per active client controller and submit an initial
   neutral state.
3. Apply button, axis, trigger, touch, motion, and battery updates as the
   client sends them.
4. Forward output callbacks, such as rumble and LEDs, to the client when the
   selected profile and backend support them.
5. Destroy the gamepad on disconnect; recreate it after a broker restart or
   other loss of the local device.

Check runtime and effective profile capabilities before advertising optional
features. `DeviceNode` paths can help diagnose host-side enumeration; client
feature support still depends on the physical controller, connection, client,
and game. See the [end-user compatibility matrix](end-user-gamepad-guide.md#compatibility-matrix)
for observed streaming behavior.

Route `RuntimeOptions::log_callback` into the host logger for lifecycle,
failure, and debug-level input coordinate diagnostics. When streaming one
monitor from a multi-monitor desktop, supply the full desktop bounds and the
selected viewport in `CreateMouseOptions`. See the
[mouse viewport setup](usage.md#absolute-mouse-viewports).
Use native screen units for that geometry: screen points on macOS and desktop
pixels on Windows, Linux, and FreeBSD. The captured image dimensions supplied
to `Mouse::move_absolute()` can differ from the viewport dimensions.
