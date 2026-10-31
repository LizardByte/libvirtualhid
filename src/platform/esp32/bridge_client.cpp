/**
 * @file src/platform/esp32/bridge_client.cpp
 * @brief Host-side client for the ESP32-S3 USB bridge wire protocol.
 */

// standard includes
#include <algorithm>
#include <format>
#include <string_view>
#include <utility>

// local includes
#include "platform/esp32/bridge_client.hpp"
#include "platform/esp32/bytes.hpp"
#include "platform/esp32/hmac_sha256.hpp"

namespace lvh::detail::esp32 {
  namespace {

    constexpr auto read_timeout = std::chrono::milliseconds {50};
    constexpr auto supervisor_tick = std::chrono::milliseconds {20};
    constexpr auto pairing_busy_retry = std::chrono::seconds {1};
    constexpr auto pairing_window = protocol::confirm_window;

    /**
     * @brief Clients that started and have not stopped; lvh::esp32_bridge_status() reports the newest.
     */
    struct RunningClients {
      std::mutex mutex;  ///< Guards clients.
      std::vector<const BridgeClient *> clients;  ///< Oldest first.
    };

    RunningClients &running_clients() {
      static RunningClients instance;
      return instance;
    }

    void register_running(const BridgeClient *client) {
      auto &running = running_clients();
      std::lock_guard lock {running.mutex};
      std::erase(running.clients, client);
      running.clients.push_back(client);
    }

    void unregister_running(const BridgeClient *client) {
      auto &running = running_clients();
      std::lock_guard lock {running.mutex};
      std::erase(running.clients, client);
    }

    bool has_flag(std::uint8_t flags, std::uint8_t flag) {
      return (std::byte {flags} & std::byte {flag}) != std::byte {0};
    }

    /**
     * @brief Collects COBS-encoded bytes until a zero delimiter completes a frame.
     */
    class FrameAccumulator {
    public:
      /**
       * @brief Add received bytes and decode every completed frame.
       *
       * @param bytes Received bytes.
       * @param on_frame Called with each valid frame (type, sequence, payload).
       * @param on_corrupt Called for each frame that fails to decode.
       */
      template<typename OnFrame, typename OnCorrupt>
      void feed(std::span<const std::uint8_t> bytes, OnFrame &&on_frame, OnCorrupt &&on_corrupt) {
        for (const auto value : bytes) {
          if (value != 0) {
            append(value);
            continue;
          }
          if (overflow_ || encoded_.empty()) {
            reset();
            continue;
          }
          if (const auto payload_size = protocol::decode_frame(encoded_, frame_); payload_size.has_value()) {
            on_frame(std::span<const std::uint8_t> {frame_}.first(*payload_size + 2));
          } else {
            on_corrupt();
          }
          reset();
        }
      }

      /**
       * @brief Drop a partial frame.
       */
      void reset() {
        encoded_.clear();
        overflow_ = false;
      }

    private:
      void append(std::uint8_t value) {
        if (encoded_.size() < protocol::max_encoded_frame) {
          encoded_.push_back(value);
        } else {
          overflow_ = true;
        }
      }

      std::vector<std::uint8_t> encoded_;  ///< Bytes of the current frame.
      std::array<std::uint8_t, protocol::max_frame> frame_ {};  ///< Decoded frame.
      bool overflow_ = false;  ///< The current frame exceeded the protocol limit.
    };

    std::string status_message(std::uint8_t status) {
      switch (status) {
        case protocol::status::bad_request:
          return "bridge rejected a malformed request";
        case protocol::status::bad_state:
          return "bridge received a layout request out of order";
        case protocol::status::invalid_layout:
          return "layout exceeds the ESP32-S3 endpoint budget";
        case protocol::status::too_large:
          return "descriptor or report exceeds the bridge limits";
        case protocol::status::version_mismatch:
          return "bridge firmware speaks a different protocol version; flash the current firmware";
        case protocol::status::unauthorized:
          return "bridge is not paired with this host";
        default:
          return std::format("bridge returned status {}", status);
      }
    }

