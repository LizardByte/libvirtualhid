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
variants, are accepted. The macOS broker receives the selected transport's HID
descriptor and reports. Xbox 360 uses a USB HID identity (`045e:028e`, version
`0114`) with numbered D-pad buttons that match Steam's macOS mapping. Xbox One
and Xbox Series use distinct HID identities (`045e:0b20` and `045e:0b13`).
macOS exposes broker-created devices as Virtual transport, so the backend
frames their input as wired Xbox GIP packets for Steam's Xbox HID decoder. The
Windows XUSB/XInput personality is Windows-specific. Individual games may use Apple's
Game Controller framework or their own HID mappings, so a signed installed
build still needs consumer testing for each profile.
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
xcrun lipo -info cmake-build-macos-universal/tools/VirtualHIDControl.app/Contents/MacOS/VirtualHIDControl
```

The broker and control app executables each contain Apple silicon and Intel slices.
CI sets `MACOSX_DEPLOYMENT_TARGET` at the workflow level. The Apple builds use
`-fexperimental-library` for libc++'s `std::jthread` support.
The CI job checks the broker, control app, license CLI, and `libvirtualhid.a` with `lipo`.
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

The script embeds the profile, signs the broker and control apps with Hardened
Runtime and a secure timestamp, verifies their signatures, makes one universal
DMG, submits it using `notarytool`, and staples the ticket. The control app uses
its own bundle ID and does not need the broker's restricted entitlement. Release
CI performs these steps with the corresponding certificate, profile, and
notarization secrets.

### Test a PR on macOS

Check out the PR branch on a Mac, install Xcode, and copy
`.env.example` to `.env` in the repository root. Fill in the Apple ID,
notarization app-specific password, Developer ID Application `.p12` file path
and export password, and the provisioning profile path. Paths must be absolute.
The script can also decode the base64 certificate and profile values used by
CI, if you have those originals. The optional team ID and signing identity are
detected from the profile and certificate. `.env` is ignored by Git and must
stay local. GitHub's secrets API cannot return stored secret values.

```sh
cp -n .env.example .env
open -e .env
bash scripts/macos/build-and-install.sh
```

The script uses the installed Xcode, finds CMake in the project `.venv` or
installs it through Homebrew if needed, imports the certificate into a temporary
Keychain, builds and tests universal binaries, signs and notarizes the DMG, and
installs it. It removes the temporary Keychain afterward. The approved profile
must be for `dev.lizardbyte.app.libvirtualhid`; a profile for another bundle
ID fails before the build. The certificate and profile are necessary even when
System Integrity Protection is disabled. A locally built unsigned broker can
test IPC and licensing, but cannot establish that virtual gamepad creation works.

Run `bash scripts/macos/build-and-install.sh --package-only` to create the
signed DMG without installing it.

After installation, activate a license if needed, then test each gamepad
profile in a macOS consumer.

For manual installation, mount the DMG and double-click
**Install libvirtualhid.command**. It asks for administrator authorization,
installs **Virtual HID Broker** and **Virtual HID Control** in `/Applications`,
installs the static library and headers under `/usr/local`, and starts the
`dev.lizardbyte.app.libvirtualhid` LaunchDaemon. Open **Virtual HID Control**
from Applications to create and inspect test devices. The broker app runs as a
system service and does not have a user interface. Run a host process in the
normal user session. The broker socket is `/var/run/libvirtualhid/broker.sock`;
only the root-owned installed broker can answer the library's requests.

macOS also requires permission for the broker to create virtual HID devices.
Open **System Settings**, then **Privacy & Security**, then **Device Control and
Data Access** (**Accessibility** on older macOS versions). Click **Add**,
authorize the settings change, and select **Virtual HID Broker** from
`/Applications/VirtualHIDBroker.app`. The
installed broker runs as a root LaunchDaemon, so macOS cannot show its prompt
during gamepad creation. This permission gives the broker broad device control
access; review the signed app before granting it.

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
`backend_failure` during virtual HID creation, check the broker's macOS
permission above, then inspect the embedded profile and signature with
`codesign -d --entitlements :- /Applications/VirtualHIDBroker.app`
and inspect the embedded profile with
`security cms -D -i /Applications/VirtualHIDBroker.app/Contents/embedded.provisionprofile`.
If it returns `license_required`, run `libvirtualhid-license activate`.
