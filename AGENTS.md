On Windows we use msys2 and ucrt64 to compile.
You need to prefix commands with `C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c`.

Prefix build directories with `cmake-build-`.

The test executable is named `test_libvirtualhid` and will be located inside the `tests` directory within
the build directory.

The project uses gtest as a test framework. GoogleTest is vendored as a submodule under `third-party/googletest`.

Keep the public c++ API platform-neutral. Platform-specific virtual HID details belong behind backend
implementations and should not leak into consumer code.

Gamepad support is the primary target. Sunshine is the first target consumer, so validate API and behavior
changes against the Sunshine-oriented adapter example and tests.

Windows support must remain user-mode. Do not add a custom kernel-mode driver. The normal c++ library should
remain buildable with both MSVC and MinGW/UCRT64; any future UMDF driver package is a separate WDK/MSVC build
artifact.

Linux gamepad support should prefer uinput, evdev, and uhid. X11 XTest fallbacks are only for secondary
keyboard and mouse goals.

Always update public documentation when changing headers, backends, or consumer-facing behavior.

Always follow the style guidelines defined in .clang-format for c/c++ code when that file is present.
