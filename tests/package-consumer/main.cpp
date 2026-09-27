/**
 * @file tests/package-consumer/main.cpp
 * @brief Smoke test that the installed package exposes built-in profiles.
 */

#include <libvirtualhid/libvirtualhid.hpp>

int main() {
  return lvh::profiles::generic_gamepad().report_descriptor.empty() ? 1 : 0;
}