    bool same_slot(const std::optional<SlotLayout> &left, const std::optional<SlotLayout> &right) {
      if (!left.has_value() || !right.has_value()) {
        return left.has_value() == right.has_value();
      }
      return left->kind == right->kind && left->report_descriptor == right->report_descriptor;
    }

    std::uint32_t initial_generation() {
      // Distinct per process so a bridge that kept an older host's layout is never mistaken for ours.
      const auto ticks = static_cast<std::uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
      return (ticks & 0x7FFFFFFFU) | 1U;
    }

    void join_or_detach(std::jthread &thread) {
      if (!thread.joinable()) {
        return;
      }
      // A worker can only end up here if the last owner was released on it; joining itself would throw.
      if (thread.get_id() == std::this_thread::get_id()) {
        thread.detach();  // NOSONAR(cpp:S5962): a worker that releases the last owner cannot join itself; it exits on its own.
      } else {
        thread.join();
      }
    }

  }  // namespace

  OperationStatus validate_layout(const BridgeLayout &layout) {
    std::array<std::uint8_t, protocol::max_slots> kinds {};
    for (std::size_t index = 0; index < protocol::max_slots; ++index) {
      const auto &slot = layout.slots[index];
      kinds[index] = slot.has_value() ? slot->kind : protocol::slot::none;
      if (!slot.has_value()) {
        continue;
      }
      if (slot->report_descriptor.size() > protocol::max_report_descriptor || slot->neutral_report.size() > protocol::max_report || slot->feature_reports.size() > protocol::max_feature_reports) {
        return OperationStatus::failure(ErrorCode::unsupported_profile, status_message(protocol::status::too_large));
      }
      for (const auto &feature : slot->feature_reports) {
        if (feature.empty() || feature.size() > protocol::max_report) {
          return OperationStatus::failure(ErrorCode::unsupported_profile, status_message(protocol::status::too_large));
        }
      }
    }
    if (!protocol::layout_fits(kinds)) {
      return OperationStatus::failure(
        ErrorCode::unsupported_profile,
        "ESP32-S3 bridge cannot host two XInput pads next to its CDC port; use a PlayStation or Switch profile for the second pad"
      );
    }
    return OperationStatus::success();
  }

  std::array<std::uint8_t, auth_message_size> make_auth_message(
    const std::array<std::uint8_t, protocol::nonce_size> &nonce,
    const std::array<std::uint8_t, protocol::key_id_size> &key_id,
    const std::array<std::uint8_t, protocol::device_id_size> &device_id
  ) {
    std::array<std::uint8_t, auth_message_size> message {};
    auto out = std::ranges::copy(protocol::auth_context, message.begin()).out;
    out = std::ranges::copy(nonce, out).out;
    out = std::ranges::copy(key_id, out).out;
    std::ranges::copy(device_id, out);
    return message;
  }

  BridgeClient::BridgeClient(SerialPortOpener opener, BridgeLogger logger, const BridgeTimings &timings, std::shared_ptr<CredentialStore> credentials, std::string client_name):
      opener_ {std::move(opener)},
      logger_ {std::move(logger)},
      timings_ {timings},
      credentials_ {std::move(credentials)},
      client_name_ {std::move(client_name)} {
    layout_.next_generation = initial_generation();
  }

  BridgeClient::~BridgeClient() {
    stop();
  }

  OperationStatus BridgeClient::start() {
    std::lock_guard threads_lock {workers_.mutex};
    stopping_ = false;
    if (!workers_.reader.joinable()) {
      workers_.reader = std::jthread {[this](std::stop_token stop_token) {
        reader_loop(stop_token);
      }};
      workers_.supervisor = std::jthread {[this](std::stop_token stop_token) {
        supervisor_loop(stop_token);
      }};
    }
    register_running(this);

    bool usable;
    bool version_mismatch;
    {
      std::unique_lock lock {state_mutex_};
      state_changed_.wait_for(lock, timings_.connect_timeout, [this]() {
        return state_.connected || state_.awaiting_pairing || state_.version_mismatch;
      });
      usable = state_.connected || state_.awaiting_pairing;
      version_mismatch = state_.version_mismatch;
    }
    if (usable) {
      return OperationStatus::success();
    }

    // Do not keep polling for a bridge that never answered; the next device creation retries.
    stop_threads();
    if (version_mismatch) {
      return OperationStatus::failure(ErrorCode::backend_unavailable, status_message(protocol::status::version_mismatch));
    }
    return OperationStatus::failure(
      ErrorCode::backend_unavailable,
      std::format("no ESP32-S3 bridge answered (looked for a CDC port with USB serial {}*)", protocol::serial_prefix)
    );
  }

