add_executable(gamepad_adapter
  "${CMAKE_CURRENT_SOURCE_DIR}/gamepad_adapter.cpp")

add_executable(keyboard_mouse_adapter
  "${CMAKE_CURRENT_SOURCE_DIR}/keyboard_mouse_adapter.cpp")

target_link_libraries(gamepad_adapter
  PRIVATE
  libvirtualhid::libvirtualhid)

target_link_libraries(keyboard_mouse_adapter
  PRIVATE
  libvirtualhid::libvirtualhid)

if(MSVC AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
    set_target_properties(gamepad_adapter keyboard_mouse_adapter PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

libvirtualhid_copy_mingw_runtime(gamepad_adapter)
libvirtualhid_copy_mingw_runtime(keyboard_mouse_adapter)
