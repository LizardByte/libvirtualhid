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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(linux_discovery_probe
      "${CMAKE_CURRENT_SOURCE_DIR}/linux_discovery_probe.cpp")

    target_link_libraries(linux_discovery_probe
      PRIVATE
      libvirtualhid::libvirtualhid)
endif()
