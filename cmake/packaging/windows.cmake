# windows specific packaging
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CPACK_PACKAGE_NAME}")

if(NOT LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
    set(CPACK_MONOLITHIC_INSTALL ON)
    return()
endif()

set(CPACK_COMPONENTS_ALL driver)
set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${CMAKE_PROJECT_NAME};driver;/")

set(LIBVIRTUALHID_DRIVER_TEST_CERTIFICATE "" CACHE FILEPATH
    "Optional public test certificate to include in the Windows driver installer.")

install(FILES
        "${PROJECT_SOURCE_DIR}/scripts/windows/install-driver.ps1"
        "${PROJECT_SOURCE_DIR}/scripts/windows/uninstall-driver.ps1"
  DESTINATION "scripts/windows"
  COMPONENT driver)

if(LIBVIRTUALHID_DRIVER_TEST_CERTIFICATE)
    install(FILES "${LIBVIRTUALHID_DRIVER_TEST_CERTIFICATE}"
      DESTINATION "certificates"
      RENAME "libvirtualhid-ci-test.cer"
      COMPONENT driver
      OPTIONAL)
endif()

set(CPACK_COMPONENT_DRIVER_DISPLAY_NAME "Windows UMDF Driver")
set(CPACK_COMPONENT_DRIVER_DESCRIPTION "libvirtualhid Windows UMDF virtual HID driver package.")
set(CPACK_COMPONENT_DRIVER_REQUIRED true)

include("${CMAKE_CURRENT_LIST_DIR}/windows_wix.cmake")
