/**
 * @file tests/unit/test_esp32_bridge.cpp
 * @brief Unit tests for the ESP32-S3 USB bridge protocol, client, and backend.
 */

// standard includes
#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// local includes
#include "fixtures/fixtures.hpp"
#include "platform/esp32/bytes.hpp"
#include "platform/esp32/credential_store.hpp"
#include "platform/esp32/esp32_backend.hpp"
#include "platform/esp32/esp32_bridge_protocol.hpp"
#include "platform/esp32/hmac_sha256.hpp"
#include "platform/esp32/keyboard_mouse_reports.hpp"
#include "platform/esp32/xinput_reports.hpp"

#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

#ifdef __linux__
  #include <fcntl.h>
  #include <stdlib.h>
  #include <unistd.h>
#endif

namespace protocol = lvh::detail::esp32::protocol;

namespace {

  using namespace std::chrono_literals;
  using lvh::detail::esp32::BridgeClient;
  using lvh::detail::esp32::BridgeCredential;
  using lvh::detail::esp32::BridgeTimings;
  using lvh::detail::esp32::CredentialStore;
  using lvh::detail::esp32::SerialPort;

  /**
   * @brief In-memory model of the bridge firmware's protocol behavior.
   */
  class FakeBridgeDevice: public std::enable_shared_from_this<FakeBridgeDevice> {
  public:
    struct Slot {
      std::uint8_t kind = protocol::slot::none;
      std::vector<std::uint8_t> descriptor;
      std::vector<std::uint8_t> neutral;
      std::vector<std::vector<std::uint8_t>> features;
    };

    struct Layout {
      std::uint16_t vendor_id = 0;
      std::uint16_t product_id = 0;
      std::uint32_t generation = 0;
      std::array<Slot, protocol::max_slots> slots {};
    };

    bool present() {
      std::lock_guard lock {mutex_};
      return std::chrono::steady_clock::now() >= present_after_;
    }

    int instance() {
      std::lock_guard lock {mutex_};
      return instance_;
    }

    bool write(int instance, std::span<const std::uint8_t> data) {
      std::lock_guard lock {mutex_};
      if (instance != instance_) {
        return false;
      }
      for (const auto byte : data) {
        if (byte != 0U) {
          rx_.push_back(byte);
          continue;
        }
        std::array<std::uint8_t, protocol::max_frame> frame {};
        const auto size = protocol::decode_frame(rx_, frame);
        rx_.clear();
        if (size.has_value()) {
          handle(std::span<const std::uint8_t> {frame}.first(*size + 2));
        }
      }
      changed_.notify_all();
      return true;
    }

    std::ptrdiff_t read(int instance, std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) {
      std::unique_lock lock {mutex_};
      changed_.wait_for(lock, timeout, [this, instance]() {
        return instance != instance_ || !to_host_.empty() || pending_reenumeration_;
      });
      if (instance != instance_) {
        return -1;
      }
      if (to_host_.empty() && pending_reenumeration_) {
        reenumerate_locked();
        return -1;
      }
      std::size_t count = 0;
      while (count < buffer.size() && !to_host_.empty()) {
        buffer[count++] = to_host_.front();
        to_host_.pop_front();
      }
      return static_cast<std::ptrdiff_t>(count);
    }

    void emit_output(std::uint8_t slot, std::vector<std::uint8_t> report) {
      std::vector payload {slot, protocol::report_kind::output};
      payload.insert(payload.end(), report.begin(), report.end());
      std::lock_guard lock {mutex_};
      send_locked(protocol::message::output_report, payload);
    }

    /**
     * @brief Simulate a power cycle: the bridge comes back with the boot layout.
     */
    void reboot() {
      std::lock_guard lock {mutex_};
      active_ = {};
      ++instance_;
      to_host_.clear();
      present_after_ = std::chrono::steady_clock::now() + 20ms;
      changed_.notify_all();
    }

    Layout active() {
      std::lock_guard lock {mutex_};
      return active_;
    }

    void require_pairing(bool required) {
      std::lock_guard lock {mutex_};
      behavior_.require_pairing = required;
    }

    bool pairing_pending() {
      std::lock_guard lock {mutex_};
      return pairing_.pending;
    }

    /**
     * @brief Simulate a BOOT press: confirm the open pairing request.
     */
    void press_boot() {
      std::lock_guard lock {mutex_};
      if (!pairing_.pending) {
        return;
      }
      pairing_.pending = false;
      BridgeCredential credential;
      credential.key_id.fill(static_cast<std::uint8_t>(0x10U + pairing_.pairings.size()));
      credential.secret.fill(static_cast<std::uint8_t>(0xA0U + pairing_.pairings.size()));
      pairing_.pairings.push_back(credential);
      pairing_.authorized = true;
      std::vector result {protocol::message::pair, protocol::status::ok};
      result.insert(result.end(), credential.key_id.begin(), credential.key_id.end());
      result.insert(result.end(), credential.secret.begin(), credential.secret.end());
      send_locked(protocol::message::confirm_result, result);
    }

    void revoke_all() {
      std::lock_guard lock {mutex_};
      pairing_.pairings.clear();
    }

    void reject_version(bool reject) {
      std::lock_guard lock {mutex_};
      behavior_.reject_version = reject;
    }

    void pair_status(std::uint8_t status) {
      std::lock_guard lock {mutex_};
      behavior_.pair_status = status;
    }

    int pair_requests() {
      std::lock_guard lock {mutex_};
      return counters_.pair_requests;
    }

    /**
     * @brief Simulate the BOOT window running out.
     */
    void confirm_timeout() {
      std::lock_guard lock {mutex_};
      if (!pairing_.pending) {
        return;
      }
      pairing_.pending = false;
      send_locked(protocol::message::confirm_result, std::vector<std::uint8_t> {protocol::message::pair, protocol::status::timeout});
    }

    int auth_attempts() {
      std::lock_guard lock {mutex_};
      return counters_.auth_attempts;
    }

    static constexpr std::array<std::uint8_t, protocol::device_id_size> device_id {0xA4, 0xCB, 0x8F, 0x00, 0x00, 0x01};

    int commits() {
      std::lock_guard lock {mutex_};
      return counters_.commits;
    }

    std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> inputs() {
      std::lock_guard lock {mutex_};
      return inputs_;
    }

