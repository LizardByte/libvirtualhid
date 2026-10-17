/**
 * @file src/platform/esp32/esp32_backend.cpp
 * @brief Backend that publishes gamepads through an ESP32-S3 USB bridge.
 *
 * The bridge enumerates as USB devices on the host it is plugged into, and
 * the host binds its inbox drivers (xusb22 for XInput, hidclass for HID).
 * Each gamepad, and keyboard plus mouse together, occupies one of the
 * bridge's two slots; any slot change re-enumerates the bridge.
 */

// standard includes
#include <algorithm>
#include <limits>
#include <mutex>
#include <utility>

// local includes
#include "platform/esp32/esp32_backend.hpp"
#include "platform/esp32/keyboard_mouse_reports.hpp"
#include "platform/esp32/xinput_reports.hpp"
#include "shared/playstation_feature_reports.hpp"
#include "shared/switch_pro_protocol.hpp"

#include <libvirtualhid/esp32_bridge.hpp>
#include <libvirtualhid/profiles.hpp>
#include <libvirtualhid/report.hpp>

// lib includes
#include <lizardbyte/common/env.h>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <unistd.h>
#endif

namespace lvh::detail::esp32 {
  namespace {

    namespace ps = playstation_feature_reports;

    constexpr std::uint8_t xinput_interval_ms = 1;
    constexpr std::uint8_t hid_interval_ms = 1;
    constexpr std::string_view slots_full_message = "both ESP32-S3 bridge slots are in use";

    /**
     * @brief USB identity a slot needs from the whole bridge device.
     */
    struct DeviceIdentity {
      std::uint16_t vendor_id = 0;  ///< USB idVendor.
      std::uint16_t product_id = 0;  ///< USB idProduct.
      std::uint16_t bcd_device = 0;  ///< USB bcdDevice.
      std::string manufacturer;  ///< Manufacturer string.
      std::string product;  ///< Product string.
    };

    /**
     * @brief Slot configuration chosen for one requested profile.
     */
    struct SlotPlan {
      SlotLayout layout;  ///< Slot configuration sent to the bridge.
      DeviceProfile effective_profile;  ///< Profile the slot actually presents.
      std::optional<DeviceIdentity> identity;  ///< USB identity the slot needs, empty for XInput.
    };

    /**
     * @brief Shared keyboard and mouse state of the keyboard/mouse slot.
     */
    struct KeyboardMouseState {
      std::mutex mutex;  ///< Serializes report generation so reports leave in event order.
      KeyboardReportState keyboard;  ///< Pressed keys.
      std::uint8_t buttons = 0;  ///< Pressed mouse buttons.
      bool last_motion_absolute = false;  ///< Route button reports through the pointer mode used last.
      std::uint16_t absolute_x = 0;  ///< Last absolute X.
      std::uint16_t absolute_y = 0;  ///< Last absolute Y.
      std::int64_t vertical_scroll_remainder = 0;  ///< High-resolution scroll not yet sent as a detent.
      std::int64_t horizontal_scroll_remainder = 0;  ///< High-resolution pan not yet sent as a detent.
      bool keyboard_open = false;  ///< A keyboard device uses the slot.
      bool mouse_open = false;  ///< A mouse device uses the slot.
    };

    /**
     * @brief Live state of one occupied slot, shared with output dispatch.
     */
    struct SlotState {
      std::uint8_t kind = LVHB_SLOT_NONE;  ///< #lvhb_slot_kind.
      std::shared_ptr<KeyboardMouseState> keyboard_mouse;  ///< Set for keyboard/mouse slots.
      DeviceProfile profile;  ///< Effective gamepad profile.
      SlotLayout layout;  ///< Slot configuration sent to the bridge.
      std::optional<DeviceIdentity> identity;  ///< USB identity the slot needs, empty for XInput and keyboard/mouse.
      std::mutex callback_mutex;  ///< Guards callback.
      OutputCallback callback;  ///< Consumer output callback.
    };

    template<std::size_t Size>
    std::vector<std::uint8_t> to_vector(const std::array<std::uint8_t, Size> &bytes) {
      return {bytes.begin(), bytes.end()};
    }

    std::array<std::uint8_t, 6> playstation_mac(DeviceId id, std::size_t slot) {
      // Locally administered address, stable for the lifetime of the device.
      return {0x02, 0x4C, static_cast<std::uint8_t>(slot), static_cast<std::uint8_t>(id >> 16U), static_cast<std::uint8_t>(id >> 8U), static_cast<std::uint8_t>(id)};
    }

    std::vector<std::uint8_t> pairing_report(std::span<const std::uint8_t> base, const std::array<std::uint8_t, 6> &mac) {
      std::vector<std::uint8_t> report {base.begin(), base.end()};
      for (std::size_t index = 0; index < mac.size() && index + 1U < report.size(); ++index) {
        report[1U + index] = mac[mac.size() - 1U - index];
      }
      return report;
    }

