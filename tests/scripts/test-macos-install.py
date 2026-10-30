import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


installer = Path(sys.argv[1]).read_text()
for quarantined in (False, True):
    for optional in (False, True):
        with tempfile.TemporaryDirectory(prefix="libvirtualhid-install-", dir="/private/tmp") as temp:
            root = Path(temp)
            image = root / "image with spaces"
            destination = root / "installed"
            for name in ("VirtualHIDBroker.app", "VirtualHIDControl.app"):
                executable = image / name / "Contents/MacOS/example"
                executable.parent.mkdir(parents=True)
                executable.write_text("fixture")
            (image / "dev.lizardbyte.app.libvirtualhid.plist").write_text("fixture")
            if optional:
                for name in (
                    "include/libvirtualhid/gamepad.hpp",
                    "lib/libvirtualhid.a",
                    "bin/libvirtualhid-license",
                    "lib/cmake/libvirtualhid/libvirtualhid-config.cmake",
                    "share/licenses/libvirtualhid/LICENSE.md",
                ):
                    path = image / "usr/local" / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text("fixture")
            run("/usr/bin/xattr", "-wr", "user.review_marker", "preserve", str(image))
            if quarantined:
                run("/usr/bin/xattr", "-wr", "com.apple.quarantine", "0083;12345678;Safari;test", str(image))
            (destination / "Applications").mkdir(parents=True)
            (destination / "Library/LaunchDaemons").mkdir(parents=True)
            # Redirect fixed installation paths, leaving image-relative payload paths intact.
            source = re.sub(
                r"(?<!\})/(Applications|Library|usr/local)(?=[/ \"\n])",
                lambda match: str(destination) + match.group(),
                installer,
            )
            # Stub only authorization, ownership, signature validation, and launchd.
            # File copying, permissions, and extended attributes use the real macOS tools.
            stubs = r'''
function /usr/bin/codesign() { return 0; }
function /usr/bin/sudo() {
  [[ "$1" != -v ]] || return 0
  case "$1" in
    /usr/sbin/chown|/usr/bin/codesign) return 0 ;;
    /bin/launchctl)
      if [[ "$2" == bootstrap ]]; then
        if /usr/bin/xattr -lr "$TEST_DESTINATION" | /usr/bin/grep -q com.apple.quarantine; then
          echo 'quarantine remains at bootstrap' >&2
          return 1
        fi
        /usr/bin/touch "$TEST_BOOTSTRAPPED"
      fi
      return 0 ;;
  esac
  "$@"
}
'''
            script = image / "Install libvirtualhid.command"
            script.write_text(stubs + source)
            marker = root / "bootstrapped"
            env = dict(os.environ, TEST_DESTINATION=str(destination), TEST_BOOTSTRAPPED=str(marker))
            result = subprocess.run(["/bin/bash", str(script)], env=env, capture_output=True, text=True)
            assert result.returncode == 0, (quarantined, optional, result.stderr)
            assert marker.exists(), "bootstrap was skipped"
            assert "user.review_marker: preserve" in run("/usr/bin/xattr", "-lr", str(destination))
            if quarantined:
                assert "com.apple.quarantine" in run("/usr/bin/xattr", "-lr", str(image))
            print(f"PASS: quarantined={quarantined}, optional_payload={optional}")
