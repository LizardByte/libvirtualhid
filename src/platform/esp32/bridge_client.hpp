/**
 * @file src/platform/esp32/bridge_client.hpp
 * @brief Host-side client for the ESP32-S3 USB bridge wire protocol.
 */
#pragma once

// standard includes
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

// local includes
#include "platform/esp32/credential_store.hpp"
#include "platform/esp32/esp32_bridge_protocol.hpp"
#include "platform/esp32/serial_port.hpp"

#include <libvirtualhid/esp32_bridge.hpp>
#include <libvirtualhid/types.hpp>

namespace lvh::detail::esp32 {

  /**
   * @brief Configuration of one enumerated bridge slot.
   */
  struct SlotLayout {
    std::uint8_t kind = protocol::slot::none;  ///< protocol::slot.
    std::uint8_t interval_ms = 1;  ///< Interrupt IN polling interval.
    std::vector<std::uint8_t> report_descriptor;  ///< HID report descriptor, empty for XInput.
    std::vector<std::uint8_t> neutral_report;  ///< Report used at enumeration and when the host goes quiet.
    std::vector<std::vector<std::uint8_t>> feature_reports;  ///< GET_REPORT(Feature) replies, each starting with its report id.
  };

  /**
   * @brief Complete USB personality requested from the bridge.
   */
  struct BridgeLayout {
    std::uint16_t vendor_id = 0x303A;  ///< USB idVendor.
    std::uint16_t product_id = 0x40F0;  ///< USB idProduct.
    std::uint16_t bcd_device = 0x0100;  ///< USB bcdDevice.
    std::string manufacturer = "libvirtualhid";  ///< Manufacturer string.
    std::string product = "ESP32-S3 HID Bridge";  ///< Product string.
    std::array<std::optional<SlotLayout>, protocol::max_slots> slots {};  ///< Slot configuration.
  };

  /**
   * @brief Report the USB host sent to one slot.
   */
  struct BridgeOutput {
    std::uint8_t slot = 0;  ///< Slot index.
    std::uint8_t kind = protocol::report_kind::output;  ///< protocol::report_kind.
    std::vector<std::uint8_t> report;  ///< Report bytes, starting with the report id when the slot uses ids.
  };

  /**
   * @brief Timeouts used by the client; tests shorten them.
   */
  struct BridgeTimings {
    std::chrono::milliseconds ack_timeout {1000};  ///< Wait for one acknowledgement.
    std::chrono::milliseconds connect_timeout {3000};  ///< Wait for the first handshake.
    std::chrono::milliseconds reenumerate_timeout {20000};  ///< Wait for the bridge to come back after a layout change.
    std::chrono::milliseconds reopen_interval {100};  ///< Delay between attempts to reopen the port.
    std::chrono::milliseconds keepalive_interval {protocol::keepalive_interval};  ///< Ping period.
    std::chrono::milliseconds silence_timeout {4000};  ///< Reopen the port after this long without any frame.
    std::chrono::milliseconds commit_settle {100};  ///< Delay between a commit ACK and reopening the port.
  };

  /**
   * @brief When a layout change has to take effect.
   */
  enum class LayoutCommit {
    now,  ///< Apply now or fail; on failure the previous layout stays desired.
    eventually,  ///< Record as desired first; the supervisor applies it once the bridge is reachable.
  };

  /**
   * @brief Diagnostic sink.
   */
  using BridgeLogger = std::function<void(LogLevel level, const std::string &message)>;

  /**
   * @brief Check a layout against the protocol limits and the ESP32-S3 endpoint budget.
   *
   * @param layout Layout to check.
   * @return Success, or `unsupported_profile` naming the violated limit.
   */
  OperationStatus validate_layout(const BridgeLayout &layout);

  /// Size of the message the AUTH answer authenticates.
  inline constexpr std::size_t auth_message_size = protocol::auth_context.size() + protocol::nonce_size + protocol::key_id_size + protocol::device_id_size;

  /**
   * @brief Build `"LVHB-AUTH" || nonce || key_id || device_id`, the message covered by the AUTH HMAC.
   *
   * @param nonce Challenge from HELLO_ACK.
   * @param key_id Pairing key id.
   * @param device_id Bridge device id from HELLO_ACK.
   * @return Message bytes.
   */
  std::array<std::uint8_t, auth_message_size> make_auth_message(
    const std::array<std::uint8_t, protocol::nonce_size> &nonce,
    const std::array<std::uint8_t, protocol::key_id_size> &key_id,
    const std::array<std::uint8_t, protocol::device_id_size> &device_id
  );

