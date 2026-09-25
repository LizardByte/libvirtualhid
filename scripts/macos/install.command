#!/bin/bash
set -euo pipefail

image_root="$(cd "$(dirname "$0")" && pwd)"
broker_app="${image_root}/usr/local/libexec/libvirtualhid/VirtualHIDBroker.app"
installed_app="/Library/Application Support/libvirtualhid/VirtualHIDBroker.app"
service_plist="/Library/LaunchDaemons/dev.lizardbyte.app.libvirtualhid.plist"

if [[ ! -d "${broker_app}" ]]; then
  echo "The virtual HID broker is missing from this disk image." >&2
  exit 1
fi

/usr/bin/codesign --verify --deep --strict "${broker_app}"
/usr/bin/sudo -v
if [[ -L "/Library/Application Support/libvirtualhid" ]]; then
  echo "The libvirtualhid state directory must not be a symbolic link." >&2
  exit 1
fi
/usr/bin/sudo /bin/mkdir -p "/Library/Application Support/libvirtualhid" /usr/local/lib /usr/local/include /usr/local/share/licenses
/usr/bin/sudo /usr/sbin/chown root:wheel "/Library/Application Support/libvirtualhid"
/usr/bin/sudo /bin/chmod 700 "/Library/Application Support/libvirtualhid"
/usr/bin/sudo /bin/launchctl bootout system/dev.lizardbyte.app.libvirtualhid 2>/dev/null || true
/usr/bin/sudo /bin/rm -rf "${installed_app}"
/usr/bin/sudo /usr/bin/ditto "${broker_app}" "${installed_app}"
/usr/bin/sudo /usr/sbin/chown -R root:wheel "${installed_app}"
/usr/bin/sudo /bin/chmod -R go-w "${installed_app}"
/usr/bin/sudo /usr/bin/codesign --verify --deep --strict "${installed_app}"
/usr/bin/sudo /usr/bin/install -m 0644 \
  "${image_root}/dev.lizardbyte.app.libvirtualhid.plist" "${service_plist}"
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
/usr/bin/sudo /bin/launchctl kickstart -k system/dev.lizardbyte.app.libvirtualhid
echo "libvirtualhid broker installed. Run: /usr/local/bin/libvirtualhid-license activate"
