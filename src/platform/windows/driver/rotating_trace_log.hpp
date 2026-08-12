// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/windows/driver/rotating_trace_log.hpp
 * @brief Bounded file logging for the Windows UMDF driver.
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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace lvh::detail::windows {
  /** Maximum size of the active UMDF trace file before it is rotated. */
  inline constexpr std::uint64_t umdf_trace_log_max_size {5ULL * 1024ULL * 1024ULL};

  /** Number of previous UMDF trace files retained. */
  inline constexpr std::size_t umdf_trace_log_retained_file_count {5};

  /** Win32 function used to query a trace-log file size. */
  using TraceLogGetFileSize = decltype(&::GetFileSizeEx);

  /** Win32 function used to write a trace-log line. */
  using TraceLogWriteFile = decltype(&::WriteFile);

  /** Win32 operations used by the trace-log writer. */
  struct TraceLogOperations {
    TraceLogGetFileSize get_file_size {&::GetFileSizeEx};
    TraceLogWriteFile write_file {&::WriteFile};
  };

  /**
   * @brief Determine whether a trace-log line can be passed to WriteFile.
   * @param line_size Line size in bytes.
   * @return `true` when the size fits in WriteFile's DWORD parameter.
   */
  constexpr bool trace_log_line_size_supported(std::size_t line_size) {
    return line_size <= (std::numeric_limits<DWORD>::max)();
  }

  /**
   * @brief Determine whether a Win32 path operation failed because the path is absent.
   * @param error Win32 error code returned by GetLastError.
   * @return `true` for missing files and missing parent paths.
   */
  constexpr bool trace_log_path_missing(DWORD error) {
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
  }

  /**
   * @brief Validate trace-log append arguments without accessing the line buffer.
   * @param log_path Path to the active trace log.
   * @param line_size Line size in bytes.
   * @param max_size Maximum active-file size in bytes.
   * @return `true` when the append arguments are valid.
   */
  constexpr bool trace_log_arguments_valid(
    std::wstring_view log_path,
    std::size_t line_size,
    std::uint64_t max_size
  ) {
    return !log_path.empty() && line_size != 0U && max_size != 0U && trace_log_line_size_supported(line_size);
  }

  /**
   * @brief Query a trace-log file size without accessing a LARGE_INTEGER union member.
   * @param file Open file handle.
   * @param size Receives the file size in bytes.
   * @param operations Win32 operations used by the trace-log writer.
   * @return `true` when the size was retrieved; otherwise `false`.
   */
  bool get_trace_log_file_size(
    HANDLE file,
    std::uint64_t &size,
    const TraceLogOperations &operations = {}
  );

  /**
   * @brief Write one complete trace-log line.
   * @param file Open file handle.
   * @param line Complete line to write.
   * @param operations Win32 operations used by the trace-log writer.
   * @return `true` when every byte was written; otherwise `false`.
   */
  bool write_trace_log_line(
    HANDLE file,
    std::string_view line,
    const TraceLogOperations &operations = {}
  );

  /**
   * @brief Return the path for a rotated trace-log generation.
   * @param log_path Path to the active trace log.
   * @param generation Generation number, where one is the newest backup.
   * @return The path with the generation number appended.
   */
  std::wstring rotated_trace_log_path(std::wstring_view log_path, std::size_t generation);

  /**
   * @brief Append a line while keeping the trace log within a bounded set of files.
   *
   * The active file is rotated before an append would exceed @p max_size. The
   * active file becomes `.1`, older generations advance by one, and the oldest
   * generation is removed. A single line larger than the limit is written to
   * an empty active file so that the diagnostic entry is not silently split.
   *
   * @param log_path Path to the active trace log.
   * @param line Complete line to append.
   * @param max_size Maximum active-file size in bytes.
   * @param retained_file_count Number of rotated files to retain.
   * @param operations Win32 operations used by the trace-log writer.
   * @return `true` when the complete line was written; otherwise `false`.
   */
  bool append_rotating_trace_log(
    std::wstring_view log_path,
    std::string_view line,
    std::uint64_t max_size = umdf_trace_log_max_size,
    std::size_t retained_file_count = umdf_trace_log_retained_file_count,
    const TraceLogOperations &operations = {}
  );
}  // namespace lvh::detail::windows
