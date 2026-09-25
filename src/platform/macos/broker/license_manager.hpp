// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

#pragma once

#include "protocol.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace lvh::detail::macos_broker {

  class LicenseManager {
  public:
    LicenseManager();
    ~LicenseManager();
    LicenseManager(const LicenseManager &) = delete;
    LicenseManager &operator=(const LicenseManager &) = delete;

    Message handle(const Message &request);
    bool authorize_create(Message &response, bool &evaluation);
    bool device_is_authorized(bool evaluation);
    void add_device(bool evaluation);
    void remove_device(bool evaluation);

  private:
    struct State {
      std::string key;
      std::string activation_id;
      std::string status;
      std::string organization_id;
      std::string benefit_id;
      std::string customer_email;
      std::uint32_t activation_limit = 0;
    };

    Message activate(const Message &request);
    Message validate();
    Message deactivate();
    Message status();
    bool licensed_locked() const;
    bool yearly_locked() const;
    void fill_status_locked(Message &response) const;
    void background_validation(std::stop_token stop);

    std::mutex operation_mutex_;
    std::mutex mutex_;
    std::optional<State> state_;
    std::optional<std::chrono::steady_clock::time_point> validated_at_;
    std::optional<std::chrono::steady_clock::time_point> unavailable_since_;
    std::optional<std::chrono::system_clock::time_point> evaluation_started_at_;
    std::uint32_t active_devices_ = 0;
    std::uint32_t active_licensed_devices_ = 0;
    bool github_actions_ = false;
    bool online_confirmed_ = false;
    std::jthread validator_;
  };

}  // namespace lvh::detail::macos_broker
