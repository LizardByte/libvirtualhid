// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/macos/broker/license_cli.cpp
 * @brief macOS command-line interface for machine license management.
 */

#include <algorithm>
#include <iostream>
#include <libvirtualhid/license.hpp>
#include <string>
#include <string_view>
#include <termios.h>
#include <unistd.h>

namespace {

  void print_status(const lvh::LicenseResult &result) {
    const auto &license = result.license;
    std::cout << license.message << '\n';
    if (!license.plan_name.empty()) {
      std::cout << "Plan: " << license.plan_name << '\n';
    }
    if (!license.customer_email.empty()) {
      std::cout << "Customer: " << license.customer_email << '\n';
    }
    std::cout << "Active gamepads: " << license.active_devices << '\n';
    if (!license.purchase_url.empty()) {
      std::cout << "Purchase: " << license.purchase_url << '\n';
    }
    if (!license.manage_account_url.empty()) {
      std::cout << "Manage: " << license.manage_account_url << '\n';
    }
    if (!result.status.ok() && license.message != result.status.message()) {
      std::cerr << result.status.message() << '\n';
    }
  }

  std::string read_key() {
    termios previous {};
    const bool terminal = ::isatty(STDIN_FILENO) && ::tcgetattr(STDIN_FILENO, &previous) == 0;
    if (terminal) {
      auto no_echo = previous;
      no_echo.c_lflag &= static_cast<tcflag_t>(~ECHO);
      static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &no_echo));
      std::cout << "License key: " << std::flush;
    }
    std::string key;
    std::getline(std::cin, key);
    if (terminal) {
      static_cast<void>(::tcsetattr(STDIN_FILENO, TCSAFLUSH, &previous));
      std::cout << '\n';
    }
    return key;
  }

}  // namespace

int main(int argc, char **argv) {
  if (argc > 3) {
    std::cerr << "Usage: libvirtualhid-license [status|activate [machine-name]|validate|deactivate]\n";
    return 2;
  }
  const auto action = argc > 1 ? std::string_view {argv[1]} : std::string_view {"status"};
  lvh::LicenseResult result;
  if (action == "status" && argc <= 2) {
    result = lvh::get_license_status();
  } else if (action == "activate") {
    auto key = read_key();
    result = lvh::activate_license(key, argc == 3 ? argv[2] : "");
    std::fill(key.begin(), key.end(), '\0');
  } else if (action == "validate" && argc == 2) {
    result = lvh::validate_license();
  } else if (action == "deactivate" && argc == 2) {
    result = lvh::deactivate_license();
  } else {
    std::cerr << "Usage: libvirtualhid-license [status|activate [machine-name]|validate|deactivate]\n";
    return 2;
  }
  print_status(result);
  return result.status.ok() ? 0 : 1;
}
