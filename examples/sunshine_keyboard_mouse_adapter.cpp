/**
 * @file examples/sunshine_keyboard_mouse_adapter.cpp
 * @brief Minimal Sunshine-style keyboard and mouse input example.
 */

// standard includes
#include <iostream>

// local includes
#include <libvirtualhid/libvirtualhid.hpp>

int main() {
  auto runtime = lvh::Runtime::create();

  auto keyboard = runtime->create_keyboard();
  if (!keyboard) {
    std::cerr << keyboard.status.message() << '\n';
    return 1;
  }

  auto mouse = runtime->create_mouse();
  if (!mouse) {
    std::cerr << mouse.status.message() << '\n';
    return 1;
  }

  if (const auto status = keyboard.keyboard->press(0x41); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  if (const auto status = keyboard.keyboard->release(0x41); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  if (const auto status = keyboard.keyboard->type_text({.text = "Hi"}); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }

  if (const auto status = mouse.mouse->move_relative(25, -10); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  if (const auto status = mouse.mouse->move_absolute(960, 540, 1920, 1080); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  if (const auto status = mouse.mouse->button(lvh::MouseButton::left, true); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  if (const auto status = mouse.mouse->button(lvh::MouseButton::left, false); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  if (const auto status = mouse.mouse->vertical_scroll(120); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }
  if (const auto status = mouse.mouse->horizontal_scroll(-120); !status.ok()) {
    std::cerr << status.message() << '\n';
    return 1;
  }

  std::cout << "keyboard " << keyboard.keyboard->submit_count() << " mouse " << mouse.mouse->submit_count() << '\n';
  return 0;
}