  /**
   * @brief Owns the bridge port, its reader thread, and layout state.
   *
   * The reader thread reopens the port whenever it disappears, which happens
   * on every layout change because the bridge re-enumerates. A supervisor
   * thread sends keepalives and restores the desired layout after the bridge
   * resets or drops slots on its own.
   *
   * When the bridge requires pairing, every handshake authenticates with the
   * stored credential. Without one the client asks the bridge to pair and
   * keeps the request open until the user presses BOOT; layouts requested in
   * the meantime are applied once pairing completes.
   */
  class BridgeClient {
  public:
    /**
     * @brief Create a client. No I/O happens until start().
     *
     * @param opener Opens the bridge port.
     * @param logger Diagnostic sink, may be empty.
     * @param timings Timeouts.
     * @param credentials Pairing secret storage, may be empty when the bridge does not require pairing.
     * @param client_name Label the bridge shows for this host's pairing.
     */
    BridgeClient(SerialPortOpener opener, BridgeLogger logger, const BridgeTimings &timings = {}, std::shared_ptr<CredentialStore> credentials = nullptr, std::string client_name = "libvirtualhid");

    BridgeClient(const BridgeClient &) = delete;
    BridgeClient &operator=(const BridgeClient &) = delete;
    BridgeClient(BridgeClient &&) noexcept = delete;
    BridgeClient &operator=(BridgeClient &&) noexcept = delete;

    /**
     * @brief Stop the worker threads and close the port.
     */
    ~BridgeClient();

    /**
     * @brief Start the worker threads and wait for the first handshake.
     *
     * Succeeds while the bridge waits for pairing; layouts are then deferred.
     * On failure the threads are stopped again, so a later call retries.
     *
     * @return Success, or `backend_unavailable` when no bridge answers.
     */
    OperationStatus start();

    /**
     * @brief Stop the worker threads and close the port.
     *
     * Pending waits return early. Safe to call more than once; the destructor calls it.
     */
    void stop();

    /**
     * @brief Replace the bridge personality and wait until it re-enumerates.
     *
     * @param layout New layout.
     * @param commit Whether the layout must take effect now.
     * @return Apply status. With LayoutCommit::eventually, a failure only means the change is still pending.
     */
    OperationStatus apply_layout(const BridgeLayout &layout, LayoutCommit commit = LayoutCommit::now);

    /**
     * @brief Send an input report to a slot. Not acknowledged.
     *
     * The latest report per gamepad slot is replayed after reconnects;
     * keyboard/mouse reports are events and are not.
     *
     * @param slot Slot index.
     * @param report Report bytes.
     */
    void send_input(std::uint8_t slot, std::span<const std::uint8_t> report);

    /**
     * @brief Register the handler for output and feature reports.
     *
     * The handler runs on the reader thread.
     *
     * @param handler Output handler.
     */
    void set_output_handler(std::function<void(const BridgeOutput &)> handler);

    /**
     * @brief Snapshot the connection and pairing state.
     *
     * @return Status with `active` set.
     */
    Esp32BridgeStatus status() const;

    /**
     * @brief Get the path of the currently open port.
     *
     * @return Port path, or empty while disconnected.
     */
    std::string port_path() const;

    /**
     * @brief Forward a diagnostic message to the logger, swallowing logger exceptions.
     *
     * @param level Severity.
     * @param message Message text.
     */
    void log(LogLevel level, const std::string &message) const;

  private:
    /**
     * @brief Acknowledgement a transact() call waits for.
     */
    struct PendingAck {
      bool active = false;  ///< A request waits for its ACK.
      bool received = false;  ///< The ACK arrived.
      std::uint8_t type = 0;  ///< Acknowledged message type.
      std::uint8_t sequence = 0;  ///< Acknowledged sequence number.
      std::uint8_t status = 0;  ///< protocol::status from the ACK.
    };

    /**
     * @brief Work handle_frame() leaves for after the state lock is released.
     */
    struct FrameFollowUp {
      bool replay = false;  ///< Resend the latest gamepad reports.
      bool authenticate = false;  ///< Answer the handshake with AUTH or PAIR.
      bool pair_again = false;  ///< The stored pairing was rejected.
      std::optional<BridgeCredential> credential;  ///< Pairing secret to store.
      std::string device_id;  ///< Bridge the credential or rejected pairing belongs to.
      std::vector<std::pair<LogLevel, std::string>> logs;  ///< Messages to log.
    };

