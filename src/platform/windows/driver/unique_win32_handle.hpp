// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/driver/unique_win32_handle.hpp
 * @brief RAII ownership for Win32 handles used by the UMDF driver.
 */
#pragma once

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
#include <Windows.h>

// standard includes
#include <memory>

namespace lvh::detail::windows {
  /**
   * @brief Unique ownership for a Win32 handle.
   */
  using UniqueWin32Handle = std::unique_ptr<void, decltype(&::CloseHandle)>;

  /**
   * @brief Adopt a Win32 handle and normalize invalid handles to null.
   * @param handle Handle to adopt.
   * @return Unique ownership of a valid handle, or an empty owner.
   */
  inline UniqueWin32Handle make_unique_win32_handle(HANDLE handle = nullptr) {
    return {
      handle == INVALID_HANDLE_VALUE ? nullptr : handle,
      &::CloseHandle,
    };
  }
}  // namespace lvh::detail::windows
