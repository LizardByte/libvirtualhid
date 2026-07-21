# SPDX-FileCopyrightText: 2026 David Lane
# SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

if(NOT WIN32)
    message(FATAL_ERROR "The libvirtualhid Windows driver package can only be built on Windows.")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "The libvirtualhid Windows driver package requires the Microsoft WDK/MSVC toolchain.")
endif()

if(LIBVIRTUALHID_ENABLE_PACKAGING
   AND LIBVIRTUALHID_INSTALL
   AND NOT CMAKE_CONFIGURATION_TYPES
   AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR
            "The libvirtualhid Windows driver package must not be built from a Debug configuration. "
            "Configure with -DCMAKE_BUILD_TYPE=Release before packaging the UMDF driver.")
endif()

set(LIBVIRTUALHID_WDK_ARCH "${CMAKE_VS_PLATFORM_NAME}")
if(NOT LIBVIRTUALHID_WDK_ARCH)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(LIBVIRTUALHID_WDK_ARCH x64)
    else()
        set(LIBVIRTUALHID_WDK_ARCH x86)
    endif()
endif()
string(TOLOWER "${LIBVIRTUALHID_WDK_ARCH}" LIBVIRTUALHID_WDK_ARCH)
if(LIBVIRTUALHID_WDK_ARCH STREQUAL "x64" OR LIBVIRTUALHID_WDK_ARCH STREQUAL "amd64")
    set(LIBVIRTUALHID_WDK_LIBRARY_ARCH x64)
    set(LIBVIRTUALHID_INF_ARCH amd64)
    set(LIBVIRTUALHID_INF2CAT_OS 10_X64)
elseif(LIBVIRTUALHID_WDK_ARCH STREQUAL "arm64")
    set(LIBVIRTUALHID_WDK_LIBRARY_ARCH arm64)
    set(LIBVIRTUALHID_INF_ARCH arm64)
    set(LIBVIRTUALHID_INF2CAT_OS 10_ARM64)
elseif(LIBVIRTUALHID_WDK_ARCH STREQUAL "win32" OR LIBVIRTUALHID_WDK_ARCH STREQUAL "x86")
    set(LIBVIRTUALHID_WDK_LIBRARY_ARCH x86)
    set(LIBVIRTUALHID_INF_ARCH x86)
    set(LIBVIRTUALHID_INF2CAT_OS 10_X86)
else()
    message(FATAL_ERROR "Unsupported WDK architecture: ${LIBVIRTUALHID_WDK_ARCH}")
endif()
set(LIBVIRTUALHID_DRIVER_VERSION
    "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK}")

set(_lvh_wdk_roots)
foreach(lvh_wdk_root IN ITEMS
        "$ENV{WDKContentRoot}"
        "$ENV{WindowsSdkDir}"
        "C:/Program Files (x86)/Windows Kits/10")
    if(NOT lvh_wdk_root)
        continue()
    endif()

    file(TO_CMAKE_PATH "${lvh_wdk_root}" lvh_wdk_root_cmake)
    string(REGEX REPLACE "/+$" "" lvh_wdk_root_cmake "${lvh_wdk_root_cmake}")
    list(APPEND _lvh_wdk_roots "${lvh_wdk_root_cmake}")
endforeach()
list(REMOVE_DUPLICATES _lvh_wdk_roots)

set(_lvh_wdf_include_candidates)
set(_lvh_wdk_shared_include_candidates)
set(_lvh_wdf_library_candidates)
set(_lvh_wdk_um_library_candidates)
set(_lvh_wdk_tool_candidates)
set(LIBVIRTUALHID_UMDF_LIBRARY_VERSION "2.15" CACHE STRING
    "UMDF library version used for the Windows driver package")
