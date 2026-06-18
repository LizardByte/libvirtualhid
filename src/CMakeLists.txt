add_library(${PROJECT_NAME} STATIC)
add_library(libvirtualhid::libvirtualhid ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME}
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/core/backend.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/profiles.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/report.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/runtime.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/types.cpp")

if(LIBVIRTUALHID_USES_THREADS)
    find_package(Threads REQUIRED)

    target_sources(${PROJECT_NAME}
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/linux/uhid_backend.cpp")
    target_link_libraries(${PROJECT_NAME}
      PUBLIC
      Threads::Threads)
else()
    target_sources(${PROJECT_NAME}
      PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/platform/unsupported_backend.cpp")
endif()

target_include_directories(${PROJECT_NAME}
  PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
set_target_properties(${PROJECT_NAME} PROPERTIES
  EXPORT_NAME libvirtualhid
  OUTPUT_NAME virtualhid)

if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS ${PROJECT_NAME}
  EXPORT libvirtualhid-targets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")

install(DIRECTORY "${PROJECT_SOURCE_DIR}/include/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

install(EXPORT libvirtualhid-targets
  NAMESPACE libvirtualhid::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/libvirtualhid")
