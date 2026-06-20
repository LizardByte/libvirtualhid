/**
 * @file tests/fixtures/fixtures.cpp
 * @brief Shared GoogleTest fixture setup definitions.
 */

// standard includes
#include <iostream>

// platform includes
#if defined(__linux__)
  #include <unistd.h>
#endif

// local includes
#include "fixtures/fixtures.hpp"

void BaseTest::SetUp() {
  cout_buffer_.str({});
  cout_buffer_.clear();
  cout_streambuf_ = std::cout.rdbuf();
  std::cout.rdbuf(cout_buffer_.rdbuf());
}

void BaseTest::TearDown() {
  if (cout_streambuf_ != nullptr) {
    std::cout.rdbuf(cout_streambuf_);
    cout_streambuf_ = nullptr;
  }

  const auto *test_info = ::testing::UnitTest::GetInstance()->current_test_info();
  if (test_info != nullptr && test_info->result()->Failed()) {
    std::cout << std::endl
              << "Test failed: " << test_info->name() << std::endl
              << std::endl
              << "Captured cout:" << std::endl
              << cout_buffer_.str() << std::endl;
  }
}

#if !defined(__linux__)
void LinuxTest::SetUp() {
  GTEST_SKIP() << "Skipping, this test is for Linux only.";
}
#endif

::testing::AssertionResult LinuxTest::HasReadableWritableDeviceNode(const char *path) {
#if defined(__linux__)
  if (::access(path, R_OK | W_OK) == 0) {
    return ::testing::AssertionSuccess();
  }

  return ::testing::AssertionFailure() << path << " must be readable and writable";
#else
  static_cast<void>(path);
  return ::testing::AssertionSuccess();
#endif
}

#if !defined(__APPLE__) || !defined(__MACH__)
void MacOSTest::SetUp() {
  GTEST_SKIP() << "Skipping, this test is for macOS only.";
}
#endif

#if !defined(_WIN32)
void WindowsTest::SetUp() {
  GTEST_SKIP() << "Skipping, this test is for Windows only.";
}
#endif