    DeviceIdentity identity_for(const DeviceProfile &profile) {
      return {profile.vendor_id, profile.product_id, profile.version, profile.manufacturer, profile.name};
    }

    SlotPlan plan_hid(DeviceProfile profile) {
      SlotPlan plan;
      plan.layout.kind = LVHB_SLOT_HID;
      plan.layout.interval_ms = hid_interval_ms;
      plan.layout.report_descriptor = profile.report_descriptor;
      plan.layout.neutral_report = reports::pack_input_report(profile, GamepadState {});
      plan.identity = identity_for(profile);
      plan.effective_profile = std::move(profile);
      return plan;
    }

    /**
     * @brief Plan a PlayStation pad with its calibration, pairing, and firmware feature reports.
     *
     * @param profile USB profile.
     * @param calibration Calibration feature report.
     * @param pairing Pairing feature report template; the MAC is filled in.
     * @param firmware Firmware feature report.
     * @param mac Controller MAC address.
     * @return Slot plan.
     */
    SlotPlan plan_playstation(DeviceProfile profile, std::span<const std::uint8_t> calibration, std::span<const std::uint8_t> pairing, std::span<const std::uint8_t> firmware, const std::array<std::uint8_t, 6> &mac) {
      auto plan = plan_hid(std::move(profile));
      plan.layout.feature_reports = {
        std::vector<std::uint8_t> {calibration.begin(), calibration.end()},
        pairing_report(pairing, mac),
        std::vector<std::uint8_t> {firmware.begin(), firmware.end()},
      };
      return plan;
    }

    std::optional<SlotPlan> plan_slot(const CreateGamepadOptions &options, DeviceId id, std::size_t slot) {
      switch (options.profile.gamepad_kind) {
        case GamepadProfileKind::xbox_360:
        case GamepadProfileKind::xbox_one:
        case GamepadProfileKind::xbox_series:
        case GamepadProfileKind::generic:
          {
            // XInput reaches nearly every Windows game through the inbox xusb22 driver.
            SlotPlan plan;
            plan.layout.kind = LVHB_SLOT_XINPUT;
            plan.layout.interval_ms = xinput_interval_ms;
            plan.layout.neutral_report = to_vector(pack_xinput_report(GamepadState {}));
            plan.effective_profile = profiles::xbox_360();
            return plan;
          }
        case GamepadProfileKind::dualsense:
          return plan_playstation(profiles::dualsense_usb(), ps::dualsense_calibration_info, ps::dualsense_pairing_info, ps::dualsense_firmware_info, playstation_mac(id, slot));
        case GamepadProfileKind::dualshock4:
          return plan_playstation(profiles::dualshock4_usb(), ps::dualshock4_usb_calibration_info, ps::dualshock4_pairing_info, ps::dualshock4_firmware_info, playstation_mac(id, slot));
        case GamepadProfileKind::switch_pro:
          return plan_hid(profiles::switch_pro());
      }
      return std::nullopt;
    }

    /**
     * @brief Bridge state shared by the backend and every gamepad it created.
     */
    class BridgeCore: public std::enable_shared_from_this<BridgeCore> {
    public:
      /**
       * @brief Wrap a bridge client.
       *
       * @param client Bridge client; started on the first device.
       */
      explicit BridgeCore(std::shared_ptr<BridgeClient> client):
          client_ {std::move(client)} {}

      /**
       * @brief Route bridge output reports to the slot callbacks; call once after construction.
       */
      void install_output_handler() {
        client_->set_output_handler([weak = weak_from_this()](const BridgeOutput &output) {
          if (const auto core = weak.lock(); core != nullptr) {
            core->dispatch_output(output);
          }
        });
      }

      /**
       * @brief Stop the bridge client so no output handler runs during teardown.
       */
      void stop() {
        client_->stop();
      }

      /**
       * @brief Start the bridge client on first use; layout_mutex_ must be held.
       *
       * @return Start status.
       */
      OperationStatus ensure_started() {
        if (started_) {
          return OperationStatus::success();
        }
        auto status = client_->start();
        started_ = status.ok();
        return status;
      }

      /**
       * @brief Enumerate a gamepad in a free slot.
       *
       * @param id Runtime device id.
       * @param options Creation options.
       * @return Gamepad, or the failure status.
       */
      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options);

      /**
       * @brief Attach a keyboard or mouse to the shared keyboard/mouse slot, enumerating it when needed.
       *
       * @param keyboard `true` for the keyboard user, `false` for the mouse user.
       * @param slot_index Receives the slot index.
       * @return Slot state, or the failure status.
       */
      std::pair<OperationStatus, std::shared_ptr<SlotState>> acquire_keyboard_mouse(bool keyboard, std::size_t &slot_index);

