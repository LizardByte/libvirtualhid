// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/driver/rotating_trace_log.cpp
 * @brief Bounded file logging for the Windows UMDF driver.
 */

#ifndef NOMINMAX
  #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
#endif

// platform includes
#include <Windows.h>

// standard includes
#include <bit>
#include <mutex>
#include <string>
#include <string_view>

// local includes
#include "rotating_trace_log.hpp"
#include "unique_win32_handle.hpp"

namespace lvh::detail::windows {
  namespace {
    class TraceLogMutex {
    public:
      [[nodiscard]] std::unique_lock<std::mutex> acquire() const {
        return std::unique_lock {mutex};
      }

    private:
      mutable std::mutex mutex;
    };

    const TraceLogMutex trace_log_mutex;

    bool remove_file_if_present(const std::wstring &path) {
      if (DeleteFileW(path.c_str()) != FALSE) {
        return true;
      }

      const auto error = GetLastError();
      return trace_log_path_missing(error);
    }

    bool move_file_if_present(const std::wstring &source, const std::wstring &destination) {
      if (MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE) {
        return true;
      }

      const auto error = GetLastError();
      return trace_log_path_missing(error);
    }

    bool rotate_trace_log(const std::wstring &log_path, std::size_t retained_file_count) {
      if (retained_file_count == 0U) {
        return remove_file_if_present(log_path);
      }

      if (!remove_file_if_present(rotated_trace_log_path(log_path, retained_file_count))) {
        return false;
      }

      for (auto generation = retained_file_count; generation > 1U; --generation) {
        if (!move_file_if_present(
              rotated_trace_log_path(log_path, generation - 1U),
              rotated_trace_log_path(log_path, generation)
            )) {
          return false;
        }
      }

      return move_file_if_present(log_path, rotated_trace_log_path(log_path, 1U));
    }

    UniqueWin32Handle open_trace_log(const std::wstring &log_path) {
      return make_unique_win32_handle(CreateFileW(
        log_path.c_str(),
        FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
      ));
    }

    bool should_rotate(std::uint64_t current_size, std::size_t incoming_size, std::uint64_t max_size) {
      if (current_size == 0U) {
        return false;
      }
      if (current_size >= max_size) {
        return true;
      }
      return incoming_size > max_size - current_size;
    }
  }  // namespace

  bool get_trace_log_file_size(HANDLE file, std::uint64_t &size, const TraceLogOperations &operations) {
    LARGE_INTEGER file_size {};
    if (operations.get_file_size(file, &file_size) == FALSE) {
      return false;
    }
    size = std::bit_cast<std::uint64_t>(file_size);
    return true;
  }

  bool write_trace_log_line(HANDLE file, std::string_view line, const TraceLogOperations &operations) {
    auto bytes_written = DWORD {};
    const auto bytes_to_write = static_cast<DWORD>(line.size());
    return operations.write_file(file, line.data(), bytes_to_write, &bytes_written, nullptr) != FALSE &&
           bytes_written == bytes_to_write;
  }

  std::wstring rotated_trace_log_path(std::wstring_view log_path, std::size_t generation) {
    auto rotated_path = std::wstring {log_path};
    rotated_path.push_back(L'.');
    rotated_path.append(std::to_wstring(generation));
    return rotated_path;
  }

  bool append_rotating_trace_log(
    std::wstring_view log_path,
    std::string_view line,
    std::uint64_t max_size,
    std::size_t retained_file_count,
    const TraceLogOperations &operations
  ) {
    if (!trace_log_arguments_valid(log_path, line.size(), max_size)) {
      return false;
    }

    auto lock = trace_log_mutex.acquire();

    const auto path = std::wstring {log_path};
    auto file = open_trace_log(path);
    if (!file) {
      return false;
    }

    auto file_size = std::uint64_t {};
    if (!get_trace_log_file_size(file.get(), file_size, operations)) {
      return false;
    }

    if (should_rotate(file_size, line.size(), max_size)) {
      file.reset();
      if (!rotate_trace_log(path, retained_file_count)) {
        return false;
      }
      file = open_trace_log(path);
    }

    return write_trace_log_line(file.get(), line, operations);
  }
}  // namespace lvh::detail::windows