  void BridgeClient::stop() {
    std::lock_guard threads_lock {workers_.mutex};
    stop_threads();
  }

  void BridgeClient::stop_threads() {
    unregister_running(this);
    {
      std::lock_guard lock {state_mutex_};
      stopping_ = true;
      state_changed_.notify_all();
    }
    workers_.supervisor.request_stop();
    workers_.reader.request_stop();
    join_or_detach(workers_.supervisor);
    join_or_detach(workers_.reader);
    drop_port();
  }

  OperationStatus BridgeClient::apply_layout(const BridgeLayout &layout, LayoutCommit commit) {
    std::lock_guard lock {layout_.mutex};
    return apply_layout_locked(layout, commit);
  }

  void BridgeClient::set_desired_layout(std::optional<BridgeLayout> layout, std::uint32_t generation) {
    if (layout.has_value()) {
      std::lock_guard lock {inputs_.mutex};
      for (std::size_t index = 0; index < protocol::max_slots; ++index) {
        if (!layout_.desired.has_value() || !same_slot(layout_.desired->slots[index], layout->slots[index])) {
          inputs_.latest[index].clear();
        }
        inputs_.replayable[index] = layout->slots[index].has_value() && layout->slots[index]->kind != protocol::slot::keyboard_mouse;
      }
    }
    layout_.desired = std::move(layout);
    std::lock_guard lock {state_mutex_};
    state_.desired_generation = layout_.desired.has_value() ? generation : 0U;
  }

  std::optional<bool> BridgeClient::wait_until_reachable() {
    std::unique_lock lock {state_mutex_};
    if (const auto reachable = state_changed_.wait_for(lock, timings_.connect_timeout, [this]() {
          return state_.connected || state_.awaiting_pairing || stopping_;
        });
        !reachable || stopping_) {
      return std::nullopt;
    }
    return !state_.connected;
  }

  OperationStatus BridgeClient::apply_layout_locked(const BridgeLayout &layout, LayoutCommit commit) {
    if (auto status = validate_layout(layout); !status.ok()) {
      return status;
    }

    const auto previous_layout = layout_.desired;
    std::uint32_t previous_generation;
    {
      std::lock_guard lock {state_mutex_};
      previous_generation = state_.desired_generation;
    }
    const auto generation = take_generation();
    if (commit == LayoutCommit::eventually) {
      // Record first: if the bridge is unreachable, the supervisor applies it later.
      set_desired_layout(layout, generation);
    }

    const auto deferred = wait_until_reachable();
    if (!deferred.has_value()) {
      return OperationStatus::failure(ErrorCode::backend_unavailable, "ESP32-S3 bridge is not connected");
    }
    if (*deferred) {
      // The supervisor applies the desired layout once pairing completes.
      set_desired_layout(layout, generation);
      log(LogLevel::info, "ESP32-S3 bridge layout will be applied once the bridge is paired");
      return OperationStatus::success();
    }

    if (auto status = send_layout(layout, generation); !status.ok()) {
      if (commit == LayoutCommit::now) {
        set_desired_layout(previous_layout, previous_generation);
      }
      return status;
    }

    log(LogLevel::info, std::format("ESP32-S3 bridge re-enumerating with layout generation {}", generation));
    // Some CDC drivers keep a stale handle readable after surprise removal, so
    // reopen explicitly instead of waiting for a read error.
    std::this_thread::sleep_for(timings_.commit_settle);
    drop_port();
    if (!wait_for_generation(generation, timings_.reenumerate_timeout)) {
      return OperationStatus::failure(
        ErrorCode::backend_failure,
        "ESP32-S3 bridge did not come back after re-enumeration; first-time Windows driver setup can take longer, retry"
      );
    }
    replay_inputs();
    return OperationStatus::success();
  }

