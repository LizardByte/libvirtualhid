/**
 * @file tests/fixtures/include/fixtures/fixtures.hpp
 * @brief Shared GoogleTest fixture setup for libvirtualhid tests.
 */
#pragma once

// standard includes
#include <sstream>
#include <streambuf>

// lib includes
#include <gtest/gtest.h>

/**
 * @brief Base class used by default for every test.
 */
class BaseTest: public ::testing::Test {
protected:
  /**
   * @brief Set up the test.
   */
  void SetUp() override;

  /**
   * @brief Tear down the test.
   */
  void TearDown() override;

private:
  std::stringstream cout_buffer_;
  std::streambuf *cout_streambuf_ {nullptr};
};

// Undefine the original TEST macro.
#undef TEST  // NOSONAR(cpp:S959): Tests intentionally wrap TEST to use BaseTest.

// Redefine TEST to automatically use the shared BaseTest fixture.
#define TEST(test_case_name, test_name) \
  GTEST_TEST_(test_case_name, test_name, ::BaseTest, ::testing::internal::GetTypeId<::BaseTest>())
