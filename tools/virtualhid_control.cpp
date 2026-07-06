/**
 * @file tools/virtualhid_control.cpp
 * @brief SDL3 and Dear ImGui UI for creating and testing libvirtualhid gamepads.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
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
  using lvh::tools::virtualhid_control::device_type_name;
  using lvh::tools::virtualhid_control::node_kind_name;
  using lvh::tools::virtualhid_control::output_kind_name;
  using lvh::tools::virtualhid_control::output_summary;
  using lvh::tools::virtualhid_control::OutputLogEntry;
  using lvh::tools::virtualhid_control::OutputState;
  using lvh::tools::virtualhid_control::profile_choices;
  using lvh::tools::virtualhid_control::profile_feature_summary;
  using lvh::tools::virtualhid_control::profile_for_choice;
  using lvh::tools::virtualhid_control::raw_hex;
  using lvh::tools::virtualhid_control::slider_to_float;
  using lvh::tools::virtualhid_control::trigger_to_slider;
  using lvh::tools::virtualhid_control::update_visible_controls_for_profile;

  struct ControlledGamepad: OutputState {
    std::string profile_label;
    std::unique_ptr<lvh::GamepadStateAdapter> adapter;
  };

  struct DeviceListItem {
    lvh::DeviceId id = 0;
    std::string label;
  };

  struct SelectedSnapshot {
    bool has_device = false;
    lvh::DeviceId id = 0;
    std::string profile_label;
    lvh::DeviceProfile profile;
    lvh::GamepadState state;
    lvh::GamepadProfileSupport support;
    std::uint64_t submit_count = 0;
    std::vector<lvh::DeviceNode> nodes;
    std::vector<OutputLogEntry> outputs;
    std::string state_text;
    std::string output_text;
  };

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

  std::string to_utf8(std::wstring_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t index = 0; index < value.size(); ++index) {
      auto codepoint = static_cast<char32_t>(value[index]);
      if constexpr (sizeof(wchar_t) == 2) {
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF && index + 1U < value.size()) {
          const auto low = static_cast<char32_t>(value[index + 1U]);
          if (low >= 0xDC00 && low <= 0xDFFF) {
            codepoint = 0x10000 + ((codepoint - 0xD800) << 10U) + (low - 0xDC00);
            ++index;
          }
        }
      }
      append_utf8(result, codepoint);
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

  int scaled_window_dimension(int value, float scale) {
    return std::max(value, static_cast<int>(static_cast<float>(value) * scale));
  }

  bool selected_battery_state_is_full(int state_index) {
    return state_index >= 0 &&
           static_cast<std::size_t>(state_index) < battery_choices.size() &&
           battery_choices[static_cast<std::size_t>(state_index)].state == lvh::GamepadBatteryState::full;
  }

  class ControlApp {
  public:
    ControlApp() {
      lvh::RuntimeOptions options;
      options.backend = lvh::BackendKind::platform_default;
      runtime_ = lvh::Runtime::create(options);
    }

    ~ControlApp() {
      close_all_devices();
    }

    ControlApp(const ControlApp &) = delete;
    ControlApp &operator=(const ControlApp &) = delete;

    void render() {
      const auto devices = snapshot_devices();
      const auto selected = snapshot_selected_device();

      const auto &io = ImGui::GetIO();
      ImGui::SetNextWindowPos({0.0F, 0.0F});
      ImGui::SetNextWindowSize(io.DisplaySize);

      constexpr auto window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
      ImGui::Begin("libvirtualhid control", nullptr, window_flags);

      const auto backend = backend_text();
      ImGui::TextUnformatted(backend.c_str());
      ImGui::Separator();

      if (ImGui::BeginTable("control-layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Devices", ImGuiTableColumnFlags_WidthFixed, 330.0F);
        ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        render_device_panel(devices);

        ImGui::TableSetColumnIndex(1);
        render_control_panel(selected);

        ImGui::EndTable();
      }

      render_error_popup();
      ImGui::End();
    }

  private:
    std::vector<DeviceListItem> snapshot_devices() const {
      std::vector<DeviceListItem> result;
      std::lock_guard lock {mutex_};
      result.reserve(devices_.size());
      for (const auto &[id, device] : devices_) {
        result.push_back({.id = id, .label = std::format("#{} {}", id, device.profile_label)});
      }
      return result;
    }

    SelectedSnapshot snapshot_selected_device() const {
      std::lock_guard lock {mutex_};
      SelectedSnapshot snapshot;
      const auto *device = selected_device_locked();
      if (device == nullptr || device->adapter == nullptr || device->adapter->gamepad() == nullptr) {
        return snapshot;
      }

      const auto *gamepad = device->adapter->gamepad();
      snapshot.has_device = true;
      snapshot.id = gamepad->device_id();
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

    std::string backend_text() const {
      const auto &caps = runtime_->capabilities();
      auto text = std::string {"Backend: "} + caps.backend_name;
      text += caps.supports_gamepad ? " | gamepad available" : " | gamepad unavailable";
      text += caps.supports_output_reports ? " | output reports available" : " | output reports unavailable";
      return text;
    }

    const lvh::tools::virtualhid_control::ProfileChoice *current_profile_choice() const {
      if (profile_index_ >= profile_choices.size()) {
        return nullptr;
      }
      return &profile_choices[profile_index_];
    }

    std::optional<lvh::DeviceProfile> current_profile() const {
      const auto *choice = current_profile_choice();
      if (choice == nullptr) {
        return std::nullopt;
      }
      return profile_for_choice(*choice);
    }

    std::optional<lvh::DeviceProfile> control_profile(const SelectedSnapshot &selected) const {
      if (selected.has_device) {
        return selected.profile;
      }
      return current_profile();
    }

    void render_device_panel(const std::vector<DeviceListItem> &devices) {
      ImGui::TextUnformatted("Profile");
      const auto *choice = current_profile_choice();
      const auto preview = choice == nullptr ? std::string {"Select profile"} : to_utf8(choice->label);
      if (ImGui::BeginCombo("##profile", preview.c_str())) {
        for (std::size_t index = 0; index < profile_choices.size(); ++index) {
          const auto label = to_utf8(profile_choices[index].label);
          const auto selected = index == profile_index_;
          if (ImGui::Selectable(label.c_str(), selected)) {
            profile_index_ = index;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      if (ImGui::Button("Create", {-FLT_MIN, 0.0F})) {
        create_gamepad();
      }

      ImGui::Spacing();
      ImGui::TextUnformatted("Devices");
      if (ImGui::BeginListBox("##devices", {-FLT_MIN, 150.0F})) {
        if (devices.empty()) {
          ImGui::TextDisabled("No devices");
        }
        for (const auto &device : devices) {
          const auto selected = selected_id_ == device.id;
          if (ImGui::Selectable(device.label.c_str(), selected)) {
            selected_id_ = device.id;
          }
        }
        ImGui::EndListBox();
      }

      {
        ScopedDisabled disabled {selected_id_ == 0};
        if (ImGui::Button("Reset", {-FLT_MIN, 0.0F})) {
          reset_selected_device();
        }
        if (ImGui::Button("Remove selected", {-FLT_MIN, 0.0F})) {
          remove_selected_device();
        }
      }
      {
        ScopedDisabled disabled {devices.empty()};
        if (ImGui::Button("Remove all", {-FLT_MIN, 0.0F})) {
          remove_all_devices();
        }
      }
    }

    void render_control_panel(const SelectedSnapshot &selected) {
      const auto profile = control_profile(selected);
      if (selected.has_device) {
        ImGui::TextWrapped("%s", selected.state_text.c_str());
      } else {
        ImGui::TextUnformatted("No device selected.");
      }

      if (profile) {
        const auto feature_text = to_utf8(profile_feature_summary(*profile));
        ImGui::TextWrapped("%s", feature_text.c_str());
      } else {
        ImGui::TextDisabled("No profile selected.");
      }

      ImGui::Separator();
      render_button_controls(selected, profile);
      ImGui::Separator();
      render_axis_controls(selected);
      ImGui::Separator();
      render_battery_controls(selected, profile);
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

      if (ImGui::BeginTable("buttons", 4, ImGuiTableFlags_SizingStretchSame)) {
        for (std::size_t index = 0; index < button_choices.size(); ++index) {
          if (!visible_buttons[index]) {
            continue;
          }

          ImGui::TableNextColumn();
          ImGui::PushID(static_cast<int>(index));

          const auto pressed = selected.has_device && selected.state.buttons.test(button_choices[index].button);
          if (pressed) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
          }

          {
            ScopedDisabled disabled {!selected.has_device};
            const auto label = to_utf8(button_choices[index].label);
            const auto clicked = ImGui::Button(label.c_str(), {-FLT_MIN, 30.0F});
            if (lock_buttons_) {
              if (clicked) {
                toggle_selected_button(index);
              }
            } else {
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
        ImGui::EndTable();
      }
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

    int axis_position(const SelectedSnapshot &selected, std::size_t index) const {
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
        const auto state_label = to_utf8(battery_choices[static_cast<std::size_t>(battery_state_index_)].label);
        if (ImGui::BeginCombo("State", state_label.c_str())) {
          for (std::size_t index = 0; index < battery_choices.size(); ++index) {
            const auto label = to_utf8(battery_choices[index].label);
            const auto selected_state = static_cast<int>(index) == battery_state_index_;
            if (ImGui::Selectable(label.c_str(), selected_state)) {
              battery_state_index_ = static_cast<int>(index);
              if (selected_battery_state_is_full(battery_state_index_)) {
                battery_percentage_ = 100;
              }
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

    void render_output_controls(const SelectedSnapshot &selected) {
      ImGui::TextWrapped("%s", selected.has_device ? selected.output_text.c_str() : "Output: no selected device.");
      if (ImGui::BeginTable("diagnostics", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Device nodes");
        ImGui::TableSetupColumn("Output reports");
        ImGui::TableHeadersRow();
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginChild("device-nodes", {0.0F, 170.0F}, ImGuiChildFlags_Borders)) {
          if (!selected.has_device) {
            ImGui::TextDisabled("No selected device.");
          } else if (selected.nodes.empty()) {
            ImGui::TextDisabled("No device nodes reported yet.");
          } else {
            for (const auto &node : selected.nodes) {
              const auto line = to_utf8(node_kind_name(node.kind)) + ": " + node.path;
              ImGui::TextWrapped("%s", line.c_str());
            }
          }
        }
        ImGui::EndChild();

        ImGui::TableSetColumnIndex(1);
        if (ImGui::BeginChild("output-reports", {0.0F, 170.0F}, ImGuiChildFlags_Borders)) {
          if (!selected.has_device) {
            ImGui::TextDisabled("No selected device.");
          } else if (selected.outputs.empty()) {
            ImGui::TextDisabled("No output reports received.");
          } else {
            for (const auto &entry : selected.outputs) {
              const auto line = output_line(entry);
              ImGui::TextWrapped("%s", line.c_str());
            }
          }
        }
        ImGui::EndChild();

        ImGui::EndTable();
      }
    }

    void render_error_popup() {
      if (open_error_popup_) {
        ImGui::OpenPopup("Error");
        open_error_popup_ = false;
      }

      if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", error_message_.c_str());
        if (ImGui::Button("OK", {120.0F, 0.0F})) {
          error_message_.clear();
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }

    void create_gamepad() {
      const auto *choice = current_profile_choice();
      if (choice == nullptr) {
        show_error("Select a profile first.");
        return;
      }

      const auto profile = profile_for_choice(*choice);
      if (!profile) {
        show_error("Could not create the selected profile.");
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
        show_error(created.status.message());
        return;
      }

      const auto *gamepad = created.adapter->gamepad();
      if (gamepad == nullptr) {
        show_error("Created gamepad handle is missing.");
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
    }

    void reset_selected_device() {
      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr) {
          return;
        }
        status = device->adapter->set_state({});
      }
      button_active_.fill(false);
      if (!status.ok()) {
        show_error(status.message());
      }
    }

    void remove_selected_device() {
      std::unique_ptr<lvh::GamepadStateAdapter> adapter;
      {
        std::lock_guard lock {mutex_};
        if (selected_id_ == 0) {
          return;
        }
        const auto iter = devices_.find(selected_id_);
        if (iter == devices_.end()) {
          return;
        }
        adapter = std::move(iter->second.adapter);
        devices_.erase(iter);
        selected_id_ = devices_.empty() ? 0 : devices_.begin()->first;
      }
      button_active_.fill(false);
      if (adapter) {
        if (const auto status = adapter->close(); !status.ok()) {
          show_error(status.message());
        }
      }
    }

    void remove_all_devices() {
      const auto adapters = take_all_adapters();
      button_active_.fill(false);
      close_adapters(adapters, true);
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
        show_error(status.message());
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
        show_error(status.message());
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
        show_error(status.message());
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
        show_error(status.message());
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
        show_error(status.message());
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
      const auto adapters = take_all_adapters();
      close_adapters(adapters, false);
    }

    std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> take_all_adapters() {
      std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> adapters;
      std::lock_guard lock {mutex_};
      for (auto &[id, device] : devices_) {
        adapters.push_back(std::move(device.adapter));
      }
      devices_.clear();
      selected_id_ = 0;
      return adapters;
    }

    void close_adapters(
      const std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> &adapters,
      bool report_errors
    ) {
      std::optional<std::string> first_error;
      for (const auto &adapter : adapters) {
        if (adapter) {
          if (const auto status = adapter->close(); !status.ok() && report_errors && !first_error) {
            first_error = std::string {status.message()};
          }
        }
      }

      if (first_error) {
        show_error(*first_error);
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

    void show_error(std::string_view message) {
      error_message_ = message.empty() ? "Operation failed." : std::string {message};
      open_error_popup_ = true;
    }

    std::unique_ptr<lvh::Runtime> runtime_;
    mutable std::mutex mutex_;
    std::map<lvh::DeviceId, ControlledGamepad> devices_;
    lvh::DeviceId selected_id_ = 0;
    std::size_t profile_index_ = 3;
    bool lock_buttons_ = false;
    std::array<bool, button_choices.size()> button_active_ {};
    int battery_state_index_ = battery_choice_index(lvh::GamepadBatteryState::full);
    int battery_percentage_ = 100;
    std::string error_message_;
    bool open_error_popup_ = false;
    std::uint64_t next_metadata_index_ = 0;
    std::uint64_t next_output_sequence_ = 1;
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

    constexpr auto base_width = 1120;
    constexpr auto base_height = 760;
    constexpr auto minimum_width = 980;
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
