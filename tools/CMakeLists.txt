add_library(virtualhid_control_model STATIC
  "${CMAKE_CURRENT_SOURCE_DIR}/virtualhid_control_model.cpp")

target_include_directories(virtualhid_control_model
  PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(virtualhid_control_model
  PUBLIC
  libvirtualhid::libvirtualhid)

if(MSVC AND LIBVIRTUALHID_TOOLS_STATIC_RUNTIME AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/cpm/CPM.cmake")
CPMUsePackageLock("${PROJECT_SOURCE_DIR}/package-lock.cmake")

CPMGetPackage(SDL3)
CPMGetPackage(imgui)

set(LIBVIRTUALHID_TOOLS_SDL3_TARGET SDL3::SDL3)
if(LIBVIRTUALHID_TOOLS_STATIC_SDL3 AND TARGET SDL3::SDL3-static)
    set(LIBVIRTUALHID_TOOLS_SDL3_TARGET SDL3::SDL3-static)
endif()

add_library(virtualhid_imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp")

target_include_directories(virtualhid_imgui
  SYSTEM
  PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/backends")

target_link_libraries(virtualhid_imgui
  PUBLIC
  ${LIBVIRTUALHID_TOOLS_SDL3_TARGET})

if(MSVC AND LIBVIRTUALHID_TOOLS_STATIC_RUNTIME AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
    set_property(TARGET virtualhid_imgui PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_executable(virtualhid_control
  "${CMAKE_CURRENT_SOURCE_DIR}/virtualhid_control.cpp")

target_link_libraries(virtualhid_control
  PRIVATE
  virtualhid_control_model
  virtualhid_imgui)

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
endif()

if(MSVC AND LIBVIRTUALHID_TOOLS_STATIC_RUNTIME AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
    set_property(TARGET virtualhid_control virtualhid_control_model PROPERTY
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
