// SPDX-FileCopyrightText: 2026 LIZARDBYTE LLC
// SPDX-License-Identifier: LicenseRef-LizardByte-SAL-1.0

/**
 * @file src/platform/macos/broker/license_manager.hpp
 * @brief Machine-scoped licensing state for the macOS virtual HID broker.
 */

#pragma once

#include "platform/shared/lvh_broker_license_policy.hpp"
#include "protocol.hpp"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace lvh::detail::macos_broker {

  class LicenseManager {
  public:
    LicenseManager();
    ~LicenseManager();
    LicenseManager(const LicenseManager &) = delete;
    LicenseManager &operator=(const LicenseManager &) = delete;

    Message handle(const Message &request);
    bool authorize_create(Message &response, bool &evaluation, std::string &authorized_key);
    /**
     * @brief Check whether an active device may remain during license validation.
     * @param evaluation Whether the device belongs to the CI evaluation.
     * @param device_id Unique ID returned by add_device.
     * @return Whether the device remains authorized.
     */
    bool device_is_authorized(bool evaluation, std::uint64_t device_id);

    /**
     * @brief Register a created device and return its unique ID.
     * @param evaluation Whether the device belongs to the CI evaluation.
     * @param authorized_key License key used for a licensed gamepad.
     * @return Unique ID for later authorization checks and removal.
     */
    std::uint64_t add_device(bool evaluation, std::string_view authorized_key);

    /**
     * @brief Unregister a device when its session ends.
     * @param evaluation Whether the device belongs to the CI evaluation.
     * @param device_id Unique ID returned by add_device.
     */
    void remove_device(bool evaluation, std::uint64_t device_id);

  private:
    struct State {
      std::string key;
      std::string activation_id;
      std::string status;
      std::string organization_id;
      std::string benefit_id;
      std::string customer_email;
      std::uint32_t activation_limit = 0;
      std::uint32_t pending_usage = 0;
    };

    Message activate(const Message &request);
    Message validate();
    Message deactivate();
    Message status();
    bool licensed_locked() const;
    bool save_state(const State &state) const;
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
    std::uint64_t next_device_id_ = 0;
    broker_license::OutageDeviceSelector outage_device_selector_;
    bool github_actions_ = false;
    bool online_confirmed_ = false;
    std::jthread validator_;
  };

}  // namespace lvh::detail::macos_broker