      /**
       * @brief Detach a keyboard or mouse; the slot is released when neither remains.
       *
       * @param slot Slot index.
       * @param keyboard `true` for the keyboard user.
       */
      void release_keyboard_mouse(std::size_t slot, bool keyboard) {
        std::lock_guard layout_lock {layout_mutex_};
        const auto state = current_slots()[slot];
        if (state == nullptr || state->keyboard_mouse == nullptr) {
          return;
        }
        {
          std::lock_guard lock {state->keyboard_mouse->mutex};
          (keyboard ? state->keyboard_mouse->keyboard_open : state->keyboard_mouse->mouse_open) = false;
          if (state->keyboard_mouse->keyboard_open || state->keyboard_mouse->mouse_open) {
            return;
          }
        }
        release_locked(slot);
      }

      /**
       * @brief Release a gamepad slot.
       *
       * @param slot Slot index.
       */
      void release(std::size_t slot) {
        std::lock_guard layout_lock {layout_mutex_};
        release_locked(slot);
      }

      /**
       * @brief Forward a diagnostic message.
       *
       * @param level Severity.
       * @param message Message text.
       */
      void log(LogLevel level, const std::string &message) const {
        client_->log(level, message);
      }

      /**
       * @brief Send an input report to a slot.
       *
       * @param slot Slot index.
       * @param report Report bytes.
       */
      void submit(std::size_t slot, std::span<const std::uint8_t> report) {
        client_->send_input(static_cast<std::uint8_t>(slot), report);
      }

      /**
       * @brief Build the device node reported for a slot.
       *
       * @param slot Slot index.
       * @param suffix Appended to the node, such as `/keyboard`.
       * @return Device node.
       */
      std::vector<DeviceNode> device_nodes(std::size_t slot, std::string_view suffix = {}) const {
        return {DeviceNode {DeviceNodeKind::other, "esp32-bridge:" + client_->port_path() + "#slot" + std::to_string(slot) + std::string {suffix}}};
      }

    private:
      using SlotArray = std::array<std::shared_ptr<SlotState>, LVHB_MAX_SLOTS>;

      /**
       * @brief Remove a slot and ask the bridge to drop it; layout_mutex_ must be held.
       *
       * @param slot Slot index.
       */
      void release_locked(std::size_t slot) {
        {
          std::lock_guard lock {slots_mutex_};
          slots_[slot].reset();
        }
        if (const auto status = client_->apply_layout(build_layout(current_slots()), LayoutCommit::eventually); !status.ok()) {
          // The release is the desired layout now; the supervisor applies it once the bridge is back.
          client_->log(LogLevel::warning, "ESP32-S3 bridge slot release pending: " + status.message());
        }
      }

      /**
       * @brief Validate a candidate slot set, apply it, and publish it on success.
       *
       * @param candidate Slots after the change.
       * @return Apply status.
       */
      OperationStatus commit_slots(const SlotArray &candidate) {
        bool conflict = false;
        static_cast<void>(merged_identity(candidate, conflict));
        if (conflict) {
          return OperationStatus::failure(
            ErrorCode::unsupported_profile,
            "ESP32-S3 bridge has a single USB identity; two different HID controller families cannot share it"
          );
        }
        if (auto status = client_->apply_layout(build_layout(candidate)); !status.ok()) {
          return status;
        }
        std::lock_guard lock {slots_mutex_};
        slots_ = candidate;
        return OperationStatus::success();
      }

      /**
       * @brief Find an unused slot.
       *
       * @param slots Slot set.
       * @return Slot index, or empty when both are taken.
       */
      std::optional<std::size_t> free_slot(const SlotArray &slots) const {
        const auto it = std::ranges::find(slots, nullptr);
        if (it == slots.end()) {
          return std::nullopt;
        }
        return static_cast<std::size_t>(it - slots.begin());
      }

      /**
       * @brief Snapshot the published slots.
       *
       * @return Slot set.
       */
      SlotArray current_slots() const {
        std::lock_guard lock {slots_mutex_};
        return slots_;
      }

      /**
       * @brief Find the USB identity the slots need.
       *
       * @param slots Slot set.
       * @param conflict Set when two slots need different identities.
       * @return Identity of the first HID slot, or empty when none needs one.
       */
      static std::optional<DeviceIdentity> merged_identity(const SlotArray &slots, bool &conflict) {
        conflict = false;
        std::optional<DeviceIdentity> identity;
        for (const auto &slot : slots) {
          if (slot == nullptr || !slot->identity.has_value()) {
            continue;
          }
          if (identity.has_value() && (identity->vendor_id != slot->identity->vendor_id || identity->product_id != slot->identity->product_id)) {
            conflict = true;
          }
          if (!identity.has_value()) {
            identity = slot->identity;
          }
        }
        return identity;
      }

