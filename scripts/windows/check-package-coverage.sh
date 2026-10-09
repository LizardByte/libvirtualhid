#!/bin/bash
# Reject coverage instrumentation in production objects and libraries before packaging.
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo 'Usage: check-package-coverage.sh <build-directory>' >&2
  exit 2
fi

build_directory="$1"
nm_command="${NM:-nm}"
if [[ ! -d "${build_directory}/src" ]]; then
  echo "Production build directory is missing: ${build_directory}/src" >&2
  exit 1
fi
if ! command -v "${nm_command}" >/dev/null 2>&1; then
  echo 'nm is required; run this script from MSYS2/UCRT64 with binutils installed.' >&2
  exit 1
fi

# PE executables may have no symbol table. Inspect their inputs before stripping,
# including the production dependencies, and keep the instrumented tests out.
production_directories=()
for directory in src tools examples _deps/sdl3-build third-party/lizardbyte-common/src; do
  if [[ -d "${build_directory}/${directory}" ]]; then
    production_directories+=("${build_directory}/${directory}")
  fi
done

artifact_list="$(mktemp)"
trap 'rm -f "${artifact_list}"' EXIT
find "${production_directories[@]}" -type f \
  \( -iname '*.obj' -o -iname '*.o' -o -iname '*.lib' -o -iname '*.a' \) \
  -print0 > "${artifact_list}"

checked_assets=0
while IFS= read -r -d '' artifact; do
  symbols="$("${nm_command}" "${artifact}")"
  if grep -Eq '(__gcov|llvm_gcda_|llvm_gcov_|llvm_profile_|__llvm_prf_)' <<< "${symbols}"; then
    echo "Coverage instrumentation found in package input: ${artifact}" >&2
    exit 1
  fi
  checked_assets=$((checked_assets + 1))
done < "${artifact_list}"

if [[ "${checked_assets}" -eq 0 ]]; then
  echo "No production objects or libraries found in: ${build_directory}" >&2
  exit 1
fi

echo "Windows package inputs contain no coverage instrumentation (${checked_assets} assets checked)."
