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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR WIN32)
    set(RUN_GAMEPAD_EXAMPLE_COMMAND "$<TARGET_FILE:gamepad_adapter>")
    set(RUN_GAMEPAD_EXAMPLE_COMMENT "Running gamepad_adapter example")
else()
    set(RUN_GAMEPAD_EXAMPLE_COMMAND
      "${CMAKE_COMMAND}" -E echo
      "Skipping gamepad_adapter example: no ${CMAKE_SYSTEM_NAME} backend")
    set(RUN_GAMEPAD_EXAMPLE_COMMENT "Skipping gamepad_adapter example")
endif()

add_custom_target(run_gamepad_adapter_example
  COMMAND ${RUN_GAMEPAD_EXAMPLE_COMMAND}
  COMMENT "${RUN_GAMEPAD_EXAMPLE_COMMENT}"
  DEPENDS gamepad_adapter
  VERBATIM
  USES_TERMINAL)
