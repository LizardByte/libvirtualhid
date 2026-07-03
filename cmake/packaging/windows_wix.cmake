# WIX Packaging
# see options at: https://cmake.org/cmake/help/latest/cpack_gen/wix.html

find_program(DOTNET_EXECUTABLE dotnet HINTS "C:/Program Files/dotnet")

if(NOT DOTNET_EXECUTABLE)
    message(WARNING "Dotnet executable not found, skipping WiX packaging.")
    return()
endif()

set(CPACK_WIX_VERSION 4)
set(CPACK_GENERATOR "WIX")
set(WIX_VERSION 4.0.4)
set(WIX_UI_VERSION 4.0.4)  # extension versioning is independent of the WiX version
set(WIX_BUILD_PARENT_DIRECTORY "${CMAKE_BINARY_DIR}/wix_packaging")
set(WIX_BUILD_DIRECTORY "${CPACK_PACKAGE_DIRECTORY}/_CPack_Packages/win64/WIX")

set(WIX_TOOL_PATH "${CMAKE_BINARY_DIR}/.wix")
file(MAKE_DIRECTORY ${WIX_TOOL_PATH})

execute_process(
        COMMAND ${DOTNET_EXECUTABLE} tool install --tool-path ${WIX_TOOL_PATH} wix --version ${WIX_VERSION}
        ERROR_VARIABLE WIX_INSTALL_OUTPUT
        RESULT_VARIABLE WIX_INSTALL_RESULT)

if(NOT WIX_INSTALL_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install WiX tools locally: ${WIX_INSTALL_OUTPUT}")
endif()

# Ensure a WiX extension is installed in the local tool cache.
function(libvirtualhid_wix_ensure_extension extension_name extension_version)
    execute_process(
            COMMAND "${WIX_TOOL_PATH}/wix" extension list --global
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            OUTPUT_VARIABLE WIX_EXTENSION_LIST_OUTPUT
            ERROR_VARIABLE WIX_EXTENSION_LIST_ERROR
            RESULT_VARIABLE WIX_EXTENSION_LIST_RESULT)

    if(WIX_EXTENSION_LIST_RESULT EQUAL 0
       AND WIX_EXTENSION_LIST_OUTPUT MATCHES "${extension_name}[ \t]+${extension_version}")
        return()
    endif()

    execute_process(
            COMMAND "${WIX_TOOL_PATH}/wix" extension add --global "${extension_name}/${extension_version}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            ERROR_VARIABLE WIX_EXTENSION_INSTALL_OUTPUT
            RESULT_VARIABLE WIX_EXTENSION_INSTALL_RESULT)

    if(NOT WIX_EXTENSION_INSTALL_RESULT EQUAL 0)
        message(FATAL_ERROR
                "Failed to install WiX extension ${extension_name}/${extension_version}: "
                "${WIX_EXTENSION_INSTALL_OUTPUT}${WIX_EXTENSION_LIST_ERROR}")
    endif()
endfunction()

libvirtualhid_wix_ensure_extension(WixToolset.UI.wixext ${WIX_UI_VERSION})

set(CPACK_WIX_ROOT "${WIX_TOOL_PATH}")
set(CPACK_WIX_UPGRADE_GUID "71D7B738-9D83-4E57-82E3-C3106D9F8053")
set(CPACK_WIX_HELP_LINK "https://app.lizardbyte.dev/support")
set(CPACK_WIX_PRODUCT_URL "${CMAKE_PROJECT_HOMEPAGE_URL}")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "LizardByte")
set(CPACK_WIX_EXTENSIONS
        "WixToolset.UI.wixext")

set(CPACK_WIX_PATCH_FILE
        "${CMAKE_CURRENT_LIST_DIR}/wix_resources/libvirtualhid-driver-installer-patch.xml")

set(LIBVIRTUALHID_DRIVER_CPACK_LICENSE_FILE
    "${CMAKE_BINARY_DIR}/LicenseRef-LizardByte-SAL-1.0.txt")
file(COPY_FILE
     "${LIBVIRTUALHID_DRIVER_LICENSE_FILE}"
     "${LIBVIRTUALHID_DRIVER_CPACK_LICENSE_FILE}"
     ONLY_IF_DIFFERENT)
set(CPACK_RESOURCE_FILE_LICENSE "${LIBVIRTUALHID_DRIVER_CPACK_LICENSE_FILE}")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64")
    set(CPACK_WIX_ARCHITECTURE "arm64")
else()
    set(CPACK_WIX_ARCHITECTURE "x64")
endif()