    template<typename Predicate>
    bool wait_until(Predicate predicate, std::chrono::milliseconds timeout = 3000ms) const {
      const auto deadline = std::chrono::steady_clock::now() + timeout;
      while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
          return true;
        }
        std::this_thread::sleep_for(5ms);
      }
      return predicate();
    }

  private:
    /**
     * @brief Behavior a test configures.
     */
    struct Behavior {
      bool require_pairing = false;
      bool reject_version = false;
      std::uint8_t pair_status = protocol::status::ok;
    };

    /**
     * @brief Pairing and authentication state.
     */
    struct PairingState {
      bool authorized = false;
      bool pending = false;
      std::uint8_t nonce_counter = 0;
      std::array<std::uint8_t, protocol::nonce_size> nonce {};
      std::vector<BridgeCredential> pairings;
    };

    /**
     * @brief Request counters tests assert on.
     */
    struct Counters {
      int commits = 0;
      int pair_requests = 0;
      int auth_attempts = 0;
    };

    static bool is_layout_message(std::uint8_t type) {
      return type == protocol::message::layout_begin || type == protocol::message::layout_slot || type == protocol::message::layout_feature || type == protocol::message::layout_commit;
    }

    void ack_locked(std::uint8_t type, std::uint8_t sequence, std::uint8_t status) {
      send_locked(protocol::message::ack, std::vector {type, sequence, status});
    }

    void handle(std::span<const std::uint8_t> frame) {
      const auto type = frame[0];
      const auto sequence = frame[1];
      const auto payload = frame.subspan(2);
      if (!pairing_.authorized && is_layout_message(type)) {
        ack_locked(type, sequence, protocol::status::unauthorized);
        return;
      }

      switch (type) {
        case protocol::message::hello:
          handle_hello(sequence);
          break;
        case protocol::message::auth:
          ack_locked(type, sequence, handle_auth(payload) ? protocol::status::ok : protocol::status::unauthorized);
          break;
        case protocol::message::pair:
          ++counters_.pair_requests;
          pairing_.pending = behavior_.pair_status == protocol::status::ok;
          ack_locked(type, sequence, behavior_.pair_status);
          break;
        case protocol::message::layout_begin:
          staged_ = Layout {};
          staged_->vendor_id = lvh::detail::esp32::read_le<std::uint16_t>(payload);
          staged_->product_id = lvh::detail::esp32::read_le<std::uint16_t>(payload.subspan(2));
          ack_locked(type, sequence, protocol::status::ok);
          break;
        case protocol::message::layout_slot:
          handle_layout_slot(payload);
          ack_locked(type, sequence, protocol::status::ok);
          break;
        case protocol::message::layout_feature:
          staged_->slots[payload[0]].features.emplace_back(payload.begin() + 1, payload.end());
          ack_locked(type, sequence, protocol::status::ok);
          break;
        case protocol::message::layout_commit:
          ack_locked(type, sequence, handle_layout_commit(payload));
          break;
        case protocol::message::input_report:
          if (pairing_.authorized) {
            inputs_.emplace_back(payload[0], std::vector<std::uint8_t> {payload.begin() + 1, payload.end()});
          }
          break;
        case protocol::message::ping:
          send_locked(protocol::message::pong, std::vector<std::uint8_t> {payload.begin(), payload.end()});
          break;
        default:
          break;
      }
    }

    void handle_hello(std::uint8_t sequence) {
      if (behavior_.reject_version) {
        ack_locked(protocol::message::hello, sequence, protocol::status::version_mismatch);
        return;
      }
      pairing_.authorized = !behavior_.require_pairing;
      pairing_.nonce.fill(++pairing_.nonce_counter);
      std::vector<std::uint8_t> reply;
      lvh::detail::esp32::append_le(reply, protocol::version);
      reply.push_back(static_cast<std::uint8_t>(protocol::max_slots));
      reply.push_back(1);
      lvh::detail::esp32::append_le(reply, active_.generation);
      lvh::detail::esp32::append_le(reply, static_cast<std::uint16_t>(protocol::max_report_descriptor));
      std::uint8_t flags = 0;
      if (behavior_.require_pairing) {
        flags += protocol::auth_flag::pairing_required;
      }
      if (pairing_.authorized) {
        flags += protocol::auth_flag::authorized;
      }
      reply.push_back(flags);
      reply.insert(reply.end(), pairing_.nonce.begin(), pairing_.nonce.end());
      reply.insert(reply.end(), device_id.begin(), device_id.end());
      reply.insert(reply.end(), {'t', 'e', 's', 't'});
      send_locked(protocol::message::hello_ack, reply);
    }

    bool handle_auth(std::span<const std::uint8_t> payload) {
      ++counters_.auth_attempts;
      if (payload.size() < protocol::key_id_size + protocol::mac_size) {
        return false;
      }
      const auto key_id = payload.first(protocol::key_id_size);
      const auto valid = std::ranges::any_of(pairing_.pairings, [this, key_id, payload](const BridgeCredential &pairing) {
        if (!std::ranges::equal(pairing.key_id, key_id)) {
          return false;
        }
        const auto mac = lvh::detail::esp32::hmac_sha256(pairing.secret, lvh::detail::esp32::make_auth_message(pairing_.nonce, pairing.key_id, device_id));
        return std::ranges::equal(mac, payload.subspan(protocol::key_id_size, protocol::mac_size));
      });
      pairing_.authorized = pairing_.authorized || valid;
      return valid;
    }

    void handle_layout_slot(std::span<const std::uint8_t> payload) {
      auto &slot = staged_->slots[payload[0]];
      slot.kind = payload[1];
      const auto descriptor_size = lvh::detail::esp32::read_le<std::uint16_t>(payload.subspan(3));
      const auto descriptor = payload.subspan(5, descriptor_size);
      slot.descriptor.assign(descriptor.begin(), descriptor.end());
      const auto neutral = payload.subspan(5 + descriptor_size);
      slot.neutral.assign(neutral.begin(), neutral.end());
    }

    std::uint8_t handle_layout_commit(std::span<const std::uint8_t> payload) {
      std::array<std::uint8_t, protocol::max_slots> kinds {};
      std::ranges::transform(staged_->slots, kinds.begin(), &Slot::kind);
      if (!protocol::layout_fits(kinds)) {
        return protocol::status::invalid_layout;
      }
      staged_->generation = lvh::detail::esp32::read_le<std::uint32_t>(payload);
      pending_reenumeration_ = true;
      return protocol::status::ok;
    }

    void reenumerate_locked() {
      pending_reenumeration_ = false;
      active_ = *staged_;
      staged_.reset();
      ++counters_.commits;
      ++instance_;
      to_host_.clear();
      present_after_ = std::chrono::steady_clock::now() + 20ms;
    }

    void send_locked(std::uint8_t type, const std::vector<std::uint8_t> &payload) {
      std::array<std::uint8_t, protocol::max_encoded_frame> encoded {};
      const auto size = protocol::encode_frame(type, tx_sequence_++, payload, encoded);
      to_host_.insert(to_host_.end(), encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(size));
      changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::uint8_t> rx_;
    std::deque<std::uint8_t> to_host_;
    std::optional<Layout> staged_;
    Layout active_;
    bool pending_reenumeration_ = false;
    int instance_ = 0;
    std::uint8_t tx_sequence_ = 0;
    std::chrono::steady_clock::time_point present_after_ {};
    std::vector<std::pair<std::uint8_t, std::vector<std::uint8_t>>> inputs_;
    Behavior behavior_;
    PairingState pairing_;
    Counters counters_;
  };

  /**
   * @brief Credential store kept in memory.
   */
  class MemoryCredentialStore final: public CredentialStore {
  public:
    std::optional<BridgeCredential> load(const std::string &device_id) override {
      std::lock_guard lock {mutex_};
      const auto found = credentials_.find(device_id);
      return found != credentials_.end() ? std::optional {found->second} : std::nullopt;
    }

    bool save(const std::string &device_id, const BridgeCredential &credential) override {
      std::lock_guard lock {mutex_};
      credentials_[device_id] = credential;
      return true;
    }

    void erase(const std::string &device_id) override {
      std::lock_guard lock {mutex_};
      credentials_.erase(device_id);
    }

    std::size_t size() {
      std::lock_guard lock {mutex_};
      return credentials_.size();
    }

  private:
    std::mutex mutex_;
    std::map<std::string, BridgeCredential, std::less<>> credentials_;
  };

  /**
   * @brief Serial port bound to one enumeration of a FakeBridgeDevice.
   */
  class FakePort final: public SerialPort {
  public:
    FakePort(std::shared_ptr<FakeBridgeDevice> device, int instance):
        device_ {std::move(device)},
        instance_ {instance} {}

    bool write(std::span<const std::uint8_t> data) override {
      return device_->write(instance_, data);
    }

    std::ptrdiff_t read(std::span<std::uint8_t> buffer, std::chrono::milliseconds timeout) override {
      return device_->read(instance_, buffer, timeout);
    }

    std::string path() const override {
      return std::format("fake{}", instance_);
    }

  private:
    std::shared_ptr<FakeBridgeDevice> device_;
    int instance_;
  };

  BridgeTimings fast_timings() {
    BridgeTimings timings;
    timings.ack_timeout = 500ms;
    timings.connect_timeout = 1000ms;
    timings.reenumerate_timeout = 3000ms;
    timings.reopen_interval = 5ms;
    timings.keepalive_interval = 50ms;
    timings.silence_timeout = 2000ms;
    timings.commit_settle = 0ms;
    return timings;
  }

  struct BridgeHarness {
    std::shared_ptr<FakeBridgeDevice> device = std::make_shared<FakeBridgeDevice>();
    std::shared_ptr<MemoryCredentialStore> credentials = std::make_shared<MemoryCredentialStore>();
    std::atomic_bool attached = true;
    std::function<void(const std::string &)> on_log;  ///< Optional extra log sink, set before the first device.
    std::shared_ptr<BridgeClient> client = std::make_shared<BridgeClient>(
      [this]() -> std::unique_ptr<SerialPort> {
        if (!attached || !device->present()) {
          return nullptr;
        }
        return std::make_unique<FakePort>(device, device->instance());
      },
      [this](lvh::LogLevel, const std::string &message) {
        if (on_log) {
          on_log(message);
        }
      },
      fast_timings(),
      credentials,
      "test host"
    );
    std::unique_ptr<lvh::detail::Backend> backend = lvh::detail::esp32::create_esp32_backend(client, nullptr);

    std::vector<std::vector<std::uint8_t>> slot_inputs(std::uint8_t slot) {
      std::vector<std::vector<std::uint8_t>> reports;
      for (const auto &[input_slot, report] : device->inputs()) {
        if (input_slot == slot) {
          reports.push_back(report);
        }
      }
      return reports;
    }
  };

  /**
   * @brief Input report sizes in bytes per report id, computed by walking a HID report descriptor.
   */
  struct DescriptorSummary {
    std::array<std::size_t, 256> input_bytes {};  ///< Indexed by report id; 0 when the id has no input report.
    std::array<std::size_t, 256> output_bytes {};  ///< Indexed by report id; 0 when the id has no output report.
    int collection_depth = 0;
    bool balanced = true;
  };

  DescriptorSummary summarize_descriptor(const std::vector<std::uint8_t> &descriptor) {
    DescriptorSummary summary;
    std::array<std::size_t, 256> input_bits {};
    std::array<std::size_t, 256> output_bits {};
    std::uint32_t report_size = 0;
    std::uint32_t report_count = 0;
    std::uint8_t report_id = 0;
    for (std::size_t index = 0; index < descriptor.size();) {
      const unsigned prefix = descriptor[index];
      const std::size_t size = prefix % 4U == 3U ? 4U : prefix % 4U;
      std::uint32_t value = 0;
      for (std::size_t byte = size; byte > 0; --byte) {
        value = value * 0x100U + descriptor[index + byte];
      }
      switch (prefix - prefix % 4U) {
        case 0x74:
          report_size = value;
          break;
        case 0x94:
          report_count = value;
          break;
        case 0x84:
          report_id = static_cast<std::uint8_t>(value);
          break;
        case 0x80:
          input_bits[report_id] += report_size * report_count;
          break;
        case 0x90:
          output_bits[report_id] += report_size * report_count;
          break;
        case 0xA0:
          ++summary.collection_depth;
          break;
        case 0xC0:
          --summary.collection_depth;
          summary.balanced = summary.balanced && summary.collection_depth >= 0;
          break;
        default:
          break;
      }
      index += 1U + size;
    }
    for (std::size_t id = 0; id < input_bits.size(); ++id) {
      summary.input_bytes[id] = input_bits[id] != 0 ? 1U + (input_bits[id] + 7U) / 8U : 0U;
      summary.output_bytes[id] = output_bits[id] != 0 ? 1U + (output_bits[id] + 7U) / 8U : 0U;
    }
    return summary;
  }

  /**
   * @brief Unique directory below the test's working directory; the caller removes it.
   *
   * @param name Distinguishes tests.
   * @return Directory path, not created yet.
   */
  std::filesystem::path scratch_directory(std::string_view name) {
    return std::filesystem::current_path() / std::format("lvh-bridge-{}-{}", name, std::chrono::steady_clock::now().time_since_epoch().count());
  }

  lvh::CreateGamepadOptions options_for(lvh::DeviceProfile profile) {
    lvh::CreateGamepadOptions options;
    options.profile = std::move(profile);
    return options;
  }

  /**
   * @brief Require pairing, create an Xbox 360 pad, and confirm the pairing with BOOT.
   *
   * @param harness Test bridge.
   * @return Creation result; the pad stays alive while the caller holds it.
   */
  auto create_paired_xbox_pad(BridgeHarness &harness) {
    harness.device->require_pairing(true);
    auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
    EXPECT_TRUE(harness.device->wait_until([&harness]() {
      return harness.device->pairing_pending();
    }));
    harness.device->press_boot();
    EXPECT_TRUE(harness.device->wait_until([&harness]() {
      return harness.device->commits() == 1;
    }));
    return created;
  }

}  // namespace

