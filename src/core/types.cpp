/**
 * @file src/core/types.cpp
 * @brief Core type helper definitions.
 */

// standard includes
#include <utility>

// local includes
#include <libvirtualhid/types.hpp>

namespace lvh {

  OperationStatus::OperationStatus():
      code_ {ErrorCode::ok},
      message_ {} {}

  OperationStatus::OperationStatus(ErrorCode code, std::string message):
      code_ {code},
      message_ {std::move(message)} {}

  OperationStatus OperationStatus::success() {
    return {};
  }

  OperationStatus OperationStatus::failure(ErrorCode code, std::string message) {
    if (code == ErrorCode::ok) {
      return {};
    }
    return {code, std::move(message)};
  }

  bool OperationStatus::ok() const {
    return code_ == ErrorCode::ok;
  }

  ErrorCode OperationStatus::code() const {
    return code_;
  }

  const std::string &OperationStatus::message() const {
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
