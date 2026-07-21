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
add_subdirectory("${PROJECT_SOURCE_DIR}/third-party/lizardbyte-common/third-party/googletest"
                 "third-party/googletest")

set(LIZARDBYTE_COMMON_BUILD_TEST_SUPPORT ON CACHE BOOL "Build lizardbyte-common GoogleTest support helpers" FORCE)
if(NOT TARGET lizardbyte::common)
    add_subdirectory("${PROJECT_SOURCE_DIR}/third-party/lizardbyte-common"
                     "third-party/lizardbyte-common")
elseif(NOT TARGET lizardbyte::test_support)
    message(FATAL_ERROR "libvirtualhid tests require lizardbyte::test_support")
endif()
set(TEST_BINARY test_libvirtualhid)

set(LIBVIRTUALHID_TEST_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_gamepad_adapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_gamepad_lifecycle.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_profiles.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_report.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_runtime.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_windows_protocol.cpp")

if(TARGET virtualhid_control_model)
    list(APPEND LIBVIRTUALHID_TEST_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_virtualhid_control_model.cpp")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
    pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

    list(APPEND LIBVIRTUALHID_TEST_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/linux_backend_test_hooks.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_linux_backend.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_linux_consumers.cpp")

    if(LIBVIRTUALHID_ENABLE_XTEST)
        find_package(X11 QUIET)
    endif()
elseif(WIN32)
    list(APPEND LIBVIRTUALHID_TEST_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/windows_backend_test_hooks.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_windows_backend.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_windows_consumers.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_windows_driver_protocol.cpp")
elseif(APPLE)
    list(APPEND LIBVIRTUALHID_TEST_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/macos_backend_test_hooks.cpp"
         "${CMAKE_CURRENT_SOURCE_DIR}/unit/test_macos_backend.cpp")
endif()

add_executable(${TEST_BINARY}
  ${LIBVIRTUALHID_TEST_SOURCES})

target_include_directories(${TEST_BINARY}
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/include"
  "${PROJECT_SOURCE_DIR}/src/platform/windows/shared"
  "${PROJECT_SOURCE_DIR}/src")

target_link_libraries(${TEST_BINARY}
  PRIVATE
  gmock_main
  libvirtualhid::libvirtualhid
  lizardbyte::test_support)

if(TARGET virtualhid_control_model)
    target_link_libraries(${TEST_BINARY}
      PRIVATE
      virtualhid_control_model)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(${TEST_BINARY}
      PRIVATE
      PkgConfig::LIBEVDEV
      PkgConfig::LIBINPUT
      PkgConfig::SDL2)
elseif(APPLE)
    target_link_libraries(${TEST_BINARY}
      PRIVATE
      "-framework ApplicationServices"
      "-framework Carbon"
      "-framework CoreFoundation"
      "-framework IOKit")
elseif(WIN32)
    target_link_libraries(${TEST_BINARY}
      PRIVATE
      dinput8
      dxguid
      hid
      setupapi)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND LIBVIRTUALHID_ENABLE_XTEST AND X11_FOUND AND X11_XTest_FOUND)
    target_compile_definitions(${TEST_BINARY}
      PRIVATE
      LIBVIRTUALHID_HAVE_XTEST=1)
    target_include_directories(${TEST_BINARY}
      PRIVATE
      ${X11_INCLUDE_DIR}
      ${X11_XTest_INCLUDE_PATH})
    target_link_libraries(${TEST_BINARY}
      PRIVATE
      ${X11_LIBRARIES}
      ${X11_XTest_LIB})
endif()

libvirtualhid_copy_mingw_runtime(${TEST_BINARY})

gtest_discover_tests(${TEST_BINARY})
