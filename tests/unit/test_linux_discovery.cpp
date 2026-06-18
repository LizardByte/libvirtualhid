/**
 * @file tests/unit/test_linux_discovery.cpp
 * @brief Linux integration tests for external discovery of virtual devices.
 */

// standard includes
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

// platform includes
#if defined(__linux__)
  #include <unistd.h>
#endif

// local includes
#include "fixtures/fixtures.hpp"

#include <libvirtualhid/libvirtualhid.hpp>

namespace {

#if defined(__linux__)
  struct DiscoveryProbe {
    std::string name;
    std::string executable;
    std::string command;
  };

  struct CommandResult {
    int exit_code = 1;
    std::string output;
  };

  std::vector<DiscoveryProbe> discovery_probes() {
    return {
      {"SDL2 joystick list", "sdl2-jstest", "sdl2-jstest --list"},
      {"HIDAPI device list", "hidapitester", "hidapitester --list"},
    };
  }

  bool command_available(std::string_view executable) {
    const auto command = "command -v " + std::string {executable} + " >/dev/null 2>&1";
    return std::system(command.c_str()) == 0;
  }

  std::string read_file(const std::filesystem::path &path) {
    std::ifstream file {path};
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
  }

  CommandResult run_command(const DiscoveryProbe &probe, std::size_t index) {
    const auto output_path = std::filesystem::temp_directory_path() /
                             ("libvirtualhid-discovery-test-" + std::to_string(::getpid()) + "-" +
                              std::to_string(index) + ".txt");
    const auto command = probe.command + " > \"" + output_path.string() + "\" 2>&1";
    CommandResult result;
    result.exit_code = std::system(command.c_str());
    result.output = read_file(output_path);
    std::error_code error;
    std::filesystem::remove(output_path, error);
    return result;
  }

  std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    return value;
  }

  std::string hex4(std::uint16_t value) {
    std::ostringstream stream;
    stream << std::hex << std::nouppercase << std::setw(4) << std::setfill('0') << value;
    return stream.str();
  }

  bool output_matches_profile(std::string_view output, const lvh::DeviceProfile &profile) {
    const auto lower_output = lowercase(std::string {output});
    const auto lower_name = lowercase(profile.name);
    const auto vendor = hex4(profile.vendor_id);
    const auto product = hex4(profile.product_id);

    return lower_output.find(lower_name) != std::string::npos ||
           lower_output.find(vendor + ":" + product) != std::string::npos ||
           lower_output.find(vendor + "/" + product) != std::string::npos;
  }

  bool wait_for_probe(const DiscoveryProbe &probe, const lvh::DeviceProfile &profile, std::size_t index) {
    CommandResult result;
    for (auto attempt = 0; attempt < 12; ++attempt) {
      result = run_command(probe, index);
      if (result.exit_code == 0 && output_matches_profile(result.output, profile)) {
        std::cout << probe.name << " discovered " << profile.name << '\n';
        return true;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds {250});
    }

    std::cout << probe.name << " output:" << '\n'
              << result.output << '\n';
    return false;
  }
#endif

}  // namespace

TEST(LinuxDiscoveryTest, SdlOrHidapiCanDiscoverUhidGamepadWhenAvailable) {
#if defined(__linux__)
  const auto *enabled = std::getenv("LIBVIRTUALHID_ENABLE_DISCOVERY_INTEGRATION_TESTS");
  if (enabled == nullptr || std::string_view {enabled} != "1") {
    GTEST_SKIP() << "set LIBVIRTUALHID_ENABLE_DISCOVERY_INTEGRATION_TESTS=1 to validate SDL/HIDAPI discovery";
  }

  lvh::RuntimeOptions runtime_options;
  runtime_options.backend = lvh::BackendKind::platform_default;
  auto runtime = lvh::Runtime::create(runtime_options);
  if (!runtime->capabilities().supports_gamepad) {
    GTEST_SKIP() << "/dev/uhid is not accessible";
  }

  lvh::CreateGamepadOptions options;
  options.profile = lvh::profiles::generic_gamepad();
  options.metadata.stable_id = "libvirtualhid-discovery-test";

  auto created = runtime->create_gamepad(options);
  ASSERT_TRUE(created) << created.status.message();

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::a);
  state.left_stick = {0.25F, -0.25F};
  ASSERT_TRUE(created.gamepad->submit(state).ok());

  std::size_t available_probe_count = 0;
  const auto probes = discovery_probes();
  for (std::size_t index = 0; index < probes.size(); ++index) {
    const auto &probe = probes[index];
    if (!command_available(probe.executable)) {
      std::cout << probe.executable << " is not installed; skipping " << probe.name << '\n';
      continue;
    }

    ++available_probe_count;
    EXPECT_TRUE(wait_for_probe(probe, options.profile, index));
  }

  if (available_probe_count == 0) {
    GTEST_SKIP() << "install sdl2-jstest or hidapitester to validate external discovery";
  }
#else
  GTEST_SKIP() << "SDL/HIDAPI discovery validation requires Linux UHID";
#endif
}
