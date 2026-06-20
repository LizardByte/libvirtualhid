# WIX Packaging
# see options at: https://cmake.org/cmake/help/latest/cpack_gen/wix.html

find_program(DOTNET_EXECUTABLE dotnet HINTS "C:/Program Files/dotnet")

if(NOT DOTNET_EXECUTABLE)
    message(WARNING "Dotnet executable not found, skipping WiX packaging.")
    return()
endif()

set(CPACK_WIX_VERSION 4)
set(WIX_VERSION 4.0.4)
set(WIX_UI_VERSION 4.0.4)
set(WIX_BUILD_PARENT_DIRECTORY "${CMAKE_BINARY_DIR}/wix_packaging")

set(WIX_TOOL_PATH "${CMAKE_BINARY_DIR}/.wix")
file(MAKE_DIRECTORY ${WIX_TOOL_PATH})

if(NOT EXISTS "${WIX_TOOL_PATH}/wix.exe")
    execute_process(
            COMMAND ${DOTNET_EXECUTABLE} tool install --tool-path ${WIX_TOOL_PATH} wix --version ${WIX_VERSION}
            ERROR_VARIABLE WIX_INSTALL_OUTPUT
            RESULT_VARIABLE WIX_INSTALL_RESULT)

    if(NOT WIX_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to install WiX tools locally: ${WIX_INSTALL_OUTPUT}")
    endif()
endif()

execute_process(
        COMMAND "${WIX_TOOL_PATH}/wix" extension add WixToolset.UI.wixext/${WIX_UI_VERSION}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        ERROR_VARIABLE WIX_UI_INSTALL_OUTPUT
        RESULT_VARIABLE WIX_UI_INSTALL_RESULT)

if(NOT WIX_UI_INSTALL_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install WiX UI extension: ${WIX_UI_INSTALL_OUTPUT}")
endif()

set(CPACK_WIX_ROOT "${WIX_TOOL_PATH}")
set(CPACK_WIX_UPGRADE_GUID "71D7B738-9D83-4E57-82E3-C3106D9F8053")
set(CPACK_WIX_HELP_LINK "https://app.lizardbyte.dev/support")
set(CPACK_WIX_PRODUCT_URL "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "LizardByte")
set(CPACK_WIX_EXTENSIONS "WixToolset.UI.wixext")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/wix_resources/"
        DESTINATION "${WIX_BUILD_PARENT_DIRECTORY}/")

set(CPACK_WIX_EXTRA_SOURCES
        "${WIX_BUILD_PARENT_DIRECTORY}/libvirtualhid-driver-installer.wxs")
set(CPACK_WIX_PATCH_FILE
        "${WIX_BUILD_PARENT_DIRECTORY}/patch.xml")

file(COPY "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_BINARY_DIR}")
file(RENAME "${CMAKE_BINARY_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/LICENSE.txt")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/LICENSE.txt")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
    set(CPACK_WIX_ARCHITECTURE "arm64")
else()
    set(CPACK_WIX_ARCHITECTURE "x64")
endif()
