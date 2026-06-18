#
# Project configuration
#
cmake_minimum_required(VERSION 3.24)
project(libvirtualhid VERSION 0.1.0
        DESCRIPTION "Cross-platform virtual HID device library."
        HOMEPAGE_URL "https://app.lizardbyte.dev"
        LANGUAGES CXX)

set(PROJECT_LICENSE "MIT")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(LIBVIRTUALHID_IS_TOP_LEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(LIBVIRTUALHID_IS_TOP_LEVEL ON)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#
# Project optional configuration
#
option(BUILD_DOCS "Build documentation" OFF)
option(BUILD_TESTS "Build tests" ${LIBVIRTUALHID_IS_TOP_LEVEL})
option(BUILD_EXAMPLES "Build examples" ${LIBVIRTUALHID_IS_TOP_LEVEL})

set(CMAKE_COLOR_MAKEFILE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

function(libvirtualhid_copy_mingw_runtime target_name)
    if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        return()
    endif()

    get_filename_component(_lvh_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach(_lvh_runtime_dll IN ITEMS libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll)
        set(_lvh_runtime_path "${_lvh_compiler_dir}/${_lvh_runtime_dll}")
        if(EXISTS "${_lvh_runtime_path}")
            add_custom_command(TARGET "${target_name}" POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_lvh_runtime_path}"
                    "$<TARGET_FILE_DIR:${target_name}>"
                    COMMENT "Copying ${_lvh_runtime_dll} to $<TARGET_FILE_DIR:${target_name}>")
        endif()
    endforeach()
endfunction()

#
# Library
#
add_subdirectory(src)

#
# Examples, tests, and docs are top-level only
#
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    if(BUILD_EXAMPLES)
        add_subdirectory(examples)
    endif()

    if(BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()

#
# Package config
#
configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libvirtualhid-config.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libvirtualhid"
)

write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config-version.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid-config-version.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libvirtualhid")