  OperationStatus BridgeClient::send_layout(const BridgeLayout &layout, std::uint32_t generation) {
    std::vector<std::uint8_t> payload;
    append_le(payload, layout.vendor_id);
    append_le(payload, layout.product_id);
    append_le(payload, layout.bcd_device);
    append_length_prefixed(payload, layout.manufacturer, protocol::max_string);
    append_length_prefixed(payload, layout.product, protocol::max_string);
    if (auto status = transact(protocol::message::layout_begin, payload); !status.ok()) {
      return status;
    }

    for (std::size_t index = 0; index < protocol::max_slots; ++index) {
      if (!layout.slots[index].has_value()) {
        continue;
      }
      const auto &slot = *layout.slots[index];
      payload.clear();
      payload.push_back(static_cast<std::uint8_t>(index));
      payload.push_back(slot.kind);
      payload.push_back(slot.interval_ms);
      append_le(payload, static_cast<std::uint16_t>(slot.report_descriptor.size()));
      payload.insert(payload.end(), slot.report_descriptor.begin(), slot.report_descriptor.end());
      payload.insert(payload.end(), slot.neutral_report.begin(), slot.neutral_report.end());
      if (auto status = transact(protocol::message::layout_slot, payload); !status.ok()) {
        return status;
      }

      for (const auto &feature : slot.feature_reports) {
        payload.clear();
        payload.push_back(static_cast<std::uint8_t>(index));
        payload.insert(payload.end(), feature.begin(), feature.end());
        if (auto status = transact(protocol::message::layout_feature, payload); !status.ok()) {
          return status;
        }
      }
    }

    // Desired before COMMIT, so the handshake after re-enumeration recognizes the generation.
    set_desired_layout(layout, generation);
    payload.clear();
    append_le(payload, generation);
    return transact(protocol::message::layout_commit, payload);
  }

  std::uint32_t BridgeClient::take_generation() {
    const auto generation = layout_.next_generation++;
    if (layout_.next_generation == 0U) {
      layout_.next_generation = 1U;
    }
    return generation;
  }

  void BridgeClient::send_input(std::uint8_t slot, std::span<const std::uint8_t> report) {
    if (slot >= protocol::max_slots || report.empty() || report.size() > protocol::max_report) {
      return;
    }
    {
      std::lock_guard lock {inputs_.mutex};
      if (inputs_.replayable[slot]) {
        inputs_.latest[slot].assign(report.begin(), report.end());
      }
    }
    write_input_frame(slot, report);
  }

  void BridgeClient::write_input_frame(std::uint8_t slot, std::span<const std::uint8_t> report) {
    std::vector<std::uint8_t> payload;
    payload.reserve(report.size() + 1U);
    payload.push_back(slot);
    payload.insert(payload.end(), report.begin(), report.end());
    static_cast<void>(write_frame(protocol::message::input_report, payload));
  }

  void BridgeClient::set_output_handler(std::function<void(const BridgeOutput &)> handler) {
    std::lock_guard lock {output_.mutex};
    output_.handler = std::move(handler);
  }

  Esp32BridgeStatus BridgeClient::status() const {
    Esp32BridgeStatus status;
    status.active = true;
    status.port = port_path();
    std::lock_guard lock {state_mutex_};
    status.connected = state_.connected;
    status.awaiting_pairing = state_.awaiting_pairing;
    status.pairing_required = state_.pairing_required;
    status.device_id = state_.device_id;
    status.firmware_version = state_.firmware_version;
    return status;
  }

  Esp32BridgeStatus active_bridge_status() {
    auto &running = running_clients();
    std::lock_guard lock {running.mutex};
    return running.clients.empty() ? Esp32BridgeStatus {} : running.clients.back()->status();
  }

  std::string BridgeClient::port_path() const {
    std::lock_guard lock {link_mutex_};
    return link_.port != nullptr ? link_.port->path() : std::string {};
  }

