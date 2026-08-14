/**
 * @file src/platform/windows/windows_broker_client.hpp
 * @brief Internal Windows broker client helpers.
 */
#pragma once

// standard includes
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// local includes
#include "lvh_windows_broker_protocol.h"

#include <libvirtualhid/types.hpp>

namespace lvh::detail::windows_broker {

  /**
   * @brief Create a versioned Windows broker request header.
   *
   * @param type Broker request type.
   * @param size Full request structure size.
   * @return Initialized request header.
   */
  LvhWindowsBrokerRequestHeader make_request_header(LvhWindowsBrokerRequestType type, std::uint32_t size);

  /**
   * @brief Map a Windows broker response status into the public status type.
   *
   * @param status Broker status code.
   * @param message Broker-supplied status message.
   * @return Public operation status.
   */
  OperationStatus response_status(std::uint32_t status, std::string_view message);

  /**
   * @brief Send fixed-size request bytes to the local Windows broker.
   *
   * @param request Request bytes.
   * @param response Writable response bytes.
   * @param operation Human-readable operation description.
   * @return Named-pipe transport status.
   */
  OperationStatus call_bytes(
    std::span<const std::byte> request,
    std::span<std::byte> response,
    std::string_view operation
  );

  /**
   * @brief Send a typed request to the local Windows broker.
   *
   * @tparam Request Fixed-size broker request structure.
   * @tparam Response Fixed-size broker response structure.
   * @param request Request structure.
   * @param response Response structure populated by the broker.
   * @param operation Human-readable operation description.
   * @return Broker operation status.
   */
  template<typename Request, typename Response>
  OperationStatus call(const Request &request, Response &response, std::string_view operation) {
    const auto request_bytes = std::as_bytes(std::span {&request, 1U});
    const auto response_bytes = std::as_writable_bytes(std::span {&response, 1U});
    if (auto status = call_bytes(request_bytes, response_bytes, operation); !status.ok()) {
      return status;
    }

    if (response.version != LVH_WINDOWS_BROKER_PROTOCOL_VERSION || response.size != sizeof(response)) {
      return OperationStatus::failure(ErrorCode::backend_failure, "Windows broker returned a truncated or invalid response");
    }

    return response_status(response.status, response.message.data());
  }

}  // namespace lvh::detail::windows_broker
