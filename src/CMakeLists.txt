# Apply the same language, runtime, and warning requirements to both library copies.
function(libvirtualhid_configure_compiler target_name)
    target_compile_features(${target_name} PUBLIC cxx_std_23)
    if((APPLE OR CMAKE_SYSTEM_NAME STREQUAL "FreeBSD") AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        # Apple's and FreeBSD's libc++ keep std::jthread behind this switch on
        # toolchains used by CI. Consumers need the matching link option too.
        target_compile_options(${target_name} PUBLIC -fexperimental-library)
        target_link_options(${target_name} PUBLIC -fexperimental-library)
    endif()
    if(MSVC AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
        set_property(TARGET ${target_name} PROPERTY
          MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
        if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Configure production and test copies from the same sources and backend requirements.
function(libvirtualhid_add_library target_name)
    add_library(${target_name} STATIC)

    target_sources(${target_name}
      PRIVATE
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/core/backend.cpp"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/core/gamepad_adapter.cpp"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/core/profiles.cpp"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/core/report.cpp"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/core/runtime.cpp"
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/core/types.cpp")

    if(LIBVIRTUALHID_USES_THREADS)
        find_package(Threads REQUIRED)
        if(LIBEVDEV_CUSTOM_INCLUDE_DIR AND LIBEVDEV_CUSTOM_LIBRARY)
            if(NOT TARGET PkgConfig::LIBEVDEV)
                add_library(PkgConfig::LIBEVDEV UNKNOWN IMPORTED)
                set_target_properties(PkgConfig::LIBEVDEV PROPERTIES
                  IMPORTED_LOCATION "${LIBEVDEV_CUSTOM_LIBRARY}"
                  INTERFACE_INCLUDE_DIRECTORIES "${LIBEVDEV_CUSTOM_INCLUDE_DIR}")
            endif()
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(LIBEVDEV REQUIRED IMPORTED_TARGET libevdev)
        endif()
        if(LIBVIRTUALHID_ENABLE_XTEST)
            find_package(X11 QUIET COMPONENTS Xtst)
        endif()

        target_sources(${target_name}
          PRIVATE
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/linux/uhid_backend.cpp")
        target_link_libraries(${target_name}
          PUBLIC
          Threads::Threads
          PRIVATE
          PkgConfig::LIBEVDEV)
        if(LIBVIRTUALHID_ENABLE_XTEST AND X11_FOUND AND X11_XTest_FOUND)
            set(LIBVIRTUALHID_USES_XTEST ON PARENT_SCOPE)
            target_compile_definitions(${target_name}
              PRIVATE
              LIBVIRTUALHID_HAVE_XTEST=1)
            target_link_libraries(${target_name}
              PRIVATE
              X11::X11
              X11::Xtst)
        endif()
    elseif(WIN32)
        target_sources(${target_name}
          PRIVATE
          "${PROJECT_SOURCE_DIR}/third-party/lizardbyte-common/src/common/env.cpp"
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/windows/windows_backend.cpp"
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/windows/windows_broker_client.cpp"
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/windows/windows_license.cpp")
        target_include_directories(${target_name}
          PRIVATE
          "${PROJECT_SOURCE_DIR}/third-party/lizardbyte-common/src/include"
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/windows/shared")
        target_compile_definitions(${target_name}
          PRIVATE
          NOMINMAX
          WIN32_LEAN_AND_MEAN
          _WIN32_WINNT=0x0600)
        target_link_libraries(${target_name}
          PRIVATE
          advapi32
          setupapi)
    elseif(APPLE)
        target_sources(${target_name}
          PRIVATE
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/macos/macos_broker_client.cpp"
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/macos/macos_backend.cpp")
        target_link_libraries(${target_name}
          PRIVATE
          "-framework ApplicationServices"
          "-framework Carbon"
          "-framework CoreFoundation"
          "-framework IOKit")
    else()
        target_sources(${target_name}
          PRIVATE
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/license_unavailable.cpp"
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/unsupported_backend.cpp")
    endif()

    if(LIBVIRTUALHID_USES_THREADS)
        target_sources(${target_name}
          PRIVATE
          "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/platform/license_unavailable.cpp")
    endif()

    target_include_directories(${target_name}
      PUBLIC
      $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
      PRIVATE
      "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")

    libvirtualhid_configure_compiler(${target_name})
endfunction()

libvirtualhid_add_library(${PROJECT_NAME})
add_library(libvirtualhid::libvirtualhid ALIAS ${PROJECT_NAME})
set_target_properties(${PROJECT_NAME} PROPERTIES
  EXPORT_NAME libvirtualhid
  OUTPUT_NAME virtualhid)
set(LIBVIRTUALHID_USES_XTEST ${LIBVIRTUALHID_USES_XTEST} PARENT_SCOPE)

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
