#!/bin/bash
set -euo pipefail
umask 077

script_directory="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repository_root="$(cd "${script_directory}/../.." && pwd)"
settings_file="${repository_root}/.env"
build_directory="${repository_root}/cmake-build-macos-universal"
bundle_id="dev.lizardbyte.app.libvirtualhid"
temporary_directory=""
temporary_keychain=""
mounted_image=""
existing_keychains=()

fail() {
  echo "Error: $*" >&2
  exit 1
}

package_only=false
case "${1:-}" in
  '') ;;
  --package-only) package_only=true ;;
  *) fail 'Usage: build-and-install.sh [--package-only]' ;;
esac
[[ $# -le 1 ]] || fail 'Usage: build-and-install.sh [--package-only]'

cleanup() {
  if [[ -n "${mounted_image}" ]]; then
    /usr/bin/hdiutil detach -quiet "${mounted_image}" || true
  fi
  if [[ -n "${temporary_keychain}" ]]; then
    if (( ${#existing_keychains[@]} > 0 )); then
      /usr/bin/security list-keychains -d user -s "${existing_keychains[@]}" || true
    fi
    /usr/bin/security delete-keychain "${temporary_keychain}" || true
  fi
  if [[ -n "${temporary_directory}" ]]; then
    /bin/rm -rf "${temporary_directory}"
  fi
}
trap cleanup EXIT

if [[ ! -f "${settings_file}" ]]; then
  /bin/cp "${repository_root}/.env.example" "${settings_file}"
  /bin/chmod 600 "${settings_file}"
  fail "Fill ${settings_file}, then rerun this script"
fi

# Parse literal KEY=value lines. Shell metacharacters in passwords are not run.
while IFS= read -r line || [[ -n "${line}" ]]; do
  line="${line%$'\r'}"
  [[ -z "${line}" || "${line}" == \#* ]] && continue
  [[ "${line}" == *=* ]] || fail "Invalid line in ${settings_file}: expected KEY=value"
  name="${line%%=*}"
  value="${line#*=}"
  case "${name}" in
    APPLE_ID|APPLE_NOTARYTOOL_PASSWORD|APPLE_TEAM_ID|APPLE_CODESIGN_IDENTITY|\
    APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_FILE|\
    APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_PASSWORD|\
    APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64|\
    APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE|\
    APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE_BASE64)
      printf -v "${name}" '%s' "${value}"
      export "${name}"
      ;;
    *) fail "Unknown setting ${name} in ${settings_file}" ;;
  esac
done < "${settings_file}"
unset line name value

[[ -n "${APPLE_ID:-}" ]] || fail "Fill APPLE_ID in ${settings_file}"
[[ -n "${APPLE_NOTARYTOOL_PASSWORD:-}" ]] || fail "Fill APPLE_NOTARYTOOL_PASSWORD in ${settings_file}"

temporary_directory="$(/usr/bin/mktemp -d "${TMPDIR:-/tmp}/libvirtualhid-local.XXXXXX")"
/bin/chmod 700 "${temporary_directory}"

profile_path="${APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE:-}"
if [[ -z "${profile_path}" && -n "${APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE_BASE64:-}" ]]; then
  profile_path="${temporary_directory}/broker.provisionprofile"
  printf '%s' "${APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE_BASE64}" | /usr/bin/base64 -D > "${profile_path}" \
    || fail "Could not decode the provisioning profile"
fi
[[ -f "${profile_path}" ]] || fail "Set APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE to the downloaded libvirtualhid profile path"
export APPLE_MACOS_VIRTUAL_HID_PROVISIONING_PROFILE="${profile_path}"

/usr/bin/security cms -D -i "${profile_path}" > "${temporary_directory}/profile.plist" \
  || fail "The provisioning profile could not be decoded"
profile_team_id="$(/usr/bin/python3 - "${temporary_directory}/profile.plist" "${bundle_id}" <<'PY'
import plistlib
import sys

with open(sys.argv[1], 'rb') as profile_file:
    profile = plistlib.load(profile_file)
teams = profile.get('TeamIdentifier', [])
if len(teams) != 1:
    raise SystemExit('Profile must identify exactly one Apple team')
team = teams[0]
entitlements = profile.get('Entitlements', {})
if entitlements.get('com.apple.application-identifier') != f'{team}.{sys.argv[2]}':
    raise SystemExit('Profile is not for dev.lizardbyte.app.libvirtualhid')
if entitlements.get('com.apple.developer.hid.virtual.device') is not True:
    raise SystemExit('Profile lacks the HID Virtual Device entitlement')
print(team)
PY
)" || fail "Use a provisioning profile approved for ${bundle_id}"
if [[ -n "${APPLE_TEAM_ID:-}" && "${APPLE_TEAM_ID}" != "${profile_team_id}" ]]; then
  fail "APPLE_TEAM_ID does not match the provisioning profile"
fi
export APPLE_TEAM_ID="${profile_team_id}"

p12_path="${APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_FILE:-}"
if [[ -z "${p12_path}" && -n "${APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64:-}" ]]; then
  p12_path="${temporary_directory}/developer-id.p12"
  printf '%s' "${APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_BASE64}" | /usr/bin/base64 -D > "${p12_path}" \
    || fail "Could not decode the Developer ID certificate"
fi
if [[ -n "${p12_path}" ]]; then
  [[ -f "${p12_path}" ]] || fail "Developer ID .p12 file does not exist: ${p12_path}"
  [[ -n "${APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_PASSWORD:-}" ]] \
    || fail "Fill APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_PASSWORD in ${settings_file}"
  while IFS= read -r keychain; do
    keychain="${keychain#"${keychain%%[![:space:]]*}"}"
    keychain="${keychain#\"}"
    keychain="${keychain%\"}"
    [[ -z "${keychain}" ]] || existing_keychains+=("${keychain}")
  done < <(/usr/bin/security list-keychains -d user)
  temporary_keychain="${temporary_directory}/signing.keychain-db"
  keychain_password="$(/usr/bin/openssl rand -hex 24)"
  /usr/bin/security create-keychain -p "${keychain_password}" "${temporary_keychain}"
  /usr/bin/security unlock-keychain -p "${keychain_password}" "${temporary_keychain}"
  /usr/bin/security set-keychain-settings -lut 21600 "${temporary_keychain}"
  /usr/bin/security import "${p12_path}" -k "${temporary_keychain}" \
    -P "${APPLE_DEVELOPER_ID_APPLICATION_CERTIFICATE_P12_PASSWORD}" \
    -T /usr/bin/codesign || fail "Could not import the Developer ID .p12 file"
  intermediate_certificate="${temporary_directory}/DeveloperIDG2CA.cer"
  /usr/bin/curl --fail --location --silent --show-error \
    --output "${intermediate_certificate}" \
    https://www.apple.com/certificateauthority/DeveloperIDG2CA.cer \
    || fail "Could not download Apple's Developer ID G2 intermediate certificate"
  intermediate_sha256="$(/usr/bin/shasum -a 256 "${intermediate_certificate}" | /usr/bin/awk '{print $1}')"
  [[ "${intermediate_sha256}" == f16cd3c54c7f83cea4bf1a3e6a0819c8aaa8e4a1528fd144715f350643d2df3a ]] \
    || fail "Apple's Developer ID G2 intermediate certificate did not match the expected digest"
  /usr/bin/security add-certificates -k "${temporary_keychain}" "${intermediate_certificate}" \
    || fail "Could not import Apple's Developer ID G2 intermediate certificate"
  /usr/bin/security set-key-partition-list -S apple-tool:,apple: -s \
    -k "${keychain_password}" "${temporary_keychain}" > /dev/null \
    || fail "Could not grant codesign access to the temporary Keychain"
  /usr/bin/security list-keychains -d user -s "${temporary_keychain}" "${existing_keychains[@]}"
fi

if [[ -z "${APPLE_CODESIGN_IDENTITY:-}" ]]; then
  identity_matches="$(/usr/bin/security find-identity -v -p codesigning | \
    /usr/bin/awk -v team="(${APPLE_TEAM_ID})" \
      'index($0, "Developer ID Application:") && index($0, team) {print $2}' | \
    /usr/bin/sort -u)"
  identity_count="$(printf '%s\n' "${identity_matches}" | /usr/bin/awk 'NF {count++} END {print count+0}')"
  [[ "${identity_count}" == 1 ]] || fail \
    "Expected one Developer ID Application identity for team ${APPLE_TEAM_ID}, found ${identity_count}. Set APPLE_CODESIGN_IDENTITY if you have more than one, or supply the .p12 file."
  export APPLE_CODESIGN_IDENTITY="${identity_matches}"
fi

if ! /usr/bin/xcodebuild -version > /dev/null 2>&1; then
  [[ -d /Applications/Xcode.app/Contents/Developer ]] \
    || fail "Install Xcode from the App Store before running this script"
  export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
  /usr/bin/xcodebuild -version > /dev/null 2>&1 \
    || fail "Open Xcode once to finish its setup, then rerun this script"
fi
if ! command -v cmake > /dev/null 2>&1; then
  if [[ -x "${repository_root}/.venv/bin/cmake" ]]; then
    export PATH="${repository_root}/.venv/bin:${PATH}"
  else
    command -v brew > /dev/null 2>&1 || fail "Install CMake or Homebrew before running this script"
    echo 'Installing CMake with Homebrew...'
    brew install cmake
  fi
fi

echo 'Updating submodules...'
git -C "${repository_root}" submodule update --init --recursive
export MACOSX_DEPLOYMENT_TARGET=14.2
echo 'Configuring and building the universal macOS binaries...'
cmake -S "${repository_root}" -B "${build_directory}" \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=OFF -DBUILD_TESTS=ON \
  -DLIBVIRTUALHID_BUILD_TOOLS=OFF -DLIBVIRTUALHID_WARNINGS_AS_ERRORS=ON
cmake --build "${build_directory}" --parallel "$(/usr/sbin/sysctl -n hw.ncpu)"

for binary in \
  "${build_directory}/src/platform/macos/broker/VirtualHIDBroker.app/Contents/MacOS/VirtualHIDBroker" \
  "${build_directory}/src/platform/macos/broker/libvirtualhid-license" \
  "${build_directory}/src/libvirtualhid.a"; do
  /usr/bin/xcrun lipo "${binary}" -verify_arch arm64
  /usr/bin/xcrun lipo "${binary}" -verify_arch x86_64
done
"${build_directory}/tests/test_libvirtualhid" \
  '--gtest_filter=BrokerLicensePolicyTest.*:GitHubActionsEvaluationTest.*:MacosBrokerProtocolTest.*'

echo 'Signing and notarizing the DMG; Apple may take several minutes...'
/bin/bash "${repository_root}/scripts/macos/package-dmg.sh" "${build_directory}" \
  "${build_directory}/artifacts"
disk_image="${build_directory}/artifacts/libvirtualhid-macOS-universal.dmg"
[[ -s "${disk_image}" ]] || fail "The DMG was not created"

if [[ "${package_only}" == true ]]; then
  echo "Signed and notarized PR build from $(git -C "${repository_root}" rev-parse --short HEAD)."
  echo "DMG: ${disk_image}"
  exit 0
fi

mounted_image="${temporary_directory}/mounted-dmg"
/bin/mkdir -p "${mounted_image}"
/usr/bin/hdiutil attach -quiet -nobrowse -mountpoint "${mounted_image}" "${disk_image}"
echo 'Installing the broker; macOS may ask for your administrator password...'
/bin/bash "${mounted_image}/Install libvirtualhid.command"
/usr/bin/hdiutil detach -quiet "${mounted_image}"
mounted_image=""

echo "Installed signed PR build from $(git -C "${repository_root}" rev-parse --short HEAD)."
echo "DMG: ${disk_image}"
echo 'License status:'
/usr/local/bin/libvirtualhid-license status || true
echo 'If a license is needed, run: /usr/local/bin/libvirtualhid-license activate'
