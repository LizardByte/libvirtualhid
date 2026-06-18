/**
 * @file tests/fixtures/fixtures.cpp
 * @brief Shared GoogleTest fixture setup definitions.
 */

// standard includes
#include <iostream>

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
