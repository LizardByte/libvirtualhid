// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/broker/xbox_360_software_device.hpp
 * @brief Broker-owned lifetime for one Xbox 360 software device.
 */
#pragma once

#include "lvh_windows_broker_protocol.h"

#include <cstdint>
#include <memory>
#include <string>

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace lvh::windows {

  class Xbox360SoftwareDevice {
    struct Implementation;

  public:
    explicit Xbox360SoftwareDevice(std::unique_ptr<Implementation> implementation);
    Xbox360SoftwareDevice(const Xbox360SoftwareDevice &) = delete;
    Xbox360SoftwareDevice &operator=(const Xbox360SoftwareDevice &) = delete;
    Xbox360SoftwareDevice(Xbox360SoftwareDevice &&) noexcept;
    Xbox360SoftwareDevice &operator=(Xbox360SoftwareDevice &&) noexcept;
    ~Xbox360SoftwareDevice();

    static std::unique_ptr<Xbox360SoftwareDevice> create(
      const LvhWindowsCreateDeviceRequest &request,
      std::uint64_t driver_device_id,
      const LvhWindowsSessionToken &session_token,
      HANDLE client_process,
      LvhWindowsCreateDeviceResponse &response,
      LvhWindowsBrokerStatusCode &status,
      std::string &message
    );

  private:
    std::unique_ptr<Implementation> implementation_;
  };

}  // namespace lvh::windows
