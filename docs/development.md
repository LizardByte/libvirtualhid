# Development

Build directories use the `cmake-build-` prefix. The shared GoogleTest binary
is `tests/test_libvirtualhid` in the build directory. Initialize submodules
before building.

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