TEST(Esp32BridgeProtocolTest, FrameRoundTripsPayloadWithZeros) {
  const std::vector<std::uint8_t> payload {0x00, 0x01, 0x00, 0x00, 0xFF, 0x00};
  std::array<std::uint8_t, protocol::max_encoded_frame> encoded {};
  const auto size = protocol::encode_frame(protocol::message::input_report, 7, payload, encoded);
  ASSERT_GT(size, payload.size());
  EXPECT_EQ(encoded[size - 1U], 0U);
  for (std::size_t index = 0; index + 1U < size; ++index) {
    EXPECT_NE(encoded[index], 0U);
  }

  std::array<std::uint8_t, protocol::max_frame> frame {};
  ASSERT_EQ(protocol::decode_frame(std::span {encoded}.first(size - 1U), frame), payload.size());
  EXPECT_EQ(frame[0], protocol::message::input_report);
  EXPECT_EQ(frame[1], 7U);
  EXPECT_TRUE(std::equal(payload.begin(), payload.end(), frame.begin() + 2));
}

TEST(Esp32BridgeProtocolTest, FrameRoundTripsLongRunWithoutZeros) {
  std::vector<std::uint8_t> payload(protocol::max_payload, 0x5A);
  std::array<std::uint8_t, protocol::max_encoded_frame> encoded {};
  const auto size = protocol::encode_frame(protocol::message::layout_slot, 1, payload, encoded);
  ASSERT_NE(size, 0U);
  ASSERT_LE(size, protocol::max_encoded_frame);
  std::array<std::uint8_t, protocol::max_frame> frame {};
  EXPECT_EQ(protocol::decode_frame(std::span {encoded}.first(size - 1U), frame), payload.size());
}

TEST(Esp32BridgeProtocolTest, CorruptedFrameFailsCrc) {
  const std::vector<std::uint8_t> payload {1, 2, 3, 4};
  std::array<std::uint8_t, protocol::max_encoded_frame> encoded {};
  const auto size = protocol::encode_frame(protocol::message::ping, 0, payload, encoded);
  encoded[3] = encoded[3] == 0x10U ? 0x20U : 0x10U;
  std::array<std::uint8_t, protocol::max_frame> frame {};
  EXPECT_FALSE(protocol::decode_frame(std::span {encoded}.first(size - 1U), frame).has_value());
}

TEST(Esp32BridgeProtocolTest, EndpointBudgetAllowsOneXinputBesideCdc) {
  constexpr std::array xinput_and_hid {protocol::slot::xinput, protocol::slot::hid};
  constexpr std::array two_hid {protocol::slot::hid, protocol::slot::hid};
  constexpr std::array two_xinput {protocol::slot::xinput, protocol::slot::xinput};
  EXPECT_TRUE(protocol::layout_fits(xinput_and_hid));
  EXPECT_TRUE(protocol::layout_fits(two_hid));
  EXPECT_FALSE(protocol::layout_fits(two_xinput));
}

TEST(Esp32BridgeReportTest, PacksXinputButtonsTriggersAndAxes) {
  using enum lvh::GamepadButton;
  lvh::GamepadState state;
  state.buttons.set(a);
  state.buttons.set(dpad_left);
  state.buttons.set(guide);
  state.buttons.set(back);
  state.left_trigger = 1.0F;
  state.left_stick = {1.0F, -1.0F};
  state.right_stick = {0.0F, 1.0F};

  const auto report = lvh::detail::esp32::pack_xinput_report(state);
  EXPECT_EQ(report[0], 0x00U);
  EXPECT_EQ(report[1], 0x14U);
  EXPECT_EQ(report[2], 0x04U | 0x20U);
  EXPECT_EQ(report[3], 0x10U | 0x04U);
  EXPECT_EQ(report[4], 0xFFU);
  EXPECT_EQ(report[5], 0x00U);
  EXPECT_EQ(report[6] | (report[7] << 8U), 0x7FFF);
  EXPECT_EQ(report[8] | (report[9] << 8U), 0x8000);
  EXPECT_EQ(report[10] | (report[11] << 8U), 0x0000);
  EXPECT_EQ(report[12] | (report[13] << 8U), 0x7FFF);
}

