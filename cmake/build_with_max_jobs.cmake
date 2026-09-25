if(NOT DEFINED LIBVIRTUALHID_BUILD_DIRECTORY OR LIBVIRTUALHID_BUILD_DIRECTORY STREQUAL "")
    message(FATAL_ERROR "LIBVIRTUALHID_BUILD_DIRECTORY is required")
endif()

cmake_host_system_information(RESULT build_jobs QUERY NUMBER_OF_LOGICAL_CORES)
if(NOT build_jobs MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "Unable to determine the available logical processor count")
endif()

message(STATUS "Building with ${build_jobs} parallel jobs")
execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${LIBVIRTUALHID_BUILD_DIRECTORY}"
          --config "${LIBVIRTUALHID_BUILD_CONFIG}" --parallel "${build_jobs}"
  COMMAND_ERROR_IS_FATAL ANY)
