#!/bin/bash
set -euo pipefail

image_root="$(cd "$(dirname "$0")" && pwd)"
bundle_id="dev.lizardbyte.app.libvirtualhid"
support_directory="/Library/Application Support/libvirtualhid"
launchd_plist="${bundle_id}.plist"
broker_app="${image_root}/VirtualHIDBroker.app"
control_app="${image_root}/VirtualHIDControl.app"
installed_broker_app="/Applications/VirtualHIDBroker.app"
installed_control_app="/Applications/VirtualHIDControl.app"
legacy_broker_app="${support_directory}/VirtualHIDBroker.app"
service_plist="/Library/LaunchDaemons/${launchd_plist}"

if [[ ! -d "${broker_app}" ]]; then
  echo "The virtual HID broker is missing from this disk image." >&2
  exit 1
fi
if [[ ! -d "${control_app}" ]]; then
  echo "The virtual HID control app is missing from this disk image." >&2
  exit 1
fi

/usr/bin/codesign --verify --deep --strict "${broker_app}"
/usr/bin/codesign --verify --deep --strict "${control_app}"
/usr/bin/sudo -v
if [[ -L "${support_directory}" ]]; then
  echo "The libvirtualhid state directory must not be a symbolic link." >&2
  exit 1
fi
/usr/bin/sudo /bin/mkdir -p "${support_directory}" /usr/local/lib /usr/local/include /usr/local/share/licenses
/usr/bin/sudo /usr/sbin/chown root:wheel "${support_directory}"
/usr/bin/sudo /bin/chmod 700 "${support_directory}"
/usr/bin/sudo /bin/launchctl bootout "system/${bundle_id}" 2>/dev/null || true
/usr/bin/sudo /bin/rm -rf "${legacy_broker_app}" "${installed_broker_app}" "${installed_control_app}"
/usr/bin/sudo /usr/bin/ditto "${broker_app}" "${installed_broker_app}"
/usr/bin/sudo /usr/bin/ditto "${control_app}" "${installed_control_app}"
/usr/bin/sudo /usr/sbin/chown -R root:wheel "${installed_broker_app}" "${installed_control_app}"
/usr/bin/sudo /bin/chmod -R a+rX "${installed_broker_app}" "${installed_control_app}"
/usr/bin/sudo /bin/chmod -R go-w "${installed_broker_app}" "${installed_control_app}"
/usr/bin/sudo /usr/bin/codesign --verify --deep --strict "${installed_broker_app}"
/usr/bin/sudo /usr/bin/codesign --verify --deep --strict "${installed_control_app}"
/usr/bin/sudo /usr/bin/install -m 0644 \
  "${image_root}/${launchd_plist}" "${service_plist}"
/usr/bin/sudo /usr/sbin/chown root:wheel "${service_plist}"

if [[ -d "${image_root}/usr/local/include/libvirtualhid" ]]; then
  /usr/bin/sudo /usr/bin/ditto "${image_root}/usr/local/include/libvirtualhid" /usr/local/include/libvirtualhid
fi
if [[ -f "${image_root}/usr/local/lib/libvirtualhid.a" ]]; then
  /usr/bin/sudo /usr/bin/install -m 0644 "${image_root}/usr/local/lib/libvirtualhid.a" /usr/local/lib/libvirtualhid.a
fi
if [[ -f "${image_root}/usr/local/bin/libvirtualhid-license" ]]; then
  /usr/bin/sudo /bin/mkdir -p /usr/local/bin
  /usr/bin/sudo /usr/bin/install -m 0755 "${image_root}/usr/local/bin/libvirtualhid-license" /usr/local/bin/libvirtualhid-license
fi
if [[ -d "${image_root}/usr/local/lib/cmake/libvirtualhid" ]]; then
  /usr/bin/sudo /bin/mkdir -p /usr/local/lib/cmake
  /usr/bin/sudo /usr/bin/ditto "${image_root}/usr/local/lib/cmake/libvirtualhid" /usr/local/lib/cmake/libvirtualhid
fi
if [[ -d "${image_root}/usr/local/share/licenses/libvirtualhid" ]]; then
  /usr/bin/sudo /usr/bin/ditto "${image_root}/usr/local/share/licenses/libvirtualhid" /usr/local/share/licenses/libvirtualhid
fi

/usr/bin/sudo /bin/launchctl bootstrap system "${service_plist}"
/usr/bin/sudo /bin/launchctl kickstart -k "system/${bundle_id}"
echo 'Virtual HID Broker and Virtual HID Control installed in /Applications.'
echo 'Activate a license if needed: /usr/local/bin/libvirtualhid-license activate'
echo 'To create gamepads, grant Virtual HID Broker from /Applications access in'
echo 'System Settings > Privacy & Security > Device Control and Data Access'
echo '(Accessibility on older macOS).'