TEST(Esp32BridgeReportTest, ParsesXinputRumbleAndLeds) {
  const std::array<std::uint8_t, 8> rumble {0x00, 0x08, 0x00, 0xFF, 0x80, 0x00, 0x00, 0x00};
  const auto rumble_outputs = lvh::detail::esp32::parse_xinput_output(rumble);
  ASSERT_EQ(rumble_outputs.size(), 1U);
  EXPECT_EQ(rumble_outputs[0].kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(rumble_outputs[0].low_frequency_rumble, 0xFFFFU);
  EXPECT_EQ(rumble_outputs[0].high_frequency_rumble, 0x8080U);

  const std::array<std::uint8_t, 3> led {0x01, 0x03, 0x07};
  const auto led_outputs = lvh::detail::esp32::parse_xinput_output(led);
  ASSERT_EQ(led_outputs.size(), 1U);
  EXPECT_EQ(led_outputs[0].kind, lvh::GamepadOutputKind::player_leds);
  EXPECT_TRUE(led_outputs[0].player_leds[1]);
  EXPECT_FALSE(led_outputs[0].flashing_player_leds[1]);

  EXPECT_TRUE(lvh::detail::esp32::parse_xinput_output(std::array<std::uint8_t, 2> {0x02, 0x00}).empty());
}

TEST(Esp32BridgeBackendTest, ReportsBackendUnavailableWithoutBridge) {
  BridgeHarness harness;
  harness.attached = false;
  auto result = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  EXPECT_FALSE(result);
  EXPECT_EQ(result.status.code(), lvh::ErrorCode::backend_unavailable);
}

TEST(Esp32BridgeBackendTest, AdvertisesBridgeCapabilities) {
  BridgeHarness harness;
  const auto &caps = harness.backend->capabilities();
  EXPECT_EQ(caps.backend_name, "esp32_bridge");
  EXPECT_TRUE(caps.supports_gamepad);
  EXPECT_TRUE(caps.supports_output_reports);
  EXPECT_TRUE(caps.supports_keyboard);
  EXPECT_TRUE(caps.supports_mouse);
  EXPECT_FALSE(caps.requires_installed_driver);
  EXPECT_FALSE(harness.backend->create_touchscreen(1, {}));
}

TEST(Esp32BridgeBackendTest, XboxProfileEnumeratesXinputSlotAndForwardsReports) {
  BridgeHarness harness;
  auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_series()));
  ASSERT_TRUE(created) << created.status.message();
  ASSERT_TRUE(created.effective_profile.has_value());
  EXPECT_EQ(created.effective_profile->gamepad_kind, lvh::GamepadProfileKind::xbox_360);

  const auto layout = harness.device->active();
  EXPECT_EQ(harness.device->commits(), 1);
  EXPECT_EQ(layout.slots[0].kind, protocol::slot::xinput);
  EXPECT_EQ(layout.slots[1].kind, protocol::slot::none);
  EXPECT_EQ(layout.vendor_id, 0x303AU);
  EXPECT_EQ(layout.slots[0].neutral.size(), protocol::xinput_report_size);

  std::mutex outputs_mutex;
  std::vector<lvh::GamepadOutput> outputs;
  created.gamepad->set_output_callback([&outputs_mutex, &outputs](const lvh::GamepadOutput &output) {
    std::lock_guard lock {outputs_mutex};
    outputs.push_back(output);
  });

  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::y);
  ASSERT_TRUE(created.gamepad->submit(state, {}).ok());
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return !harness.device->inputs().empty();
  }));
  const auto [input_slot, input_report] = harness.device->inputs().back();
  EXPECT_EQ(input_slot, 0U);
  ASSERT_EQ(input_report.size(), protocol::xinput_report_size);
  EXPECT_EQ(input_report[3], 0x80U);

  harness.device->emit_output(0, {0x00, 0x08, 0x00, 0x40, 0x20, 0x00, 0x00, 0x00});
  ASSERT_TRUE(harness.device->wait_until([&]() {
    std::lock_guard lock {outputs_mutex};
    return !outputs.empty();
  }));
  std::lock_guard lock {outputs_mutex};
  EXPECT_EQ(outputs[0].kind, lvh::GamepadOutputKind::rumble);
  EXPECT_EQ(outputs[0].low_frequency_rumble, 0x40U * 257U);
}

TEST(Esp32BridgeBackendTest, DualSenseUsesHidSlotWithSonyIdentityAndFeatureReports) {
  BridgeHarness harness;
  auto xbox = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(xbox) << xbox.status.message();
  auto dualsense = harness.backend->create_gamepad(2, options_for(lvh::profiles::dualsense_bluetooth()));
  ASSERT_TRUE(dualsense) << dualsense.status.message();
  ASSERT_TRUE(dualsense.effective_profile.has_value());
  EXPECT_EQ(dualsense.effective_profile->bus_type, lvh::BusType::usb);

  const auto usb_profile = lvh::profiles::dualsense_usb();
  const auto layout = harness.device->active();
  EXPECT_EQ(harness.device->commits(), 2);
  EXPECT_EQ(layout.slots[0].kind, protocol::slot::xinput);
  EXPECT_EQ(layout.slots[1].kind, protocol::slot::hid);
  EXPECT_EQ(layout.vendor_id, usb_profile.vendor_id);
  EXPECT_EQ(layout.product_id, usb_profile.product_id);
  EXPECT_EQ(layout.slots[1].descriptor, usb_profile.report_descriptor);
  // DualSense reports carry a sequence counter and timestamp, so compare shape only.
  const auto neutral = lvh::reports::pack_input_report(usb_profile, {});
  ASSERT_EQ(layout.slots[1].neutral.size(), neutral.size());
  EXPECT_EQ(layout.slots[1].neutral[0], neutral[0]);
  ASSERT_EQ(layout.slots[1].features.size(), 3U);
  EXPECT_EQ(layout.slots[1].features[0][0], 0x05U);
  EXPECT_EQ(layout.slots[1].features[1][0], 0x09U);
  EXPECT_EQ(layout.slots[1].features[2][0], 0x20U);
}

TEST(Esp32BridgeBackendTest, RejectsSecondXinputPadAndThirdPad) {
  BridgeHarness harness;
  auto first = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(first) << first.status.message();
  auto second_xbox = harness.backend->create_gamepad(2, options_for(lvh::profiles::xbox_one()));
  EXPECT_FALSE(second_xbox);
  EXPECT_EQ(second_xbox.status.code(), lvh::ErrorCode::unsupported_profile);
  EXPECT_EQ(harness.device->commits(), 1);

  auto second = harness.backend->create_gamepad(3, options_for(lvh::profiles::dualshock4()));
  ASSERT_TRUE(second) << second.status.message();
  auto third = harness.backend->create_gamepad(4, options_for(lvh::profiles::dualshock4()));
  EXPECT_FALSE(third);
  EXPECT_EQ(third.status.code(), lvh::ErrorCode::unsupported_profile);
}

TEST(Esp32BridgeBackendTest, RejectsMixedHidIdentities) {
  BridgeHarness harness;
  auto dualsense = harness.backend->create_gamepad(1, options_for(lvh::profiles::dualsense()));
  ASSERT_TRUE(dualsense) << dualsense.status.message();
  auto dualshock = harness.backend->create_gamepad(2, options_for(lvh::profiles::dualshock4()));
  EXPECT_FALSE(dualshock);
  EXPECT_EQ(dualshock.status.code(), lvh::ErrorCode::unsupported_profile);
}

TEST(Esp32BridgeBackendTest, ClosingGamepadReleasesSlot) {
  BridgeHarness harness;
  auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(created) << created.status.message();
  ASSERT_TRUE(created.gamepad->close().ok());
  EXPECT_EQ(harness.device->commits(), 2);
  EXPECT_EQ(harness.device->active().slots[0].kind, protocol::slot::none);
  EXPECT_EQ(created.gamepad->submit({}, {}).code(), lvh::ErrorCode::device_closed);

  auto again = harness.backend->create_gamepad(2, options_for(lvh::profiles::xbox_360()));
  EXPECT_TRUE(again) << again.status.message();
}