      /**
       * @brief Build the bridge layout for a slot set.
       *
       * @param slots Slot set.
       * @return Layout.
       */
      static BridgeLayout build_layout(const SlotArray &slots) {
        BridgeLayout layout;
        bool conflict = false;
        // HID pads are recognized by VID/PID (SDL, Steam), so they decide the
        // device identity; XInput binds through its compatible id instead.
        if (const auto identity = merged_identity(slots, conflict); identity.has_value()) {
          layout.vendor_id = identity->vendor_id;
          layout.product_id = identity->product_id;
          layout.bcd_device = identity->bcd_device;
          layout.manufacturer = identity->manufacturer;
          layout.product = identity->product;
        }
        for (std::size_t index = 0; index < slots.size(); ++index) {
          if (slots[index] != nullptr) {
            layout.slots[index] = slots[index]->layout;
          }
        }
        return layout;
      }

      /**
       * @brief Translate a bridge output report for the slot's callback.
       *
       * @param output Output report.
       */
      void dispatch_output(const BridgeOutput &output) {
        std::shared_ptr<SlotState> slot;
        {
          std::lock_guard lock {slots_mutex_};
          slot = slots_[output.slot];
        }
        if (slot == nullptr || slot->kind == LVHB_SLOT_KEYBOARD_MOUSE) {
          // Keyboard LED reports have no consumer-facing output API.
          return;
        }

        std::vector<GamepadOutput> outputs;
        if (slot->kind == LVHB_SLOT_XINPUT) {
          outputs = parse_xinput_output(output.report);
        } else {
          if (slot->profile.gamepad_kind == GamepadProfileKind::switch_pro) {
            if (const auto reply = switch_pro_protocol::make_switch_pro_reply(output.report); reply.has_value()) {
              client_->send_input(output.slot, *reply);
            }
          }
          outputs = reports::parse_output_reports(slot->profile, output.report);
        }

        OutputCallback callback;
        {
          std::lock_guard lock {slot->callback_mutex};
          callback = slot->callback;
        }
        if (!callback) {
          return;
        }
        for (const auto &item : outputs) {
          callback(item);
        }
      }

      std::shared_ptr<BridgeClient> client_;  ///< Bridge connection.
      bool started_ = false;  ///< The client has started; guarded by layout_mutex_.
      std::mutex layout_mutex_;  ///< Serializes slot changes.
      mutable std::mutex slots_mutex_;  ///< Guards slots_.
      SlotArray slots_ {};  ///< Published slots.
    };

    /**
     * @brief Gamepad published through one bridge slot.
     */
    class Esp32Gamepad final: public BackendGamepad {
    public:
      Esp32Gamepad(std::shared_ptr<BridgeCore> core, std::size_t slot, std::shared_ptr<SlotState> state):
          core_ {std::move(core)},
          slot_ {slot},
          state_ {std::move(state)} {}

      ~Esp32Gamepad() override {
        static_cast<void>(Esp32Gamepad::close());
      }

      OperationStatus submit(const GamepadState &state, const std::vector<std::uint8_t> &report) override {
        if (closed_) {
          return OperationStatus::failure(ErrorCode::device_closed, "gamepad is closed");
        }
        if (state_->kind == LVHB_SLOT_XINPUT) {
          const auto xinput = pack_xinput_report(state);
          core_->submit(slot_, xinput);
        } else {
          core_->submit(slot_, report);
        }
        return OperationStatus::success();
      }

      std::vector<DeviceNode> device_nodes() const override {
        return core_->device_nodes(slot_);
      }

      void set_output_callback(OutputCallback callback) override {
        std::lock_guard lock {state_->callback_mutex};
        state_->callback = std::move(callback);
      }

      OperationStatus close() override {
        if (closed_) {
          return OperationStatus::success();
        }
        closed_ = true;
        set_output_callback({});
        core_->release(slot_);
        return OperationStatus::success();
      }

    private:
      std::shared_ptr<BridgeCore> core_;
      std::size_t slot_;
      std::shared_ptr<SlotState> state_;
      bool closed_ = false;
    };

    BackendGamepadCreationResult BridgeCore::create_gamepad(DeviceId id, const CreateGamepadOptions &options) {
      std::lock_guard layout_lock {layout_mutex_};
      if (auto status = ensure_started(); !status.ok()) {
        return {std::move(status), nullptr};
      }

      auto slots = current_slots();
      const auto free = free_slot(slots);
      if (!free.has_value()) {
        return {OperationStatus::failure(ErrorCode::unsupported_profile, std::string {slots_full_message}), nullptr};
      }
      const auto slot_index = *free;

      auto plan = plan_slot(options, id, slot_index);
      if (!plan.has_value()) {
        return {OperationStatus::failure(ErrorCode::unsupported_profile, "profile is not supported by the ESP32-S3 bridge"), nullptr};
      }

      auto state = std::make_shared<SlotState>();
      state->kind = plan->layout.kind;
      state->profile = plan->effective_profile;
      state->layout = plan->layout;
      state->identity = plan->identity;
      slots[slot_index] = state;
      if (auto status = commit_slots(slots); !status.ok()) {
        return {std::move(status), nullptr};
      }

      BackendGamepadCreationResult result;
      result.status = OperationStatus::success();
      result.gamepad = std::make_unique<Esp32Gamepad>(shared_from_this(), slot_index, state);
      if (plan->effective_profile.gamepad_kind != options.profile.gamepad_kind || plan->effective_profile.bus_type != options.profile.bus_type) {
        result.effective_profile = plan->effective_profile;
      }
      return result;
    }