  void BridgeClient::reader_loop(const std::stop_token &stop_token) {
    FrameAccumulator frames;
    std::array<std::uint8_t, 512> chunk {};
    const auto on_frame = [this](std::span<const std::uint8_t> frame) {
      handle_frame(frame);
    };
    const auto on_corrupt = [this]() {
      log(LogLevel::debug, "dropped corrupt frame from ESP32-S3 bridge");
    };

    while (!stop_token.stop_requested()) {
      std::shared_ptr<SerialPort> port;
      {
        std::lock_guard lock {link_mutex_};
        port = link_.port;
      }
      if (port == nullptr) {
        frames.reset();
        if (!open_port()) {
          std::this_thread::sleep_for(timings_.reopen_interval);
        }
        continue;
      }

      const auto count = port->read(chunk, read_timeout);
      if (count < 0) {
        log(LogLevel::debug, std::format("ESP32-S3 bridge port {} closed", port->path()));
        drop_port(port);
        continue;
      }
      frames.feed(std::span<const std::uint8_t> {chunk}.first(static_cast<std::size_t>(count)), on_frame, on_corrupt);
    }
  }

  void BridgeClient::supervisor_loop(const std::stop_token &stop_token) {
    auto next_keepalive = std::chrono::steady_clock::now();
    std::uint32_t ping_token = 0;
    while (!stop_token.stop_requested()) {
      std::this_thread::sleep_for(supervisor_tick);
      const auto now = std::chrono::steady_clock::now();
      if (now < next_keepalive) {
        continue;
      }
      next_keepalive = now + timings_.keepalive_interval;

      bool connected;
      bool retry_pairing;
      std::uint32_t device_generation;
      std::uint32_t desired_generation;
      std::chrono::steady_clock::time_point last_frame;
      {
        std::lock_guard lock {state_mutex_};
        connected = state_.connected;
        device_generation = state_.device_generation;
        desired_generation = state_.desired_generation;
        last_frame = state_.last_frame_time;
        retry_pairing = state_.awaiting_pairing && state_.pairing_retry_at.has_value() && now >= *state_.pairing_retry_at;
        if (retry_pairing) {
          state_.pairing_retry_at.reset();
        }
      }
      std::shared_ptr<SerialPort> port;
      {
        std::lock_guard lock {link_mutex_};
        port = link_.port;
      }
      if (port == nullptr) {
        continue;
      }
      if (retry_pairing) {
        request_pairing();
      }

      // The bridge answers PING before the handshake and while unpaired, so
      // silence always means the port is stale or the HELLO was lost.
      std::vector<std::uint8_t> payload;
      append_le(payload, ++ping_token);
      static_cast<void>(write_frame(protocol::message::ping, payload));
      if (now - last_frame > timings_.silence_timeout) {
        log(connected ? LogLevel::warning : LogLevel::debug, "ESP32-S3 bridge stopped answering; reopening the port");
        drop_port(port);
        continue;
      }

      if (!connected || desired_generation == 0U || device_generation == desired_generation) {
        continue;
      }
      if (std::unique_lock layout_lock {layout_.mutex, std::try_to_lock}; layout_lock.owns_lock() && layout_.desired.has_value()) {
        log(LogLevel::warning, "ESP32-S3 bridge lost its layout (reset or host timeout); restoring it");
        const auto layout = *layout_.desired;
        if (const auto status = apply_layout_locked(layout, LayoutCommit::eventually); !status.ok() && !stopping_) {
          log(LogLevel::error, "failed to restore ESP32-S3 bridge layout: " + status.message());
        }
      }
    }
  }

  bool BridgeClient::open_port() {
    std::shared_ptr<SerialPort> port = opener_ ? opener_() : nullptr;
    if (port == nullptr) {
      return false;
    }
    log(LogLevel::debug, "opened ESP32-S3 bridge port " + port->path());
    {
      std::lock_guard lock {link_mutex_};
      link_.port = port;
    }
    {
      std::lock_guard lock {state_mutex_};
      state_.last_frame_time = std::chrono::steady_clock::now();
    }
    std::vector<std::uint8_t> payload;
    append_le(payload, protocol::version);
    return write_frame(protocol::message::hello, payload);
  }

