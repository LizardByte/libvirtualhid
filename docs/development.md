# Development

Build directories use the `cmake-build-` prefix. The shared GoogleTest binary
is `tests/test_libvirtualhid` in the build directory. Initialize submodules
before building.

With `BUILD_TESTS=ON`, the test build compiles a separate copy of the library
sources and, when enabled, the diagnostic control model. GCC and Clang coverage
instrumentation is confined to the tests directory. The normal library,
examples, tools, and brokers remain free of coverage instrumentation, including
when they are packaged from a build that also runs tests. MSVC coverage uses
OpenCppCoverage rather than compiler instrumentation.

## Windows library and tests

Run each command from the repository root through MSYS2/UCRT64:

```powershell
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'cmake -S . -B cmake-build-debug-mingw-ucrt64-ninja -G Ninja -DCMAKE_BUILD_TYPE=Debug'
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'cmake --build cmake-build-debug-mingw-ucrt64-ninja'
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c './cmake-build-debug-mingw-ucrt64-ninja/tests/test_libvirtualhid.exe'
```

The normal library also builds with MSVC. The Windows driver package requires
the WDK/MSVC toolchain and is built separately; see
[Windows package](windows-driver.md#build-the-driver-package).

Windows CI checks production objects and static libraries for coverage symbols
in both compiler builds and before signing the driver package. Inspecting the
inputs also catches instrumentation when the final executable has no symbol
table. Run the same check on a local build with MSYS2/UCRT64 binutils installed:

```powershell
& 'C:\msys64\msys2_shell.cmd' -defterm -here -no-start -ucrt64 -c 'bash scripts/windows/check-package-coverage.sh cmake-build-debug-mingw-ucrt64-ninja'
```

The script checks all built configurations and excludes the separate test
targets. Pass the MSVC build directory to check a Visual Studio build.

## Linux and macOS

```sh
cmake -S . -B cmake-build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
./cmake-build-debug/tests/test_libvirtualhid
```

Linux installed-device tests need access to `/dev/uhid` or `/dev/uinput`.
A macOS test build does not prove virtual gamepad creation; that requires a
signed broker with Apple's virtual HID entitlement. See
[macOS setup](macos-gamepad.md).

## Documentation and validation

Public declarations and changed behavior should have accurate Doxygen
comments. Markdown pages cover tasks readers perform and limits they need to
know. The documentation target uses Dockle and the sources listed in
`dockle.toml`:

```sh
cmake --build cmake-build-debug --target docs
```

Validate gamepad changes against the GoogleTest lifecycle suite and
[adapter example](../examples/gamepad_adapter.cpp). Installed driver, signing,
and physical-consumer behavior require platform-specific checks.
