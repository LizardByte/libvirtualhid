# Repository guidance

## Build and tests

On Windows, compile the normal C++ library with MSYS2/UCRT64. Prefix Windows
build and test commands with
`C:\msys64\msys2_shell.cmd -defterm -here -no-start -ucrt64 -c`.
Keep the library buildable with both MSVC and MinGW/UCRT64. The UMDF driver
package is a separate WDK/MSVC build.

Prefix build directories with `cmake-build-`. The GoogleTest executable is
`tests/test_libvirtualhid` inside the build directory (with `.exe` on
Windows). GoogleTest is vendored under
`third-party/lizardbyte-common/third-party/googletest`.

Add or update meaningful tests for changed behavior. Aim for full coverage of
changed code, and validate gamepad API and lifecycle changes against
`examples/gamepad_adapter.cpp` and the lifecycle tests. Run installed-device
or hardware checks when the change depends on real devices; describe any
validation that cannot run locally.

## API and platform boundaries

Keep the public C++ API platform-neutral. Put virtual HID details behind
backend implementations, not in consumer code. Gamepads are the primary
target, with remote streaming hosts as the first consumer class.

Windows support must remain user-mode; do not add a custom kernel-mode driver.
For Linux, prefer `uhid` for descriptor-driven gamepads and `uinput` for
keyboard and mouse, using X11/XTest only as a fallback.

## Code style and documentation

Follow `.clang-format` for C and C++ code. Do not add decorative separator
comments; use descriptive names and code structure to show grouping.

When changing headers, backends, or consumer-facing behavior, add or update
the affected Doxygen documentation blocks. Document relevant declarations and
behavior in code, including parameters and return values where applicable.
Use this style for primary documentation:

```cpp
/**
 * @brief Describe the function or type.
 *
 * @param value Describe the parameter.
 * @return Describe the result.
 */
```

Use `///< ...` for inline Doxygen comments. Keep Markdown guides focused on
installation, usage, limitations, and troubleshooting that readers need.
Update a Markdown page when one of those user-facing instructions actually
changes; routine source changes do not require Markdown edits.
Pad Markdown table cells so the `|` separators align vertically in source,
accounting for wide Unicode characters such as emoji.

## Issues and pull requests

When asked to create an issue or pull request, use the applicable templates
from LizardByte/.github, or this repository if it has a more specific template.
At the end of the body, add an attribution naming the AI agent, model, and
thinking level used to generate it. Repeat that attribution in a separate
comment on the created issue or pull request.

When asked only to create an issue, investigate enough to verify and accurately
describe the observed behavior, expected behavior, and reproduction or
evidence. Keep the issue concise. Investigate or fix fully when that is the
request.

## Code reviews

Report concrete issues introduced by a change. Explain the trigger, impact,
and a practical fix; point to the relevant code or test. Keep workflow status
separate from code findings.

If GitHub Actions workflows await maintainer approval, or a dependent check
times out before its job can run, do not leave an inline comment, request
changes, or ask the contributor to run them. If missing results materially
limit the review, mention that once in the summary. Review actual failed
checks when their logs are available.

For pull request reviews, verify the current PR head and mergeability against
its target branch. If conflicts are confirmed, tell the author to rebase and
resolve them. Do not infer conflicts from unknown or pending mergeability.
When approving a pull request, the approval itself is sufficient; do not add
a comment solely to accompany it.