string(REGEX REPLACE "\\." "\\\\." _lvh_umdf_library_version_regex "${LIBVIRTUALHID_UMDF_LIBRARY_VERSION}")
foreach(lvh_wdk_root_cmake IN LISTS _lvh_wdk_roots)
    if(EXISTS "${lvh_wdk_root_cmake}")
        file(GLOB _lvh_wdf_include_glob
             LIST_DIRECTORIES true
             "${lvh_wdk_root_cmake}/Include/wdf/umdf/2.*")
        file(GLOB _lvh_wdf_library_glob
             LIST_DIRECTORIES true
             "${lvh_wdk_root_cmake}/Lib/wdf/umdf/${LIBVIRTUALHID_WDK_LIBRARY_ARCH}/2.*")
        file(GLOB _lvh_wdk_shared_include_glob
             LIST_DIRECTORIES true
             "${lvh_wdk_root_cmake}/Include/*/shared")
        file(GLOB _lvh_wdk_um_library_glob
             LIST_DIRECTORIES true
             "${lvh_wdk_root_cmake}/Lib/*/um/${LIBVIRTUALHID_WDK_LIBRARY_ARCH}")
        file(GLOB _lvh_wdk_tool_glob
             LIST_DIRECTORIES true
             "${lvh_wdk_root_cmake}/bin/*/x64"
             "${lvh_wdk_root_cmake}/bin/x64"
             "${lvh_wdk_root_cmake}/bin/*/x86"
             "${lvh_wdk_root_cmake}/bin/x86")
        list(APPEND _lvh_wdf_include_candidates ${_lvh_wdf_include_glob})
        list(APPEND _lvh_wdk_shared_include_candidates ${_lvh_wdk_shared_include_glob})
        list(APPEND _lvh_wdf_library_candidates ${_lvh_wdf_library_glob})
        list(APPEND _lvh_wdk_um_library_candidates ${_lvh_wdk_um_library_glob})
        list(APPEND _lvh_wdk_tool_candidates ${_lvh_wdk_tool_glob})
    endif()
endforeach()

if(_lvh_wdf_include_candidates)
    list(SORT _lvh_wdf_include_candidates COMPARE NATURAL ORDER DESCENDING)
    list(FILTER _lvh_wdf_include_candidates INCLUDE REGEX "/${_lvh_umdf_library_version_regex}$")
endif()
if(_lvh_wdk_shared_include_candidates)
    list(SORT _lvh_wdk_shared_include_candidates COMPARE NATURAL ORDER DESCENDING)
endif()
if(_lvh_wdf_library_candidates)
    list(SORT _lvh_wdf_library_candidates COMPARE NATURAL ORDER DESCENDING)
    list(FILTER _lvh_wdf_library_candidates INCLUDE REGEX "/${_lvh_umdf_library_version_regex}$")
endif()
if(_lvh_wdk_um_library_candidates)
    list(SORT _lvh_wdk_um_library_candidates COMPARE NATURAL ORDER DESCENDING)
endif()
if(_lvh_wdk_tool_candidates)
    list(SORT _lvh_wdk_tool_candidates COMPARE NATURAL ORDER DESCENDING)
    list(REMOVE_DUPLICATES _lvh_wdk_tool_candidates)
endif()

find_path(LIBVIRTUALHID_WDF_INCLUDE_DIR
          NAMES wdf.h
          PATHS ${_lvh_wdf_include_candidates}
          NO_DEFAULT_PATH)

find_path(LIBVIRTUALHID_VHF_INCLUDE_DIR
          NAMES vhf.h
          PATHS ${_lvh_wdk_shared_include_candidates}
          NO_DEFAULT_PATH)

find_library(LIBVIRTUALHID_WDF_DRIVER_STUB_UM_LIBRARY
             NAMES WdfDriverStubUm
             PATHS ${_lvh_wdf_library_candidates}
             NO_DEFAULT_PATH)

find_library(LIBVIRTUALHID_VHF_UM_LIBRARY
             NAMES VhfUm
             PATHS ${_lvh_wdk_um_library_candidates}
             NO_DEFAULT_PATH)

find_library(LIBVIRTUALHID_NTDLL_LIBRARY
             NAMES ntdll
             PATHS ${_lvh_wdk_um_library_candidates}
             NO_DEFAULT_PATH)