  void BridgeClient::drop_port(const std::shared_ptr<SerialPort> &expected) {
    {
      std::lock_guard lock {link_mutex_};
      // A failure seen on an old port must not close the one the reader opened since.
      if (expected != nullptr && link_.port != expected) {
        return;
      }
      link_.port.reset();
    }
    std::lock_guard lock {state_mutex_};
    state_.connected = false;
    state_.awaiting_pairing = false;
    state_.pairing_retry_at.reset();
    state_changed_.notify_all();
  }

  bool BridgeClient::write_frame(std::uint8_t type, std::span<const std::uint8_t> payload, const std::function<void(std::uint8_t)> &on_sequence) {
    std::array<std::uint8_t, protocol::max_encoded_frame> encoded {};
    std::shared_ptr<SerialPort> port;
    {
      // Writes stay under the lock so frames from different threads never
      // interleave and sequence numbers go out in order.
      std::lock_guard lock {link_mutex_};
      if (link_.port == nullptr) {
        return false;
      }
      const auto sequence = link_.next_sequence++;
      if (on_sequence) {
        on_sequence(sequence);
      }
      const auto size = protocol::encode_frame(type, sequence, payload, encoded);
      if (size == 0U) {
        return false;
      }
      if (link_.port->write(std::span<const std::uint8_t> {encoded}.first(size))) {
        return true;
      }
      port = link_.port;
    }
    drop_port(port);
    return false;
  }

  bool BridgeClient::Connection::mark_authorized() {
    connected = true;
    awaiting_pairing = false;
    confirmed_generation = device_generation;
    return device_generation != 0U && device_generation == desired_generation;
  }

  void BridgeClient::handle_frame(std::span<const std::uint8_t> frame) {
    const auto type = frame[0];
    const auto payload = frame.subspan(2);
    FrameFollowUp follow_up;
    {
      std::lock_guard lock {state_mutex_};
      state_.last_frame_time = std::chrono::steady_clock::now();
      switch (type) {
        case protocol::message::hello_ack:
          state_.on_hello_ack(payload, follow_up);
          break;
        case protocol::message::ack:
          state_.on_ack(payload, follow_up);
          break;
        case protocol::message::status:
          if (payload.size() >= 6U) {
            state_.device_generation = read_le<std::uint32_t>(payload.subspan(2));
            if (state_.connected) {
              state_.confirmed_generation = state_.device_generation;
            }
          }
          break;
        case protocol::message::confirm_result:
          state_.on_confirm_result(payload, follow_up);
          break;
        default:
          break;
      }
      state_changed_.notify_all();
    }
    finish_frame(type, payload, follow_up);
  }

  void BridgeClient::finish_frame(std::uint8_t type, std::span<const std::uint8_t> payload, const FrameFollowUp &follow_up) {
    // Logging, storage, and writes happen without the state lock: a logger may query the status.
    if (follow_up.credential.has_value()) {
      if (credentials_ == nullptr || !credentials_->save(follow_up.device_id, *follow_up.credential)) {
        log(LogLevel::error, std::format("paired with ESP32-S3 bridge {} but could not store the pairing; it will ask again next time", follow_up.device_id));
      } else {
        log(LogLevel::info, "paired with ESP32-S3 bridge " + follow_up.device_id);
      }
    }
    for (const auto &[level, message] : follow_up.logs) {
      log(level, message);
    }
    if (type == protocol::message::log) {
      log(LogLevel::debug, "ESP32-S3 bridge: " + std::string {payload.begin(), payload.end()});
    } else if (type == protocol::message::output_report) {
      dispatch_output(payload);
    }
    if (follow_up.authenticate) {
      authenticate();
    } else if (follow_up.pair_again) {
      log(LogLevel::warning, "ESP32-S3 bridge rejected the stored pairing (it was removed on the bridge); pairing again");
      if (credentials_ != nullptr) {
        credentials_->erase(follow_up.device_id);
      }
      request_pairing();
    }
    if (follow_up.replay) {
      replay_inputs();
    }
  }

