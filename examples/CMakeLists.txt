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

libvirtualhid_copy_mingw_runtime(gamepad_adapter)
libvirtualhid_copy_mingw_runtime(keyboard_mouse_adapter)
