/**
 * @file tools/virtualhid_control.cpp
 * @brief SDL3 and Dear ImGui UI for creating and testing libvirtualhid devices.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <exception>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// third-party includes
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// local includes
#include "virtualhid_control_model.hpp"

namespace {
  using lvh::tools::virtualhid_control::axis_choices;
  using lvh::tools::virtualhid_control::axis_to_slider;
  using lvh::tools::virtualhid_control::battery_choice_index;
  using lvh::tools::virtualhid_control::battery_choices;
  using lvh::tools::virtualhid_control::battery_state_name;
  using lvh::tools::virtualhid_control::button_choices;
  using lvh::tools::virtualhid_control::device_feature_summary;
  using lvh::tools::virtualhid_control::device_type_choices;
  using lvh::tools::virtualhid_control::device_type_name;
  using lvh::tools::virtualhid_control::mouse_button_choices;
  using lvh::tools::virtualhid_control::mouse_button_event;
  using lvh::tools::virtualhid_control::mouse_event_for_action;
  using lvh::tools::virtualhid_control::MouseControlAction;
  using lvh::tools::virtualhid_control::node_kind_name;
  using lvh::tools::virtualhid_control::normalized_license_key;
  using lvh::tools::virtualhid_control::output_kind_name;
  using lvh::tools::virtualhid_control::output_summary;
  using lvh::tools::virtualhid_control::OutputLogEntry;
  using lvh::tools::virtualhid_control::OutputState;
  using lvh::tools::virtualhid_control::profile_choices;
  using lvh::tools::virtualhid_control::profile_for_choice;
  using lvh::tools::virtualhid_control::raw_hex;
  using lvh::tools::virtualhid_control::slider_to_float;
  using lvh::tools::virtualhid_control::trigger_to_slider;
  using lvh::tools::virtualhid_control::update_visible_controls_for_profile;

  struct ControlledGamepad: OutputState {
    std::string profile_label;
    std::unique_ptr<lvh::GamepadStateAdapter> adapter;
  };

  struct ControlledMouse {
    std::string profile_label;
    std::unique_ptr<lvh::Mouse> mouse;
    std::array<bool, mouse_button_choices.size()> buttons {};
  };

  struct DeviceHandles {
    std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> gamepads;
    std::vector<std::unique_ptr<lvh::Mouse>> mice;
  };

  struct ScheduledMouseEvent {
    lvh::DeviceId device_id = 0;
    lvh::MouseEvent event;
    std::chrono::steady_clock::time_point due_at;
  };

  struct DeviceListItem {
    lvh::DeviceId id = 0;
    std::string label;
  };

  struct SelectedSnapshot {
    bool has_device = false;
    lvh::DeviceId id = 0;
    lvh::DeviceType device_type = lvh::DeviceType::gamepad;
    std::string profile_label;
    lvh::DeviceProfile profile;
    lvh::GamepadState state;
    lvh::GamepadProfileSupport support;
    std::array<bool, mouse_button_choices.size()> mouse_buttons {};
    std::uint64_t submit_count = 0;
    std::vector<lvh::DeviceNode> nodes;
    std::vector<OutputLogEntry> outputs;
    std::string state_text;
    std::string output_text;
  };

  lvh::DeviceId first_device_id(
    const std::map<lvh::DeviceId, ControlledGamepad> &gamepads,
    const std::map<lvh::DeviceId, ControlledMouse> &mice
  ) {
    if (gamepads.empty()) {
      return mice.empty() ? 0 : mice.begin()->first;
    }
    if (mice.empty()) {
      return gamepads.begin()->first;
    }
    return std::min(gamepads.begin()->first, mice.begin()->first);
  }

  class ScopedDisabled {
  public:
    explicit ScopedDisabled(bool disabled):
        disabled_ {disabled} {
      if (disabled_) {
        ImGui::BeginDisabled();
      }
    }

    ScopedDisabled(const ScopedDisabled &) = delete;
    ScopedDisabled &operator=(const ScopedDisabled &) = delete;

    ~ScopedDisabled() {
      if (disabled_) {
        ImGui::EndDisabled();
      }
    }

  private:
    bool disabled_ = false;
  };

  void append_utf8(std::string &target, char32_t codepoint) {
    if (codepoint <= 0x7F) {
      target.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
      target.push_back(static_cast<char>(0xC0 | (codepoint >> 6U)));
      target.push_back(static_cast<char>(0x80 | (codepoint & 0x3FU)));
    } else if (codepoint <= 0xFFFF) {
      target.push_back(static_cast<char>(0xE0 | (codepoint >> 12U)));
      target.push_back(static_cast<char>(0x80 | ((codepoint >> 6U) & 0x3FU)));
      target.push_back(static_cast<char>(0x80 | (codepoint & 0x3FU)));
    } else {
      target.push_back(static_cast<char>(0xF0 | (codepoint >> 18U)));
      target.push_back(static_cast<char>(0x80 | ((codepoint >> 12U) & 0x3FU)));
      target.push_back(static_cast<char>(0x80 | ((codepoint >> 6U) & 0x3FU)));
      target.push_back(static_cast<char>(0x80 | (codepoint & 0x3FU)));
    }
  }

  bool is_high_surrogate(char32_t codepoint) {
    return codepoint >= 0xD800 && codepoint <= 0xDBFF;
  }

  bool is_low_surrogate(char32_t codepoint) {
    return codepoint >= 0xDC00 && codepoint <= 0xDFFF;
  }

  char32_t next_wide_codepoint(std::wstring_view value, std::size_t &index) {
    auto codepoint = static_cast<char32_t>(value[index]);
    ++index;

    if constexpr (sizeof(wchar_t) == 2) {
      if (is_high_surrogate(codepoint) && index < value.size()) {
        const auto low = static_cast<char32_t>(value[index]);
        if (is_low_surrogate(low)) {
          ++index;
          return 0x10000 + ((codepoint - 0xD800) << 10U) + (low - 0xDC00);
        }
      }
    }

    return codepoint;
  }

  std::string to_utf8(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    std::size_t index = 0;
    while (index < value.size()) {
      append_utf8(result, next_wide_codepoint(value, index));
    }
    return result;
  }

  std::string output_line(const OutputLogEntry &entry) {
    const auto &output = entry.output;
    std::ostringstream line;
    line << "#" << entry.sequence << " " << to_utf8(output_kind_name(output.kind))
         << " low=" << output.low_frequency_rumble
         << " high=" << output.high_frequency_rumble
         << " rgb=" << static_cast<unsigned>(output.red) << "," << static_cast<unsigned>(output.green) << ","
         << static_cast<unsigned>(output.blue);
    if (!output.raw_report.empty()) {
      line << " raw=" << to_utf8(raw_hex(output.raw_report));
    }
    return line.str();
  }

  void render_device_nodes(const SelectedSnapshot &selected) {
    if (!selected.has_device) {
      ImGui::TextDisabled("No selected device.");
      return;
    }
    if (selected.nodes.empty()) {
      ImGui::TextDisabled("No device nodes reported yet.");
      return;
    }

    for (const auto &node : selected.nodes) {
      const auto line = to_utf8(node_kind_name(node.kind)) + ": " + node.path;
      ImGui::TextWrapped("%s", line.c_str());
    }
  }

  void render_output_reports(const SelectedSnapshot &selected) {
    if (!selected.has_device) {
      ImGui::TextDisabled("No selected device.");
      return;
    }
    if (selected.outputs.empty()) {
      ImGui::TextDisabled("No output reports received.");
      return;
    }

    for (const auto &entry : selected.outputs) {
      const auto line = output_line(entry);
      ImGui::TextWrapped("%s", line.c_str());
    }
  }

  int scaled_window_dimension(int value, float scale) {
    return std::max(value, static_cast<int>(static_cast<float>(value) * scale));
  }

  bool selected_battery_state_is_full(int state_index) {
    return state_index >= 0 &&
           static_cast<std::size_t>(state_index) < battery_choices.size() &&
           battery_choices[static_cast<std::size_t>(state_index)].state == lvh::GamepadBatteryState::full;
  }

  std::string backend_text(const lvh::Runtime &runtime) {
    const auto &caps = runtime.capabilities();
    auto text = std::string {"Backend: "} + caps.backend_name;
    text += caps.supports_gamepad ? " | gamepad available" : " | gamepad unavailable";
    text += caps.supports_mouse ? " | mouse available" : " | mouse unavailable";
    text += caps.supports_output_reports ? " | output reports available" : " | output reports unavailable";
    return text;
  }

  struct LicenseSnapshot {
    bool broker_supported = false;
    bool broker_available = false;
    bool licensed = false;
    std::string plan_name = "Unavailable";
    std::string customer_email;
    std::string state_text = "License broker unavailable.";
    std::string message;
    std::string purchase_url;
    std::string manage_account_url;
    std::uint32_t active_devices = 0;
    std::uint32_t free_limit = 0;
    std::uint32_t activation_limit = 0;
    std::uint32_t activation_usage = 0;
  };

  bool open_url(std::string_view url, std::string &error) {
    if (url.empty()) {
      error = "URL is not configured.";
      return false;
    }
    if (SDL_OpenURL(std::string {url}.c_str())) {
      return true;
    }

    error = SDL_GetError();
    if (error.empty()) {
      error = "Unable to open URL.";
    }
    return false;
  }

  std::string license_state_name(lvh::LicenseState state) {
    using enum lvh::LicenseState;

    switch (state) {
      case unlicensed:
        return "Unlicensed";
      case licensed:
        return "Licensed";
      case expired:
        return "Expired";
      case disabled:
        return "Disabled";
      case invalid:
        return "Invalid";
      case unavailable:
      default:
        return "Unavailable";
    }
  }

  LicenseSnapshot license_snapshot_from(const lvh::LicenseResult &result) {
    const auto &status = result.license;
    LicenseSnapshot snapshot;
    snapshot.broker_supported = status.state != lvh::LicenseState::unavailable;
    snapshot.broker_available = status.service_available;
    snapshot.licensed = status.licensed();
    snapshot.plan_name = status.plan_name;
    snapshot.customer_email = status.customer_email;
    snapshot.message = status.message.empty() ? result.status.message() : status.message;
    snapshot.purchase_url = status.purchase_url;
    snapshot.manage_account_url = status.manage_account_url;
    snapshot.active_devices = status.active_devices;
    snapshot.activation_limit = status.activation_limit;
    snapshot.activation_usage = status.activation_usage;

    const auto state = license_state_name(status.state);
    snapshot.state_text = state;
    if (!snapshot.plan_name.empty() && snapshot.plan_name != state) {
      snapshot.state_text += " | " + snapshot.plan_name;
    }
    snapshot.state_text += std::format(" | active devices {}", status.active_devices);
    if (status.licensed()) {
      snapshot.state_text += " / unlimited";
    } else {
      snapshot.state_text += " | license required";
    }
    if (status.activation_limit > 0U) {
      snapshot.state_text += std::format(" | machine limit {}", status.activation_limit);
    }
    return snapshot;
  }

  LicenseSnapshot license_snapshot_from(const lvh::LicenseResult &result, std::string &error) {
    error = result.status.ok() ? std::string {} : result.status.message();
    return license_snapshot_from(result);
  }

  class LicensePanel {
  public:
    LicensePanel() {
      refresh();
    }

    void refresh() {
      std::string ignored;
      apply_result(lvh::get_license_status(), ignored);
    }

    template<typename ErrorHandler>
    void render(ErrorHandler show_error) {
      ImGui::TextUnformatted("License");
      ImGui::TextWrapped("%s", snapshot_.state_text.c_str());
      if (!snapshot_.customer_email.empty()) {
        ImGui::TextWrapped("%s", snapshot_.customer_email.c_str());
      }
      if (!snapshot_.message.empty()) {
        ImGui::TextWrapped("%s", snapshot_.message.c_str());
      }

#if defined(_WIN32)
      ImGui::TextUnformatted("License key");
      ImGui::InputText("##license-key", license_key_input_.data(), license_key_input_.size());
      {
        const auto license_key = normalized_license_key(license_key_input_.data());
        ScopedDisabled disabled {license_key.empty()};
        if (ImGui::Button("Activate license", {-FLT_MIN, 0.0F})) {
          activate(license_key, show_error);
        }
      }
      {
        ScopedDisabled disabled {!snapshot_.broker_available};
        if (ImGui::Button("Refresh", {-FLT_MIN, 0.0F})) {
          validate(show_error);
        }
        if (ImGui::Button("Deactivate this machine", {-FLT_MIN, 0.0F})) {
          deactivate(show_error);
        }
      }
#else
      if (ImGui::Button("Refresh", {-FLT_MIN, 0.0F})) {
        refresh_and_report(show_error);
      }
#endif

      {
        ScopedDisabled disabled {buy_url_.empty()};
        if (ImGui::Button("Buy license", {-FLT_MIN, 0.0F})) {
          open_configured_url(buy_url_, show_error);
        }
      }
      {
        ScopedDisabled disabled {manage_account_url_.empty()};
        if (ImGui::Button("Manage account", {-FLT_MIN, 0.0F})) {
          open_configured_url(manage_account_url_, show_error);
        }
      }
    }

    template<typename CreateHandler>
    void render_create_button(CreateHandler create_device) const {
#if defined(_WIN32)
      ScopedDisabled disabled {!snapshot_.broker_available || !snapshot_.licensed};
#endif
      if (ImGui::Button("Create", {-FLT_MIN, 0.0F})) {
        create_device();
      }
    }

  private:
    void apply_result(const lvh::LicenseResult &result, std::string &error) {
      snapshot_ = license_snapshot_from(result, error);
      buy_url_ = snapshot_.purchase_url;
      manage_account_url_ = snapshot_.manage_account_url;
    }

    template<typename ErrorHandler>
    void refresh_and_report(ErrorHandler &show_error) {
      std::string error;
      apply_result(lvh::get_license_status(), error);
      if (!error.empty()) {
        show_error(error);
      }
    }

    template<typename ErrorHandler>
    void open_configured_url(const std::string &url, ErrorHandler &show_error) const {
      std::string error;
      if (!open_url(url, error)) {
        show_error(error);
      }
    }

#if defined(_WIN32)
    template<typename ErrorHandler>
    void activate(std::string_view license_key, ErrorHandler &show_error) {
      std::string error;
      apply_result(lvh::activate_license(license_key), error);
      if (!error.empty()) {
        show_error(error);
      } else {
        license_key_input_.fill('\0');
      }
    }

    template<typename ErrorHandler>
    void validate(ErrorHandler &show_error) {
      std::string error;
      apply_result(lvh::validate_license(), error);
      if (!error.empty()) {
        show_error(error);
      }
    }

    template<typename ErrorHandler>
    void deactivate(ErrorHandler &show_error) {
      std::string error;
      apply_result(lvh::deactivate_license(), error);
      if (!error.empty()) {
        show_error(error);
      }
    }
#endif

    LicenseSnapshot snapshot_;
#if defined(_WIN32)
    std::array<char, 128> license_key_input_ {};
#endif
    std::string buy_url_;
    std::string manage_account_url_;
  };

  class ErrorPanel {
  public:
    void show(std::string_view message) {
      message_ = message.empty() ? "Operation failed." : std::string {message};
      open_popup_ = true;
    }

    void render() {
      if (open_popup_) {
        ImGui::OpenPopup("Error");
        open_popup_ = false;
      }

      if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", message_.c_str());
        if (ImGui::Button("OK", {120.0F, 0.0F})) {
          message_.clear();
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }

  private:
    std::string message_;
    bool open_popup_ = false;
  };

  class DevicePanel {
  public:
    void refresh_license() {
      license_panel_.refresh();
    }

    lvh::DeviceType current_device_type() const {
      const auto *choice = current_device_type_choice();
      return choice == nullptr ? lvh::DeviceType::gamepad : choice->type;
    }

    std::optional<lvh::DeviceProfile> current_profile() const {
      const auto *choice = current_profile_choice();
      if (choice == nullptr) {
        return std::nullopt;
      }
      return profile_for_choice(*choice);
    }

    const lvh::tools::virtualhid_control::ProfileChoice *selected_profile_choice() const {
      return current_profile_choice();
    }

    std::optional<lvh::DeviceProfile> control_profile(const SelectedSnapshot &selected) const {
      if (selected.has_device) {
        return selected.profile;
      }
      if (current_device_type() == lvh::DeviceType::mouse) {
        return lvh::profiles::mouse();
      }
      return current_profile();
    }

    template<typename CreateHandler, typename ResetHandler, typename RemoveHandler, typename RemoveAllHandler, typename ErrorHandler>
    void render(
      const std::vector<DeviceListItem> &devices,
      lvh::DeviceId &selected_id,
      CreateHandler create_device,
      ResetHandler reset_device,
      RemoveHandler remove_device,
      RemoveAllHandler remove_all_devices,
      ErrorHandler show_error
    ) {
      license_panel_.render(show_error);
      ImGui::Separator();
      render_device_type_selector();
      render_profile_selector();
      license_panel_.render_create_button(create_device);
      render_device_list(devices, selected_id);
      render_device_actions(devices.empty(), selected_id, reset_device, remove_device, remove_all_devices);
    }

  private:
    const lvh::tools::virtualhid_control::DeviceTypeChoice *current_device_type_choice() const {
      if (device_type_index_ >= device_type_choices.size()) {
        return nullptr;
      }
      return &device_type_choices[device_type_index_];
    }

    const lvh::tools::virtualhid_control::ProfileChoice *current_profile_choice() const {
      if (profile_index_ >= profile_choices.size()) {
        return nullptr;
      }
      return &profile_choices[profile_index_];
    }

    void render_device_type_selector() {
      ImGui::TextUnformatted("Device type");
      const auto *choice = current_device_type_choice();
      if (const auto preview = choice == nullptr ? std::string {"Select device type"} : to_utf8(choice->label); !ImGui::BeginCombo("##device-type", preview.c_str())) {
        return;
      }

      for (std::size_t index = 0; index < device_type_choices.size(); ++index) {
        render_device_type_option(index);
      }
      ImGui::EndCombo();
    }

    void render_device_type_option(std::size_t index) {
      const auto label = to_utf8(device_type_choices[index].label);
      const auto selected = index == device_type_index_;
      if (ImGui::Selectable(label.c_str(), selected)) {
        device_type_index_ = index;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }

    void render_profile_selector() {
      ImGui::TextUnformatted("Profile");
      if (current_device_type() != lvh::DeviceType::gamepad) {
        ImGui::TextUnformatted("Generic five-button mouse");
        return;
      }

      const auto *choice = current_profile_choice();
      if (const auto preview = choice == nullptr ? std::string {"Select profile"} : to_utf8(choice->label); !ImGui::BeginCombo("##profile", preview.c_str())) {
        return;
      }

      for (std::size_t index = 0; index < profile_choices.size(); ++index) {
        render_profile_option(index);
      }
      ImGui::EndCombo();
    }

    void render_profile_option(std::size_t index) {
      const auto label = to_utf8(profile_choices[index].label);
      const auto selected = index == profile_index_;
      if (ImGui::Selectable(label.c_str(), selected)) {
        profile_index_ = index;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }

    void render_device_list(const std::vector<DeviceListItem> &devices, lvh::DeviceId &selected_id) const {
      ImGui::Spacing();
      ImGui::TextUnformatted("Devices");
      if (!ImGui::BeginListBox("##devices", {-FLT_MIN, 150.0F})) {
        return;
      }

      if (devices.empty()) {
        ImGui::TextDisabled("No devices");
      }
      for (const auto &device : devices) {
        if (ImGui::Selectable(device.label.c_str(), selected_id == device.id)) {
          selected_id = device.id;
        }
      }
      ImGui::EndListBox();
    }

    template<typename ResetHandler, typename RemoveHandler, typename RemoveAllHandler>
    void render_device_actions(
      bool devices_empty,
      lvh::DeviceId selected_id,
      ResetHandler reset_device,
      RemoveHandler remove_device,
      RemoveAllHandler remove_all_devices
    ) const {
      {
        ScopedDisabled disabled {selected_id == 0};
        if (ImGui::Button("Reset", {-FLT_MIN, 0.0F})) {
          reset_device();
        }
        if (ImGui::Button("Remove selected", {-FLT_MIN, 0.0F})) {
          remove_device();
        }
      }
      {
        ScopedDisabled disabled {devices_empty};
        if (ImGui::Button("Remove all", {-FLT_MIN, 0.0F})) {
          remove_all_devices();
        }
      }
    }

    LicensePanel license_panel_;
    std::size_t device_type_index_ = 0;
    std::size_t profile_index_ = 3;
  };

  class MouseControlPanel {
  public:
    template<typename SubmitHandler>
    void tick(SubmitHandler submit_event) {
      dispatch_due_events(submit_event);
    }

    template<typename SubmitHandler>
    void render(const SelectedSnapshot &selected, SubmitHandler submit_event) {
      using enum MouseControlAction;

      ImGui::TextWrapped(
        "Keyboard control: use Tab or arrow keys to highlight a control, then Space or Enter to activate it. "
        "Mouse buttons remain pressed only while the activation key is held."
      );

      ImGui::SliderInt("Movement step", &motion_step_, 1, 500);
      ImGui::SliderInt("Scroll step", &scroll_step_, 1, 1200);

      ImGui::Checkbox("Delayed browser test", &delayed_test_);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        release_active_buttons(submit_event);
        cancel_all_scheduled_events(submit_event);
      }
      if (delayed_test_) {
        render_delayed_test_controls(submit_event);
      }

      ImGui::TextUnformatted("Relative movement");
      if (ImGui::BeginTable("mouse-motion", 4, ImGuiTableFlags_SizingStretchSame)) {
        render_action_button(selected, "Move left", move_left, submit_event);
        render_action_button(selected, "Move up", move_up, submit_event);
        render_action_button(selected, "Move down", move_down, submit_event);
        render_action_button(selected, "Move right", move_right, submit_event);
        ImGui::EndTable();
      }

      ImGui::TextUnformatted("Buttons (momentary)");
      if (ImGui::BeginTable("mouse-buttons", 3, ImGuiTableFlags_SizingStretchSame)) {
        for (std::size_t index = 0; index < mouse_button_choices.size(); ++index) {
          render_button_control(selected, index, submit_event);
        }
        ImGui::EndTable();
      }

      ImGui::TextUnformatted("Wheel");
      if (ImGui::BeginTable("mouse-wheel", 4, ImGuiTableFlags_SizingStretchSame)) {
        render_action_button(selected, "Pan left", pan_left, submit_event);
        render_action_button(selected, "Wheel up", wheel_up, submit_event);
        render_action_button(selected, "Wheel down", wheel_down, submit_event);
        render_action_button(selected, "Pan right", pan_right, submit_event);
        ImGui::EndTable();
      }
    }

    void forget_device(lvh::DeviceId id) {
      std::erase_if(scheduled_events_, [id](const ScheduledMouseEvent &event) {
        return event.device_id == id;
      });
      for (std::size_t index = 0; index < active_device_ids_.size(); ++index) {
        if (active_device_ids_[index] == id) {
          button_active_[index] = false;
          active_device_ids_[index] = 0;
        }
      }
    }

    void reset() {
      button_active_.fill(false);
      active_device_ids_.fill(0);
      scheduled_events_.clear();
    }

  private:
    template<typename SubmitHandler>
    void render_delayed_test_controls(SubmitHandler submit_event) {
      ImGui::SliderInt("Delay (seconds)", &delay_seconds_, 1, 10);
      ImGui::TextWrapped(
        "Activate an action, then switch to the browser before the delay expires. Button actions send one "
        "press-and-release click. Keep the pointer over the browser test target."
      );
      render_scheduled_status(submit_event);
    }

    template<typename SubmitHandler>
    void render_scheduled_status(SubmitHandler submit_event) {
      if (scheduled_events_.empty()) {
        ImGui::TextDisabled("No browser-test action queued.");
        return;
      }

      const auto next = std::ranges::min_element(scheduled_events_, {}, &ScheduledMouseEvent::due_at);
      const auto remaining = std::max(
        std::chrono::duration<float> {next->due_at - std::chrono::steady_clock::now()}.count(),
        0.0F
      );
      ImGui::TextColored(
        {1.0F, 0.8F, 0.2F, 1.0F},
        "Browser-test input queued: switch windows now (%.1f s).",
        remaining
      );
      if (ImGui::Button("Cancel queued input")) {
        cancel_all_scheduled_events(submit_event);
      }
    }

    template<typename SubmitHandler>
    void render_action_button(
      const SelectedSnapshot &selected,
      const char *label,
      MouseControlAction action,
      SubmitHandler submit_event
    ) {
      ImGui::TableNextColumn();
      ScopedDisabled disabled {!selected.has_device || selected.device_type != lvh::DeviceType::mouse};
      if (!ImGui::Button(label, {-FLT_MIN, 30.0F})) {
        return;
      }

      const auto event = mouse_event_for_action(action, motion_step_, scroll_step_);
      if (delayed_test_) {
        schedule_event(selected.id, event);
      } else {
        submit_event(selected.id, event);
      }
    }

    template<typename SubmitHandler>
    void render_button_control(const SelectedSnapshot &selected, std::size_t index, SubmitHandler submit_event) {
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(index));

      const auto pressed = selected.has_device && selected.device_type == lvh::DeviceType::mouse &&
                           selected.mouse_buttons[index];
      if (pressed) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      }

      auto clicked = false;
      auto active = false;
      {
        ScopedDisabled disabled {!selected.has_device || selected.device_type != lvh::DeviceType::mouse};
        const auto label = to_utf8(mouse_button_choices[index].label);
        clicked = ImGui::Button(label.c_str(), {-FLT_MIN, 30.0F});
        active = ImGui::IsItemActive();
      }

      if (delayed_test_ && clicked) {
        schedule_click(selected.id, mouse_button_choices[index].button);
      } else if (!delayed_test_) {
        update_active_button(selected.id, index, active, submit_event);
      }

      if (pressed) {
        ImGui::PopStyleColor();
      }
      ImGui::PopID();
    }

    template<typename SubmitHandler>
    void update_active_button(lvh::DeviceId selected_id, std::size_t index, bool active, SubmitHandler submit_event) {
      if (button_active_[index] == active) {
        return;
      }

      button_active_[index] = active;
      if (active) {
        active_device_ids_[index] = selected_id;
        submit_event(selected_id, mouse_button_event(mouse_button_choices[index].button, true));
        return;
      }

      const auto device_id = active_device_ids_[index];
      active_device_ids_[index] = 0;
      submit_event(device_id, mouse_button_event(mouse_button_choices[index].button, false));
    }

    void schedule_event(lvh::DeviceId id, const lvh::MouseEvent &event) {
      if (id == 0) {
        return;
      }
      scheduled_events_.push_back({
        .device_id = id,
        .event = event,
        .due_at = std::chrono::steady_clock::now() + std::chrono::seconds {delay_seconds_},
      });
    }

    void schedule_click(lvh::DeviceId id, lvh::MouseButton button) {
      if (id == 0) {
        return;
      }

      const auto press_at = std::chrono::steady_clock::now() + std::chrono::seconds {delay_seconds_};
      scheduled_events_.push_back({
        .device_id = id,
        .event = mouse_button_event(button, true),
        .due_at = press_at,
      });
      scheduled_events_.push_back({
        .device_id = id,
        .event = mouse_button_event(button, false),
        .due_at = press_at + std::chrono::milliseconds {100},
      });
    }

    template<typename SubmitHandler>
    void dispatch_due_events(SubmitHandler submit_event) {
      const auto now = std::chrono::steady_clock::now();
      auto iter = scheduled_events_.begin();
      while (iter != scheduled_events_.end()) {
        if (iter->due_at > now) {
          ++iter;
          continue;
        }

        const auto event = *iter;
        iter = scheduled_events_.erase(iter);
        submit_event(event.device_id, event.event);
      }
    }

    template<typename SubmitHandler>
    void release_active_buttons(SubmitHandler submit_event) {
      for (std::size_t index = 0; index < button_active_.size(); ++index) {
        if (!button_active_[index]) {
          continue;
        }
        submit_event(
          active_device_ids_[index],
          mouse_button_event(mouse_button_choices[index].button, false)
        );
        button_active_[index] = false;
        active_device_ids_[index] = 0;
      }
    }

    template<typename SubmitHandler>
    void cancel_all_scheduled_events(SubmitHandler submit_event) {
      std::vector<ScheduledMouseEvent> releases;
      for (const auto &event : scheduled_events_) {
        if (event.event.kind == lvh::MouseEventKind::button && !event.event.pressed) {
          releases.push_back(event);
        }
      }
      scheduled_events_.clear();
      for (const auto &release : releases) {
        submit_event(release.device_id, release.event);
      }
    }

    std::array<bool, mouse_button_choices.size()> button_active_ {};
    std::array<lvh::DeviceId, mouse_button_choices.size()> active_device_ids_ {};
    int motion_step_ = 25;
    int scroll_step_ = 120;
    bool delayed_test_ = false;
    int delay_seconds_ = 3;
    std::vector<ScheduledMouseEvent> scheduled_events_;
  };

  int axis_position(const SelectedSnapshot &selected, std::size_t index) {
    if (!selected.has_device) {
      return 0;
    }

    switch (index) {
      case 0:
        return axis_to_slider(selected.state.left_stick.x);
      case 1:
        return axis_to_slider(selected.state.left_stick.y);
      case 2:
        return axis_to_slider(selected.state.right_stick.x);
      case 3:
        return axis_to_slider(selected.state.right_stick.y);
      case 4:
        return trigger_to_slider(selected.state.left_trigger);
      case 5:
        return trigger_to_slider(selected.state.right_trigger);
      default:
        return 0;
    }
  }

  class ControlApp {
  public:
    ControlApp() {
      lvh::RuntimeOptions options;
      options.backend = lvh::BackendKind::platform_default;
      runtime_ = lvh::Runtime::create(options);
    }

    ~ControlApp() noexcept {
      try {
        close_all_devices();
      } catch (const std::exception &ex) {
        SDL_Log("Failed to close virtual devices: %s", ex.what());
      } catch (...) {
        SDL_Log("Failed to close virtual devices.");
      }
    }

    ControlApp(const ControlApp &) = delete;
    ControlApp &operator=(const ControlApp &) = delete;

    void tick() {
      mouse_control_panel_.tick([this](lvh::DeviceId id, const lvh::MouseEvent &event) {
        submit_mouse_event(id, event);
      });
    }

    void render() {
      const auto devices = snapshot_devices();
      const auto selected = snapshot_selected_device();

      const auto &io = ImGui::GetIO();
      ImGui::SetNextWindowPos({0.0F, 0.0F});
      ImGui::SetNextWindowSize(io.DisplaySize);

      constexpr auto window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
      ImGui::Begin("libvirtualhid control", nullptr, window_flags);

      const auto backend = backend_text(*runtime_);
      ImGui::TextWrapped("%s", backend.c_str());
      ImGui::Separator();

      const auto create_device = [this] {
        create_selected_device();
      };
      const auto reset_device = [this] {
        reset_selected_device();
      };
      const auto remove_device = [this] {
        remove_selected_device();
      };
      const auto remove_all = [this] {
        remove_all_devices();
      };
      const auto show_error = [this](std::string_view message) {
        error_panel_.show(message);
      };
      const auto render_devices = [this, &devices, &create_device, &reset_device, &remove_device, &remove_all, &show_error] {
        device_panel_.render(
          devices,
          selected_id_,
          create_device,
          reset_device,
          remove_device,
          remove_all,
          show_error
        );
      };

      if (ImGui::GetContentRegionAvail().x < 760.0F) {
        render_devices();
        ImGui::Separator();
        render_control_panel(selected);
      } else if (ImGui::BeginTable("control-layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Devices", ImGuiTableColumnFlags_WidthFixed, 220.0F);
        ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        render_devices();

        ImGui::TableSetColumnIndex(1);
        render_control_panel(selected);

        ImGui::EndTable();
      }

      error_panel_.render();
      ImGui::End();
    }

  private:
    std::vector<DeviceListItem> snapshot_devices() const {
      std::vector<DeviceListItem> result;
      std::lock_guard lock {mutex_};
      result.reserve(devices_.size() + mice_.size());
      for (const auto &[id, device] : devices_) {
        result.push_back({.id = id, .label = std::format("#{} {}", id, device.profile_label)});
      }
      for (const auto &[id, device] : mice_) {
        result.push_back({.id = id, .label = std::format("#{} {}", id, device.profile_label)});
      }
      std::ranges::sort(result, {}, &DeviceListItem::id);
      return result;
    }

    SelectedSnapshot snapshot_selected_device() const {
      std::lock_guard lock {mutex_};
      SelectedSnapshot snapshot;
      if (const auto *device = selected_device_locked(); device != nullptr && device->adapter != nullptr && device->adapter->gamepad() != nullptr) {
        const auto *gamepad = device->adapter->gamepad();
        snapshot.has_device = true;
        snapshot.id = gamepad->device_id();
        snapshot.device_type = lvh::DeviceType::gamepad;
        snapshot.profile_label = device->profile_label;
        snapshot.profile = gamepad->profile();
        snapshot.state = device->adapter->state();
        snapshot.support = device->adapter->support();
        snapshot.submit_count = gamepad->submit_count();
        snapshot.nodes = gamepad->device_nodes();
        snapshot.outputs = device->outputs;
        snapshot.output_text = to_utf8(output_summary(*device, snapshot.profile));

        std::ostringstream state;
        state << snapshot.profile_label << " #" << snapshot.id << "\n"
              << to_utf8(device_type_name(snapshot.profile.device_type)) << " | " << snapshot.profile.name << "\n"
              << "L(" << snapshot.state.left_stick.x << ", " << snapshot.state.left_stick.y << ") "
              << "R(" << snapshot.state.right_stick.x << ", " << snapshot.state.right_stick.y << ") "
              << "LT " << snapshot.state.left_trigger << " RT " << snapshot.state.right_trigger << "\n";
        if (snapshot.state.battery) {
          state << "Battery " << to_utf8(battery_state_name(snapshot.state.battery->state)) << " "
                << static_cast<unsigned>(snapshot.state.battery->percentage) << "% | ";
        } else {
          state << "Battery unset | ";
        }
        state << snapshot.submit_count << " submits";
        snapshot.state_text = state.str();
        return snapshot;
      }

      const auto *device = selected_mouse_locked();
      if (device == nullptr || device->mouse == nullptr) {
        return snapshot;
      }

      snapshot.has_device = true;
      snapshot.id = device->mouse->device_id();
      snapshot.device_type = lvh::DeviceType::mouse;
      snapshot.profile_label = device->profile_label;
      snapshot.profile = device->mouse->profile();
      snapshot.mouse_buttons = device->buttons;
      snapshot.submit_count = device->mouse->submit_count();
      snapshot.nodes = device->mouse->device_nodes();
      snapshot.output_text = "Output: mouse is input-only.";

      std::ostringstream state;
      state << snapshot.profile_label << " #" << snapshot.id << "\n"
            << to_utf8(device_type_name(snapshot.profile.device_type)) << " | " << snapshot.profile.name << "\n"
            << snapshot.submit_count << " submits";
#if defined(_WIN32)
      state << "\n"
            << (snapshot.nodes.empty() ? "SendInput fallback (no HID device node)" : "Driver-backed HID mouse");
#endif
      snapshot.state_text = state.str();
      return snapshot;
    }

    void render_control_panel(const SelectedSnapshot &selected) {
      const auto profile = device_panel_.control_profile(selected);
      if (selected.has_device) {
        ImGui::TextWrapped("%s", selected.state_text.c_str());
      } else {
        ImGui::TextUnformatted("No device selected.");
      }

      if (profile) {
        const auto feature_text = to_utf8(device_feature_summary(*profile));
        ImGui::TextWrapped("%s", feature_text.c_str());
      } else {
        ImGui::TextDisabled("No profile selected.");
      }

      ImGui::Separator();
      if (const auto device_type = selected.has_device ? selected.device_type : device_panel_.current_device_type(); device_type == lvh::DeviceType::mouse) {
        mouse_control_panel_.render(selected, [this](lvh::DeviceId id, const lvh::MouseEvent &event) {
          submit_mouse_event(id, event);
        });
      } else {
        render_button_controls(selected, profile);
        ImGui::Separator();
        render_axis_controls(selected);
        ImGui::Separator();
        render_battery_controls(selected, profile);
      }
      ImGui::Separator();
      render_output_controls(selected);
    }

    void render_button_controls(
      const SelectedSnapshot &selected,
      const std::optional<lvh::DeviceProfile> &profile
    ) {
      ImGui::Checkbox("Lock buttons", &lock_buttons_);
      if (ImGui::IsItemDeactivatedAfterEdit()) {
        handle_button_lock_changed();
      }

      std::array<bool, button_choices.size()> visible_buttons {};
      auto battery_visible = false;
      if (profile) {
        update_visible_controls_for_profile(*profile, visible_buttons, battery_visible);
      }

      if (!selected.has_device || lock_buttons_) {
        button_active_.fill(false);
      }

      const auto column_count = ImGui::GetContentRegionAvail().x < 480.0F ? 3 : 4;
      if (ImGui::BeginTable("buttons", column_count, ImGuiTableFlags_SizingStretchSame)) {
        for (std::size_t index = 0; index < button_choices.size(); ++index) {
          if (!visible_buttons[index]) {
            continue;
          }

          render_button_control(selected, index);
        }
        ImGui::EndTable();
      }
    }

    void render_button_control(const SelectedSnapshot &selected, std::size_t index) {
      ImGui::TableNextColumn();
      ImGui::PushID(static_cast<int>(index));

      const auto pressed = selected.has_device && selected.state.buttons.test(button_choices[index].button);
      if (pressed) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      }

      {
        ScopedDisabled disabled {!selected.has_device};
        const auto label = to_utf8(button_choices[index].label);
        if (const auto clicked = ImGui::Button(label.c_str(), {-FLT_MIN, 30.0F}); lock_buttons_ && clicked) {
          toggle_selected_button(index);
        }
        if (!lock_buttons_) {
          const auto active = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
          if (button_active_[index] != active) {
            button_active_[index] = active;
            set_selected_button(index, active);
          }
        }
      }

      if (pressed) {
        ImGui::PopStyleColor();
      }
      ImGui::PopID();
    }

    void render_axis_controls(const SelectedSnapshot &selected) {
      ImGui::TextUnformatted("Axes");
      if (ImGui::BeginTable("axes", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (std::size_t index = 0; index < axis_choices.size(); ++index) {
          ImGui::TableNextColumn();
          ImGui::PushID(static_cast<int>(index));
          const auto label = to_utf8(axis_choices[index].label);
          ImGui::TextUnformatted(label.c_str());

          auto position = axis_position(selected, index);
          {
            ScopedDisabled disabled {!selected.has_device};
            if (ImGui::SliderInt("##axis", &position, axis_choices[index].minimum, axis_choices[index].maximum)) {
              set_selected_axis(index, position);
            }
          }
          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    }

    void render_battery_controls(
      const SelectedSnapshot &selected,
      const std::optional<lvh::DeviceProfile> &profile
    ) {
      auto visible_buttons = std::array<bool, button_choices.size()> {};
      auto battery_visible = false;
      if (profile) {
        update_visible_controls_for_profile(*profile, visible_buttons, battery_visible);
      }
      if (!battery_visible) {
        return;
      }

      ImGui::TextUnformatted("Battery");
      const auto enabled = selected.has_device && selected.support.supports_battery;
      if (selected.state.battery) {
        battery_state_index_ = battery_choice_index(selected.state.battery->state);
        battery_percentage_ = selected.state.battery->percentage;
      }
      if (selected_battery_state_is_full(battery_state_index_)) {
        battery_percentage_ = 100;
      }

      {
        ScopedDisabled disabled {!enabled};
        if (const auto state_label = to_utf8(battery_choices[static_cast<std::size_t>(battery_state_index_)].label); ImGui::BeginCombo("State", state_label.c_str())) {
          for (std::size_t index = 0; index < battery_choices.size(); ++index) {
            const auto label = to_utf8(battery_choices[index].label);
            const auto selected_state = static_cast<int>(index) == battery_state_index_;
            if (ImGui::Selectable(label.c_str(), selected_state)) {
              battery_state_index_ = static_cast<int>(index);
              battery_percentage_ = selected_battery_state_is_full(battery_state_index_) ? 100 : battery_percentage_;
              set_selected_battery_from_controls();
            }
            if (selected_state) {
              ImGui::SetItemDefaultFocus();
            }
          }
          ImGui::EndCombo();
        }

        {
          ScopedDisabled percentage_disabled {selected_battery_state_is_full(battery_state_index_)};
          if (ImGui::SliderInt("Percentage", &battery_percentage_, 0, 100)) {
            set_selected_battery_from_controls();
          }
        }
      }

      {
        ScopedDisabled disabled {!enabled || !selected.state.battery.has_value()};
        if (ImGui::Button("Clear battery")) {
          clear_selected_battery();
        }
      }
    }

    void render_output_controls(const SelectedSnapshot &selected) const {
      ImGui::TextWrapped("%s", selected.has_device ? selected.output_text.c_str() : "Output: no selected device.");
      if (ImGui::BeginTable("diagnostics", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Device nodes");
        ImGui::TableSetupColumn("Output reports");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("device-nodes", {0.0F, 170.0F}, ImGuiChildFlags_Borders)) {
          render_device_nodes(selected);
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("output-reports", {0.0F, 170.0F}, ImGuiChildFlags_Borders)) {
          render_output_reports(selected);
        }
        ImGui::EndChild();

        ImGui::EndTable();
      }
    }

    void create_selected_device() {
      if (device_panel_.current_device_type() == lvh::DeviceType::mouse) {
        create_mouse();
      } else {
        create_gamepad();
      }
    }

    void create_gamepad() {
      const auto *choice = device_panel_.selected_profile_choice();
      if (choice == nullptr) {
        error_panel_.show("Select a profile first.");
        return;
      }

      const auto profile = profile_for_choice(*choice);
      if (!profile) {
        error_panel_.show("Could not create the selected profile.");
        return;
      }

      lvh::CreateGamepadOptions options;
      options.profile = *profile;
      options.metadata.global_index = static_cast<int>(next_metadata_index_++);
      options.metadata.client_relative_index = 0;
      options.metadata.client_type = choice->client_type;
      options.metadata.has_motion_sensors = profile->capabilities.supports_motion;
      options.metadata.has_touchpad = profile->capabilities.supports_touchpad;
      options.metadata.has_rgb_led = profile->capabilities.supports_rgb_led;
      options.metadata.has_battery = profile->capabilities.supports_battery;
      options.metadata.stable_id = std::format("libvirtualhid-control-{}", options.metadata.global_index);

      auto created = lvh::GamepadStateAdapter::create(*runtime_, options);
      if (!created) {
        error_panel_.show(created.status.message());
        return;
      }

      const auto *gamepad = created.adapter->gamepad();
      if (gamepad == nullptr) {
        error_panel_.show("Created gamepad handle is missing.");
        return;
      }

      const auto id = gamepad->device_id();
      created.adapter->set_output_callback([this, id](const lvh::GamepadOutput &output) {
        record_output(id, output);
      });

      ControlledGamepad device;
      device.profile_label = to_utf8(choice->label);
      device.adapter = std::move(created.adapter);

      {
        std::lock_guard lock {mutex_};
        devices_[id] = std::move(device);
      }
      selected_id_ = id;
      device_panel_.refresh_license();
    }

    void create_mouse() {
      lvh::CreateMouseOptions options;
      options.profile = lvh::profiles::mouse();
      options.stable_id = std::format("libvirtualhid-control-mouse-{}", next_metadata_index_++);

      auto created = runtime_->create_mouse(options);
      if (!created) {
        error_panel_.show(created.status.message());
        return;
      }

      const auto id = created.mouse->device_id();
      ControlledMouse device;
      device.profile_label = "Mouse";
      device.mouse = std::move(created.mouse);

      {
        std::lock_guard lock {mutex_};
        mice_[id] = std::move(device);
      }
      selected_id_ = id;
      device_panel_.refresh_license();
    }

    void reset_selected_device() {
      auto status = lvh::OperationStatus::success();
      auto reset_id = lvh::DeviceId {0};
      {
        std::lock_guard lock {mutex_};
        reset_id = selected_id_;
        if (auto *device = selected_device_locked(); device != nullptr) {
          status = device->adapter->set_state({});
        } else if (auto *mouse = selected_mouse_locked(); mouse != nullptr) {
          for (std::size_t index = 0; index < mouse->buttons.size(); ++index) {
            if (!mouse->buttons[index]) {
              continue;
            }

            const auto release_status = mouse->mouse->button(mouse_button_choices[index].button, false);
            if (release_status.ok()) {
              mouse->buttons[index] = false;
            } else if (status.ok()) {
              status = release_status;
            }
          }
        } else {
          return;
        }
      }
      button_active_.fill(false);
      mouse_control_panel_.forget_device(reset_id);
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
    }

    void remove_selected_device() {
      std::unique_ptr<lvh::GamepadStateAdapter> adapter;
      std::unique_ptr<lvh::Mouse> mouse;
      auto removed_id = lvh::DeviceId {0};
      {
        std::lock_guard lock {mutex_};
        if (selected_id_ == 0) {
          return;
        }
        removed_id = selected_id_;

        if (const auto gamepad_iter = devices_.find(selected_id_); gamepad_iter != devices_.end()) {
          adapter = std::move(gamepad_iter->second.adapter);
          devices_.erase(gamepad_iter);
        } else if (const auto mouse_iter = mice_.find(selected_id_); mouse_iter != mice_.end()) {
          mouse = std::move(mouse_iter->second.mouse);
          mice_.erase(mouse_iter);
        } else {
          return;
        }

        selected_id_ = first_device_id(devices_, mice_);
      }
      button_active_.fill(false);
      mouse_control_panel_.forget_device(removed_id);

      auto status = lvh::OperationStatus::success();
      if (adapter != nullptr) {
        status = adapter->close();
      } else if (mouse != nullptr) {
        status = mouse->close();
      }
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
      device_panel_.refresh_license();
    }

    void remove_all_devices() {
      const auto handles = take_all_devices();
      button_active_.fill(false);
      mouse_control_panel_.reset();
      close_devices(handles, true);
      device_panel_.refresh_license();
    }

    void submit_mouse_event(lvh::DeviceId id, const lvh::MouseEvent &event) {
      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        const auto iter = mice_.find(id);
        if (iter == mice_.end() || iter->second.mouse == nullptr) {
          return;
        }

        auto &device = iter->second;
        status = device.mouse->submit(event);
        if (status.ok() && event.kind == lvh::MouseEventKind::button) {
          for (std::size_t index = 0; index < mouse_button_choices.size(); ++index) {
            if (mouse_button_choices[index].button == event.button) {
              device.buttons[index] = event.pressed;
              break;
            }
          }
        }
      }
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
    }

    void toggle_selected_button(std::size_t index) {
      auto pressed = false;
      {
        std::lock_guard lock {mutex_};
        const auto *device = selected_device_locked();
        if (device == nullptr || index >= button_choices.size()) {
          return;
        }
        pressed = !device->adapter->state().buttons.test(button_choices[index].button);
      }
      set_selected_button(index, pressed);
    }

    void set_selected_button(std::size_t index, bool pressed) {
      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr || index >= button_choices.size()) {
          return;
        }
        status = device->adapter->set_button(button_choices[index].button, pressed);
      }
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
    }

    void handle_button_lock_changed() {
      button_active_.fill(false);
      if (lock_buttons_) {
        return;
      }

      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr) {
          return;
        }

        auto state = device->adapter->state();
        state.buttons.clear();
        status = device->adapter->set_state(state);
      }
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
    }

    void set_selected_axis(std::size_t index, int position) {
      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr) {
          return;
        }

        auto state = device->adapter->state();
        const auto value = slider_to_float(position);
        switch (index) {
          case 0:
            state.left_stick.x = value;
            status = device->adapter->set_left_stick(state.left_stick);
            break;
          case 1:
            state.left_stick.y = value;
            status = device->adapter->set_left_stick(state.left_stick);
            break;
          case 2:
            state.right_stick.x = value;
            status = device->adapter->set_right_stick(state.right_stick);
            break;
          case 3:
            state.right_stick.y = value;
            status = device->adapter->set_right_stick(state.right_stick);
            break;
          case 4:
            status = device->adapter->set_left_trigger(value);
            break;
          case 5:
            status = device->adapter->set_right_trigger(value);
            break;
          default:
            break;
        }
      }
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
    }

    void set_selected_battery_from_controls() {
      if (battery_state_index_ < 0 || static_cast<std::size_t>(battery_state_index_) >= battery_choices.size()) {
        return;
      }

      lvh::GamepadBattery battery;
      battery.state = battery_choices[static_cast<std::size_t>(battery_state_index_)].state;
      if (battery.state == lvh::GamepadBatteryState::full) {
        battery_percentage_ = 100;
      }
      battery.percentage = static_cast<std::uint8_t>(std::clamp(battery_percentage_, 0, 100));

      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr) {
          return;
        }
        status = device->adapter->set_battery(battery);
      }
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
    }

    void clear_selected_battery() {
      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr) {
          return;
        }
        status = device->adapter->clear_battery();
      }
      if (!status.ok()) {
        error_panel_.show(status.message());
      }
    }

    void record_output(lvh::DeviceId id, const lvh::GamepadOutput &output) {
      std::lock_guard lock {mutex_};
      const auto iter = devices_.find(id);
      if (iter == devices_.end()) {
        return;
      }

      lvh::tools::virtualhid_control::record_output(
        iter->second,
        output,
        next_output_sequence_,
        max_output_events_
      );
    }

    void close_all_devices() {
      const auto handles = take_all_devices();
      close_devices(handles, false);
    }

    DeviceHandles take_all_devices() {
      DeviceHandles handles;
      std::lock_guard lock {mutex_};
      for (auto &[id, device] : devices_) {
        handles.gamepads.push_back(std::move(device.adapter));
      }
      for (auto &[id, device] : mice_) {
        handles.mice.push_back(std::move(device.mouse));
      }
      devices_.clear();
      mice_.clear();
      selected_id_ = 0;
      return handles;
    }

    void close_devices(const DeviceHandles &handles, bool report_errors) {
      std::optional<std::string> first_error;
      for (const auto &adapter : handles.gamepads) {
        if (adapter) {
          if (const auto status = adapter->close(); !status.ok() && report_errors && !first_error) {
            first_error = std::string {status.message()};
          }
        }
      }
      for (const auto &mouse : handles.mice) {
        if (mouse) {
          if (const auto status = mouse->close(); !status.ok() && report_errors && !first_error) {
            first_error = std::string {status.message()};
          }
        }
      }

      if (first_error) {
        error_panel_.show(*first_error);
      }
    }

    ControlledGamepad *selected_device_locked() {
      if (selected_id_ == 0) {
        return nullptr;
      }
      const auto iter = devices_.find(selected_id_);
      if (iter == devices_.end()) {
        return nullptr;
      }
      return &iter->second;
    }

    const ControlledGamepad *selected_device_locked() const {
      if (selected_id_ == 0) {
        return nullptr;
      }
      const auto iter = devices_.find(selected_id_);
      if (iter == devices_.end()) {
        return nullptr;
      }
      return &iter->second;
    }

    ControlledMouse *selected_mouse_locked() {
      if (selected_id_ == 0) {
        return nullptr;
      }
      const auto iter = mice_.find(selected_id_);
      if (iter == mice_.end()) {
        return nullptr;
      }
      return &iter->second;
    }

    const ControlledMouse *selected_mouse_locked() const {
      if (selected_id_ == 0) {
        return nullptr;
      }
      const auto iter = mice_.find(selected_id_);
      if (iter == mice_.end()) {
        return nullptr;
      }
      return &iter->second;
    }

    std::unique_ptr<lvh::Runtime> runtime_;
    mutable std::mutex mutex_;
    std::map<lvh::DeviceId, ControlledGamepad> devices_;
    std::map<lvh::DeviceId, ControlledMouse> mice_;
    lvh::DeviceId selected_id_ = 0;
    bool lock_buttons_ = false;
    std::array<bool, button_choices.size()> button_active_ {};
    int battery_state_index_ = battery_choice_index(lvh::GamepadBatteryState::full);
    int battery_percentage_ = 100;
    std::uint64_t next_metadata_index_ = 0;
    std::uint64_t next_output_sequence_ = 1;
    DevicePanel device_panel_;
    MouseControlPanel mouse_control_panel_;
    ErrorPanel error_panel_;
    static constexpr std::size_t max_output_events_ = 50;
  };

  int run_control_ui() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
      SDL_Log("SDL_Init failed: %s", SDL_GetError());
      return 1;
    }

    auto main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (main_scale <= 0.0F) {
      main_scale = 1.0F;
    }

    constexpr auto base_width = 860;
    constexpr auto base_height = 760;
    constexpr auto minimum_width = 600;
    constexpr auto minimum_height = 700;
    const auto window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    auto *window = SDL_CreateWindow(
      "libvirtualhid control",
      scaled_window_dimension(base_width, main_scale),
      scaled_window_dimension(base_height, main_scale),
      window_flags
    );
    if (window == nullptr) {
      SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
      SDL_Quit();
      return 1;
    }

    if (!SDL_SetWindowMinimumSize(
          window,
          scaled_window_dimension(minimum_width, main_scale),
          scaled_window_dimension(minimum_height, main_scale)
        )) {
      SDL_Log("SDL_SetWindowMinimumSize failed: %s", SDL_GetError());
    }

    auto *renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
      SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
      SDL_DestroyWindow(window);
      SDL_Quit();
      return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The tool creates virtual gamepads that SDL can see, so gamepad navigation would feed back into the UI.
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    style.FontScaleDpi = main_scale;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ControlApp app;
    auto done = false;
    while (!done) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
          done = true;
        }
        if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
          done = true;
        }
      }

      app.tick();

      if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0U) {
        SDL_Delay(10);
        continue;
      }

      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();

      app.render();

      ImGui::Render();
      SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
      SDL_SetRenderDrawColorFloat(renderer, 0.08F, 0.08F, 0.09F, 1.0F);
      SDL_RenderClear(renderer);
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
      SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
  }

}  // namespace

/**
 * @brief Run the libvirtualhid diagnostic control UI.
 * @return Process exit code.
 */
int main(int, char **) {
  return run_control_ui();
}