TEST(Esp32BridgeBackendTest, RestoresLayoutAndLatestInputAfterBridgeReboot) {
  BridgeHarness harness;
  auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(created) << created.status.message();
  lvh::GamepadState state;
  state.buttons.set(lvh::GamepadButton::b);
  ASSERT_TRUE(created.gamepad->submit(state, {}).ok());

  harness.device->reboot();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->commits() >= 2 && harness.device->active().slots[0].kind == protocol::slot::xinput;
  }));
  ASSERT_TRUE(harness.device->wait_until([&]() {
    const auto inputs = harness.device->inputs();
    return inputs.size() >= 2U && inputs.back().second.size() == protocol::xinput_report_size && inputs.back().second[3] == 0x20U;
  }));
}

TEST(Esp32BridgePairingTest, HmacSha256MatchesRfc4231) {
  const std::string key = "Jefe";
  const std::string message = "what do ya want for nothing?";
  const auto mac = lvh::detail::esp32::hmac_sha256(
    std::span<const std::uint8_t> {reinterpret_cast<const std::uint8_t *>(key.data()), key.size()},
    std::span<const std::uint8_t> {reinterpret_cast<const std::uint8_t *>(message.data()), message.size()}
  );
  const std::array<std::uint8_t, 32> expected {
    0x5b,
    0xdc,
    0xc1,
    0x46,
    0xbf,
    0x60,
    0x75,
    0x4e,
    0x6a,
    0x04,
    0x24,
    0x26,
    0x08,
    0x95,
    0x75,
    0xc7,
    0x5a,
    0x00,
    0x3f,
    0x08,
    0x9d,
    0x27,
    0x39,
    0x83,
    0x9d,
    0xec,
    0x58,
    0xb9,
    0x64,
    0xec,
    0x38,
    0x43,
  };
  EXPECT_EQ(mac, expected);
}

TEST(Esp32BridgePairingTest, UnpairedBridgeDefersLayoutUntilBootPress) {
  BridgeHarness harness;
  harness.device->require_pairing(true);
  auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(created) << created.status.message();
  EXPECT_TRUE(harness.client->status().awaiting_pairing);
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->pairing_pending();
  }));
  EXPECT_EQ(harness.device->commits(), 0);
  EXPECT_TRUE(lvh::esp32_bridge_status().awaiting_pairing);

  harness.device->press_boot();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->commits() == 1 && harness.device->active().slots[0].kind == protocol::slot::xinput;
  }));
  EXPECT_FALSE(harness.client->status().awaiting_pairing);
  EXPECT_EQ(harness.credentials->size(), 1U);
  EXPECT_EQ(harness.client->status().device_id, "A4CB8F000001");
  const auto status = lvh::esp32_bridge_status();
  EXPECT_TRUE(status.active);
  EXPECT_TRUE(status.pairing_required);
  EXPECT_FALSE(status.awaiting_pairing);
  EXPECT_EQ(status.device_id, "A4CB8F000001");
}

TEST(Esp32BridgePairingTest, StoredPairingAuthenticatesAfterReboot) {
  BridgeHarness harness;
  auto created = create_paired_xbox_pad(harness);
  ASSERT_TRUE(created) << created.status.message();

  // The reconnect after the commit authenticates with the stored pairing.
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->auth_attempts() >= 1 && harness.client->status().connected;
  }));
  const auto attempts = harness.device->auth_attempts();
  harness.device->reboot();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->commits() == 2 && harness.device->active().slots[0].kind == protocol::slot::xinput;
  }));
  EXPECT_GT(harness.device->auth_attempts(), attempts);
  EXPECT_FALSE(harness.device->pairing_pending());
}

TEST(Esp32BridgePairingTest, RevokedPairingAsksToPairAgain) {
  BridgeHarness harness;
  auto created = create_paired_xbox_pad(harness);
  ASSERT_TRUE(created) << created.status.message();

  harness.device->revoke_all();
  harness.device->reboot();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->pairing_pending();
  }));
  EXPECT_EQ(harness.credentials->size(), 0U);
  EXPECT_TRUE(harness.client->status().awaiting_pairing);
}

TEST(Esp32BridgePairingTest, FileStoreRoundTripsCredential) {
  const auto directory = scratch_directory("test");
  auto store = lvh::detail::esp32::make_file_credential_store(directory.string());
  BridgeCredential credential;
  credential.key_id.fill(0x42);
  credential.secret.fill(0x99);
  ASSERT_TRUE(store->save("A4CB8F000001", credential));
  const auto loaded = store->load("A4CB8F000001");
  ASSERT_TRUE(loaded.has_value());
  EXPECT_EQ(loaded->key_id, credential.key_id);
  EXPECT_EQ(loaded->secret, credential.secret);
  store->erase("A4CB8F000001");
  EXPECT_FALSE(store->load("A4CB8F000001").has_value());
  std::filesystem::remove_all(directory);
}

TEST(Esp32BridgeKeyboardMouseReportTest, DescriptorMatchesPackedReportSizes) {
  const auto summary = summarize_descriptor(lvh::detail::esp32::keyboard_mouse_report_descriptor());
  EXPECT_TRUE(summary.balanced);
  EXPECT_EQ(summary.collection_depth, 0);
  EXPECT_EQ(std::ranges::count_if(summary.input_bytes, [](std::size_t bytes) {
              return bytes != 0;
            }),
            3);
  EXPECT_EQ(summary.input_bytes[protocol::km::keyboard_report_id], protocol::km::keyboard_report_size);
  EXPECT_EQ(summary.input_bytes[protocol::km::relative_report_id], protocol::km::relative_report_size);
  EXPECT_EQ(summary.input_bytes[protocol::km::absolute_report_id], protocol::km::absolute_report_size);
  EXPECT_EQ(summary.output_bytes[protocol::km::keyboard_report_id], 2U);
  EXPECT_LE(lvh::detail::esp32::keyboard_mouse_report_descriptor().size(), std::size_t {protocol::max_report_descriptor});
}

TEST(Esp32BridgeKeyboardMouseReportTest, NeutralReportsAreLengthPrefixed) {
  const auto neutral = lvh::detail::esp32::keyboard_mouse_neutral_reports();
  ASSERT_EQ(neutral.size(), 2U + protocol::km::keyboard_report_size + protocol::km::relative_report_size);
  EXPECT_EQ(neutral[0], protocol::km::keyboard_report_size);
  EXPECT_EQ(neutral[1], protocol::km::keyboard_report_id);
  EXPECT_EQ(neutral[1U + protocol::km::keyboard_report_size], protocol::km::relative_report_size);
  EXPECT_EQ(neutral[2U + protocol::km::keyboard_report_size], protocol::km::relative_report_id);
}

TEST(Esp32BridgeKeyboardMouseReportTest, MapsVirtualKeysToHidUsages) {
  using lvh::detail::esp32::hid_usage_from_virtual_key;
  EXPECT_EQ(hid_usage_from_virtual_key(0x41, false), 0x04U);  // A
  EXPECT_EQ(hid_usage_from_virtual_key(0x5A, false), 0x1DU);  // Z
  EXPECT_EQ(hid_usage_from_virtual_key(0x31, false), 0x1EU);  // 1
  EXPECT_EQ(hid_usage_from_virtual_key(0x30, false), 0x27U);  // 0
  EXPECT_EQ(hid_usage_from_virtual_key(0x60, false), 0x62U);  // Keypad 0
  EXPECT_EQ(hid_usage_from_virtual_key(0x69, false), 0x61U);  // Keypad 9
  EXPECT_EQ(hid_usage_from_virtual_key(0x70, false), 0x3AU);  // F1
  EXPECT_EQ(hid_usage_from_virtual_key(0x87, false), 0x73U);  // F24
  EXPECT_EQ(hid_usage_from_virtual_key(0x0D, false), 0x28U);  // Enter
  EXPECT_EQ(hid_usage_from_virtual_key(0x0D, true), 0x58U);  // Keypad Enter
  EXPECT_EQ(hid_usage_from_virtual_key(0x11, true), 0xE4U);  // Right Control
  EXPECT_EQ(hid_usage_from_virtual_key(0xA5, false), 0xE6U);  // Right Alt
  EXPECT_EQ(hid_usage_from_virtual_key(0xDE, false), 0x34U);  // '
  EXPECT_FALSE(hid_usage_from_virtual_key(0x01, false).has_value());  // Left mouse button
}

