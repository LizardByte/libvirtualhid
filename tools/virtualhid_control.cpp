/**
 * @file tools/virtualhid_control.cpp
 * @brief Native UI for creating and testing libvirtualhid gamepads.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// local includes
#include <libvirtualhid/libvirtualhid.hpp>

#if defined(_WIN32)

  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef UNICODE
    #define UNICODE
  #endif
  #ifndef _UNICODE
    #define _UNICODE
  #endif

  #include <windows.h>
  #include <commctrl.h>

namespace {

  constexpr auto output_changed_message = WM_APP + 1U;
  constexpr auto slider_scale = 100;

  enum ControlId : int {
    profile_combo_id = 1000,
    create_button_id,
    device_list_id,
    reset_button_id,
    close_button_id,
    state_text_id,
    node_list_id,
    output_list_id,
    button_base_id = 2000,
    axis_base_id = 3000,
  };

  struct ProfileChoice {
    std::wstring_view id;
    std::wstring_view label;
    lvh::GamepadProfileKind kind;
    lvh::ClientControllerType client_type;
  };

  struct ButtonChoice {
    std::wstring_view label;
    lvh::GamepadButton button;
  };

  struct AxisChoice {
    std::wstring_view label;
    int minimum;
    int maximum;
  };

  constexpr std::array profile_choices {
    ProfileChoice {L"generic", L"Generic HID", lvh::GamepadProfileKind::generic, lvh::ClientControllerType::unknown},
    ProfileChoice {L"x360", L"Xbox 360", lvh::GamepadProfileKind::xbox_360, lvh::ClientControllerType::xbox},
    ProfileChoice {L"xone", L"Xbox One", lvh::GamepadProfileKind::xbox_one, lvh::ClientControllerType::xbox},
    ProfileChoice {L"xseries", L"Xbox Series", lvh::GamepadProfileKind::xbox_series, lvh::ClientControllerType::xbox},
    ProfileChoice {L"ds4", L"DualShock 4", lvh::GamepadProfileKind::dualshock4, lvh::ClientControllerType::playstation},
    ProfileChoice {L"ds5", L"DualSense", lvh::GamepadProfileKind::dualsense, lvh::ClientControllerType::playstation},
    ProfileChoice {L"switch", L"Switch Pro", lvh::GamepadProfileKind::switch_pro, lvh::ClientControllerType::nintendo},
  };

  constexpr std::array button_choices {
    ButtonChoice {L"A", lvh::GamepadButton::a},
    ButtonChoice {L"B", lvh::GamepadButton::b},
    ButtonChoice {L"X", lvh::GamepadButton::x},
    ButtonChoice {L"Y", lvh::GamepadButton::y},
    ButtonChoice {L"Back", lvh::GamepadButton::back},
    ButtonChoice {L"Start", lvh::GamepadButton::start},
    ButtonChoice {L"Guide", lvh::GamepadButton::guide},
    ButtonChoice {L"L3", lvh::GamepadButton::left_stick},
    ButtonChoice {L"R3", lvh::GamepadButton::right_stick},
    ButtonChoice {L"LB", lvh::GamepadButton::left_shoulder},
    ButtonChoice {L"RB", lvh::GamepadButton::right_shoulder},
    ButtonChoice {L"D-pad Up", lvh::GamepadButton::dpad_up},
    ButtonChoice {L"D-pad Down", lvh::GamepadButton::dpad_down},
    ButtonChoice {L"D-pad Left", lvh::GamepadButton::dpad_left},
    ButtonChoice {L"D-pad Right", lvh::GamepadButton::dpad_right},
    ButtonChoice {L"Misc", lvh::GamepadButton::misc1},
    ButtonChoice {L"Touchpad", lvh::GamepadButton::touchpad},
    ButtonChoice {L"Paddle 1", lvh::GamepadButton::paddle1},
    ButtonChoice {L"Paddle 2", lvh::GamepadButton::paddle2},
    ButtonChoice {L"Paddle 3", lvh::GamepadButton::paddle3},
    ButtonChoice {L"Paddle 4", lvh::GamepadButton::paddle4},
  };

  constexpr std::array axis_choices {
    AxisChoice {L"Left X", -slider_scale, slider_scale},
    AxisChoice {L"Left Y", -slider_scale, slider_scale},
    AxisChoice {L"Right X", -slider_scale, slider_scale},
    AxisChoice {L"Right Y", -slider_scale, slider_scale},
    AxisChoice {L"Left Trigger", 0, slider_scale},
    AxisChoice {L"Right Trigger", 0, slider_scale},
  };

  struct OutputLogEntry {
    std::uint64_t sequence = 0;
    lvh::GamepadOutput output;
  };

  struct ControlledGamepad {
    std::wstring profile_label;
    std::unique_ptr<lvh::GamepadStateAdapter> adapter;
    std::vector<OutputLogEntry> outputs;
  };

  std::wstring widen(std::string_view value) {
    if (value.empty()) {
      return {};
    }

    const auto required = ::MultiByteToWideChar(
      CP_UTF8,
      0,
      value.data(),
      static_cast<int>(value.size()),
      nullptr,
      0
    );
    if (required <= 0) {
      return std::wstring {value.begin(), value.end()};
    }

    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const auto copied = ::MultiByteToWideChar(
      CP_UTF8,
      0,
      value.data(),
      static_cast<int>(value.size()),
      result.data(),
      required
    );
    if (copied <= 0) {
      return std::wstring {value.begin(), value.end()};
    }
    return result;
  }

  std::wstring device_type_name(lvh::DeviceType type) {
    switch (type) {
      using enum lvh::DeviceType;

      case gamepad:
        return L"gamepad";
      case keyboard:
        return L"keyboard";
      case mouse:
        return L"mouse";
      case touchscreen:
        return L"touchscreen";
      case trackpad:
        return L"trackpad";
      case pen_tablet:
        return L"pen tablet";
    }
    return L"unknown";
  }

  std::wstring node_kind_name(lvh::DeviceNodeKind kind) {
    switch (kind) {
      using enum lvh::DeviceNodeKind;

      case input_event:
        return L"input";
      case joystick:
        return L"joystick";
      case hidraw:
        return L"hidraw";
      case sysfs:
        return L"sysfs";
      case other:
        return L"other";
    }
    return L"other";
  }

  std::wstring output_kind_name(lvh::GamepadOutputKind kind) {
    switch (kind) {
      using enum lvh::GamepadOutputKind;

      case rumble:
        return L"rumble";
      case rgb_led:
        return L"rgb led";
      case adaptive_triggers:
        return L"adaptive triggers";
      case raw_report:
        return L"raw report";
      case trigger_rumble:
        return L"trigger rumble";
    }
    return L"raw report";
  }

  std::wstring raw_hex(const std::vector<std::uint8_t> &bytes) {
    std::wostringstream stream;
    stream << std::hex << std::setfill(L'0');
    for (const auto value : bytes) {
      stream << std::setw(2) << static_cast<unsigned>(value);
    }
    return stream.str();
  }

  std::optional<lvh::DeviceProfile> profile_for_choice(const ProfileChoice &choice) {
    switch (choice.kind) {
      using enum lvh::GamepadProfileKind;

      case generic:
        return lvh::profiles::generic_gamepad();
      case xbox_360:
        return lvh::profiles::xbox_360();
      case xbox_one:
        return lvh::profiles::xbox_one();
      case xbox_series:
        return lvh::profiles::xbox_series();
      case dualshock4:
        return lvh::profiles::dualshock4();
      case dualsense:
        return lvh::profiles::dualsense();
      case switch_pro:
        return lvh::profiles::switch_pro();
    }
    return std::nullopt;
  }

  int axis_to_slider(float value) {
    return static_cast<int>(std::lround(std::clamp(value, -1.0F, 1.0F) * static_cast<float>(slider_scale)));
  }

  int trigger_to_slider(float value) {
    return static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * static_cast<float>(slider_scale)));
  }

  float slider_to_float(LRESULT value) {
    return static_cast<float>(value) / static_cast<float>(slider_scale);
  }

  class ControlWindow {
  public:
    explicit ControlWindow(HINSTANCE instance):
        instance_ {instance} {
      lvh::RuntimeOptions options;
      options.backend = lvh::BackendKind::platform_default;
      runtime_ = lvh::Runtime::create(options);
    }

    int run() {
      INITCOMMONCONTROLSEX controls {};
      controls.dwSize = sizeof(controls);
      controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
      static_cast<void>(::InitCommonControlsEx(&controls));

      WNDCLASSEXW window_class {};
      window_class.cbSize = sizeof(window_class);
      window_class.hInstance = instance_;
      window_class.lpfnWndProc = &ControlWindow::window_proc;
      window_class.lpszClassName = L"LibVirtualHidControlWindow";
      window_class.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
      window_class.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
      window_class.hIconSm = window_class.hIcon;
      window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

      if (::RegisterClassExW(&window_class) == 0U) {
        return 1;
      }

      window_ = ::CreateWindowExW(
        0,
        window_class.lpszClassName,
        L"libvirtualhid control",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        980,
        700,
        nullptr,
        nullptr,
        instance_,
        this
      );
      if (window_ == nullptr) {
        return 1;
      }

      ::ShowWindow(window_, SW_SHOW);
      ::UpdateWindow(window_);

      MSG message {};
      while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
      }
      return static_cast<int>(message.wParam);
    }

  private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
      auto *self = reinterpret_cast<ControlWindow *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
      if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<CREATESTRUCTW *>(lparam);
        self = static_cast<ControlWindow *>(create->lpCreateParams);
        self->window_ = window;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
      }

      if (self != nullptr) {
        return self->handle_message(message, wparam, lparam);
      }
      return ::DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
      switch (message) {
        case WM_CREATE:
          create_controls();
          refresh_all();
          return 0;
        case WM_SIZE:
          layout_controls(LOWORD(lparam), HIWORD(lparam));
          return 0;
        case WM_COMMAND:
          handle_command(LOWORD(wparam), HIWORD(wparam));
          return 0;
        case WM_HSCROLL:
          handle_slider(reinterpret_cast<HWND>(lparam));
          return 0;
        case output_changed_message:
          refresh_selected_device();
          return 0;
        case WM_DESTROY:
          close_all_devices();
          ::PostQuitMessage(0);
          return 0;
        default:
          break;
      }
      return ::DefWindowProcW(window_, message, wparam, lparam);
    }

    HWND create_child(
      const wchar_t *class_name,
      const wchar_t *text,
      DWORD style,
      int id
    ) {
      auto *font = reinterpret_cast<HFONT>(::GetStockObject(DEFAULT_GUI_FONT));
      auto *control = ::CreateWindowExW(
        0,
        class_name,
        text,
        WS_CHILD | WS_VISIBLE | style,
        0,
        0,
        10,
        10,
        window_,
        reinterpret_cast<HMENU>(static_cast<std::intptr_t>(id)),
        instance_,
        nullptr
      );
      if (control != nullptr) {
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
      }
      return control;
    }

    void create_controls() {
      backend_text_ = create_child(L"STATIC", L"", SS_LEFT, 0);
      profile_combo_ = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, profile_combo_id);
      create_button_ = create_child(L"BUTTON", L"Create", BS_PUSHBUTTON, create_button_id);
      device_list_ = create_child(L"LISTBOX", L"", LBS_NOTIFY | WS_BORDER | WS_VSCROLL, device_list_id);
      reset_button_ = create_child(L"BUTTON", L"Reset", BS_PUSHBUTTON, reset_button_id);
      close_button_ = create_child(L"BUTTON", L"Close", BS_PUSHBUTTON, close_button_id);
      state_text_ = create_child(L"STATIC", L"No device selected.", SS_LEFT, state_text_id);
      nodes_list_ = create_child(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL, node_list_id);
      output_list_ = create_child(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL, output_list_id);

      for (const auto &choice : profile_choices) {
        ::SendMessageW(profile_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.data()));
      }
      ::SendMessageW(profile_combo_, CB_SETCURSEL, 4, 0);

      for (std::size_t index = 0; index < button_choices.size(); ++index) {
        button_controls_[index] = create_child(
          L"BUTTON",
          button_choices[index].label.data(),
          BS_AUTOCHECKBOX | BS_PUSHLIKE,
          button_base_id + static_cast<int>(index)
        );
      }

      for (std::size_t index = 0; index < axis_choices.size(); ++index) {
        axis_labels_[index] = create_child(L"STATIC", axis_choices[index].label.data(), SS_LEFT, 0);
        axis_sliders_[index] = create_child(
          TRACKBAR_CLASSW,
          L"",
          TBS_AUTOTICKS,
          axis_base_id + static_cast<int>(index)
        );
        ::SendMessageW(
          axis_sliders_[index],
          TBM_SETRANGE,
          TRUE,
          MAKELPARAM(axis_choices[index].minimum, axis_choices[index].maximum)
        );
        ::SendMessageW(axis_sliders_[index], TBM_SETPOS, TRUE, 0);
      }
    }

    void layout_controls(int width, int height) {
      constexpr auto margin = 12;
      constexpr auto gap = 10;
      constexpr auto row = 28;
      constexpr auto left_width = 280;
      constexpr auto top_height = 72;
      constexpr auto button_width = 112;
      constexpr auto button_height = 28;
      constexpr auto button_columns = 4;

      const auto right_x = margin + left_width + gap;
      const auto right_width = std::max(320, width - right_x - margin);
      const auto list_top = margin + top_height;
      const auto left_list_height = std::max(120, height - list_top - margin - row - gap);

      move(profile_combo_, margin, margin, 170, 200);
      move(create_button_, margin + 180, margin, 90, row);
      move(backend_text_, margin, margin + 38, left_width, 28);
      move(device_list_, margin, list_top, left_width, left_list_height);
      move(reset_button_, margin, height - margin - row, 90, row);
      move(close_button_, margin + 100, height - margin - row, 90, row);

      move(state_text_, right_x, margin, right_width, 86);

      auto buttons_top = margin + 96;
      for (std::size_t index = 0; index < button_controls_.size(); ++index) {
        const auto column = static_cast<int>(index % button_columns);
        const auto row_index = static_cast<int>(index / button_columns);
        move(
          button_controls_[index],
          right_x + column * (button_width + gap),
          buttons_top + row_index * (button_height + gap),
          button_width,
          button_height
        );
      }

      const auto slider_top = buttons_top + 6 * (button_height + gap) + gap;
      const auto slider_width = std::max(180, (right_width - gap) / 2);
      for (std::size_t index = 0; index < axis_sliders_.size(); ++index) {
        const auto column = static_cast<int>(index % 2U);
        const auto row_index = static_cast<int>(index / 2U);
        const auto x = right_x + column * (slider_width + gap);
        const auto y = slider_top + row_index * 54;
        move(axis_labels_[index], x, y, slider_width, 18);
        move(axis_sliders_[index], x, y + 18, slider_width, 34);
      }

      const auto bottom_top = slider_top + 3 * 54 + gap;
      const auto bottom_height = std::max(110, height - bottom_top - margin);
      move(nodes_list_, right_x, bottom_top, (right_width - gap) / 2, bottom_height);
      move(output_list_, right_x + (right_width + gap) / 2, bottom_top, (right_width - gap) / 2, bottom_height);
    }

    static void move(HWND control, int x, int y, int width, int height) {
      if (control != nullptr) {
        ::SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }

    void handle_command(int id, int notification) {
      if (id == create_button_id && notification == BN_CLICKED) {
        create_gamepad();
        return;
      }
      if (id == reset_button_id && notification == BN_CLICKED) {
        reset_selected_device();
        return;
      }
      if (id == close_button_id && notification == BN_CLICKED) {
        close_selected_device();
        return;
      }
      if (id == device_list_id && notification == LBN_SELCHANGE) {
        const auto selection = ::SendMessageW(device_list_, LB_GETCURSEL, 0, 0);
        if (selection != LB_ERR) {
          selected_id_ = static_cast<lvh::DeviceId>(::SendMessageW(device_list_, LB_GETITEMDATA, static_cast<WPARAM>(selection), 0));
          refresh_selected_device();
        }
        return;
      }
      if (id >= button_base_id && id < button_base_id + static_cast<int>(button_choices.size()) && notification == BN_CLICKED) {
        set_selected_button(static_cast<std::size_t>(id - button_base_id));
      }
    }

    void handle_slider(HWND slider) {
      for (std::size_t index = 0; index < axis_sliders_.size(); ++index) {
        if (axis_sliders_[index] == slider) {
          set_selected_axis(index);
          return;
        }
      }
    }

    void create_gamepad() {
      const auto selection = ::SendMessageW(profile_combo_, CB_GETCURSEL, 0, 0);
      if (selection == CB_ERR || selection < 0 || static_cast<std::size_t>(selection) >= profile_choices.size()) {
        show_error(L"Select a profile first.");
        return;
      }

      const auto &choice = profile_choices[static_cast<std::size_t>(selection)];
      const auto profile = profile_for_choice(choice);
      if (!profile) {
        show_error(L"Could not create the selected profile.");
        return;
      }

      lvh::CreateGamepadOptions options;
      options.profile = *profile;
      options.metadata.global_index = static_cast<int>(next_metadata_index_++);
      options.metadata.client_relative_index = 0;
      options.metadata.client_type = choice.client_type;
      options.metadata.has_motion_sensors = profile->capabilities.supports_motion;
      options.metadata.has_touchpad = profile->capabilities.supports_touchpad;
      options.metadata.has_rgb_led = profile->capabilities.supports_rgb_led;
      options.metadata.has_battery = profile->capabilities.supports_battery;
      options.metadata.stable_id = "libvirtualhid-control-" + std::to_string(options.metadata.global_index);

      auto created = lvh::GamepadStateAdapter::create(*runtime_, options);
      if (!created) {
        show_error(widen(created.status.message()));
        return;
      }

      auto *gamepad = created.adapter->gamepad();
      if (gamepad == nullptr) {
        show_error(L"Created gamepad handle is missing.");
        return;
      }

      const auto id = gamepad->device_id();
      created.adapter->set_output_callback([this, id](const lvh::GamepadOutput &output) {
        record_output(id, output);
      });

      ControlledGamepad device;
      device.profile_label = std::wstring {choice.label};
      device.adapter = std::move(created.adapter);

      {
        std::lock_guard lock {mutex_};
        devices_[id] = std::move(device);
      }
      selected_id_ = id;
      refresh_all();
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
      if (!status.ok()) {
        show_error(widen(status.message()));
      }
      refresh_selected_device();
    }

    void close_selected_device() {
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
      if (adapter) {
        if (const auto status = adapter->close(); !status.ok()) {
          show_error(widen(status.message()));
        }
      }
      refresh_all();
    }

    void set_selected_button(std::size_t index) {
      const auto control_id = button_base_id + static_cast<int>(index);
      const auto pressed = ::IsDlgButtonChecked(window_, control_id) == BST_CHECKED;
      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr) {
          return;
        }
        status = device->adapter->set_button(button_choices[index].button, pressed);
      }
      if (!status.ok()) {
        show_error(widen(status.message()));
      }
      refresh_selected_device();
    }

    void set_selected_axis(std::size_t index) {
      auto status = lvh::OperationStatus::success();
      const auto position = ::SendMessageW(axis_sliders_[index], TBM_GETPOS, 0, 0);
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
        show_error(widen(status.message()));
      }
      refresh_selected_device();
    }

    void refresh_all() {
      refresh_backend_text();
      refresh_device_list();
      refresh_selected_device();
    }

    void refresh_backend_text() {
      const auto &caps = runtime_->capabilities();
      std::wstring text = L"Backend: " + widen(caps.backend_name);
      text += caps.supports_gamepad ? L" | gamepad available" : L" | gamepad unavailable";
      text += caps.supports_output_reports ? L" | output reports available" : L" | output reports unavailable";
      ::SetWindowTextW(backend_text_, text.c_str());
    }

    void refresh_device_list() {
      ::SendMessageW(device_list_, LB_RESETCONTENT, 0, 0);
      std::lock_guard lock {mutex_};
      for (const auto &[id, device] : devices_) {
        std::wostringstream label;
        label << L"#" << id << L" " << device.profile_label;
        const auto index = ::SendMessageW(device_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.str().c_str()));
        if (index != LB_ERR) {
          ::SendMessageW(device_list_, LB_SETITEMDATA, static_cast<WPARAM>(index), static_cast<LPARAM>(id));
          if (id == selected_id_) {
            ::SendMessageW(device_list_, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
          }
        }
      }
    }

    void refresh_selected_device() {
      std::lock_guard lock {mutex_};
      auto *device = selected_device_locked();
      const auto enabled = device != nullptr;
      ::EnableWindow(reset_button_, enabled);
      ::EnableWindow(close_button_, enabled);
      for (auto *control : button_controls_) {
        ::EnableWindow(control, enabled);
      }
      for (auto *slider : axis_sliders_) {
        ::EnableWindow(slider, enabled);
      }

      if (device == nullptr) {
        ::SetWindowTextW(state_text_, L"No device selected. Create a gamepad to begin testing.");
        ::SendMessageW(nodes_list_, LB_RESETCONTENT, 0, 0);
        ::SendMessageW(output_list_, LB_RESETCONTENT, 0, 0);
        for (std::size_t index = 0; index < button_controls_.size(); ++index) {
          ::CheckDlgButton(window_, button_base_id + static_cast<int>(index), BST_UNCHECKED);
        }
        reset_sliders();
        return;
      }

      const auto *gamepad = device->adapter->gamepad();
      const auto &profile = gamepad->profile();
      const auto state = device->adapter->state();

      std::wostringstream state_text;
      state_text << device->profile_label << L" #" << gamepad->device_id() << L"\r\n"
                 << device_type_name(profile.device_type) << L" | " << widen(profile.name) << L"\r\n"
                 << L"L(" << state.left_stick.x << L", " << state.left_stick.y << L") "
                 << L"R(" << state.right_stick.x << L", " << state.right_stick.y << L") "
                 << L"LT " << state.left_trigger << L" RT " << state.right_trigger << L" | "
                 << gamepad->submit_count() << L" submits";
      ::SetWindowTextW(state_text_, state_text.str().c_str());

      for (std::size_t index = 0; index < button_choices.size(); ++index) {
        ::CheckDlgButton(
          window_,
          button_base_id + static_cast<int>(index),
          state.buttons.test(button_choices[index].button) ? BST_CHECKED : BST_UNCHECKED
        );
      }

      ::SendMessageW(axis_sliders_[0], TBM_SETPOS, TRUE, axis_to_slider(state.left_stick.x));
      ::SendMessageW(axis_sliders_[1], TBM_SETPOS, TRUE, axis_to_slider(state.left_stick.y));
      ::SendMessageW(axis_sliders_[2], TBM_SETPOS, TRUE, axis_to_slider(state.right_stick.x));
      ::SendMessageW(axis_sliders_[3], TBM_SETPOS, TRUE, axis_to_slider(state.right_stick.y));
      ::SendMessageW(axis_sliders_[4], TBM_SETPOS, TRUE, trigger_to_slider(state.left_trigger));
      ::SendMessageW(axis_sliders_[5], TBM_SETPOS, TRUE, trigger_to_slider(state.right_trigger));

      refresh_nodes(*gamepad);
      refresh_outputs(*device);
    }

    void reset_sliders() {
      for (std::size_t index = 0; index < axis_sliders_.size(); ++index) {
        ::SendMessageW(axis_sliders_[index], TBM_SETPOS, TRUE, 0);
      }
    }

    void refresh_nodes(const lvh::Gamepad &gamepad) {
      ::SendMessageW(nodes_list_, LB_RESETCONTENT, 0, 0);
      const auto nodes = gamepad.device_nodes();
      if (nodes.empty()) {
        ::SendMessageW(nodes_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No device nodes reported yet."));
        return;
      }

      for (const auto &node : nodes) {
        const auto line = node_kind_name(node.kind) + L": " + widen(node.path);
        ::SendMessageW(nodes_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
      }
    }

    void refresh_outputs(const ControlledGamepad &device) {
      ::SendMessageW(output_list_, LB_RESETCONTENT, 0, 0);
      if (device.outputs.empty()) {
        ::SendMessageW(output_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No output reports received."));
        return;
      }

      for (const auto &entry : device.outputs) {
        const auto &output = entry.output;
        std::wostringstream line;
        line << L"#" << entry.sequence << L" " << output_kind_name(output.kind)
             << L" low=" << output.low_frequency_rumble
             << L" high=" << output.high_frequency_rumble
             << L" rgb=" << static_cast<unsigned>(output.red) << L"," << static_cast<unsigned>(output.green) << L","
             << static_cast<unsigned>(output.blue);
        if (!output.raw_report.empty()) {
          line << L" raw=" << raw_hex(output.raw_report);
        }
        ::SendMessageW(output_list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.str().c_str()));
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

    void record_output(lvh::DeviceId id, const lvh::GamepadOutput &output) {
      {
        std::lock_guard lock {mutex_};
        const auto iter = devices_.find(id);
        if (iter == devices_.end()) {
          return;
        }

        auto &outputs = iter->second.outputs;
        outputs.push_back({.sequence = next_output_sequence_++, .output = output});
        if (outputs.size() > max_output_events_) {
          outputs.erase(outputs.begin(), outputs.begin() + static_cast<std::ptrdiff_t>(outputs.size() - max_output_events_));
        }
      }
      if (window_ != nullptr) {
        ::PostMessageW(window_, output_changed_message, 0, 0);
      }
    }

    void close_all_devices() {
      std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> adapters;
      {
        std::lock_guard lock {mutex_};
        for (auto &[id, device] : devices_) {
          adapters.push_back(std::move(device.adapter));
        }
        devices_.clear();
        selected_id_ = 0;
      }

      for (auto &adapter : adapters) {
        if (adapter) {
          static_cast<void>(adapter->close());
        }
      }
    }

    void show_error(const std::wstring &message) const {
      ::MessageBoxW(window_, message.c_str(), L"libvirtualhid control", MB_ICONERROR | MB_OK);
    }

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HWND backend_text_ = nullptr;
    HWND profile_combo_ = nullptr;
    HWND create_button_ = nullptr;
    HWND device_list_ = nullptr;
    HWND reset_button_ = nullptr;
    HWND close_button_ = nullptr;
    HWND state_text_ = nullptr;
    HWND nodes_list_ = nullptr;
    HWND output_list_ = nullptr;
    std::array<HWND, button_choices.size()> button_controls_ {};
    std::array<HWND, axis_choices.size()> axis_labels_ {};
    std::array<HWND, axis_choices.size()> axis_sliders_ {};
    std::unique_ptr<lvh::Runtime> runtime_;
    std::mutex mutex_;
    std::map<lvh::DeviceId, ControlledGamepad> devices_;
    lvh::DeviceId selected_id_ = 0;
    std::uint64_t next_metadata_index_ = 0;
    std::uint64_t next_output_sequence_ = 1;
    static constexpr std::size_t max_output_events_ = 50;
  };

}  // namespace

/**
 * @brief Run the native Windows libvirtualhid control UI.
 * @param instance Module instance.
 * @return Process exit code.
 */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int) {
  ControlWindow window {instance};
  return window.run();
}

#else

// standard includes
#include <iostream>

/**
 * @brief Report that the native UI is not implemented on this platform yet.
 * @return Nonzero because no UI was shown.
 */
int main() {
  std::cerr << "virtualhid_control native UI is currently implemented for Windows only.\n";
  return 1;
}

#endif