  void BridgeClient::Connection::on_hello_ack(std::span<const std::uint8_t> payload, FrameFollowUp &follow_up) {
    constexpr std::size_t flags_offset = 10U;
    constexpr std::size_t nonce_offset = flags_offset + 1U;
    constexpr std::size_t device_offset = nonce_offset + protocol::nonce_size;
    constexpr std::size_t firmware_offset = device_offset + protocol::device_id_size;
    if (payload.size() < firmware_offset) {
      return;
    }
    device_generation = read_le<std::uint32_t>(payload.subspan(4));
    firmware_version.assign(payload.begin() + firmware_offset, payload.end());
    std::ranges::copy(payload.subspan(nonce_offset, protocol::nonce_size), nonce.begin());
    std::ranges::copy(payload.subspan(device_offset, protocol::device_id_size), device_id_bytes.begin());
    device_id = hex_encode(device_id_bytes, true);
    version_mismatch = false;
    pairing_required = has_flag(payload[flags_offset], protocol::auth_flag::pairing_required);
    if (has_flag(payload[flags_offset], protocol::auth_flag::authorized)) {
      follow_up.replay = mark_authorized();
    } else {
      follow_up.authenticate = true;
    }
  }

  void BridgeClient::Connection::on_ack(std::span<const std::uint8_t> payload, FrameFollowUp &follow_up) {
    if (payload.size() < 3U) {
      return;
    }
    const auto acknowledged = payload[0];
    const auto status = payload[2];
    switch (acknowledged) {
      case protocol::message::hello:
        if (status == protocol::status::version_mismatch) {
          version_mismatch = true;
          firmware_version.clear();
          follow_up.logs.emplace_back(LogLevel::error, status_message(status));
        }
        break;
      case protocol::message::auth:
        if (status == protocol::status::ok) {
          follow_up.replay = mark_authorized();
          follow_up.logs.emplace_back(LogLevel::debug, "authenticated with ESP32-S3 bridge " + device_id);
        } else {
          follow_up.pair_again = true;
          follow_up.device_id = device_id;
        }
        break;
      case protocol::message::pair:
        if (status == protocol::status::busy) {
          pairing_retry_at = last_frame_time + pairing_busy_retry;
        } else if (status == protocol::status::full) {
          pairing_retry_at = last_frame_time + pairing_window;
          follow_up.logs.emplace_back(
            LogLevel::error,
            std::format("ESP32-S3 bridge already has {} paired hosts; remove one on the flasher site's configure page, or hold BOOT for 10 s to remove all", protocol::max_pairings)
          );
        }
        break;
      default:
        break;
    }
    if (pending_ack.active && pending_ack.type == acknowledged && pending_ack.sequence == payload[1]) {
      pending_ack.received = true;
      pending_ack.status = status;
    }
  }

  void BridgeClient::Connection::on_confirm_result(std::span<const std::uint8_t> payload, FrameFollowUp &follow_up) {
    if (payload.size() < 2U || payload[0] != protocol::message::pair || !awaiting_pairing) {
      return;
    }
    if (payload[1] == protocol::status::ok && payload.size() >= 2U + protocol::key_id_size + protocol::secret_size) {
      BridgeCredential credential;
      std::ranges::copy(payload.subspan(2, protocol::key_id_size), credential.key_id.begin());
      std::ranges::copy(payload.subspan(2 + protocol::key_id_size, protocol::secret_size), credential.secret.begin());
      follow_up.credential = credential;
      follow_up.device_id = device_id;
      pairing_announced = false;
      // The bridge authorizes the session that completed pairing.
      follow_up.replay = mark_authorized();
      return;
    }
    // Keep a request open while unpaired so a later BOOT press still pairs.
    pairing_retry_at = payload[1] == protocol::status::timeout ? last_frame_time : last_frame_time + pairing_window;
  }

  void BridgeClient::dispatch_output(std::span<const std::uint8_t> payload) {
    if (payload.size() < 2U || payload[0] >= protocol::max_slots) {
      return;
    }
    BridgeOutput output;
    output.slot = payload[0];
    output.kind = payload[1];
    output.report.assign(payload.begin() + 2, payload.end());
    std::function<void(const BridgeOutput &)> handler;
    {
      std::lock_guard lock {output_.mutex};
      handler = output_.handler;
    }
    if (handler) {
      handler(output);
    }
  }

