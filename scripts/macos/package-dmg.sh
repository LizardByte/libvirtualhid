#!/bin/bash
# Build one universal macOS disk image. The release path requires an Apple-granted
# virtual HID entitlement in a Developer ID provisioning profile.
set -euo pipefail

repository_root="$(cd "$(dirname "$0")/../.." && pwd)"
bundle_id="dev.lizardbyte.app.libvirtualhid"
launchd_plist="${bundle_id}.plist"
build_directory="${1:-${repository_root}/cmake-build-macos-universal}"
output_directory="${2:-${repository_root}/cmake-build-macos-universal/artifacts}"
build_directory="$(cd "${build_directory}" && pwd)"
mkdir -p "${output_directory}"
output_directory="$(cd "${output_directory}" && pwd)"
stage_directory="${build_directory}/macos-dmg-stage"
image_root="${stage_directory}/image"
broker_app="${image_root}/VirtualHIDBroker.app"
control_app="${image_root}/VirtualHIDControl.app"
profile_path="${APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE:-}"
signing_identity="${APPLE_CODESIGN_IDENTITY:-}"

if [[ -z "${signing_identity}" || -z "${profile_path}" || ! -f "${profile_path}" ||
      -z "${APPLE_ID:-}" || -z "${APPLE_TEAM_ID:-}" || -z "${APPLE_NOTARYTOOL_PASSWORD:-}" ]]; then
  echo "A Developer ID identity, approved virtual HID profile, and notarization credentials are required." >&2
  exit 1
fi

rm -rf "${stage_directory}"
mkdir -p "${image_root}"
DESTDIR="${image_root}" cmake --install "${build_directory}" --prefix /usr/local
mv "${image_root}/usr/local/libexec/libvirtualhid/VirtualHIDBroker.app" "${broker_app}"
mv "${image_root}/usr/local/Applications/VirtualHIDControl.app" "${control_app}"

profile_details="${stage_directory}/profile.plist"
/usr/bin/security cms -D -i "${profile_path}" > "${profile_details}"
/usr/bin/python3 - "${profile_details}" "${APPLE_TEAM_ID}" "${bundle_id}" <<'PY'
import plistlib
import sys

with open(sys.argv[1], "rb") as profile_file:
    profile = plistlib.load(profile_file)
entitlements = profile.get("Entitlements", {})
app_id = entitlements.get("com.apple.application-identifier", "")
if app_id != f"{sys.argv[2]}.{sys.argv[3]}":
    raise SystemExit("Provisioning profile has the wrong App ID")
if sys.argv[2] not in profile.get("TeamIdentifier", []):
    raise SystemExit("Provisioning profile belongs to another Apple team")
if entitlements.get("com.apple.developer.hid.virtual.device") is not True:
    raise SystemExit("Provisioning profile lacks Apple's virtual HID entitlement")
PY

cp "${profile_path}" "${broker_app}/Contents/embedded.provisionprofile"
/usr/bin/codesign --force --timestamp --options runtime \
  --sign "${signing_identity}" \
  --entitlements "${repository_root}/src/platform/macos/broker/entitlements.plist" \
  "${broker_app}"
/usr/bin/codesign --verify --deep --strict --verbose=2 "${broker_app}"
/usr/bin/codesign --force --timestamp --options runtime \
  --sign "${signing_identity}" "${control_app}"
/usr/bin/codesign --verify --deep --strict --verbose=2 "${control_app}"
/usr/bin/codesign --force --timestamp --options runtime \
  --sign "${signing_identity}" "${image_root}/usr/local/bin/libvirtualhid-license"
/usr/bin/codesign --verify --strict --verbose=2 \
  "${image_root}/usr/local/bin/libvirtualhid-license"

cp "${repository_root}/scripts/macos/install.command" "${image_root}/Install libvirtualhid.command"
cp "${repository_root}/scripts/macos/${launchd_plist}" "${image_root}/"
cp "${repository_root}/LICENSE.md" "${image_root}/"
cp -R "${repository_root}/LICENSES" "${image_root}/"

output_image="${output_directory}/libvirtualhid-macOS-universal.dmg"
/usr/bin/hdiutil create -volname libvirtualhid -srcfolder "${image_root}" \
  -format UDZO -ov "${output_image}"

xcrun notarytool submit "${output_image}" \
  --apple-id "${APPLE_ID}" \
  --team-id "${APPLE_TEAM_ID}" \
  --password "${APPLE_NOTARYTOOL_PASSWORD}" \
  --wait
xcrun stapler staple "${output_image}"

echo "${output_image}"
