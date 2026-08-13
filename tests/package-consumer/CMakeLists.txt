cmake_minimum_required(VERSION 3.24)
project(libvirtualhid_package_consumer LANGUAGES CXX)

find_package(libvirtualhid CONFIG REQUIRED)

add_executable(libvirtualhid_package_consumer main.cpp)
target_link_libraries(libvirtualhid_package_consumer
  PRIVATE
  libvirtualhid::libvirtualhid)