  OperationStatus BridgeClient::transact(std::uint8_t type, std::span<const std::uint8_t> payload) {
    // The pending ACK is armed inside write_frame, before the bytes leave, so
    // a fast reply always finds its match.
    if (const auto arm = [this, type](std::uint8_t sequence) {
          std::lock_guard lock {state_mutex_};
          state_.pending_ack = {};
          state_.pending_ack.active = true;
          state_.pending_ack.type = type;
          state_.pending_ack.sequence = sequence;
        };
        !write_frame(type, payload, arm)) {
      return OperationStatus::failure(ErrorCode::backend_unavailable, "ESP32-S3 bridge port is not open");
    }

    std::unique_lock lock {state_mutex_};
    const auto received = state_changed_.wait_for(lock, timings_.ack_timeout, [this]() {
      return state_.pending_ack.received || !state_.connected || stopping_;
    });
    const auto acknowledged = received && state_.pending_ack.received;
    const auto status = state_.pending_ack.status;
    state_.pending_ack.active = false;
    if (!acknowledged) {
      return OperationStatus::failure(ErrorCode::backend_failure, "ESP32-S3 bridge did not acknowledge a request");
    }
    if (status != protocol::status::ok) {
      const auto code = status == protocol::status::invalid_layout || status == protocol::status::too_large ? ErrorCode::unsupported_profile : ErrorCode::backend_failure;
      return OperationStatus::failure(code, status_message(status));
    }
    return OperationStatus::success();
  }

  bool BridgeClient::wait_for_generation(std::uint32_t generation, std::chrono::milliseconds timeout) {
    std::unique_lock lock {state_mutex_};
    state_changed_.wait_for(lock, timeout, [this, generation]() {
      return state_.confirmed_generation == generation || stopping_;
    });
    return state_.confirmed_generation == generation;
  }

  void BridgeClient::replay_inputs() {
    std::array<std::vector<std::uint8_t>, protocol::max_slots> inputs;
    {
      std::lock_guard lock {inputs_.mutex};
      inputs = inputs_.latest;
    }
    for (std::size_t index = 0; index < inputs.size(); ++index) {
      if (!inputs[index].empty()) {
        write_input_frame(static_cast<std::uint8_t>(index), inputs[index]);
      }
    }
  }

  void BridgeClient::authenticate() {
    std::string device_id;
    std::array<std::uint8_t, protocol::nonce_size> nonce;
    std::array<std::uint8_t, protocol::device_id_size> device_bytes;
    {
      std::lock_guard lock {state_mutex_};
      device_id = state_.device_id;
      nonce = state_.nonce;
      device_bytes = state_.device_id_bytes;
    }
    const auto credential = credentials_ != nullptr ? credentials_->load(device_id) : std::nullopt;
    if (!credential.has_value()) {
      request_pairing();
      return;
    }

    const auto mac = hmac_sha256(credential->secret, make_auth_message(nonce, credential->key_id, device_bytes));
    std::array<std::uint8_t, protocol::key_id_size + protocol::mac_size> payload {};
    std::ranges::copy(mac, std::ranges::copy(credential->key_id, payload.begin()).out);
    static_cast<void>(write_frame(protocol::message::auth, payload));
  }

  void BridgeClient::request_pairing() {
    bool announce;
    std::string device_id;
    {
      std::lock_guard lock {state_mutex_};
      state_.awaiting_pairing = true;
      announce = !state_.pairing_announced;
      state_.pairing_announced = true;
      device_id = state_.device_id;
      state_changed_.notify_all();
    }
    if (announce) {
      log(LogLevel::warning, "ESP32-S3 bridge " + device_id + " is not paired with this host. Press the BOOT button on the bridge to allow " + client_name_ + " to use it.");
    }
    std::vector<std::uint8_t> payload;
    append_length_prefixed(payload, client_name_, protocol::max_pairing_name);
    static_cast<void>(write_frame(protocol::message::pair, payload));
  }

  void BridgeClient::log(LogLevel level, const std::string &message) const {
    if (!logger_) {
      return;
    }
    try {
      logger_(level, message);
    } catch (...) {  // NOSONAR(cpp:S2738,cpp:S2486): a logger may throw anything; diagnostics must never take the bridge down.
    }
  }

}  // namespace lvh::detail::esp32
