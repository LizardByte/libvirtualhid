#
# Setup GoogleTest
#
set(INSTALL_GTEST OFF)
set(INSTALL_GMOCK OFF)
if(WIN32)
    set(gtest_force_shared_crt ON CACHE BOOL  # cmake-lint: disable=C0103
        "Always use msvcrt.dll" FORCE)
endif()

include(GoogleTest)
add_subdirectory("${PROJECT_SOURCE_DIR}/third-party/googletest"
                 "third-party/googletest")

set(TEST_BINARY test_libvirtualhid)

add_executable(${TEST_BINARY}
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/fixtures.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_profiles.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_report.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_runtime.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_sunshine_adapter.cpp")

target_include_directories(${TEST_BINARY}
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include")

target_link_libraries(${TEST_BINARY}
  PRIVATE
  gmock_main
  libvirtualhid::libvirtualhid)

libvirtualhid_copy_mingw_runtime(${TEST_BINARY})

gtest_discover_tests(${TEST_BINARY})
