add_executable(sunshine_gamepad_adapter
  "${CMAKE_CURRENT_SOURCE_DIR}/sunshine_gamepad_adapter.cpp")

add_executable(sunshine_keyboard_mouse_adapter
  "${CMAKE_CURRENT_SOURCE_DIR}/sunshine_keyboard_mouse_adapter.cpp")

target_link_libraries(sunshine_gamepad_adapter
  PRIVATE
  libvirtualhid::libvirtualhid)

target_link_libraries(sunshine_keyboard_mouse_adapter
  PRIVATE
  libvirtualhid::libvirtualhid)

libvirtualhid_copy_mingw_runtime(sunshine_gamepad_adapter)
libvirtualhid_copy_mingw_runtime(sunshine_keyboard_mouse_adapter)