TEST(Esp32BridgeKeyboardMouseReportTest, MapsAsciiToUsLayout) {
  using lvh::detail::esp32::hid_usage_from_ascii;
  EXPECT_EQ(hid_usage_from_ascii('a'), (std::pair<std::uint8_t, bool> {0x04, false}));
  EXPECT_EQ(hid_usage_from_ascii('Q'), (std::pair<std::uint8_t, bool> {0x14, true}));
  EXPECT_EQ(hid_usage_from_ascii(')'), (std::pair<std::uint8_t, bool> {0x27, true}));
  EXPECT_EQ(hid_usage_from_ascii('?'), (std::pair<std::uint8_t, bool> {0x38, true}));
  EXPECT_FALSE(hid_usage_from_ascii('\x7F').has_value());
}

TEST(Esp32BridgeKeyboardMouseReportTest, KeyboardStateTracksModifiersAndRollover) {
  lvh::detail::esp32::KeyboardReportState state;
  EXPECT_TRUE(state.apply(0xE1, true));  // Left Shift
  EXPECT_FALSE(state.apply(0xE1, true));
  for (std::uint8_t usage = 0x04; usage < 0x0B; ++usage) {
    EXPECT_TRUE(state.apply(usage, true));
  }
  auto report = state.report();
  EXPECT_EQ(report[0], protocol::km::keyboard_report_id);
  EXPECT_EQ(report[1], 0x02U);
  // The seventh key displaces the oldest.
  EXPECT_EQ(report[3], 0x05U);
  EXPECT_EQ(report[8], 0x0AU);
  EXPECT_FALSE(state.apply(0x04, false));
  EXPECT_TRUE(state.apply(0x05, false));
  EXPECT_TRUE(state.apply(0xE1, false));
  report = state.report_with(0x2C, true);
  EXPECT_EQ(report[1], 0x02U);
  EXPECT_EQ(report[8], 0x2CU);
}

TEST(Esp32BridgeKeyboardMouseReportTest, ScalesAbsolutePointerThroughViewport) {
  using lvh::detail::esp32::scale_absolute_pointer_axis;
  EXPECT_EQ(scale_absolute_pointer_axis(0.0F, 1920, 0, 0, 0, 0), 0U);
  EXPECT_EQ(scale_absolute_pointer_axis(1920.0F, 1920, 0, 0, 0, 0), 32767U);
  EXPECT_EQ(scale_absolute_pointer_axis(960.0F, 1920, 0, 0, 0, 0), 16384U);
  EXPECT_EQ(scale_absolute_pointer_axis(-5.0F, 1920, 0, 0, 0, 0), 0U);
  // Right half of a 3840-wide desktop that starts at -1920.
  EXPECT_EQ(scale_absolute_pointer_axis(0.0F, 1920, 0, 1920, -1920, 3840), 16384U);
  EXPECT_EQ(scale_absolute_pointer_axis(1920.0F, 1920, 0, 1920, -1920, 3840), 32758U);
  EXPECT_EQ(scale_absolute_pointer_axis(10.0F, 0, 0, 0, 0, 0), 0U);
}

TEST(Esp32BridgeKeyboardMouseTest, KeyboardAndMouseShareOneSlot) {
  BridgeHarness harness;
  auto keyboard = harness.backend->create_keyboard(1, {});
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  auto mouse = harness.backend->create_mouse(2, {});
  ASSERT_TRUE(mouse) << mouse.status.message();
  EXPECT_EQ(harness.device->commits(), 1);
  const auto layout = harness.device->active();
  EXPECT_EQ(layout.slots[0].kind, protocol::slot::keyboard_mouse);
  EXPECT_EQ(layout.slots[0].descriptor, lvh::detail::esp32::keyboard_mouse_report_descriptor());
  EXPECT_EQ(layout.slots[0].neutral, lvh::detail::esp32::keyboard_mouse_neutral_reports());
  EXPECT_EQ(layout.slots[1].kind, protocol::slot::none);
  EXPECT_FALSE(harness.backend->create_keyboard(3, {}));
}

TEST(Esp32BridgeKeyboardMouseTest, KeyboardSendsReportsOnlyOnChange) {
  BridgeHarness harness;
  auto keyboard = harness.backend->create_keyboard(1, {});
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  ASSERT_TRUE(keyboard.keyboard->submit({.key_code = 0xA0, .pressed = true}).ok());
  ASSERT_TRUE(keyboard.keyboard->submit({.key_code = 0x41, .pressed = true}).ok());
  ASSERT_TRUE(keyboard.keyboard->submit({.key_code = 0x41, .pressed = true}).ok());
  ASSERT_TRUE(keyboard.keyboard->submit({.key_code = 0x41, .pressed = false}).ok());
  EXPECT_EQ(keyboard.keyboard->submit({.key_code = 0x01, .pressed = true}).code(), lvh::ErrorCode::invalid_argument);
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.slot_inputs(0).size() >= 3U;
  }));
  const auto reports = harness.slot_inputs(0);
  ASSERT_EQ(reports.size(), 3U);
  EXPECT_EQ(reports[0], (std::vector<std::uint8_t> {1, 0x02, 0, 0, 0, 0, 0, 0, 0}));
  EXPECT_EQ(reports[1], (std::vector<std::uint8_t> {1, 0x02, 0, 0x04, 0, 0, 0, 0, 0}));
  EXPECT_EQ(reports[2], (std::vector<std::uint8_t> {1, 0x02, 0, 0, 0, 0, 0, 0, 0}));
}

TEST(Esp32BridgeKeyboardMouseTest, TypesAsciiTextAndRejectsUnicodeWithoutFallback) {
  BridgeHarness harness;
  auto keyboard = harness.backend->create_keyboard(1, {});
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  ASSERT_TRUE(keyboard.keyboard->type_text({.text = "aA"}).ok());
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.slot_inputs(0).size() >= 4U;
  }));
  const auto reports = harness.slot_inputs(0);
  EXPECT_EQ(reports[0][3], 0x04U);
  EXPECT_EQ(reports[0][1], 0x00U);
  EXPECT_EQ(reports[1][3], 0x00U);
  EXPECT_EQ(reports[2][1], 0x02U);
  EXPECT_EQ(reports[2][3], 0x04U);
  EXPECT_EQ(reports[3][1], 0x00U);
  EXPECT_EQ(keyboard.keyboard->type_text({.text = "\xC3\xA9"}).code(), lvh::ErrorCode::unsupported_profile);
}

TEST(Esp32BridgeKeyboardMouseTest, MouseSendsRelativeButtonScrollAndAbsoluteReports) {
  BridgeHarness harness;
  lvh::CreateMouseOptions options;
  options.desktop = {.offset_x = 0, .offset_y = 0, .width = 1920, .height = 1080};
  options.viewport = {.offset_x = 0, .offset_y = 0, .width = 1920, .height = 1080};
  auto mouse = harness.backend->create_mouse(1, options);
  ASSERT_TRUE(mouse) << mouse.status.message();
  auto &device = *mouse.mouse;
  ASSERT_TRUE(device.submit({.kind = lvh::MouseEventKind::relative_motion, .x = 40000, .y = -3}).ok());
  ASSERT_TRUE(device.submit({.kind = lvh::MouseEventKind::button, .button = lvh::MouseButton::right, .pressed = true}).ok());
  ASSERT_TRUE(device.submit({.kind = lvh::MouseEventKind::vertical_scroll, .high_resolution_scroll = 60}).ok());
  ASSERT_TRUE(device.submit({.kind = lvh::MouseEventKind::vertical_scroll, .high_resolution_scroll = 60}).ok());
  ASSERT_TRUE(device.submit({.kind = lvh::MouseEventKind::horizontal_scroll, .high_resolution_scroll = -240}).ok());
  ASSERT_TRUE(device.submit({.kind = lvh::MouseEventKind::absolute_motion, .x = 1920, .y = 0, .width = 1920, .height = 1080}).ok());
  ASSERT_TRUE(device.submit({.kind = lvh::MouseEventKind::button, .button = lvh::MouseButton::right, .pressed = false}).ok());
  EXPECT_EQ(device.submit({.kind = lvh::MouseEventKind::absolute_motion, .x = 1, .y = 1}).code(), lvh::ErrorCode::invalid_argument);

  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.slot_inputs(0).size() >= 7U;
  }));
  const auto reports = harness.slot_inputs(0);
  ASSERT_EQ(reports.size(), 7U);
  // 40000 splits into 32767 + 7233.
  EXPECT_EQ(reports[0], (std::vector<std::uint8_t> {2, 0, 0xFF, 0x7F, 0xFD, 0xFF, 0, 0}));
  EXPECT_EQ(reports[1], (std::vector<std::uint8_t> {2, 0, 0x41, 0x1C, 0, 0, 0, 0}));
  EXPECT_EQ(reports[2], (std::vector<std::uint8_t> {2, 0x04, 0, 0, 0, 0, 0, 0}));
  // Two half detents make one wheel click.
  EXPECT_EQ(reports[3], (std::vector<std::uint8_t> {2, 0x04, 0, 0, 0, 0, 1, 0}));
  EXPECT_EQ(reports[4], (std::vector<std::uint8_t> {2, 0x04, 0, 0, 0, 0, 0, 0xFE}));
  EXPECT_EQ(reports[5][0], protocol::km::absolute_report_id);
  EXPECT_EQ(reports[5][1], 0x04U);
  EXPECT_EQ(reports[5][2] | (reports[5][3] << 8U), 32750);
  // After absolute motion, buttons travel with the absolute position.
  EXPECT_EQ(reports[6], (std::vector<std::uint8_t> {3, 0, reports[5][2], reports[5][3], 0, 0}));
}