    std::pair<OperationStatus, std::shared_ptr<SlotState>> BridgeCore::acquire_keyboard_mouse(bool keyboard, std::size_t &slot_index) {
      std::lock_guard layout_lock {layout_mutex_};
      if (auto status = ensure_started(); !status.ok()) {
        return {std::move(status), nullptr};
      }

      auto slots = current_slots();
      std::shared_ptr<SlotState> state;
      for (std::size_t index = 0; index < slots.size(); ++index) {
        if (slots[index] != nullptr && slots[index]->keyboard_mouse != nullptr) {
          state = slots[index];
          slot_index = index;
        }
      }

      if (state == nullptr) {
        const auto free = free_slot(slots);
        if (!free.has_value()) {
          return {OperationStatus::failure(ErrorCode::unsupported_profile, std::string {slots_full_message}), nullptr};
        }
        state = std::make_shared<SlotState>();
        state->kind = LVHB_SLOT_KEYBOARD_MOUSE;
        state->keyboard_mouse = std::make_shared<KeyboardMouseState>();
        state->layout.kind = LVHB_SLOT_KEYBOARD_MOUSE;
        state->layout.interval_ms = hid_interval_ms;
        state->layout.report_descriptor = keyboard_mouse_report_descriptor();
        state->layout.neutral_report = keyboard_mouse_neutral_reports();
        slots[*free] = state;
        if (auto status = commit_slots(slots); !status.ok()) {
          return {std::move(status), nullptr};
        }
        slot_index = *free;
      }

      std::lock_guard lock {state->keyboard_mouse->mutex};
      auto &open = keyboard ? state->keyboard_mouse->keyboard_open : state->keyboard_mouse->mouse_open;
      if (open) {
        return {OperationStatus::failure(ErrorCode::unsupported_profile, std::string {"ESP32-S3 bridge already hosts a "} + (keyboard ? "keyboard" : "mouse")), nullptr};
      }
      open = true;
      return {OperationStatus::success(), state};
    }

    /**
     * @brief Keyboard published through the bridge's keyboard/mouse slot.
     *
     * Windows applies its own typematic repeat to held HID keys, so the
     * auto-repeat option is not emulated.
     */
    class Esp32Keyboard final: public BackendKeyboard {
    public:
      Esp32Keyboard(
        std::shared_ptr<BridgeCore> core,
        std::size_t slot,
        std::shared_ptr<SlotState> state,
        std::shared_ptr<Backend> fallback,
        DeviceId id,
        CreateKeyboardOptions options
      ):
          core_ {std::move(core)},
          slot_ {slot},
          state_ {std::move(state)},
          fallback_ {std::move(fallback)},
          id_ {id},
          options_ {std::move(options)} {}

      ~Esp32Keyboard() override {
        static_cast<void>(Esp32Keyboard::close());
      }

      OperationStatus submit(const KeyboardEvent &event) override {
        if (closed_) {
          return OperationStatus::failure(ErrorCode::device_closed, "keyboard is closed");
        }
        const auto usage = hid_usage_from_virtual_key(event.key_code, event.extended);
        if (!usage.has_value()) {
          return OperationStatus::failure(ErrorCode::invalid_argument, "key code " + std::to_string(event.key_code) + " has no HID keyboard usage");
        }
        auto &km = *state_->keyboard_mouse;
        std::lock_guard lock {km.mutex};
        if (km.keyboard.apply(*usage, event.pressed)) {
          core_->submit(slot_, km.keyboard.report());
        }
        return OperationStatus::success();
      }

      OperationStatus type_text(const KeyboardTextEvent &event) override {
        if (closed_) {
          return OperationStatus::failure(ErrorCode::device_closed, "keyboard is closed");
        }
        const auto ascii = std::ranges::all_of(event.text, [](char character) {
          return hid_usage_from_ascii(character).has_value();
        });
        if (!ascii) {
          // A HID keyboard can only press keys; Unicode text needs the host's text input API.
          return type_text_with_fallback(event);
        }
        auto &km = *state_->keyboard_mouse;
        std::lock_guard lock {km.mutex};
        for (const auto character : event.text) {
          const auto [usage, shift] = *hid_usage_from_ascii(character);
          // US layout positions; release in between so repeated letters register.
          core_->submit(slot_, km.keyboard.report_with(usage, shift));
          core_->submit(slot_, km.keyboard.report());
        }
        return OperationStatus::success();
      }