if(NOT LIBVIRTUALHID_WDF_INCLUDE_DIR
   OR NOT LIBVIRTUALHID_VHF_INCLUDE_DIR
   OR NOT LIBVIRTUALHID_WDF_DRIVER_STUB_UM_LIBRARY
   OR NOT LIBVIRTUALHID_VHF_UM_LIBRARY
   OR NOT LIBVIRTUALHID_NTDLL_LIBRARY)
    message(FATAL_ERROR
            "Could not find UMDF2 WDK headers/libraries. Install the Windows Driver Kit or set the "
            "LIBVIRTUALHID_WDF_INCLUDE_DIR, LIBVIRTUALHID_VHF_INCLUDE_DIR, "
            "LIBVIRTUALHID_WDF_DRIVER_STUB_UM_LIBRARY, LIBVIRTUALHID_VHF_UM_LIBRARY, and "
            "LIBVIRTUALHID_NTDLL_LIBRARY cache variables.")
endif()

message(STATUS "WDF include directory: ${LIBVIRTUALHID_WDF_INCLUDE_DIR}")
message(STATUS "VHF include directory: ${LIBVIRTUALHID_VHF_INCLUDE_DIR}")
message(STATUS "WDF UMDF stub library: ${LIBVIRTUALHID_WDF_DRIVER_STUB_UM_LIBRARY}")
message(STATUS "VHF UMDF library: ${LIBVIRTUALHID_VHF_UM_LIBRARY}")
message(STATUS "NTDLL import library: ${LIBVIRTUALHID_NTDLL_LIBRARY}")

get_filename_component(_lvh_wdf_include_version "${LIBVIRTUALHID_WDF_INCLUDE_DIR}" NAME)
get_filename_component(_lvh_wdf_stub_directory "${LIBVIRTUALHID_WDF_DRIVER_STUB_UM_LIBRARY}" DIRECTORY)
get_filename_component(_lvh_wdf_stub_version "${_lvh_wdf_stub_directory}" NAME)
if(NOT _lvh_wdf_include_version MATCHES "^2\\.[0-9]+$")
    message(FATAL_ERROR
            "Could not determine the UMDF library version from ${LIBVIRTUALHID_WDF_INCLUDE_DIR}.")
endif()
if(NOT _lvh_wdf_include_version STREQUAL LIBVIRTUALHID_UMDF_LIBRARY_VERSION)
    message(FATAL_ERROR
            "UMDF include version ${_lvh_wdf_include_version} does not match requested "
            "version ${LIBVIRTUALHID_UMDF_LIBRARY_VERSION}.")
endif()
if(NOT _lvh_wdf_stub_version STREQUAL LIBVIRTUALHID_UMDF_LIBRARY_VERSION)
    message(FATAL_ERROR
            "UMDF stub library version ${_lvh_wdf_stub_version} does not match requested "
            "version ${LIBVIRTUALHID_UMDF_LIBRARY_VERSION}.")
endif()
message(STATUS "UMDF library version: ${LIBVIRTUALHID_UMDF_LIBRARY_VERSION}")
find_program(LIBVIRTUALHID_STAMPINF
             NAMES stampinf stampinf.exe
             PATHS ${_lvh_wdk_tool_candidates})
find_program(LIBVIRTUALHID_INF2CAT
             NAMES inf2cat inf2cat.exe
             PATHS ${_lvh_wdk_tool_candidates})
if(NOT LIBVIRTUALHID_STAMPINF OR NOT LIBVIRTUALHID_INF2CAT)
    message(FATAL_ERROR
            "Could not find stampinf and inf2cat. Install the Windows Driver Kit before building the driver package.")
endif()
message(STATUS "stampinf executable: ${LIBVIRTUALHID_STAMPINF}")
message(STATUS "inf2cat executable: ${LIBVIRTUALHID_INF2CAT}")

configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/libvirtualhid.inf.in"
  "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid.inf"
  @ONLY)

add_library(libvirtualhid_umdf SHARED
  "${CMAKE_CURRENT_SOURCE_DIR}/libvirtualhid_umdf.cpp")

