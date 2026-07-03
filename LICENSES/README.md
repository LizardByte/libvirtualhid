# License Map

`libvirtualhid` uses separate licenses for the cross-platform library and the
Windows driver package.

- LB-SAL 1.0 SPDX custom identifier: `LicenseRef-LizardByte-SAL-1.0`.
- Cross-platform library source, public headers, non-driver backends, examples,
  tests, build scripts, and documentation unless listed below:
  MIT.
- Windows UMDF driver source under `src/platform/windows/driver/`:
  [LizardByte Source-Available License 1.0](LicenseRef-LizardByte-SAL-1.0.md).
- Generated Windows driver package artifacts, including the driver MSI:
  [LizardByte Source-Available License 1.0](LicenseRef-LizardByte-SAL-1.0.md).

The Windows driver MSI may include MIT-licensed helper components from this
repository; packaged installs include both license texts for that reason.
