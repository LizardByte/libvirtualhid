/**
 * @file tests/unit/test_windows_rotating_trace_log.cpp
 * @brief Unit tests for the bounded Windows UMDF trace log.
 */

// platform includes
#include <Windows.h>

// standard includes
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

// local includes
#include "fixtures/fixtures.hpp"
#include "platform/windows/driver/rotating_trace_log.hpp"
#include "platform/windows/driver/unique_win32_handle.hpp"

namespace {
  BOOL WINAPI fail_file_size(HANDLE, PLARGE_INTEGER) {
    return FALSE;
  }

  void write_file(const std::filesystem::path &path, std::string_view content) {
    auto output = std::ofstream {path, std::ios::binary};
    output << content;
  }

  std::string read_file(const std::filesystem::path &path) {
    auto input = std::ifstream {path, std::ios::binary};
    return {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
  }

  class WindowsRotatingTraceLogTest: public WindowsTest {
  protected:
    void SetUp() override {
      WindowsTest::SetUp();
      std::filesystem::remove_all(test_directory);
      std::filesystem::create_directories(test_directory);
    }

    void TearDown() override {
      std::filesystem::remove_all(test_directory);
      WindowsTest::TearDown();
    }

    std::filesystem::path rotated_path(std::size_t generation) const {
      return lvh::detail::windows::rotated_trace_log_path(log_path.native(), generation);
    }

    const std::filesystem::path test_directory {
      std::filesystem::path {LIBVIRTUALHID_TEST_BINARY_DIR} /
      std::format("libvirtualhid-rotating-trace-log-tests-{}", GetCurrentProcessId())
    };
    const std::filesystem::path log_path {test_directory / "driver.log"};
  };

  TEST_F(WindowsRotatingTraceLogTest, AppendsWithoutRotatingAtTheSizeLimit) {
    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "1234", 8U, 2U));
    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "5678", 8U, 2U));

    EXPECT_EQ(read_file(log_path), "12345678");
    EXPECT_FALSE(std::filesystem::exists(rotated_path(1U)));
  }

  TEST_F(WindowsRotatingTraceLogTest, RejectsInvalidArguments) {
    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log({}, "line", 8U, 2U));
    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), {}, 8U, 2U));
    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "line", 0U, 2U));

    EXPECT_TRUE(
      lvh::detail::windows::trace_log_arguments_valid(log_path.native(), (std::numeric_limits<DWORD>::max)(), 8U)
    );
    EXPECT_FALSE(
      lvh::detail::windows::trace_log_arguments_valid(
        log_path.native(),
        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)()) + 1U,
        8U
      )
    );
  }

  TEST_F(WindowsRotatingTraceLogTest, RejectsAnUnavailableLogPath) {
    const auto unavailable_path = test_directory / "missing" / "driver.log";
    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(unavailable_path.native(), "line", 8U, 2U));
  }

  TEST_F(WindowsRotatingTraceLogTest, ReportsFileSizeAndWriteFailures) {
    auto operations = lvh::detail::windows::TraceLogOperations {};
    operations.get_file_size = &fail_file_size;
    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "line", 8U, 2U, operations));

    operations = {};
    operations.write_file = [](HANDLE, auto, DWORD, LPDWORD bytes_written, LPOVERLAPPED) {
      *bytes_written = 0U;
      return FALSE;
    };
    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "line", 8U, 2U, operations));

    operations.write_file = [](HANDLE, auto, DWORD bytes_to_write, LPDWORD bytes_written, LPOVERLAPPED) {
      *bytes_written = bytes_to_write - 1U;
      return TRUE;
    };
    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "line", 8U, 2U, operations));
  }

  TEST_F(WindowsRotatingTraceLogTest, RecognizesMissingPathErrors) {
    EXPECT_TRUE(lvh::detail::windows::trace_log_path_missing(ERROR_FILE_NOT_FOUND));
    EXPECT_TRUE(lvh::detail::windows::trace_log_path_missing(ERROR_PATH_NOT_FOUND));
    EXPECT_FALSE(lvh::detail::windows::trace_log_path_missing(ERROR_ACCESS_DENIED));
  }

  TEST_F(WindowsRotatingTraceLogTest, NormalizesAnInvalidWin32Handle) {
    EXPECT_FALSE(lvh::detail::windows::make_unique_win32_handle(INVALID_HANDLE_VALUE));
  }

  TEST_F(WindowsRotatingTraceLogTest, RotatesBeforeAnAppendExceedsTheLimit) {
    write_file(log_path, "12345678");

    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "new", 8U, 2U));

    EXPECT_EQ(read_file(log_path), "new");
    EXPECT_EQ(read_file(rotated_path(1U)), "12345678");
  }

  TEST_F(WindowsRotatingTraceLogTest, RetainsOnlyTheConfiguredGenerations) {
    constexpr auto max_size = std::uint64_t {4};
    constexpr auto retained_files = std::size_t {2};

    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "aaaa", max_size, retained_files));
    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "bbbb", max_size, retained_files));
    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "cccc", max_size, retained_files));
    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "dddd", max_size, retained_files));

    EXPECT_EQ(read_file(log_path), "dddd");
    EXPECT_EQ(read_file(rotated_path(1U)), "cccc");
    EXPECT_EQ(read_file(rotated_path(2U)), "bbbb");
    EXPECT_FALSE(std::filesystem::exists(rotated_path(3U)));
  }

  TEST_F(WindowsRotatingTraceLogTest, DiscardsTheCurrentLogWhenNoBackupsAreRetained) {
    write_file(log_path, "full");

    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "new", 4U, 0U));

    EXPECT_EQ(read_file(log_path), "new");
    EXPECT_FALSE(std::filesystem::exists(rotated_path(1U)));
  }

  TEST_F(WindowsRotatingTraceLogTest, SupportsMissingGenerations) {
    write_file(log_path, "current");
    write_file(rotated_path(2U), "second");

    EXPECT_TRUE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "new", 7U, 3U));

    EXPECT_EQ(read_file(log_path), "new");
    EXPECT_EQ(read_file(rotated_path(1U)), "current");
    EXPECT_FALSE(std::filesystem::exists(rotated_path(2U)));
    EXPECT_EQ(read_file(rotated_path(3U)), "second");
  }

  TEST_F(WindowsRotatingTraceLogTest, LeavesTheBoundedLogUntouchedWhenRotationFails) {
    write_file(log_path, "full");
    std::filesystem::create_directory(rotated_path(1U));

    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "new", 4U, 1U));

    EXPECT_EQ(read_file(log_path), "full");
    EXPECT_TRUE(std::filesystem::is_directory(rotated_path(1U)));
  }

  TEST_F(WindowsRotatingTraceLogTest, ReportsALockedGenerationMoveFailure) {
    write_file(log_path, "full");
    write_file(rotated_path(1U), "previous");
    const auto locked_generation = lvh::detail::windows::make_unique_win32_handle(CreateFileW(
      rotated_path(1U).c_str(),
      GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr
    ));
    ASSERT_TRUE(locked_generation);

    EXPECT_FALSE(lvh::detail::windows::append_rotating_trace_log(log_path.native(), "new", 4U, 2U));

    EXPECT_EQ(read_file(log_path), "full");
    EXPECT_EQ(read_file(rotated_path(1U)), "previous");
    EXPECT_FALSE(std::filesystem::exists(rotated_path(2U)));
  }
}  // namespace
