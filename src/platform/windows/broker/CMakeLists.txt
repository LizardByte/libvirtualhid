if(NOT WIN32)
    message(FATAL_ERROR "The libvirtualhid Windows broker can only be built on Windows.")
endif()

include("${PROJECT_SOURCE_DIR}/cmake/cpm/CPM.cmake")
CPMUsePackageLock("${PROJECT_SOURCE_DIR}/package-lock.cmake")
CPMGetPackage(nlohmann_json)

add_executable(libvirtualhid_broker
  "${CMAKE_CURRENT_SOURCE_DIR}/libvirtualhid_broker.cpp")

target_compile_features(libvirtualhid_broker PRIVATE cxx_std_23)
target_include_directories(libvirtualhid_broker
  PRIVATE
  "${PROJECT_SOURCE_DIR}/src/platform/windows/shared")
target_compile_definitions(libvirtualhid_broker
  PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  _WIN32_WINNT=0x0A00)
target_link_libraries(libvirtualhid_broker
  PRIVATE
  lizardbyte::common
  nlohmann_json::nlohmann_json
  advapi32
  crypt32
  winhttp)

if(MSVC AND LIBVIRTUALHID_BUILD_WINDOWS_DRIVER)
    set_property(TARGET libvirtualhid_broker PROPERTY
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(MSVC)
    # nlohmann/json uses C++ exceptions while serializing Polar request bodies.
    target_compile_options(libvirtualhid_broker PRIVATE /EHsc /W4)
    if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
        target_compile_options(libvirtualhid_broker PRIVATE /WX)
    endif()
else()
    target_compile_options(libvirtualhid_broker PRIVATE -Wall -Wextra -Wpedantic)
    if(LIBVIRTUALHID_WARNINGS_AS_ERRORS)
        target_compile_options(libvirtualhid_broker PRIVATE -Werror)
    endif()
endif()

if(LIBVIRTUALHID_INSTALL)
    install(TARGETS libvirtualhid_broker
      RUNTIME DESTINATION "services/windows"
      COMPONENT driver)
endif()
