add_executable(virtualhid_control
  "${CMAKE_CURRENT_SOURCE_DIR}/virtualhid_control.cpp")

target_link_libraries(virtualhid_control
  PRIVATE
  libvirtualhid::libvirtualhid)

if(WIN32)
    set_target_properties(virtualhid_control PROPERTIES
      WIN32_EXECUTABLE TRUE)
    target_sources(virtualhid_control
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/windows/virtualhid_control.rc")
    target_include_directories(virtualhid_control
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/windows"
      "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(virtualhid_control
      PRIVATE
      NOMINMAX
      WIN32_LEAN_AND_MEAN
      _WIN32_WINNT=0x0600)
    target_link_libraries(virtualhid_control
      PRIVATE
      comctl32
      gdi32
      user32)
endif()

if(MSVC AND LIBVIRTUALHID_TOOLS_STATIC_RUNTIME AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
    set_property(TARGET virtualhid_control PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND LIBVIRTUALHID_TOOLS_STATIC_RUNTIME)
    target_link_options(virtualhid_control
      PRIVATE
      -static-libgcc
      -static-libstdc++)

    if(WIN32)
        target_link_options(virtualhid_control
          PRIVATE
          -static)
    endif()
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND LIBVIRTUALHID_TOOLS_FULLY_STATIC)
    target_link_options(virtualhid_control
      PRIVATE
      -static)
endif()

install(TARGETS virtualhid_control
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
