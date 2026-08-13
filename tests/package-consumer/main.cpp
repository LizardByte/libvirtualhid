#include <libvirtualhid/libvirtualhid.hpp>

int main() {
  return lvh::profiles::generic_gamepad().report_descriptor.empty() ? 1 : 0;
}
