/**
 * @file src/core/types.cpp
 * @brief Core type helper definitions.
 */

// standard includes
#include <utility>

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh {

  Status::Status():
      code_ {ErrorCode::ok},
      message_ {} {}

  Status::Status(ErrorCode code, std::string message):
      code_ {code},
      message_ {std::move(message)} {}

  Status Status::success() {
    return {};
  }

  Status Status::failure(ErrorCode code, std::string message) {
    if (code == ErrorCode::ok) {
      return {};
    }
    return {code, std::move(message)};
  }

  bool Status::ok() const {
    return code_ == ErrorCode::ok;
  }

  ErrorCode Status::code() const {
    return code_;
  }

  const std::string &Status::message() const {
    return message_;
  }

  void ButtonSet::set(GamepadButton button, bool pressed) {
    const auto mask = 1U << static_cast<std::uint8_t>(button);
    if (pressed) {
      bits_ |= mask;
    } else {
      bits_ &= ~mask;
    }
  }

  void ButtonSet::reset(GamepadButton button) {
    set(button, false);
  }

  void ButtonSet::clear() {
    bits_ = 0;
  }

  bool ButtonSet::test(GamepadButton button) const {
    return (bits_ & (1U << static_cast<std::uint8_t>(button))) != 0;
  }

  std::uint32_t ButtonSet::raw_bits() const {
    return bits_;
  }

}  // namespace lvh
