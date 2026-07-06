#
# Project configuration
#
cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(libvirtualhid VERSION 0.0.0
        DESCRIPTION "Cross-platform virtual HID device library."
        HOMEPAGE_URL "https://app.lizardbyte.dev"
        LANGUAGES CXX)

set(PROJECT_LICENSE "MIT")
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIBVIRTUALHID_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(LIBVIRTUALHID_IS_TOP_LEVEL ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING
        "Choose the type of build." FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_version.cmake")

#
# Project optional configuration
#
option(BUILD_DOCS "Build documentation" ${LIBVIRTUALHID_IS_TOP_LEVEL})
option(BUILD_TESTS "Build tests" ${LIBVIRTUALHID_IS_TOP_LEVEL})
option(BUILD_EXAMPLES "Build examples" ${LIBVIRTUALHID_IS_TOP_LEVEL})
option(LIBVIRTUALHID_BUILD_TOOLS "Build libvirtualhid diagnostic tools" ${LIBVIRTUALHID_IS_TOP_LEVEL})
option(LIBVIRTUALHID_TOOLS_STATIC_RUNTIME "Link libvirtualhid tools to static compiler runtimes where supported" ON)
option(LIBVIRTUALHID_TOOLS_FULLY_STATIC "Attempt to link libvirtualhid tools as fully static binaries" OFF)
option(LIBVIRTUALHID_ENABLE_XTEST "Enable X11/XTest keyboard and mouse fallback on Linux" ON)
option(LIBVIRTUALHID_BUILD_WINDOWS_DRIVER "Build the Windows UMDF2 driver package with the WDK/MSVC toolchain" OFF)
option(LIBVIRTUALHID_INSTALL
        "Install libvirtualhid targets, headers, and CMake package files" ${LIBVIRTUALHID_IS_TOP_LEVEL})
option(LIBVIRTUALHID_ENABLE_PACKAGING "Enable CPack package metadata" ${LIBVIRTUALHID_INSTALL})
option(LIBVIRTUALHID_WARNINGS_AS_ERRORS "Treat libvirtualhid warnings as errors" ${LIBVIRTUALHID_IS_TOP_LEVEL})

set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(LIBVIRTUALHID_USES_THREADS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(LIBVIRTUALHID_USES_THREADS ON)
endif()

#
# Additional setup for coverage
# https://gcovr.com/en/stable/guide/compiling.html#compiler-options
#
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND BUILD_TESTS AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "-fprofile-arcs -ftest-coverage -ggdb -O0")
    set(CMAKE_C_FLAGS "-fprofile-arcs -ftest-coverage -ggdb -O0")
endif()

# Copy MinGW runtime DLLs beside a target when using GNU toolchains on Windows.
function(libvirtualhid_copy_mingw_runtime target_name)
    if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        return()
    endif()

    get_filename_component(lvh_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(lvh_runtime_dll IN ITEMS
            libgcc_s_seh-1.dll
            libstdc++-6.dll
            libwinpthread-1.dll)
        set(lvh_runtime_path "${lvh_compiler_dir}/${lvh_runtime_dll}")
        if(EXISTS "${lvh_runtime_path}")
            add_custom_command(TARGET "${target_name}" POST_BUILD
                               COMMAND ${CMAKE_COMMAND} -E copy_if_different
                                       "${lvh_runtime_path}"
                                       "$<TARGET_FILE_DIR:${target_name}>"
                               COMMENT "Copying MinGW runtime ${lvh_runtime_dll}")
        endif()
    endforeach()
endfunction()

#
# Library code is located here
# When building tests this must be after the coverage flags are set
#
add_subdirectory(src)

#
# Examples, tests, and docs are top-level only
#
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    if(WIN32 AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
        add_subdirectory(src/platform/windows/driver)
    endif()

    if(BUILD_DOCS)
        add_subdirectory(third-party/doxyconfig docs)
    endif()

    if(BUILD_EXAMPLES)
        add_subdirectory(examples)
    endif()

    if(LIBVIRTUALHID_BUILD_TOOLS)
        add_subdirectory(tools)
    endif()

    if(BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

#
# Package config
#
if(LIBVIRTUALHID_INSTALL)
    configure_package_config_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libvirtualhid-config.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config.cmake"
      INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libvirtualhid")

    write_basic_package_version_file(
      "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config-version.cmake"
      VERSION ${PROJECT_VERSION}
      COMPATIBILITY SameMajorVersion)

    install(FILES
            "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config.cmake"
            "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config-version.cmake"
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libvirtualhid")
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME
   AND LIBVIRTUALHID_ENABLE_PACKAGING
   AND LIBVIRTUALHID_INSTALL)
    include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/packaging/common.cmake")
endif()
