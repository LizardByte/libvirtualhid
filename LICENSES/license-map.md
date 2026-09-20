# License Map

`libvirtualhid` uses separate licenses for the cross-platform library and the
Windows driver package.

- LB-SAL 1.0 SPDX custom identifier: `LicenseRef-LizardByte-SAL-1.0`.
- Cross-platform library source, public headers, non-driver backends, examples,
  tests, build scripts, and documentation unless listed below:
  [MIT](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/MIT.md).
- Windows UMDF driver source under `src/platform/windows/driver/`, the broker
  service under `src/platform/windows/broker/`, and the broker entitlement and
  evaluation sources
  `src/platform/windows/shared/lvh_windows_broker_config.hpp` and
  `src/platform/windows/shared/lvh_windows_github_actions_evaluation.hpp`:
  [LizardByte Source-Available License 1.0](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/LicenseRef-LizardByte-SAL-1.0.md).
- Generated Windows driver package artifacts, including the driver MSI:
  [LizardByte Source-Available License 1.0](https://github.com/LizardByte/libvirtualhid/blob/master/LICENSES/LicenseRef-LizardByte-SAL-1.0.md).

Every installed library distribution includes the MIT notice under
`share/licenses/libvirtualhid`. The Windows driver MSI may include MIT-licensed
helper components from this repository; packaged installs include both license
texts for that reason.
