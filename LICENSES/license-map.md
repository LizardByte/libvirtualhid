# License Map

`libvirtualhid` uses separate licenses for the cross-platform library and the
Windows driver and macOS broker packages.

- LB-SAL 1.0 SPDX custom identifier: `LicenseRef-LizardByte-SAL-1.0`.
- Cross-platform library source, public headers, non-driver backends, examples,
  tests, build scripts, and documentation unless listed below:
  [MIT](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/MIT.md).
- HIDMaestro's `MinimumViablePidFfbBlock` descriptor bytes in
  `src/platform/windows/shared/generic_pid_protocol.hpp`, used by the Windows
  library backend, UMDF driver, and packaged tools:
  [HIDMaestro MIT notice](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/HIDMaestro-MIT.md),
  Copyright (c) 2026 HIDMaestro Contributors. The notice is copied from
  [HIDMaestro's LICENSE](https://github.com/hifihedgehog/HIDMaestro/blob/master/LICENSE).
- Windows UMDF driver source under `src/platform/windows/driver/`, the broker
  service under `src/platform/windows/broker/`, and the broker entitlement and
  evaluation sources
  `src/platform/windows/shared/lvh_windows_broker_config.hpp` and
  `src/platform/windows/shared/lvh_windows_github_actions_evaluation.hpp`:
  [LizardByte Source-Available License 1.0](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/LicenseRef-LizardByte-SAL-1.0.md).
- Generated Windows driver package artifacts, including the driver MSI:
  [LizardByte Source-Available License 1.0](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/LicenseRef-LizardByte-SAL-1.0.md).
- macOS broker source under `src/platform/macos/broker/`, plus generated macOS
  broker bundles and DMGs:
  [LizardByte Source-Available License 1.0](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/LicenseRef-LizardByte-SAL-1.0.md).

Every installed library distribution includes the LizardByte and HIDMaestro MIT
notices under `share/licenses/libvirtualhid`. The Windows driver MSI includes
both MIT notices and the LB-SAL license under `licenses`. The macOS broker DMG
includes the `LICENSES` directory with the MIT notices and the LB-SAL license.