TEST(Esp32BridgeKeyboardMouseTest, KeyboardMouseFitsBesideXinputButNotTwoPads) {
  BridgeHarness harness;
  auto keyboard = harness.backend->create_keyboard(1, {});
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  auto xbox = harness.backend->create_gamepad(2, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(xbox) << xbox.status.message();
  const auto layout = harness.device->active();
  EXPECT_EQ(layout.slots[0].kind, protocol::slot::keyboard_mouse);
  EXPECT_EQ(layout.slots[1].kind, protocol::slot::xinput);
  auto second_pad = harness.backend->create_gamepad(3, options_for(lvh::profiles::dualsense()));
  EXPECT_FALSE(second_pad);
  EXPECT_EQ(second_pad.status.code(), lvh::ErrorCode::unsupported_profile);
  // The mouse joins the existing keyboard/mouse slot without re-enumerating.
  auto mouse = harness.backend->create_mouse(4, {});
  EXPECT_TRUE(mouse) << mouse.status.message();
  EXPECT_EQ(harness.device->commits(), 2);
}

TEST(Esp32BridgeKeyboardMouseTest, SlotStaysUntilKeyboardAndMouseClose) {
  BridgeHarness harness;
  auto keyboard = harness.backend->create_keyboard(1, {});
  auto mouse = harness.backend->create_mouse(2, {});
  ASSERT_TRUE(keyboard && mouse);
  ASSERT_TRUE(keyboard.keyboard->submit({.key_code = 0x41, .pressed = true}).ok());
  ASSERT_TRUE(keyboard.keyboard->close().ok());
  EXPECT_EQ(harness.device->commits(), 1);
  // Closing releases held keys before the slot goes away.
  ASSERT_TRUE(harness.device->wait_until([&]() {
    const auto reports = harness.slot_inputs(0);
    return !reports.empty() && reports.back() == std::vector<std::uint8_t> {1, 0, 0, 0, 0, 0, 0, 0, 0};
  }));
  ASSERT_TRUE(mouse.mouse->close().ok());
  EXPECT_EQ(harness.device->commits(), 2);
  EXPECT_EQ(harness.device->active().slots[0].kind, protocol::slot::none);
}

TEST(Esp32BridgeKeyboardMouseTest, FullBridgeFailsWithoutFallbackAndUsesFallbackWhenPresent) {
  BridgeHarness harness;
  auto first = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  auto second = harness.backend->create_gamepad(2, options_for(lvh::profiles::dualsense()));
  ASSERT_TRUE(first && second);
  auto keyboard = harness.backend->create_keyboard(3, {});
  EXPECT_FALSE(keyboard);
  EXPECT_EQ(keyboard.status.code(), lvh::ErrorCode::unsupported_profile);

  lvh::RuntimeOptions fake_options;
  fake_options.backend = lvh::BackendKind::fake;
  BridgeHarness with_fallback;
  with_fallback.backend = lvh::detail::esp32::create_esp32_backend(with_fallback.client, lvh::detail::create_backend(fake_options));
  ASSERT_TRUE(with_fallback.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360())));
  ASSERT_TRUE(with_fallback.backend->create_gamepad(2, options_for(lvh::profiles::dualsense())));
  auto fallback_keyboard = with_fallback.backend->create_keyboard(3, {});
  EXPECT_TRUE(fallback_keyboard) << fallback_keyboard.status.message();
  auto fallback_mouse = with_fallback.backend->create_mouse(4, {});
  EXPECT_TRUE(fallback_mouse) << fallback_mouse.status.message();
}

TEST(Esp32BridgeKeyboardMouseTest, UnicodeTextUsesFallbackKeyboard) {
  lvh::RuntimeOptions fake_options;
  fake_options.backend = lvh::BackendKind::fake;
  BridgeHarness harness;
  harness.backend = lvh::detail::esp32::create_esp32_backend(harness.client, lvh::detail::create_backend(fake_options));
  auto keyboard = harness.backend->create_keyboard(1, {});
  ASSERT_TRUE(keyboard) << keyboard.status.message();
  EXPECT_TRUE(keyboard.keyboard->type_text({.text = "\xC3\xA9"}).ok());
  EXPECT_TRUE(harness.slot_inputs(0).empty());
}

TEST(Esp32BridgeKeyboardMouseTest, KeyboardMouseReportsAreNotReplayedAfterReboot) {
  BridgeHarness harness;
  auto mouse = harness.backend->create_mouse(1, {});
  ASSERT_TRUE(mouse) << mouse.status.message();
  ASSERT_TRUE(mouse.mouse->submit({.kind = lvh::MouseEventKind::relative_motion, .x = 5, .y = 5}).ok());
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.slot_inputs(0).size() == 1U;
  }));
  harness.device->reboot();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->commits() >= 2 && harness.device->active().slots[0].kind == protocol::slot::keyboard_mouse;
  }));
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(harness.slot_inputs(0).size(), 1U);
}

TEST(Esp32BridgeSelectionTest, PlatformDefaultPicksBridgeFromEnvironmentPortOrDetection) {
  using lvh::detail::esp32::select_bridge;
  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::platform_default;
  int probes = 0;
  const auto present = [&probes]() {
    ++probes;
    return true;
  };
  const auto absent = []() {
    return false;
  };
  const auto port_of = [](const auto &selection) {
    return selection.has_value() ? std::optional<std::string> {selection->port} : std::nullopt;
  };
  EXPECT_FALSE(select_bridge(options, "", "", absent).has_value());
  EXPECT_EQ(port_of(select_bridge(options, "", "", present)), std::optional<std::string> {""});
  EXPECT_TRUE(select_bridge(options, "", "", present)->implicit);
  EXPECT_FALSE(select_bridge(options, "platform", "COM5", present).has_value());
  EXPECT_EQ(port_of(select_bridge(options, "esp32_bridge", "", absent)), std::optional<std::string> {""});
  EXPECT_EQ(port_of(select_bridge(options, "", "COM5", absent)), std::optional<std::string> {"COM5"});
  options.esp32_bridge.port = "/dev/ttyACM1";
  EXPECT_EQ(port_of(select_bridge(options, "", "", absent)), std::optional<std::string> {"/dev/ttyACM1"});
  EXPECT_EQ(probes, 2);
  options.esp32_bridge.autodetect = false;
  EXPECT_FALSE(select_bridge(options, "esp32_bridge", "COM5", present).has_value());
}