      std::vector<DeviceNode> device_nodes() const override {
        return core_->device_nodes(slot_, "/keyboard");
      }

      OperationStatus close() override {
        if (closed_) {
          return OperationStatus::success();
        }
        closed_ = true;
        {
          auto &km = *state_->keyboard_mouse;
          std::lock_guard lock {km.mutex};
          km.keyboard = {};
          core_->submit(slot_, km.keyboard.report());
        }
        if (text_fallback_ != nullptr) {
          static_cast<void>(text_fallback_->close());
        }
        core_->release_keyboard_mouse(slot_, true);
        return OperationStatus::success();
      }

    private:
      OperationStatus type_text_with_fallback(const KeyboardTextEvent &event) {
        if (fallback_ == nullptr) {
          return OperationStatus::failure(ErrorCode::unsupported_profile, "ESP32-S3 bridge keyboard can only type US-ASCII text");
        }
        if (text_fallback_ == nullptr) {
          auto created = fallback_->create_keyboard(id_, options_);
          if (!created) {
            return created.status;
          }
          text_fallback_ = std::move(created.keyboard);
        }
        return text_fallback_->type_text(event);
      }

      std::shared_ptr<BridgeCore> core_;
      std::size_t slot_;
      std::shared_ptr<SlotState> state_;
      std::shared_ptr<Backend> fallback_;
      DeviceId id_;
      CreateKeyboardOptions options_;
      std::unique_ptr<BackendKeyboard> text_fallback_;
      bool closed_ = false;
    };

    /**
     * @brief Mouse published through the bridge's keyboard/mouse slot.
     */
    class Esp32Mouse final: public BackendMouse {
    public:
      Esp32Mouse(std::shared_ptr<BridgeCore> core, std::size_t slot, std::shared_ptr<SlotState> state, CreateMouseOptions options):
          core_ {std::move(core)},
          slot_ {slot},
          state_ {std::move(state)},
          options_ {std::move(options)} {}

      ~Esp32Mouse() override {
        static_cast<void>(Esp32Mouse::close());
      }

      OperationStatus submit(const MouseEvent &event) override {
        if (closed_) {
          return OperationStatus::failure(ErrorCode::device_closed, "mouse is closed");
        }
        auto &km = *state_->keyboard_mouse;
        std::lock_guard lock {km.mutex};
        switch (event.kind) {
          using enum MouseEventKind;

          case relative_motion:
            km.last_motion_absolute = false;
            send_relative_motion(km, event.x, event.y);
            break;
          case absolute_motion:
            {
              if (event.width <= 0 || event.height <= 0) {
                return OperationStatus::failure(ErrorCode::invalid_argument, "absolute mouse motion needs a positive source size");
              }
              const auto x = event.has_fractional_absolute_coordinates ? event.absolute_x : static_cast<float>(event.x);
              const auto y = event.has_fractional_absolute_coordinates ? event.absolute_y : static_cast<float>(event.y);
              km.absolute_x = scale_absolute_pointer_axis(x, event.width, options_.viewport.offset_x, options_.viewport.width, options_.desktop.offset_x, options_.desktop.width);
              km.absolute_y = scale_absolute_pointer_axis(y, event.height, options_.viewport.offset_y, options_.viewport.height, options_.desktop.offset_y, options_.desktop.height);
              km.last_motion_absolute = true;
              core_->submit(slot_, make_absolute_mouse_report(km.buttons, km.absolute_x, km.absolute_y));
              break;
            }
          case button:
            {
              const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(event.button));
              km.buttons = static_cast<std::uint8_t>(event.pressed ? (km.buttons | bit) : (km.buttons & ~bit));
              send_buttons(km);
              break;
            }
          case vertical_scroll:
            send_scroll(km, km.vertical_scroll_remainder, event.high_resolution_scroll, true);
            break;
          case horizontal_scroll:
            send_scroll(km, km.horizontal_scroll_remainder, event.high_resolution_scroll, false);
            break;
        }
        return OperationStatus::success();
      }

      std::vector<DeviceNode> device_nodes() const override {
        return core_->device_nodes(slot_, "/mouse");
      }

      OperationStatus close() override {
        if (closed_) {
          return OperationStatus::success();
        }
        closed_ = true;
        {
          auto &km = *state_->keyboard_mouse;
          std::lock_guard lock {km.mutex};
          if (km.buttons != 0U) {
            km.buttons = 0;
            send_buttons(km);
          }
        }
        core_->release_keyboard_mouse(slot_, false);
        return OperationStatus::success();
      }

