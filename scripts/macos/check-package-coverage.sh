#!/bin/bash
# Reject coverage instrumentation in the binaries used to build the macOS DMG.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo 'Usage: check-package-coverage.sh <build-directory>' >&2
  exit 2
fi

build_directory="$1"
for binary in \
  "${build_directory}/src/platform/macos/broker/VirtualHIDBroker.app/Contents/MacOS/VirtualHIDBroker" \
  "${build_directory}/tools/VirtualHIDControl.app/Contents/MacOS/VirtualHIDControl" \
  "${build_directory}/src/platform/macos/broker/libvirtualhid-license" \
  "${build_directory}/src/libvirtualhid.a"; do
  symbols="$(/usr/bin/xcrun nm -arch all "${binary}")"
  if /usr/bin/grep -Eq '(__gcov|llvm_gcda_|llvm_gcov_|llvm_profile_|__llvm_prf_)' <<< "${symbols}"; then
    echo "Coverage instrumentation found in package asset: ${binary}" >&2
    exit 1
  fi
done

echo 'macOS package assets contain no coverage instrumentation.'