target_include_directories(libvirtualhid_umdf
  PRIVATE
  "${LIBVIRTUALHID_WDF_INCLUDE_DIR}"
  "${LIBVIRTUALHID_VHF_INCLUDE_DIR}"
  "${PROJECT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

target_compile_definitions(libvirtualhid_umdf
  PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  UMDF_USING_NTSTATUS
  NTDDI_VERSION=0x0A000000
  _WIN32_WINNT=0x0A00)

target_compile_options(libvirtualhid_umdf PRIVATE /W4)
if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
    target_compile_options(libvirtualhid_umdf PRIVATE /WX)
endif()
target_link_libraries(libvirtualhid_umdf
  PRIVATE
  "${LIBVIRTUALHID_WDF_DRIVER_STUB_UM_LIBRARY}"
  "${LIBVIRTUALHID_VHF_UM_LIBRARY}"
  "${LIBVIRTUALHID_NTDLL_LIBRARY}")
set_target_properties(libvirtualhid_umdf PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  OUTPUT_NAME libvirtualhid_umdf
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package")

add_custom_target(libvirtualhid_windows_inf
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "$<TARGET_FILE_DIR:libvirtualhid_umdf>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid.inf"
          "$<TARGET_FILE_DIR:libvirtualhid_umdf>/libvirtualhid.inf"
  COMMAND "${CMAKE_COMMAND}" -E chdir "$<TARGET_FILE_DIR:libvirtualhid_umdf>"
          "${LIBVIRTUALHID_STAMPINF}"
          -f libvirtualhid.inf
          -d "*"
          -v "${LIBVIRTUALHID_DRIVER_VERSION}"
  DEPENDS
  libvirtualhid_umdf
  "${CMAKE_CURRENT_BINARY_DIR}/libvirtualhid.inf"
  COMMENT "Preparing libvirtualhid driver INF"
  VERBATIM)

add_custom_target(libvirtualhid_windows_catalog
  COMMAND "${CMAKE_COMMAND}" -E chdir "$<TARGET_FILE_DIR:libvirtualhid_umdf>"
          "${LIBVIRTUALHID_INF2CAT}"
          /driver:.
          /os:${LIBVIRTUALHID_INF2CAT_OS}
  DEPENDS libvirtualhid_windows_inf
  COMMENT "Generating libvirtualhid driver catalog"
  VERBATIM)

if(LIBVIRTUALHID_INSTALL)
    install(TARGETS libvirtualhid_umdf
      RUNTIME DESTINATION "drivers/windows"
      COMPONENT driver)
    install(CODE
      "
      set(lvh_driver_dll \"$<TARGET_FILE:libvirtualhid_umdf>\")
      set(lvh_driver_inf \"$<TARGET_FILE_DIR:libvirtualhid_umdf>/libvirtualhid.inf\")
      set(lvh_driver_cat \"$<TARGET_FILE_DIR:libvirtualhid_umdf>/libvirtualhid.cat\")
      foreach(lvh_driver_file IN ITEMS \"\${lvh_driver_dll}\" \"\${lvh_driver_inf}\" \"\${lvh_driver_cat}\")
        if(NOT EXISTS \"\${lvh_driver_file}\")
          message(FATAL_ERROR \"Windows driver package file is missing: \${lvh_driver_file}\")
        endif()
      endforeach()
      if(\"\${lvh_driver_dll}\" IS_NEWER_THAN \"\${lvh_driver_cat}\"
         OR \"\${lvh_driver_inf}\" IS_NEWER_THAN \"\${lvh_driver_cat}\")
        message(FATAL_ERROR
                \"Windows driver catalog is stale; build libvirtualhid_windows_catalog \"
                \"and sign libvirtualhid.cat before packaging.\")
      endif()
      "
      COMPONENT driver)
    install(FILES
            "$<TARGET_FILE_DIR:libvirtualhid_umdf>/libvirtualhid.inf"
            "$<TARGET_FILE_DIR:libvirtualhid_umdf>/libvirtualhid.cat"
      DESTINATION "drivers/windows"
      COMPONENT driver)
endif()