    private:
      void send_relative_motion(KeyboardMouseState &km, std::int32_t x, std::int32_t y) {
        constexpr std::int32_t limit = std::numeric_limits<std::int16_t>::max();
        // Split motion beyond the 16-bit report range instead of clipping it.
        do {
          const auto step_x = std::clamp(x, -limit, limit);
          const auto step_y = std::clamp(y, -limit, limit);
          core_->submit(slot_, make_relative_mouse_report(km.buttons, static_cast<std::int16_t>(step_x), static_cast<std::int16_t>(step_y), 0, 0));
          x -= step_x;
          y -= step_y;
        } while (x != 0 || y != 0);
      }

      void send_buttons(KeyboardMouseState &km) {
        if (km.last_motion_absolute) {
          core_->submit(slot_, make_absolute_mouse_report(km.buttons, km.absolute_x, km.absolute_y));
        } else {
          core_->submit(slot_, make_relative_mouse_report(km.buttons, 0, 0, 0, 0));
        }
      }

      void send_scroll(KeyboardMouseState &km, std::int64_t &remainder, std::int32_t amount, bool vertical) {
        remainder += amount;
        auto detents = remainder / wheel_detent;
        remainder -= detents * wheel_detent;
        while (detents != 0) {
          const auto step = static_cast<std::int8_t>(std::clamp<std::int64_t>(detents, -127, 127));
          core_->submit(slot_, make_relative_mouse_report(km.buttons, 0, 0, vertical ? step : std::int8_t {0}, vertical ? std::int8_t {0} : step));
          detents -= step;
        }
      }