    /**
     * @brief Connection and pairing state; guarded by state_mutex_.
     */
    struct Connection {
      bool connected = false;  ///< Handshake done and the bridge accepts layout and input frames.
      bool version_mismatch = false;  ///< The bridge rejected the protocol version.
      std::uint32_t device_generation = 0;  ///< Layout generation the bridge reported.
      std::uint32_t desired_generation = 0;  ///< Generation of the desired layout, 0 when none.
      std::string firmware_version;  ///< Firmware version from the last handshake.
      PendingAck pending_ack;  ///< ACK transact() waits for.
      std::chrono::steady_clock::time_point last_frame_time;  ///< Arrival of the last frame, or the port open time.
      bool awaiting_pairing = false;  ///< Handshake done, waiting for the BOOT press.
      bool pairing_required = false;  ///< Pairing flag from the last HELLO_ACK.
      bool pairing_announced = false;  ///< The "press BOOT" hint was logged for this bridge.
      std::optional<std::chrono::steady_clock::time_point> pairing_retry_at;  ///< When to send PAIR again.
      std::array<std::uint8_t, protocol::nonce_size> nonce {};  ///< Challenge from the last HELLO_ACK.
      std::array<std::uint8_t, protocol::device_id_size> device_id_bytes {};  ///< Bridge identity from the last HELLO_ACK.
      std::string device_id;  ///< Upper-case hex of device_id_bytes.

      /**
       * @brief Record a successful handshake.
       *
       * @return True when the bridge already runs the desired layout, so inputs can be replayed.
       */
      bool mark_authorized();

      /**
       * @brief Apply HELLO_ACK.
       *
       * @param payload Frame payload.
       * @param follow_up Work to do after the state lock is released.
       */
      void on_hello_ack(std::span<const std::uint8_t> payload, FrameFollowUp &follow_up);

      /**
       * @brief Apply ACK.
       *
       * @param payload Frame payload.
       * @param follow_up Work to do after the state lock is released.
       */
      void on_ack(std::span<const std::uint8_t> payload, FrameFollowUp &follow_up);

      /**
       * @brief Apply CONFIRM_RESULT.
       *
       * @param payload Frame payload.
       * @param follow_up Work to do after the state lock is released.
       */
      void on_confirm_result(std::span<const std::uint8_t> payload, FrameFollowUp &follow_up);
    };

    /**
     * @brief Open port and frame sequence; guarded by link_mutex_, and writes stay under it so frames never interleave.
     */
    struct Link {
      std::shared_ptr<SerialPort> port;  ///< Open port; the reader reads from a copy.
      std::uint8_t next_sequence = 0;  ///< Sequence number of the next frame.
    };

    /**
     * @brief Desired layout; the mutex also serializes layout changes.
     */
    struct LayoutState {
      std::mutex mutex;  ///< Guards the members below.
      std::optional<BridgeLayout> desired;  ///< Layout the bridge should have.
      std::uint32_t next_generation = 0;  ///< Generation assigned to the next layout.
    };

    /**
     * @brief Latest input per slot, resent after the bridge comes back.
     */
    struct InputState {
      std::mutex mutex;  ///< Guards the members below.
      std::array<std::vector<std::uint8_t>, protocol::max_slots> latest {};  ///< Latest report per slot for replay.
      std::array<bool, protocol::max_slots> replayable {};  ///< Event slots (keyboard/mouse) must never be replayed.
    };

    /**
     * @brief Output report sink.
     */
    struct OutputSink {
      std::mutex mutex;  ///< Guards handler.
      std::function<void(const BridgeOutput &)> handler;  ///< Receives output reports.
    };

    /**
     * @brief Worker threads.
     */
    struct Workers {
      std::mutex mutex;  ///< Serializes start() and stop().
      std::jthread reader;  ///< Reads frames and reopens the port.
      std::jthread supervisor;  ///< Sends keepalives and restores the layout.
    };

    /**
     * @brief Unregister, wake every wait, join the workers, and close the port; workers_.mutex must be held.
     */
    void stop_threads();

    /**
     * @brief Read and decode frames, reopening the port whenever it is gone.
     *
     * @param stop_token Stops the loop.
     */
    void reader_loop(const std::stop_token &stop_token);

    /**
     * @brief Send keepalives, retry pairing, reopen silent ports, and restore the desired layout.
     *
     * @param stop_token Stops the loop.
     */
    void supervisor_loop(const std::stop_token &stop_token);

    /**
     * @brief Open the port and send HELLO.
     *
     * @return `true` when a port was opened.
     */
    bool open_port();

    /**
     * @brief Close the port and reset the connection state.
     *
     * @param expected Only close when this is still the open port; empty closes any port.
     */
    void drop_port(const std::shared_ptr<SerialPort> &expected = nullptr);

