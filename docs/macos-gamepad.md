# macOS virtual gamepads

The macOS backend creates descriptor-driven virtual gamepads through a separate
root-owned broker. The broker alone calls Apple's `IOHIDUserDevice` API and holds
the virtual HID entitlement. The ordinary C++ library has no Apple entitlement
and uses CoreGraphics for keyboard and mouse input.

The client checks root ownership of the broker directory, socket, and
connected peer before exchanging versioned messages. Socket transfers handle
partial reads and writes so truncated messages are not treated as complete.
The root-owned broker directory permits local clients to reach its socket, and
the broker checks the machine license before gamepad creation.

The built-in generic, Xbox 360, Xbox One, Xbox Series, DualShock 4, DualSense,
and Switch Pro profiles, including the explicit USB and Bluetooth PlayStation
variants, are accepted as HID descriptors. Their VID/PID, transport, input
reports, output reports, and PlayStation feature reports are carried through the
broker. Xbox 360 is an ordinary HID device on macOS; the Windows XUSB/XInput
personality is Windows-specific. Individual games may use Apple's Game
Controller framework or their own HID mappings, so a signed installed build
still needs consumer testing for each profile.
When metadata omits a stable ID, the client derives a locally administered
`02:00:xx:xx:xx:xx` identifier from the device ID.

## Signing prerequisites

The broker needs a Developer ID provisioning profile for
`dev.lizardbyte.app.libvirtualhid` containing
`com.apple.developer.hid.virtual.device`. An existing Developer ID Application
certificate and notarization credentials can be reused for this Apple team. A
profile for another bundle ID cannot authorize the broker. Apple explains the
[restricted entitlement bundle and embedded profile](https://developer.apple.com/documentation/xcode/signing-a-daemon-with-a-restricted-entitlement).

Release CI reads the profile from the
`APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE_BASE64` secret. The profile is a
binary file; on Windows, encode it with PowerShell and paste the clipboard
contents into that secret:

```powershell
[Convert]::ToBase64String([IO.File]::ReadAllBytes("C:\path\to\broker.provisionprofile")) | Set-Clipboard
```

Release CI also uses `APPLE_CODESIGN_IDENTITY`,
`APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64`,
`APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_PASSWORD`, `APPLE_ID`,
`APPLE_TEAM_ID`, and `APPLE_NOTARYTOOL_PASSWORD`.

## Build and distribute

On macOS with Xcode and CMake installed:

```sh
export MACOSX_DEPLOYMENT_TARGET=14.2
cmake -S . -B cmake-build-macos-universal \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF -DBUILD_TESTS=ON
cmake --build cmake-build-macos-universal --parallel "$(sysctl -n hw.ncpu)"
xcrun lipo -info cmake-build-macos-universal/src/platform/macos/broker/VirtualHIDBroker.app/Contents/MacOS/VirtualHIDBroker
```

The single resulting executable contains both Apple silicon and Intel slices.
CI sets `MACOSX_DEPLOYMENT_TARGET` at the workflow level. The Apple builds use
`-fexperimental-library` for libc++'s `std::jthread` support.
The CI job checks the broker, license CLI, and `libvirtualhid.a` with `lipo`.
It runs the shared license-policy and macOS wire-protocol tests, starts the
broker as root, and checks license IPC. These checks do not prove virtual HID
creation: Apple's restricted entitlement needs a matching profile embedded in
the signed app bundle, independent of the runner's System Integrity Protection
setting.
PR workflows receive no Apple signing or notarization secrets, so PR CI does
not sign a package. Debug mode does not change the entitlement requirement.
Use a Mac with the approved profile for a full device test before merging.

For a release, set `APPLE_CODESIGN_IDENTITY` to the Developer ID Application
identity, set `APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE` to the broker
profile path, and set the notarization variables (`APPLE_ID`, `APPLE_TEAM_ID`,
`APPLE_NOTARYTOOL_PASSWORD`). Then run:

```sh
bash scripts/macos/package-dmg.sh cmake-build-macos-universal
```

The script embeds the profile, signs the broker app with Hardened Runtime and
a secure timestamp, verifies its signature, makes one universal DMG, submits
it using `notarytool`, and staples the ticket. Release CI performs these steps
with the corresponding certificate, profile, and notarization secrets.

### Test a PR on a Mac mini

Install Xcode on the Mac mini and select it with
`sudo xcode-select --switch /Applications/Xcode.app/Contents/Developer`.
Confirm `xcodebuild -version` works, install CMake, and check out the PR branch.
Import the Developer ID Application `.p12` file through Keychain Access into
the login keychain, entering its export password. Confirm that
`security find-identity -v -p codesigning` lists the certificate **with its
private key**. Keep the approved libvirtualhid `.provisionprofile` on the Mac
mini.
Set `APPLE_CODESIGN_IDENTITY` to that certificate's identity,
`APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE` to the profile's full path, and
`APPLE_ID`, `APPLE_TEAM_ID`, and `APPLE_NOTARYTOOL_PASSWORD` to the Apple
notarization values in the local shell. Do not commit these values.

Run the universal CMake commands above, then run
`bash scripts/macos/package-dmg.sh cmake-build-macos-universal`. This produces a
signed, notarized, stapled DMG from the PR branch. Install it using the steps
below, activate a license, and test each gamepad profile in a macOS consumer.
The certificate and profile are necessary even when System Integrity Protection
is disabled. A locally built unsigned broker can test IPC and licensing, but
cannot establish that virtual gamepad creation works.

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

The Windows and macOS brokers share the Polar organization, Yearly and Lifetime
benefit IDs, purchase URL, customer portal, and license time limits. No
unlicensed production gamepad is created. `lvh::get_license_status()`,
`activate_license()`, `validate_license()`, and `deactivate_license()` talk to
the installed broker;
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