      std::shared_ptr<BridgeCore> core_;
      std::size_t slot_;
      std::shared_ptr<SlotState> state_;
      CreateMouseOptions options_;
      bool closed_ = false;
    };

    /**
     * @brief Backend facade over BridgeCore plus a fallback for non-gamepad devices.
     */
    class Esp32Backend final: public Backend {
    public:
      /**
       * @brief Create the backend.
       *
       * @param client Bridge client.
       * @param fallback Platform backend, or `nullptr`.
       * @param implicit Gamepads also fall back to @p fallback when no bridge answers.
       */
      Esp32Backend(std::shared_ptr<BridgeClient> client, std::shared_ptr<Backend> fallback, bool implicit):
          core_ {std::make_shared<BridgeCore>(std::move(client))},
          fallback_ {std::move(fallback)},
          implicit_ {implicit} {
        core_->install_output_handler();
        if (fallback_ != nullptr) {
          capabilities_ = fallback_->capabilities();
        }
        capabilities_.backend_name = "esp32_bridge";
        capabilities_.supports_virtual_hid = true;
        capabilities_.supports_gamepad = true;
        capabilities_.supports_keyboard = true;
        capabilities_.supports_mouse = true;
        capabilities_.supports_output_reports = true;
        capabilities_.requires_installed_driver = false;
      }

      Esp32Backend(const Esp32Backend &) = delete;
      Esp32Backend &operator=(const Esp32Backend &) = delete;
      Esp32Backend(Esp32Backend &&) noexcept = delete;
      Esp32Backend &operator=(Esp32Backend &&) noexcept = delete;

      /**
       * @brief Stop the bridge threads before the core can be released on one of them.
       */
      ~Esp32Backend() override {
        core_->stop();
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId id, const CreateGamepadOptions &options) override {
        auto result = core_->create_gamepad(id, options);
        if (result || !implicit_ || fallback_ == nullptr || result.status.code() != ErrorCode::backend_unavailable) {
          return result;
        }
        core_->log(LogLevel::warning, "ESP32-S3 bridge unavailable (" + result.status.message() + "); using the platform backend");
        return fallback_->create_gamepad(id, options);
      }

      BackendKeyboardCreationResult create_keyboard(DeviceId id, const CreateKeyboardOptions &options) override {
        std::size_t slot = 0;
        auto [status, state] = core_->acquire_keyboard_mouse(true, slot);
        if (status.ok()) {
          return {OperationStatus::success(), std::make_unique<Esp32Keyboard>(core_, slot, std::move(state), fallback_, id, options)};
        }
        if (fallback_ == nullptr) {
          return {std::move(status), nullptr};
        }
        core_->log(LogLevel::warning, "ESP32-S3 bridge keyboard unavailable (" + status.message() + "); using the platform keyboard");
        return fallback_->create_keyboard(id, options);
      }

      BackendMouseCreationResult create_mouse(DeviceId id, const CreateMouseOptions &options) override {
        std::size_t slot = 0;
        auto [status, state] = core_->acquire_keyboard_mouse(false, slot);
        if (status.ok()) {
          return {OperationStatus::success(), std::make_unique<Esp32Mouse>(core_, slot, std::move(state), options)};
        }
        if (fallback_ == nullptr) {
          return {std::move(status), nullptr};
        }
        core_->log(LogLevel::warning, "ESP32-S3 bridge mouse unavailable (" + status.message() + "); using the platform mouse");
        return fallback_->create_mouse(id, options);
      }

      BackendTouchscreenCreationResult create_touchscreen(DeviceId id, const CreateTouchscreenOptions &options) override {
        return forward<BackendTouchscreenCreationResult>("touchscreen", &Backend::create_touchscreen, id, options);
      }

      BackendTrackpadCreationResult create_trackpad(DeviceId id, const CreateTrackpadOptions &options) override {
        return forward<BackendTrackpadCreationResult>("trackpad", &Backend::create_trackpad, id, options);
      }

      BackendPenTabletCreationResult create_pen_tablet(DeviceId id, const CreatePenTabletOptions &options) override {
        return forward<BackendPenTabletCreationResult>("pen tablet", &Backend::create_pen_tablet, id, options);
      }

    private:
      /**
       * @brief Create a device the bridge has no slot type for on the fallback backend.
       *
       * @param device Device name for the error message.
       * @param create Fallback factory.
       * @param id Runtime device id.
       * @param options Creation options.
       * @return Fallback result, or `backend_unavailable` without a fallback.
       */
      template<typename Result, typename Options>
      Result forward(std::string_view device, Result (Backend::*create)(DeviceId, const Options &), DeviceId id, const Options &options) {
        if (fallback_ == nullptr) {
          return {OperationStatus::failure(ErrorCode::backend_unavailable, "ESP32-S3 bridge has no " + std::string {device} + " fallback backend"), nullptr};
        }
        return ((*fallback_).*create)(id, options);
      }

      std::shared_ptr<BridgeCore> core_;  ///< Bridge slots and connection.
      std::shared_ptr<Backend> fallback_;  ///< Platform backend, or `nullptr`.
      bool implicit_;  ///< Gamepads fall back when no bridge answers.
      BackendCapabilities capabilities_;  ///< Advertised capabilities.
    };

  }  // namespace

  std::optional<BridgeSelection> select_bridge(
    const RuntimeOptions &options,
    std::string_view env_backend,
    std::string_view env_port,
    const std::function<bool()> &bridge_present
  ) {
    const auto port = !env_port.empty() ? std::string {env_port} : options.esp32_bridge.port;
    if (options.backend == BackendKind::esp32_bridge) {
      return BridgeSelection {port, false};
    }
    if (options.backend != BackendKind::platform_default || !options.esp32_bridge.autodetect || env_backend == "platform") {
      return std::nullopt;
    }
    if (env_backend == "esp32_bridge" || !port.empty() || (bridge_present && bridge_present())) {
      return BridgeSelection {port, true};
    }
    return std::nullopt;
  }

  std::string local_host_name() {
#ifdef _WIN32
    char buffer[MAX_COMPUTERNAME_LENGTH + 1] {};
    DWORD size = sizeof(buffer);
    return ::GetComputerNameA(buffer, &size) ? std::string {buffer, size} : std::string {};
#else
    char buffer[256] {};
    return ::gethostname(buffer, sizeof(buffer) - 1U) == 0 ? std::string {buffer} : std::string {};
#endif
  }

  std::string pairing_client_name(const std::string &application, const std::string &host) {
    auto name = application.empty() ? std::string {"libvirtualhid"} : application;
    if (!host.empty()) {
      name += " on " + host;
    }
    if (name.size() > LVHB_MAX_PAIRING_NAME) {
      name.resize(LVHB_MAX_PAIRING_NAME);
    }
    return name;
  }

  std::unique_ptr<Backend> create_esp32_backend(std::shared_ptr<BridgeClient> client, std::unique_ptr<Backend> fallback, bool implicit) {
    return std::make_unique<Esp32Backend>(std::move(client), std::move(fallback), implicit);
  }

  std::unique_ptr<Backend> create_backend_if_selected(const RuntimeOptions &options) {
    const auto selection = select_bridge(options, lizardbyte::common::get_env("LVH_BACKEND"), lizardbyte::common::get_env("LVH_ESP32_PORT"), []() {
      return !find_bridge_ports().empty();
    });
    if (!selection.has_value()) {
      return nullptr;
    }
    auto client = std::make_shared<BridgeClient>(
      make_serial_port_opener(selection->port),
      options.log_callback,
      BridgeTimings {},
      make_file_credential_store(options.esp32_bridge.credential_dir),
      pairing_client_name(options.esp32_bridge.client_name, local_host_name())
    );
    return create_esp32_backend(std::move(client), create_platform_backend(), selection->implicit);
  }

}  // namespace lvh::detail::esp32

std::vector<std::string> lvh::find_esp32_bridge_ports() {
  return detail::esp32::find_bridge_ports();
}

lvh::Esp32BridgeStatus lvh::esp32_bridge_status() {
  return detail::esp32::active_bridge_status();
}
