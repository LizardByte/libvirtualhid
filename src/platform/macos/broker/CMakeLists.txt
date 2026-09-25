if(NOT APPLE)
    message(FATAL_ERROR "The macOS broker can only be built on macOS.")
endif()

set(LIBVIRTUALHID_MACOS_BUNDLE_VERSION "0.0.0")
if(PROJECT_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(LIBVIRTUALHID_MACOS_BUNDLE_VERSION
      "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
endif()
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)

add_executable(libvirtualhid_macos_broker MACOSX_BUNDLE
  "${CMAKE_CURRENT_SOURCE_DIR}/libvirtualhid_macos_broker.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/license_manager.mm"
  "${PROJECT_SOURCE_DIR}/assets/macos/libvirtualhid.icns")
set_source_files_properties("${PROJECT_SOURCE_DIR}/assets/macos/libvirtualhid.icns"
  PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
target_compile_features(libvirtualhid_macos_broker PRIVATE cxx_std_23)
set_target_properties(libvirtualhid_macos_broker PROPERTIES
  OUTPUT_NAME VirtualHIDBroker
  OBJCXX_STANDARD 23
  OBJCXX_STANDARD_REQUIRED ON
  OBJCXX_EXTENSIONS OFF
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist")
target_include_directories(libvirtualhid_macos_broker PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/src/include")
target_compile_options(libvirtualhid_macos_broker PRIVATE
  -Wall -Wextra -Wpedantic -fblocks)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Apple's libc++ keeps std::jthread behind this switch on some Xcode versions.
    target_compile_options(libvirtualhid_macos_broker PRIVATE -fexperimental-library)
    target_link_options(libvirtualhid_macos_broker PRIVATE -fexperimental-library)
endif()
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/license_manager.mm"
  PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
    target_compile_options(libvirtualhid_macos_broker PRIVATE -Werror)
endif()
target_link_libraries(libvirtualhid_macos_broker PRIVATE
  "-framework CoreFoundation"
  "-framework Foundation"
  "-framework IOKit")

add_executable(libvirtualhid_macos_license
  "${CMAKE_CURRENT_SOURCE_DIR}/license_cli.cpp")
set_target_properties(libvirtualhid_macos_license PROPERTIES
  OUTPUT_NAME libvirtualhid-license)
target_compile_features(libvirtualhid_macos_license PRIVATE cxx_std_23)
target_compile_options(libvirtualhid_macos_license PRIVATE -Wall -Wextra -Wpedantic)
if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
    target_compile_options(libvirtualhid_macos_license PRIVATE -Werror)
endif()
target_link_libraries(libvirtualhid_macos_license PRIVATE libvirtualhid::libvirtualhid)

if(LIBVIRTUALHID_INSTALL)
    install(TARGETS libvirtualhid_macos_broker
      BUNDLE DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/libvirtualhid"
      COMPONENT broker)
    install(TARGETS libvirtualhid_macos_license
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
      COMPONENT broker)
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSES/LicenseRef-LizardByte-SAL-1.0.md"
      DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/libvirtualhid"
      COMPONENT broker)
endif()