TEST(Esp32BridgeSelectionTest, ExplicitBridgeKindHonorsPortOverrideAndNeverFallsBack) {
  using lvh::detail::esp32::select_bridge;
  lvh::RuntimeOptions options;
  options.backend = lvh::BackendKind::esp32_bridge;
  options.esp32_bridge.autodetect = false;
  options.esp32_bridge.port = "COM3";
  const auto absent = []() {
    return false;
  };
  const auto explicit_selection = select_bridge(options, "platform", "COM7", absent);
  ASSERT_TRUE(explicit_selection.has_value());
  EXPECT_EQ(explicit_selection->port, "COM7");
  EXPECT_FALSE(explicit_selection->implicit);

  options.backend = lvh::BackendKind::fake;
  EXPECT_FALSE(select_bridge(options, "esp32_bridge", "COM7", absent).has_value());
}

TEST(Esp32BridgeClientTest, VersionMismatchFailsStartWithAFirmwareHint) {
  BridgeHarness harness;
  harness.device->reject_version(true);
  const auto status = harness.client->start();
  EXPECT_FALSE(status.ok());
  EXPECT_NE(status.message().find("protocol version"), std::string::npos) << status.message();
  EXPECT_FALSE(lvh::esp32_bridge_status().active);
}

TEST(Esp32BridgeClientTest, StatusTracksRunningClientsOnly) {
  EXPECT_FALSE(lvh::esp32_bridge_status().active);
  {
    BridgeHarness first;
    ASSERT_TRUE(first.client->start().ok());
    {
      BridgeHarness second;
      ASSERT_TRUE(second.client->start().ok());
      EXPECT_TRUE(lvh::esp32_bridge_status().active);
    }
    // The first client is still running after the second one is gone.
    EXPECT_TRUE(lvh::esp32_bridge_status().active);
    EXPECT_TRUE(lvh::esp32_bridge_status().connected);
  }
  EXPECT_FALSE(lvh::esp32_bridge_status().active);
}

TEST(Esp32BridgeClientTest, LoggerMayQueryTheStatusDuringPairing) {
  BridgeHarness harness;
  std::atomic_int queries = 0;
  harness.on_log = [&queries](const std::string &) {
    static_cast<void>(lvh::esp32_bridge_status());
    ++queries;
  };
  harness.device->require_pairing(true);
  auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(created) << created.status.message();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->pairing_pending();
  }));
  harness.device->press_boot();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->commits() == 1 && harness.client->status().connected;
  }));
  EXPECT_GT(queries.load(), 0);
}

TEST(Esp32BridgeClientTest, StopReturnsPromptlyWhileALayoutWaitsForTheBridge) {
  BridgeHarness harness;
  ASSERT_TRUE(harness.client->start().ok());
  harness.attached = false;
  harness.device->reboot();
  std::jthread release {[&harness]() {
    lvh::detail::esp32::BridgeLayout layout;
    static_cast<void>(harness.client->apply_layout(layout));
  }};
  std::this_thread::sleep_for(50ms);
  const auto started = std::chrono::steady_clock::now();
  harness.client->stop();
  release.join();
  EXPECT_LT(std::chrono::steady_clock::now() - started, 500ms);
}

TEST(Esp32BridgePairingTest, PairingRequestIsRepeatedAfterTheBootWindowEnds) {
  BridgeHarness harness;
  harness.device->require_pairing(true);
  ASSERT_TRUE(harness.client->start().ok());
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->pairing_pending();
  }));
  const auto requests = harness.device->pair_requests();
  harness.device->confirm_timeout();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->pair_requests() > requests && harness.device->pairing_pending();
  }));
  EXPECT_TRUE(harness.client->status().awaiting_pairing);
}

TEST(Esp32BridgePairingTest, BusyBridgeIsAskedAgain) {
  BridgeHarness harness;
  harness.device->require_pairing(true);
  harness.device->pair_status(protocol::status::busy);
  ASSERT_TRUE(harness.client->start().ok());
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->pair_requests() >= 1;
  }));
  harness.device->pair_status(protocol::status::ok);
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.device->pairing_pending();
  }));
}

TEST(Esp32BridgeBackendTest, ReleasedPadStaysGoneWhenTheBridgeReturns) {
  BridgeHarness harness;
  auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(created) << created.status.message();

  harness.attached = false;
  harness.device->reboot();
  ASSERT_TRUE(harness.device->wait_until([&]() {
    return !harness.client->status().connected;
  }));
  ASSERT_TRUE(created.gamepad->close().ok());
  harness.attached = true;

  ASSERT_TRUE(harness.device->wait_until([&]() {
    return harness.client->status().connected && harness.device->commits() >= 2;
  }));
  std::this_thread::sleep_for(200ms);
  EXPECT_EQ(harness.device->active().slots[0].kind, protocol::slot::none);
}

TEST(Esp32BridgeBackendTest, ImplicitBridgeFallsBackForGamepadsWhenNoneAnswers) {
  BridgeHarness harness;
  harness.attached = false;
  auto backend = lvh::detail::esp32::create_esp32_backend(harness.client, lvh::detail::create_backend(lvh::RuntimeOptions {}), true);
  auto created = backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
  ASSERT_TRUE(created) << created.status.message();
  for (const auto &node : created.gamepad->device_nodes()) {
    EXPECT_EQ(node.path.find("esp32-bridge:"), std::string::npos);
  }
  EXPECT_FALSE(lvh::esp32_bridge_status().active);
}

TEST(Esp32BridgeBackendTest, TeardownWhileOutputReportsArrive) {
  for (int round = 0; round < 5; ++round) {
    BridgeHarness harness;
    auto created = harness.backend->create_gamepad(1, options_for(lvh::profiles::xbox_360()));
    ASSERT_TRUE(created) << created.status.message();
    std::atomic_int outputs = 0;
    created.gamepad->set_output_callback([&outputs](const lvh::GamepadOutput &) {
      ++outputs;
    });
    std::jthread emitter {[device = harness.device](std::stop_token stop) {
      while (!stop.stop_requested()) {
        device->emit_output(0, {0x00, 0x08, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00});
        std::this_thread::sleep_for(1ms);
      }
    }};
    std::this_thread::sleep_for(20ms);
    created.gamepad.reset();
    harness.backend.reset();
    harness.client.reset();
  }
}

TEST(Esp32BridgeCredentialStoreTest, RejectsCorruptFilesAndKeepsSecretsPrivate) {
  const auto directory = scratch_directory("corrupt");
  std::filesystem::create_directories(directory);
  {
    std::ofstream file {directory / "bridge-A4CB8F000002.key"};
    file << "zz 1234\n";
  }
  auto store = lvh::detail::esp32::make_file_credential_store(directory.string());
  EXPECT_FALSE(store->load("A4CB8F000002").has_value());

  BridgeCredential credential;
  credential.key_id.fill(0x01);
  ASSERT_TRUE(store->save("A4CB8F000002", credential));
  ASSERT_TRUE(store->load("A4CB8F000002").has_value());
#ifndef _WIN32
  const auto permissions = std::filesystem::status(directory / "bridge-A4CB8F000002.key").permissions();
  EXPECT_EQ(permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all), std::filesystem::perms::none);
#endif
  std::filesystem::remove_all(directory);
}

TEST(Esp32BridgePairingTest, ClientNameAddsTheHostAndFitsTheBridgeLimit) {
  using lvh::detail::esp32::pairing_client_name;
  EXPECT_EQ(pairing_client_name("Sunshine", "DESKTOP-1"), "Sunshine on DESKTOP-1");
  EXPECT_EQ(pairing_client_name("", ""), "libvirtualhid");
  EXPECT_EQ(pairing_client_name(std::string(40, 'a'), "host").size(), protocol::max_pairing_name);
}

#ifdef __linux__
TEST(Esp32BridgeSerialPortTest, OpenedPortIsExclusive) {
  const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
  ASSERT_GE(master, 0);
  ASSERT_EQ(::grantpt(master), 0);
  ASSERT_EQ(::unlockpt(master), 0);
  const std::string path = ::ptsname(master);
  auto port = lvh::detail::esp32::open_serial_port(path);
  ASSERT_NE(port, nullptr);
  const int second = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  EXPECT_LT(second, 0);
  EXPECT_EQ(errno, EBUSY);
  if (second >= 0) {
    ::close(second);
  }
  port.reset();
  ::close(master);
}
#endif