    /**
     * @brief Encode and write one frame.
     *
     * @param type Message type.
     * @param payload Payload bytes.
     * @param on_sequence Called with the frame's sequence number before it is written.
     * @return `true` when the frame was written.
     */
    bool write_frame(std::uint8_t type, std::span<const std::uint8_t> payload, const std::function<void(std::uint8_t)> &on_sequence = {});

    /**
     * @brief Write an INPUT_REPORT frame.
     *
     * @param slot Slot index.
     * @param report Report bytes.
     */
    void write_input_frame(std::uint8_t slot, std::span<const std::uint8_t> report);

    /**
     * @brief Handle one decoded frame from the bridge.
     *
     * @param frame Type, sequence, and payload.
     */
    void handle_frame(std::span<const std::uint8_t> frame);

    /**
     * @brief Run the work a frame left for after the state lock: storage, logging, and writes.
     *
     * @param type Message type.
     * @param payload Frame payload.
     * @param follow_up Work recorded while the state lock was held.
     */
    void finish_frame(std::uint8_t type, std::span<const std::uint8_t> payload, const FrameFollowUp &follow_up);

    /**
     * @brief Pass an OUTPUT_REPORT to the registered handler.
     *
     * @param payload OUTPUT_REPORT payload.
     */
    void dispatch_output(std::span<const std::uint8_t> payload);

    /**
     * @brief Send a request and wait for its ACK.
     *
     * @param type Message type.
     * @param payload Payload bytes.
     * @return Success, or the failure the ACK or timeout reported.
     */
    OperationStatus transact(std::uint8_t type, std::span<const std::uint8_t> payload);

    /**
     * @brief Wait until the bridge answered a handshake.
     *
     * @return Empty when it did not answer in time or the client stopped; otherwise whether pairing is still pending.
     */
    std::optional<bool> wait_until_reachable();

    /**
     * @brief Implement apply_layout(); layout_.mutex must be held.
     *
     * @param layout New layout.
     * @param commit Whether the layout must take effect now.
     * @return Apply status.
     */
    OperationStatus apply_layout_locked(const BridgeLayout &layout, LayoutCommit commit);

    /**
     * @brief Send LAYOUT_BEGIN, SLOT, FEATURE, and COMMIT for a validated layout.
     *
     * @param layout Layout.
     * @param generation Generation to commit.
     * @return Transfer status.
     */
    OperationStatus send_layout(const BridgeLayout &layout, std::uint32_t generation);

    /**
     * @brief Replace the desired layout and forget inputs of slots that changed; layout_.mutex must be held.
     *
     * @param layout Desired layout, or empty for none.
     * @param generation Its generation.
     */
    void set_desired_layout(std::optional<BridgeLayout> layout, std::uint32_t generation);

    /**
     * @brief Wait until the bridge reports a layout generation.
     *
     * @param generation Expected generation.
     * @param timeout Maximum wait.
     * @return `true` when the bridge is connected with that generation.
     */
    bool wait_for_generation(std::uint32_t generation, std::chrono::milliseconds timeout);

    /**
     * @brief Resend the latest report of every gamepad slot.
     */
    void replay_inputs();

    /**
     * @brief Allocate the next non-zero layout generation; layout_.mutex must be held.
     *
     * @return Generation.
     */
    std::uint32_t take_generation();

    /**
     * @brief Answer the handshake with AUTH, or request pairing without a stored credential.
     */
    void authenticate();

    /**
     * @brief Ask the bridge to pair and wait for its BOOT button.
     */
    void request_pairing();

    SerialPortOpener opener_;  ///< Opens the bridge port.
    BridgeLogger logger_;  ///< Diagnostic sink.
    BridgeTimings timings_;  ///< Timeouts.
    std::shared_ptr<CredentialStore> credentials_;  ///< Pairing secrets.
    std::string client_name_;  ///< Pairing label.
    std::atomic_bool stopping_ = false;  ///< stop() was called; waits return early.
    mutable std::mutex link_mutex_;  ///< Guards link_.
    Link link_;  ///< Port and sequence numbers.
    mutable std::mutex state_mutex_;  ///< Guards state_.
    std::condition_variable state_changed_;  ///< Signals changes of state_.
    Connection state_;  ///< Connection and pairing state.
    LayoutState layout_;  ///< Desired layout.
    InputState inputs_;  ///< Inputs to replay.
    OutputSink output_;  ///< Output report sink.
    Workers workers_;  ///< Reader and supervisor threads.
  };

  /**
   * @brief Status of the most recently started client that is still running.
   *
   * @return Status, `active == false` when no client is running.
   */
  Esp32BridgeStatus active_bridge_status();

}  // namespace lvh::detail::esp32
