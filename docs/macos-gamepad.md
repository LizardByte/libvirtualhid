# macOS virtual gamepads

The macOS backend creates descriptor-driven virtual gamepads through a separate
root-owned broker. The broker alone calls Apple's `IOHIDUserDevice` API and holds
the virtual HID entitlement. The ordinary C++ library has no Apple entitlement
and continues to use CoreGraphics for keyboard and mouse input.

The built-in generic, Xbox 360, Xbox One, Xbox Series, DualShock 4, DualSense,
and Switch Pro profiles, including the explicit USB and Bluetooth PlayStation
variants, are accepted as HID descriptors. Their VID/PID, transport, input
reports, output reports, and PlayStation feature reports are carried through the
broker. Xbox 360 is an ordinary HID device on macOS; the Windows XUSB/XInput
personality is Windows-specific. Individual games may use Apple's Game
Controller framework or their own HID mappings, so a signed installed build
still needs consumer testing for each profile.

## What to do in Apple Developer

The Sunshine **Developer ID Application** signing certificate and existing
notarization credentials can be reused. Apple's HID Virtual Device approval may
be assigned to the team or to a particular App ID. A separate provisioning
profile for this broker's App ID is required even if Sunshine already has one.

1. Sign in to [Certificates, Identifiers & Profiles](https://developer.apple.com/account/resources/identifiers/list)
   as the Apple Developer team's **Account Holder**. If the team is an
   organization, Apple says the Account Holder must submit managed-capability
   requests.
2. Under **Identifiers**, register an explicit macOS App ID with bundle ID
   **`dev.lizardbyte.app.libvirtualhid`**. This follows Sunshine's
   `dev.lizardbyte.app.Sunshine` naming pattern. If it already exists, open it.
3. If the Sunshine request is still pending, wait for its decision. Then open
   the new App ID's **Capabilities** tab. If **HID Virtual Device** is
   available from that approval, enable it and save. Otherwise, in
   **Capability Requests**, request **HID Virtual Device**
   (`com.apple.developer.hid.virtual.device`) for this App ID. Explain that
   libvirtualhid is a signed, root-owned user-space broker that publishes
   descriptor-driven gamepads to other local applications for remote streaming
   hosts. It does not attach to physical hardware or install a kernel driver.
   List the generic, Xbox, PlayStation, and Switch Pro profiles and the
   broker's paid-license gate. After approval, enable the capability and save.
4. Under **Profiles**, create a **Developer ID** distribution provisioning
   profile for `dev.lizardbyte.app.libvirtualhid`, selecting the same Developer
   ID Application certificate used for Sunshine. Download the resulting
   `.provisionprofile` file. The profile must contain the virtual HID
   entitlement. A Mac App Development profile is for local development and
   cannot replace the Developer ID distribution profile in the release DMG.
5. In this repository's GitHub Actions secrets, add
   **`APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE_BASE64`** containing a
   single-line base64 encoding of the downloaded profile. The downloaded
   `.provisionprofile` itself is a signed binary file; base64 is only the text
   encoding used to store it in a GitHub secret. On macOS, run
   `base64 -i broker.provisionprofile | tr -d '\n'`. On Windows, run this in
   PowerShell, replacing the path with the downloaded file's location:

   ```powershell
   [Convert]::ToBase64String([IO.File]::ReadAllBytes("C:\path\to\broker.provisionprofile")) | Set-Clipboard
   ```

   Paste the clipboard contents as the secret value. Configure the existing
   Sunshine secret names here as well: `APPLE_ID`, `APPLE_TEAM_ID`,
   `APPLE_NOTARYTOOL_PASSWORD`, `APPLE_CODESIGN_IDENTITY`,
   `APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64`, and
   `APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_PASSWORD`.

Apple documents the [virtual HID entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.developer.hid.virtual.device),
the [managed-capability request steps](https://developer.apple.com/help/account/capabilities/capability-requests),
and why a [daemon with a restricted entitlement needs an app-like bundle and
embedded profile](https://developer.apple.com/documentation/xcode/signing-a-daemon-with-a-restricted-entitlement).
Approval is controlled by Apple; the same certificate does not itself grant
this entitlement. A profile issued for `dev.lizardbyte.app.Sunshine` cannot
authorize `dev.lizardbyte.app.libvirtualhid`.

## Build and distribute

On macOS with Xcode and CMake installed:

```sh
export MACOSX_DEPLOYMENT_TARGET=14.2
cmake -S . -B cmake-build-macos-universal \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DBUILD_DOCS=OFF -DBUILD_TESTS=OFF
cmake --build cmake-build-macos-universal --parallel "$(sysctl -n hw.ncpu)"
xcrun lipo -info cmake-build-macos-universal/src/platform/macos/broker/VirtualHIDBroker.app/Contents/MacOS/VirtualHIDBroker
```

The single resulting executable contains both Apple silicon and Intel slices.
CI sets `MACOSX_DEPLOYMENT_TARGET` at the workflow level, as Sunshine does.
The Apple builds use `-fexperimental-library` for libc++'s `std::jthread`
support, following Sunshine's macOS build configuration.
The CI job checks the broker, license CLI, and
`libvirtualhid.a` with `lipo`.

For a release, set `APPLE_CODESIGN_IDENTITY` to the Sunshine Developer ID
Application identity, set `APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE` to
the downloaded profile path, and set the existing Sunshine notarization
variables (`APPLE_ID`, `APPLE_TEAM_ID`, `APPLE_NOTARYTOOL_PASSWORD`). Then run:

```sh
bash scripts/macos/package-dmg.sh cmake-build-macos-universal
```

The script embeds the profile, signs the broker app with Hardened Runtime and
a secure timestamp, verifies its signature, makes one universal DMG, submits
it using `notarytool`, and staples the ticket. Release CI performs these steps
using the same certificate and notarization secret names as Sunshine. The
profile secret is the only new secret.

Mount the DMG and double-click **Install libvirtualhid.command**. It asks for
administrator authorization, installs the signed broker app under
`/Library/Application Support/libvirtualhid`, installs the static library and
headers under `/usr/local`, and starts the `dev.lizardbyte.app.libvirtualhid`
LaunchDaemon. Run a host process in the normal user session. The broker socket
is `/var/run/libvirtualhid/broker.sock`; only the root-owned installed broker
can answer the library's requests.

To activate a purchased license, open Terminal and run:

```sh
/usr/local/bin/libvirtualhid-license activate
/usr/local/bin/libvirtualhid-license status
```

The command prompts for the key without echoing it or placing it in the process
arguments. `validate` refreshes from Polar and `deactivate` releases this
machine activation. These commands do not require `sudo` after installation.

With the broker installed and licensed, hold a test controller for a minute:

```sh
cmake-build-macos-universal/examples/gamepad_adapter generic --hold-seconds 60
```

While it is running, use another Terminal window to inspect HID enumeration:

```sh
hidutil list
```

Repeat with `x360`, `xone`, `xseries`, `ds4`, `ds5`, and `switch`. Check each
device in the intended macOS game or streaming client; enumeration alone does
not prove that a particular consumer recognizes its profile.

The DMG contains both the MIT library and the LB-SAL broker. A release must
retain both license texts.

## License and validation

The macOS broker uses the same Polar organization, yearly and lifetime benefit
IDs, purchase URL, and customer portal as Windows. No unlicensed production
gamepad is created. `lvh::get_license_status()`, `activate_license()`,
`validate_license()`, and `deactivate_license()` talk to the installed broker;
the license key never enters the virtual gamepad report stream. A five-minute
GitHub Actions evaluation is available only when the broker itself starts in
the GitHub Actions environment.

The broker stores its machine activation in a root-only file under
`/Library/Application Support/libvirtualhid`. It revalidates with Polar every
24 hours. On a network outage, a previously validated license may create one
gamepad while authorization remains current. Existing virtual gamepads close
when their authorization expires or is revoked. A yearly activation needs
online validation after the broker restarts; this deliberately fails closed
when trusted elapsed time cannot be reconstructed.

If creation returns `backend_unavailable`, inspect the launchd job with
`sudo launchctl print system/dev.lizardbyte.app.libvirtualhid`. If it returns
`backend_failure` with an entitlement message, inspect the embedded profile
and signature with
`codesign -d --entitlements :- '/Library/Application Support/libvirtualhid/VirtualHIDBroker.app'`
and inspect the embedded profile with
`security cms -D -i '/Library/Application Support/libvirtualhid/VirtualHIDBroker.app/Contents/embedded.provisionprofile'`.
If it returns `license_required`, run `libvirtualhid-license activate`.
