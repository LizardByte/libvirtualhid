# End-User Gamepad Guide

This page is for people using an application that embeds `libvirtualhid`, such
as [Sunshine](https://github.com/LizardByte/Sunshine). You do not normally run
or configure `libvirtualhid` directly. The streaming host uses it to create the
virtual controller that Windows, Linux, FreeBSD, Steam, and games see.

## Understand the Input Path

Standard input and controller-specific features must pass through several
independent layers:

```text
physical controller -> Moonlight client -> Sunshine -> libvirtualhid -> game
physical controller <- Moonlight client <- Sunshine <- libvirtualhid <- game
```

Buttons, sticks, triggers, touch, motion, and battery state travel toward the
host. Rumble, Xbox Impulse Triggers, DualSense adaptive-trigger effects, and
LEDs travel back toward the client. A feature works end to end only when every
layer in its direction supports it.

The capabilities advertised by a `libvirtualhid` profile describe what the
host-side virtual controller can represent. They do not guarantee that a
Moonlight client can read the feature from the physical controller, transmit
it, or play feedback on the client device. The game and any compatibility
layer, such as Steam Input, must support the feature too.

## Prepare the Physical Controller

Before troubleshooting the stream, update the controller firmware and confirm
that ordinary buttons and sticks work on the client device. Use the controller
manufacturer's instructions:

- Xbox Wireless Controller:
  [connect to a Windows device](https://support.xbox.com/en-US/help/hardware-network/controller/connect-xbox-wireless-controller-to-pc)
  and
  [update the controller](https://support.xbox.com/en-US/help/hardware-network/controller/update-xbox-wireless-controller).
- DualShock 4:
  [pair with PC, Mac, Android, or iOS](https://www.playstation.com/en-us/support/hardware/ps4-pair-dualshock-4-wireless-with-pc-or-mac/).
  Sony notes that feature availability differs by device and connection type.
- DualSense and DualSense Edge:
  [use with PC, Mac, or mobile devices](https://www.playstation.com/en-us/support/hardware/pair-dualsense-controller-bluetooth/)
  and
  [pair with multiple devices](https://controller.dl.playstation.net/controller/lang/en/2100006.html).
  The guides also cover controller firmware updates and connection-specific
  feature limits.
- Nintendo Switch Pro Controller:
  [pair, use, and troubleshoot the controller](https://www.nintendo.com/my/support/switch/controller/nintendoswitchpro.html)
  and
  [update the controller firmware](https://en-americas-support.nintendo.com/app/answers/detail/a_id/26321/~/how-to-update-the-controller-firmware).

A controller working locally proves only the physical controller-to-client
part of the path. It does not prove that an extended feature is implemented by
that Moonlight client.

## Configure Sunshine

Use the current Sunshine and Virtual HID Driver versions recommended by the
Sunshine release you installed. On Windows, the Virtual HID Driver must be
installed and have a valid machine license before Sunshine can create a
driver-backed controller. After installing or updating the driver, restart
Windows.

In Sunshine's Web UI, confirm that controller input is enabled and review the
selected virtual gamepad under **Configuration > Input**. `auto` lets Sunshine
choose a host-side profile from the features reported by the client. Selecting
a profile manually changes what the game sees; it cannot add data that the
client did not send.

Restart Sunshine, then reconnect the stream after changing the profile or
updating the client, host, or driver. Sunshine creates the virtual gamepad for
the streaming session.

See the Sunshine documentation for the current host-specific setup:

- [Getting started](https://docs.lizardbyte.dev/projects/sunshine/master/md_docs_2getting__started.html)
- [Input and gamepad configuration](https://docs.lizardbyte.dev/projects/sunshine/master/md_docs_2configuration.html#gamepad)
- [Troubleshooting](https://docs.lizardbyte.dev/projects/sunshine/master/md_docs_2troubleshooting.html)

## End-to-End Compatibility

These results describe the complete path through a Moonlight client, Sunshine,
`libvirtualhid`, and the selected host backend. They do not describe what a
Moonlight client can support by itself.

Moonlight is available as several clients with platform-specific input
implementations. Their controller behavior can differ because the client
platform, operating-system input APIs, controller connection, and Moonlight
implementation expose different capabilities. Results from one client device
should not be treated as proof for another, even when both run Android or use
the same physical controller.

These observations are snapshots, not a permanent compatibility guarantee.
The feature rows include the controller capabilities relevant to streaming,
including manufacturer-specific features that are not yet implemented end to
end. ✅ means that the complete path was observed working, ❌ means that it
did not work, ❓ means that it was not tested, and ➖ means that the client
does not support that virtual profile.

### Compatibility Matrix

The backend columns use Moonlight Qt as the common test client. The client
columns compare the existing Windows Virtual HID Driver results.

| Feature                             | Backend: Windows via Virtual HID Driver              | Backend: Linux via `libvirtualhid`                   | Client: [Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt) | Client: [Moonlight Android](https://github.com/moonlight-stream/moonlight-android) | Client: [Moonlight iOS](https://github.com/moonlight-stream/moonlight-ios) | Client: [Moonlight Xbox](https://github.com/TheElixZammuto/moonlight-xbox) |
|-------------------------------------|------------------------------------------------------|------------------------------------------------------|--------------------------------------------------------------------------|------------------------------------------------------------------------------------|----------------------------------------------------------------------------|----------------------------------------------------------------------------|
| **Xbox 360**                        |                                                      |                                                      |                                                                          |                                                                                    |                                                                            |                                                                            |
| Standard buttons, sticks, and D-pad | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Analog trigger input (0 to 1)       | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Basic rumble                        | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| **Xbox One**                        |                                                      |                                                      |                                                                          |                                                                                    |                                                                            |                                                                            |
| Standard buttons, sticks, and D-pad | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Analog trigger input (0 to 1)       | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Basic rumble                        | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Impulse Triggers                    | ✅                                                   | ❌<sup><a href="#compatibility-note-11">11</a></sup> | ✅                                                                       | ❌                                                                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ✅                                                                         |
| Battery state                       | ❌<sup><a href="#compatibility-note-4">4</a></sup>   | ❌<sup><a href="#compatibility-note-4">4</a></sup>   | ❌<sup><a href="#compatibility-note-4">4</a></sup>                       | ❌<sup><a href="#compatibility-note-4">4</a></sup>                                 | ❌<sup><a href="#compatibility-note-4">4</a></sup>                         | ❌<sup><a href="#compatibility-note-4">4</a></sup>                         |
| **Xbox Series**                     |                                                      |                                                      |                                                                          |                                                                                    |                                                                            |                                                                            |
| Standard buttons, sticks, and D-pad | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Analog trigger input (0 to 1)       | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Basic rumble                        | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ✅                                                                         |
| Impulse Triggers                    | ✅                                                   | ❌<sup><a href="#compatibility-note-11">11</a></sup> | ✅                                                                       | ❌                                                                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ✅                                                                         |
| Battery state                       | ❌                                                   | ❌                                                   | ❌                                                                       | ❌                                                                                 | ❌                                                                         | ❌                                                                         |
| Share button                        | ❌<sup><a href="#compatibility-note-3">3</a></sup>   | ❌<sup><a href="#compatibility-note-8">8</a></sup>   | ❌<sup><a href="#compatibility-note-3">3</a></sup>                       | ❌<sup><a href="#compatibility-note-3">3</a></sup>                                 | ❌<sup><a href="#compatibility-note-3">3</a></sup>                         | ❌<sup><a href="#compatibility-note-3">3</a></sup>                         |
| **DualShock 4**                     |                                                      |                                                      |                                                                          |                                                                                    |                                                                            |                                                                            |
| Standard buttons, sticks, and D-pad | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| Analog trigger input (0 to 1)       | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| Basic rumble                        | ✅                                                   | ✅                                                   | ✅                                                                       | ✅<sup><a href="#compatibility-note-5">5</a></sup>                                 | ✅                                                                         | ➖                                                                         |
| Motion/gyro                         | ✅                                                   | ✅                                                   | ✅                                                                       | ✅<sup><a href="#compatibility-note-1">1</a></sup>                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |
| Touchpad position                   | ✅                                                   | ✅                                                   | ✅                                                                       | ❌<sup><a href="#compatibility-note-2">2</a></sup>                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |
| Touchpad click                      | ✅                                                   | ✅                                                   | ✅                                                                       | ❌<sup><a href="#compatibility-note-2">2</a></sup>                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |
| Light bar (RGB/player color)        | ✅                                                   | ✅                                                   | ✅                                                                       | ✅<sup><a href="#compatibility-note-6">6</a></sup>                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |
| Battery state                       | ❌                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| **DualSense**                       |                                                      |                                                      |                                                                          |                                                                                    |                                                                            |                                                                            |
| Standard buttons, sticks, and D-pad | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| Analog trigger input (0 to 1)       | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| Basic rumble                        | ✅                                                   | ✅                                                   | ✅                                                                       | ✅<sup><a href="#compatibility-note-5">5</a></sup>                                 | ✅                                                                         | ➖                                                                         |
| Motion/gyro                         | ✅                                                   | ✅                                                   | ✅                                                                       | ✅<sup><a href="#compatibility-note-1">1</a></sup>                                 | ❌                                                                         | ➖                                                                         |
| Touchpad position                   | ✅                                                   | ✅                                                   | ✅                                                                       | ❌<sup><a href="#compatibility-note-2">2</a></sup>                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |
| Touchpad click                      | ✅                                                   | ✅                                                   | ✅                                                                       | ❌<sup><a href="#compatibility-note-2">2</a></sup>                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |
| Light bar (RGB)                     | ✅                                                   | ✅                                                   | ✅                                                                       | ✅<sup><a href="#compatibility-note-6">6</a></sup>                                 | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |
| Battery state                       | ❌                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| Adaptive triggers                   | ❓<sup><a href="#compatibility-note-12">12</a></sup> | ❓<sup><a href="#compatibility-note-12">12</a></sup> | ❓<sup><a href="#compatibility-note-12">12</a></sup>                     | ❌<sup><a href="#compatibility-note-2">2</a></sup>                                 | ❓                                                                         | ➖                                                                         |
| Player indicator                    | ❌<sup><a href="#compatibility-note-7">7</a></sup>   | ❌<sup><a href="#compatibility-note-7">7</a></sup>   | ❌<sup><a href="#compatibility-note-7">7</a></sup>                       | ❌<sup><a href="#compatibility-note-7">7</a></sup>                                 | ❌                                                                         | ➖                                                                         |
| MUTE button                         | ✅                                                   | ✅                                                   | ✅                                                                       | ❌                                                                                 | ❌                                                                         | ➖                                                                         |
| MUTE button LED                     | ❌<sup><a href="#compatibility-note-7">7</a></sup>   | ❌<sup><a href="#compatibility-note-7">7</a></sup>   | ❌<sup><a href="#compatibility-note-7">7</a></sup>                       | ❌<sup><a href="#compatibility-note-7">7</a></sup>                                 | ❌                                                                         | ➖                                                                         |
| **Nintendo Switch Pro Controller**  |                                                      |                                                      |                                                                          |                                                                                    |                                                                            |                                                                            |
| Standard buttons, sticks, and D-pad | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| Digital trigger input (0 or 1)      | ✅                                                   | ✅                                                   | ✅                                                                       | ✅                                                                                 | ✅                                                                         | ➖                                                                         |
| Basic rumble                        | ✅                                                   | ✅                                                   | ✅                                                                       | ❌                                                                                 | ❌                                                                         | ➖                                                                         |
| Motion/gyro                         | ❌<sup><a href="#compatibility-note-9">9</a></sup>   | ❌<sup><a href="#compatibility-note-13">13</a></sup> | ❌<sup><a href="#compatibility-note-9">9</a></sup>                       | ❌<sup><a href="#compatibility-note-9">9</a></sup>                                 | ❌<sup><a href="#compatibility-note-9">9</a></sup>                         | ➖                                                                         |
| Battery state                       | ❌                                                   | ❌                                                   | ❌                                                                       | ❌                                                                                 | ❌                                                                         | ➖                                                                         |
| Player LED                          | ❌<sup><a href="#compatibility-note-14">14</a></sup> | ❌<sup><a href="#compatibility-note-14">14</a></sup> | ❌<sup><a href="#compatibility-note-14">14</a></sup>                     | ❌<sup><a href="#compatibility-note-14">14</a></sup>                               | ❌<sup><a href="#compatibility-note-14">14</a></sup>                       | ➖                                                                         |
| Capture button                      | ✅                                                   | ✅                                                   | ✅                                                                       | ❌<sup><a href="#compatibility-note-10">10</a></sup>                               | ✅<sup><a href="#compatibility-note-15">15</a></sup>                       | ➖                                                                         |

When a backend is marked ❌, that path cannot establish whether an additional
client-side limitation exists. The owner below identifies the first known layer
that prevents the feature from working end to end.

| Note                                            | Owner                                              | Limitation or status                                                                                                                                                                                                                                                                                                                                                            | Tracker or reference                                                                                                                                                                                                                                                                                                                                                                                              |
|-------------------------------------------------|----------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| <a id="compatibility-note-1"></a><sup>1</sup>   | Client platform                                    | Moonlight Android exposes gamepad motion on Android 12 or later when motion is enabled and the Android device exposes the controller sensors. Available settings can differ between devices.                                                                                                                                                                                    | [Moonlight Android motion settings](https://github.com/moonlight-stream/moonlight-android/blob/f10085f552b367cf7203007693d91c322a0a2936/app/src/main/java/com/limelight/preferences/StreamSettings.java#L296-L309)                                                                                                                                                                                                |
| <a id="compatibility-note-2"></a><sup>2</sup>   | Client platform                                    | Android may expose a PlayStation touchpad as a mouse instead of a native controller touchpad. Leave **Gamepad touchpad as mouse** disabled when native forwarding is available. DualSense support requires Android 12 or later, and Sony documents that adaptive triggers are unavailable on Android mobile devices.                                                            | [Sony Android requirements](https://www.playstation.com/en-us/support/hardware/pair-dualsense-controller-bluetooth/) and [Moonlight Android touchpad handling](https://github.com/moonlight-stream/moonlight-android/blob/f10085f552b367cf7203007693d91c322a0a2936/app/src/main/java/com/limelight/binding/input/ControllerHandler.java#L1680-L1778)                                                              |
| <a id="compatibility-note-3"></a><sup>3</sup>   | Windows host backend                               | Steam does not expose the Xbox Series Share button through Virtual HID Driver on Windows.                                                                                                                                                                                                                                                                                       | [libvirtualhid issue #106](https://github.com/LizardByte/libvirtualhid/issues/106)                                                                                                                                                                                                                                                                                                                                |
| <a id="compatibility-note-4"></a><sup>4</sup>   | Host profile                                       | The Xbox One profile rejects battery updates independently of the client.                                                                                                                                                                                                                                                                                                       | [libvirtualhid issue #107](https://github.com/LizardByte/libvirtualhid/issues/107)                                                                                                                                                                                                                                                                                                                                |
| <a id="compatibility-note-5"></a><sup>5</sup>   | Client platform and external consumer              | Android rumble depends on device vibration APIs and compatible motors. Steam may not dispatch PlayStation rumble until its controller settings or calibration page initializes the controller.                                                                                                                                                                                  | [Moonlight Android vibration handling](https://github.com/moonlight-stream/moonlight-android/blob/master/app/src/main/java/com/limelight/binding/input/ControllerHandler.java#L3373-L3419), [libvirtualhid issue #80](https://github.com/LizardByte/libvirtualhid/issues/80), and [Steam for Linux issue #13435](https://github.com/ValveSoftware/steam-for-linux/issues/13435)                                   |
| <a id="compatibility-note-6"></a><sup>6</sup>   | Client platform                                    | Moonlight Android uses the RGB lights API available on Android 12 or later. It worked on tested newer devices but was unavailable on NVIDIA Shield running Android 11.                                                                                                                                                                                                          | [Moonlight Android RGB-light detection](https://github.com/moonlight-stream/moonlight-android/blob/master/app/src/main/java/com/limelight/binding/input/ControllerHandler.java#L3454-L3470)                                                                                                                                                                                                                       |
| <a id="compatibility-note-7"></a><sup>7</sup>   | Host output pipeline                               | DualSense player-indicator and MUTE-button LED forwarding is covered by open host pull requests. This note applies only to the LEDs; the MUTE button input works through both host backends with Moonlight Qt. When a DualSense is connected to Android, its physical MUTE-button LED works locally, but that LED state is not forwarded to the virtual controller on the host. | [libvirtualhid pull request #97](https://github.com/LizardByte/libvirtualhid/pull/97) and [Sunshine pull request #5537](https://github.com/LizardByte/Sunshine/pull/5537)                                                                                                                                                                                                                                         |
| <a id="compatibility-note-8"></a><sup>8</sup>   | Linux host backend                                 | Steam exposes the Xbox Series Share button through the Linux uinput device, but pressing it does not change the button state.                                                                                                                                                                                                                                                   | [libvirtualhid issue #110](https://github.com/LizardByte/libvirtualhid/issues/110)                                                                                                                                                                                                                                                                                                                                |
| <a id="compatibility-note-9"></a><sup>9</sup>   | Windows host backend and Android client            | On Windows, Steam exposes Switch Pro gyro input but its values remain static. The host behavior is tracked in libvirtualhid; the Android client separately tracks exposing Switch Pro with gyro.                                                                                                                                                                                | [libvirtualhid issue #111](https://github.com/LizardByte/libvirtualhid/issues/111) and [Moonlight Android issue #1497](https://github.com/moonlight-stream/moonlight-android/issues/1497)                                                                                                                                                                                                                         |
| <a id="compatibility-note-10"></a><sup>10</sup> | Client                                             | Moonlight Android exposes the tested Switch Pro Capture input as A instead of Capture. A broader Android Switch Pro mapping issue exists, but the exact Capture symptom is not explicitly tracked.                                                                                                                                                                              | [Moonlight Android issue #842](https://github.com/moonlight-stream/moonlight-android/issues/842)                                                                                                                                                                                                                                                                                                                  |
| <a id="compatibility-note-11"></a><sup>11</sup> | Host backend                                       | The Linux uinput/evdev Xbox One and Xbox Series profiles support basic rumble but cannot deliver independent Impulse Triggers.                                                                                                                                                                                                                                                  | [libvirtualhid issue #109](https://github.com/LizardByte/libvirtualhid/issues/109)                                                                                                                                                                                                                                                                                                                                |
| <a id="compatibility-note-12"></a><sup>12</sup> | Client and protocol; resolved upstream, unreleased | Moonlight Qt adaptive-trigger support and its protocol and Sunshine dependencies are merged, but the latest published Moonlight Qt release predates them. The feature has not yet been validated with these backends.                                                                                                                                                           | [Moonlight Qt pull request #1561](https://github.com/moonlight-stream/moonlight-qt/pull/1561), [moonlight-common-c pull request #102](https://github.com/moonlight-stream/moonlight-common-c/pull/102), [Sunshine pull request #3738](https://github.com/LizardByte/Sunshine/pull/3738), and [Moonlight Qt v6.1.0](https://github.com/moonlight-stream/moonlight-qt/releases/tag/v6.1.0)                          |
| <a id="compatibility-note-13"></a><sup>13</sup> | Linux host backend                                 | Steam does not expose Switch Pro gyro because the current Linux uinput route cannot carry the profile's native motion reports. The libvirtualhid issue supersedes the closed, pre-libvirtualhid Sunshine request.                                                                                                                                                               | [libvirtualhid issue #112](https://github.com/LizardByte/libvirtualhid/issues/112) and [closed Sunshine issue #3838](https://github.com/LizardByte/Sunshine/issues/3838)                                                                                                                                                                                                                                          |
| <a id="compatibility-note-14"></a><sup>14</sup> | Host output pipeline                               | Switch Pro Player LED output is not decoded and forwarded by either host backend.                                                                                                                                                                                                                                                                                               | [libvirtualhid issue #113](https://github.com/LizardByte/libvirtualhid/issues/113)                                                                                                                                                                                                                                                                                                                                |
| <a id="compatibility-note-15"></a><sup>15</sup> | Client platform and version                        | The marked features worked when tested with Moonlight on an iPhone running iOS 18.7.10, but did not work on an Apple TV 4K running tvOS 26.6. Moonlight enables these extended features only when Apple's Game Controller framework exposes the corresponding buttons, haptics localities, motion sensors, or light.                                                            | [Moonlight capability detection](https://github.com/moonlight-stream/moonlight-ios/blob/85af0f75622bb2636481afda8b0fc5cc33d5956e/Limelight/Input/ControllerSupport.m#L547-L608), [Apple controller-haptics capabilities](https://developer.apple.com/documentation/gamecontroller/gcdevicehaptics), and [Apple controller-motion capabilities](https://developer.apple.com/documentation/gamecontroller/gcmotion) |

Analog trigger input reports intermediate values between 0 and 1. Switch Pro
ZL/ZR input is digital and reports only 0 or 1. Trigger input is also separate
from feedback: basic rumble,
[Xbox Impulse Triggers](https://learn.microsoft.com/en-us/windows/uwp/gaming/gamepad-and-vibration),
and DualSense adaptive triggers are distinct features. One working does not
imply that the others work. Likewise, a client may forward motion while omitting
battery or LED data.

## Troubleshoot by Symptom

### The host does not see a controller

1. Confirm that the physical controller works on the client before starting
   Moonlight.
2. Confirm that controller input is enabled in Sunshine.
3. On Windows, check the Virtual HID Driver version and license status on
   Sunshine's **Troubleshooting** page.
4. End and reconnect the stream, then check whether the host operating system
   sees a newly created controller.
5. Review the Sunshine log for controller creation, driver, permission, or
   license errors.

On Windows, `joy.cpl` is useful for checking ordinary buttons, sticks, and
triggers. Browser testers, Steam, and individual games use different controller
APIs and mappings, so do not use any one of them as the only compatibility
test.

### Buttons work but an extended feature does not

1. Identify the direction of the missing feature. Motion and touch travel from
   the client to the host; rumble and LEDs travel from the game back to the
   client.
2. Check whether the physical-controller vendor documents the feature for the
   client operating system and USB or Bluetooth connection being used.
3. Check the client-specific observations above and the issue tracker for that
   Moonlight client.
4. Confirm that Sunshine selected a virtual profile that represents the
   feature. A game seeing an Xbox controller will not gain PlayStation motion or
   adaptive-trigger support.
5. Test with a game or tool known to use that exact feature. Standard rumble is
   not a valid test for Xbox Impulse Triggers or DualSense adaptive triggers.
6. If Steam is involved, test once with Steam Input enabled and once with it
   disabled. Record which path works instead of treating Steam calibration or
   remapping as a driver fix.

### The controller works in Steam but not in a game

The game may support a different controller API or profile than Steam. Check
the game's controller requirements, try Steam Input both enabled and disabled,
and verify that the Sunshine virtual-gamepad selection matches a controller the
game supports. Disconnect unused host-side controllers if the game always
opens the first controller slot.

### PlayStation gyro, light bar, or rumble does not work in Steam

Steam may need a one-time gyro calibration before its controller tester or
Steam Input fully initializes a virtual DualShock 4 or DualSense controller's
gyro, light bar, and rumble. Open Steam's controller settings and complete the
gyro calibration, then test the features again. This has only been observed
with Steam's handling of virtual DualShock 4 and DualSense controllers and may
be a Steam bug rather than a remaining controller-protocol failure. See
[ValveSoftware/steam-for-linux issue #13435](https://github.com/ValveSoftware/steam-for-linux/issues/13435)
for a related DualSense rumble report.

If Steam repeatedly treats the virtual controller as a new device, disabling
Sunshine's **Randomize virtual controller MAC** option may help it retain the
controller's calibration and settings. Restart Sunshine and reconnect the
stream after changing the option. A stable MAC can cause different physical
controllers that reuse the same client controller slot to share Steam's
per-controller settings.

## Report a Compatibility Problem

Include enough information to identify which layer failed:

- Moonlight client name and exact version.
- Client device, operating-system version, and whether the controller uses USB,
  Bluetooth, a wireless adapter, or a built-in connection.
- Physical controller model and firmware version.
- Sunshine version, host operating system, and selected virtual-gamepad
  profile.
- Virtual HID Driver version on Windows.
- Game or test tool, whether Steam Input is enabled, and whether standard input
  works.
- The exact missing feature and its direction, such as Switch Pro motion to the
  host or Xbox Impulse Triggers back to the client.
- Relevant Sunshine logs and a comparison with another Moonlight client, when
  available.

Report client capture or playback problems to the relevant Moonlight client.
Report streaming-session mapping or forwarding problems to Sunshine. Report a
`libvirtualhid` issue when the same host-side virtual profile can be reproduced
without Moonlight and Sunshine, or when Sunshine logs show the expected data
reaching the library but the virtual device reports it incorrectly.

The [Moonlight setup guide](https://github.com/moonlight-stream/moonlight-docs/wiki/Setup-Guide)
links the official clients and their support resources.
