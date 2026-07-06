add_library(${PROJECT_NAME} STATIC)
add_library(libvirtualhid::libvirtualhid ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/core/backend.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/gamepad_adapter.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/profiles.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/report.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/runtime.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/types.cpp")

if(LIBVIRTUALHID_USES_THREADS)
    find_package(Threads REQUIRED)
    if(LIBEVDEV_CUSTOM_INCLUDE_DIR AND LIBEVDEV_CUSTOM_LIBRARY)
        add_library(PkgConfig::LIBEVDEV UNKNOWN IMPORTED)
        set_target_properties(PkgConfig::LIBEVDEV PROPERTIES
          IMPORTED_LOCATION "${LIBEVDEV_CUSTOM_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${LIBEVDEV_CUSTOM_INCLUDE_DIR}")
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
    endif()
    if(LIBVIRTUALHID_ENABLE_XTEST)
        find_package(X11 QUIET)
    endif()

    target_sources(${PROJECT_NAME}
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/uhid_backend.cpp")
    target_link_libraries(${PROJECT_NAME}
      PUBLIC
      Threads::Threads
      PRIVATE
      PkgConfig::LIBEVDEV)
    if(LIBVIRTUALHID_ENABLE_XTEST AND X11_FOUND AND X11_XTest_FOUND)
        target_compile_definitions(${PROJECT_NAME}
          PRIVATE
          LIBVIRTUALHID_HAVE_XTEST=1)
        target_include_directories(${PROJECT_NAME}
          PRIVATE
          ${X11_INCLUDE_DIR}
          ${X11_XTest_INCLUDE_PATH})
        target_link_libraries(${PROJECT_NAME}
          PRIVATE
          ${X11_LIBRARIES}
          ${X11_XTest_LIB})
    endif()
elseif(WIN32)
    target_sources(${PROJECT_NAME}
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/windows_backend.cpp")
    target_include_directories(${PROJECT_NAME}
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/windows/shared")
    target_compile_definitions(${PROJECT_NAME}
      PRIVATE
      NOMINMAX
      WIN32_LEAN_AND_MEAN
      _WIN32_WINNT=0x0600)
    target_link_libraries(${PROJECT_NAME}
      PRIVATE
      setupapi)
else()
    target_sources(${PROJECT_NAME}
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/unsupported_backend.cpp")
endif()

target_include_directories(${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)
set_target_properties(${PROJECT_NAME} PROPERTIES
  EXPORT_NAME libvirtualhid
  OUTPUT_NAME virtualhid)
if(MSVC AND (LIBVIRTUALHID_BUILD_WINDOWS_DRIVER OR (LIBVIRTUALHID_BUILD_TOOLS AND LIBVIRTUALHID_TOOLS_STATIC_RUNTIME)))
    set_property(TARGET ${PROJECT_NAME} PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
    if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
        target_compile_options(${PROJECT_NAME} PRIVATE /WX)
    endif()
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
        target_compile_options(${PROJECT_NAME} PRIVATE -Werror)
    endif()
endif()

if(LIBVIRTUALHID_INSTALL)
    install(TARGETS ${PROJECT_NAME}
      EXPORT libvirtualhid-targets
      ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
      RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

    install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/include/"
      DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    install(EXPORT libvirtualhid-targets
      NAMESPACE libvirtualhid::
      DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libvirtualhid")
endif()
