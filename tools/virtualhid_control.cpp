/**
 * @file tools/virtualhid_control.cpp
 * @brief Native UI for creating and testing libvirtualhid gamepads.
 */

// standard includes
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

  #include "resource.h"

// clang-format off
  #include <Windows.h>
  #include <CommCtrl.h>
// clang-format on

namespace {

  constexpr auto output_changed_message = WM_APP + 1U;
  constexpr auto button_subclass_id = 1U;
  constexpr auto slider_scale = 100;

  enum ControlId : int {
    profile_combo_id = 1000,
    create_button_id,
    device_list_id,
    reset_button_id,
    remove_selected_button_id,
    remove_all_button_id,
    lock_buttons_check_id,
    state_text_id,
    feature_text_id,
    battery_state_combo_id,
    battery_slider_id,
    clear_battery_button_id,
    output_summary_text_id,
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

  struct BatteryChoice {
    std::wstring_view label;
    lvh::GamepadBatteryState state;
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

  constexpr std::array battery_choices {
    BatteryChoice {L"Unknown", lvh::GamepadBatteryState::unknown},
    BatteryChoice {L"Discharging", lvh::GamepadBatteryState::discharging},
    BatteryChoice {L"Charging", lvh::GamepadBatteryState::charging},
    BatteryChoice {L"Full", lvh::GamepadBatteryState::full},
    BatteryChoice {L"Voltage/temperature error", lvh::GamepadBatteryState::voltage_or_temperature_error},
    BatteryChoice {L"Temperature error", lvh::GamepadBatteryState::temperature_error},
    BatteryChoice {L"Charging error", lvh::GamepadBatteryState::charging_error},
  };

  struct OutputLogEntry {
    std::uint64_t sequence = 0;
    lvh::GamepadOutput output;
  };

  struct ControlledGamepad {
    std::wstring profile_label;
    std::unique_ptr<lvh::GamepadStateAdapter> adapter;
    std::vector<OutputLogEntry> outputs;
    std::optional<lvh::GamepadOutput> latest_rumble;
    std::optional<lvh::GamepadOutput> latest_trigger_rumble;
    std::optional<lvh::GamepadOutput> latest_rgb_led;
    std::optional<lvh::GamepadOutput> latest_adaptive_triggers;
    std::optional<lvh::GamepadOutput> latest_raw_report;
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

  std::wstring battery_state_name(lvh::GamepadBatteryState state) {
    switch (state) {
      using enum lvh::GamepadBatteryState;

      case unknown:
        return L"unknown";
      case discharging:
        return L"discharging";
      case charging:
        return L"charging";
      case full:
        return L"full";
      case voltage_or_temperature_error:
        return L"voltage/temperature error";
      case temperature_error:
        return L"temperature error";
      case charging_error:
        return L"charging error";
    }
    return L"unknown";
  }

  int battery_choice_index(lvh::GamepadBatteryState state) {
    for (std::size_t index = 0; index < battery_choices.size(); ++index) {
      if (battery_choices[index].state == state) {
        return static_cast<int>(index);
      }
    }
    return 0;
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

  std::wstring yes_no(bool value) {
    return value ? L"yes" : L"no";
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
      window_class.hIcon = ::LoadIconW(instance_, MAKEINTRESOURCEW(IDI_VIRTUALHID));
      if (window_class.hIcon == nullptr) {
        window_class.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);
      }
      window_class.hIconSm = reinterpret_cast<HICON>(::LoadImageW(
        instance_,
        MAKEINTRESOURCEW(IDI_VIRTUALHID),
        IMAGE_ICON,
        ::GetSystemMetrics(SM_CXSMICON),
        ::GetSystemMetrics(SM_CYSMICON),
        LR_DEFAULTCOLOR
      ));
      if (window_class.hIconSm == nullptr) {
        window_class.hIconSm = window_class.hIcon;
      }
      window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

      if (::RegisterClassExW(&window_class) == 0U) {
        return 1;
      }

      window_ = ::CreateWindowExW(
        0,
        window_class.lpszClassName,
        L"libvirtualhid control",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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

    static LRESULT CALLBACK button_subclass_proc(HWND control, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR ref_data) {
      auto *self = reinterpret_cast<ControlWindow *>(ref_data);
      if (self == nullptr) {
        return ::DefSubclassProc(control, message, wparam, lparam);
      }

      return self->handle_button_message(control, message, wparam, lparam);
    }

    LRESULT handle_button_message(HWND control, UINT message, WPARAM wparam, LPARAM lparam) {
      const auto id = ::GetDlgCtrlID(control);
      if (id < button_base_id || id >= button_base_id + static_cast<int>(button_choices.size())) {
        return ::DefSubclassProc(control, message, wparam, lparam);
      }

      const auto index = static_cast<std::size_t>(id - button_base_id);
      if (!buttons_locked()) {
        switch (message) {
          case WM_LBUTTONDOWN:
          case WM_LBUTTONDBLCLK:
            momentary_pointer_pressed_[index] = true;
            set_selected_button(index, true);
            break;
          case WM_LBUTTONUP:
            if (momentary_pointer_pressed_[index]) {
              momentary_pointer_pressed_[index] = false;
              set_selected_button(index, false);
            }
            break;
          case WM_CAPTURECHANGED:
            if (momentary_pointer_pressed_[index]) {
              momentary_pointer_pressed_[index] = false;
              set_selected_button(index, false);
            }
            break;
          case WM_KEYDOWN:
            if (wparam == VK_SPACE && !momentary_key_pressed_[index]) {
              momentary_key_pressed_[index] = true;
              set_selected_button(index, true);
            }
            break;
          case WM_KEYUP:
            if (wparam == VK_SPACE && momentary_key_pressed_[index]) {
              momentary_key_pressed_[index] = false;
              set_selected_button(index, false);
            }
            break;
          case WM_KILLFOCUS:
            if (momentary_key_pressed_[index]) {
              momentary_key_pressed_[index] = false;
              set_selected_button(index, false);
            }
            break;
          default:
            break;
        }
      }

      return ::DefSubclassProc(control, message, wparam, lparam);
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
      switch (message) {
        case WM_CREATE:
          create_controls();
          refresh_all();
          return 0;
        case WM_SIZE:
          layout_controls(LOWORD(lparam), HIWORD(lparam));
          redraw_window();
          return 0;
        case WM_GETMINMAXINFO:
          handle_min_max_info(reinterpret_cast<MINMAXINFO *>(lparam));
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
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style,
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
      remove_selected_button_ = create_child(L"BUTTON", L"Remove selected", BS_PUSHBUTTON, remove_selected_button_id);
      remove_all_button_ = create_child(L"BUTTON", L"Remove all", BS_PUSHBUTTON, remove_all_button_id);
      lock_buttons_check_ = create_child(L"BUTTON", L"Lock buttons", BS_AUTOCHECKBOX, lock_buttons_check_id);
      state_text_ = create_child(L"STATIC", L"No device selected.", SS_LEFT, state_text_id);
      feature_text_ = create_child(L"STATIC", L"", SS_LEFT, feature_text_id);
      battery_label_ = create_child(L"STATIC", L"Battery", SS_LEFT, 0);
      battery_state_combo_ = create_child(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_VSCROLL, battery_state_combo_id);
      battery_slider_ = create_child(TRACKBAR_CLASSW, L"", TBS_NOTICKS, battery_slider_id);
      clear_battery_button_ = create_child(L"BUTTON", L"Clear", BS_PUSHBUTTON, clear_battery_button_id);
      output_summary_text_ = create_child(L"STATIC", L"", SS_LEFT, output_summary_text_id);
      nodes_label_ = create_child(L"STATIC", L"Device nodes", SS_LEFT, 0);
      output_label_ = create_child(L"STATIC", L"Output reports", SS_LEFT, 0);
      nodes_list_ = create_child(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL, node_list_id);
      output_list_ = create_child(L"LISTBOX", L"", WS_BORDER | WS_VSCROLL, output_list_id);

      for (const auto &choice : profile_choices) {
        ::SendMessageW(profile_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.data()));
      }
      ::SendMessageW(profile_combo_, CB_SETCURSEL, 3, 0);

      for (const auto &choice : battery_choices) {
        ::SendMessageW(battery_state_combo_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(choice.label.data()));
      }
      ::SendMessageW(battery_state_combo_, CB_SETCURSEL, battery_choice_index(lvh::GamepadBatteryState::full), 0);
      ::SendMessageW(battery_slider_, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
      ::SendMessageW(battery_slider_, TBM_SETPOS, TRUE, 100);

      for (std::size_t index = 0; index < button_choices.size(); ++index) {
        button_controls_[index] = create_child(
          L"BUTTON",
          button_choices[index].label.data(),
          BS_PUSHBUTTON | BS_NOTIFY,
          button_base_id + static_cast<int>(index)
        );
        ::SetWindowSubclass(
          button_controls_[index],
          &ControlWindow::button_subclass_proc,
          button_subclass_id,
          reinterpret_cast<DWORD_PTR>(this)
        );
      }

      for (std::size_t index = 0; index < axis_choices.size(); ++index) {
        axis_labels_[index] = create_child(L"STATIC", axis_choices[index].label.data(), SS_LEFT, 0);
        axis_sliders_[index] = create_child(
          TRACKBAR_CLASSW,
          L"",
          TBS_NOTICKS,
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
      constexpr auto gap = 8;
      constexpr auto row = 28;
      constexpr auto label_height = 18;
      constexpr auto axis_label_height = 22;
      constexpr auto axis_slider_height = 34;
      constexpr auto axis_row_height = 64;
      constexpr auto button_width = 108;
      constexpr auto button_height = 28;
      constexpr auto state_height = 72;
      constexpr auto feature_height = 34;
      constexpr auto output_summary_height = 34;

      const auto left_width = std::clamp(width / 3, 250, 320);
      const auto right_x = margin + left_width + gap;
      const auto right_width = std::max(360, width - right_x - margin);
      const auto profile_width = std::max(120, left_width - 98);
      const auto left_actions_top = std::max(margin + 140, height - margin - (row * 2) - gap);
      const auto list_top = margin + row + 48;
      const auto left_list_height = std::max(120, left_actions_top - list_top - gap);

      move(profile_combo_, margin, margin, profile_width, 200);
      move(create_button_, margin + profile_width + gap, margin, left_width - profile_width - gap, row);
      move(backend_text_, margin, margin + row + gap, left_width, 40);
      move(device_list_, margin, list_top, left_width, left_list_height);
      move(reset_button_, margin, left_actions_top, 80, row);
      move(remove_selected_button_, margin + 88, left_actions_top, left_width - 88, row);
      move(remove_all_button_, margin, left_actions_top + row + gap, left_width, row);

      move(state_text_, right_x, margin, right_width, state_height);
      move(feature_text_, right_x, margin + state_height, right_width, feature_height);

      const auto buttons_header_top = margin + state_height + feature_height + gap;
      move(lock_buttons_check_, right_x, buttons_header_top, 130, row);

      const auto button_columns = std::max(1, std::min(5, (right_width + gap) / (button_width + gap)));
      const auto actual_button_width = std::max(button_width, (right_width - ((button_columns - 1) * gap)) / button_columns);
      auto visible_button_count = 0;
      const auto buttons_top = buttons_header_top + row + gap;
      for (std::size_t index = 0; index < button_controls_.size(); ++index) {
        if (!visible_buttons_[index]) {
          ::ShowWindow(button_controls_[index], SW_HIDE);
          continue;
        }

        ::ShowWindow(button_controls_[index], SW_SHOW);
        const auto column = visible_button_count % button_columns;
        const auto row_index = visible_button_count / button_columns;
        move(
          button_controls_[index],
          right_x + column * (actual_button_width + gap),
          buttons_top + row_index * (button_height + gap),
          actual_button_width,
          button_height
        );
        ++visible_button_count;
      }

      const auto button_rows = std::max(1, (visible_button_count + button_columns - 1) / button_columns);
      const auto slider_top = buttons_top + button_rows * (button_height + gap) + gap;
      const auto slider_columns = right_width >= 520 ? 2 : 1;
      const auto slider_width = std::max(180, (right_width - ((slider_columns - 1) * gap)) / slider_columns);
      for (std::size_t index = 0; index < axis_sliders_.size(); ++index) {
        const auto column = static_cast<int>(index % static_cast<std::size_t>(slider_columns));
        const auto row_index = static_cast<int>(index / static_cast<std::size_t>(slider_columns));
        const auto x = right_x + column * (slider_width + gap);
        const auto y = slider_top + row_index * axis_row_height;
        move(axis_labels_[index], x, y, slider_width, axis_label_height);
        move(axis_sliders_[index], x, y + axis_label_height, slider_width, axis_slider_height);
      }

      const auto slider_rows = static_cast<int>((axis_sliders_.size() + static_cast<std::size_t>(slider_columns) - 1U) / static_cast<std::size_t>(slider_columns));
      auto next_top = slider_top + slider_rows * axis_row_height + gap;
      const auto show_battery = battery_controls_visible_;
      ::ShowWindow(battery_label_, show_battery ? SW_SHOW : SW_HIDE);
      ::ShowWindow(battery_state_combo_, show_battery ? SW_SHOW : SW_HIDE);
      ::ShowWindow(battery_slider_, show_battery ? SW_SHOW : SW_HIDE);
      ::ShowWindow(clear_battery_button_, show_battery ? SW_SHOW : SW_HIDE);
      if (show_battery) {
        const auto clear_width = 70;
        const auto battery_label_width = 84;
        const auto combo_width = std::min(220, std::max(150, right_width / 3));
        move(battery_label_, right_x, next_top + 5, battery_label_width, label_height);
        move(battery_state_combo_, right_x + battery_label_width + gap, next_top, combo_width, 180);
        move(clear_battery_button_, right_x + right_width - clear_width, next_top, clear_width, row);
        move(
          battery_slider_,
          right_x + battery_label_width + gap + combo_width + gap,
          next_top,
          std::max(120, right_width - battery_label_width - combo_width - clear_width - (gap * 3)),
          row
        );
        next_top += row + gap;
      }

      move(output_summary_text_, right_x, next_top, right_width, output_summary_height);
      next_top += output_summary_height + gap;

      const auto list_width = (right_width - gap) / 2;
      const auto bottom_height = std::max(90, height - next_top - label_height - margin);
      move(nodes_label_, right_x, next_top, list_width, label_height);
      move(output_label_, right_x + list_width + gap, next_top, list_width, label_height);
      move(nodes_list_, right_x, next_top + label_height, list_width, bottom_height);
      move(output_list_, right_x + list_width + gap, next_top + label_height, list_width, bottom_height);
    }

    static void move(HWND control, int x, int y, int width, int height) {
      if (control != nullptr) {
        ::SetWindowPos(control, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
      }
    }

    static void handle_min_max_info(MINMAXINFO *info) {
      if (info == nullptr) {
        return;
      }

      info->ptMinTrackSize.x = 980;
      info->ptMinTrackSize.y = 740;
    }

    bool buttons_locked() const {
      return ::IsDlgButtonChecked(window_, lock_buttons_check_id) == BST_CHECKED;
    }

    void layout_current_client() {
      RECT rect {};
      if (::GetClientRect(window_, &rect) == FALSE) {
        return;
      }
      layout_controls(rect.right - rect.left, rect.bottom - rect.top);
    }

    void relayout_and_redraw() {
      layout_current_client();
      redraw_window();
    }

    void redraw_window() {
      ::RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
    }

    void handle_command(int id, int notification) {
      if (id == create_button_id && notification == BN_CLICKED) {
        create_gamepad();
        return;
      }
      if (id == profile_combo_id && notification == CBN_SELCHANGE) {
        refresh_selected_device();
        relayout_and_redraw();
        return;
      }
      if (id == reset_button_id && notification == BN_CLICKED) {
        reset_selected_device();
        return;
      }
      if (id == remove_selected_button_id && notification == BN_CLICKED) {
        remove_selected_device();
        return;
      }
      if (id == remove_all_button_id && notification == BN_CLICKED) {
        remove_all_devices();
        return;
      }
      if (id == lock_buttons_check_id && notification == BN_CLICKED) {
        handle_button_lock_changed();
        return;
      }
      if (id == battery_state_combo_id && notification == CBN_SELCHANGE) {
        set_selected_battery_from_controls();
        return;
      }
      if (id == clear_battery_button_id && notification == BN_CLICKED) {
        clear_selected_battery();
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
        if (buttons_locked()) {
          toggle_selected_button(static_cast<std::size_t>(id - button_base_id));
        }
      }
    }

    void handle_slider(HWND slider) {
      if (slider == battery_slider_) {
        set_selected_battery_from_controls();
        return;
      }

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
      if (adapter) {
        if (const auto status = adapter->close(); !status.ok()) {
          show_error(widen(status.message()));
        }
      }
      refresh_all();
    }

    void remove_all_devices() {
      std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> adapters;
      {
        std::lock_guard lock {mutex_};
        adapters = take_all_adapters_locked();
      }

      close_adapters(adapters, true);
      refresh_all();
    }

    void toggle_selected_button(std::size_t index) {
      auto pressed = false;
      {
        std::lock_guard lock {mutex_};
        const auto *device = selected_device_locked();
        if (device == nullptr) {
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

    void handle_button_lock_changed() {
      if (buttons_locked()) {
        refresh_selected_device();
        return;
      }

      auto status = lvh::OperationStatus::success();
      {
        std::lock_guard lock {mutex_};
        auto *device = selected_device_locked();
        if (device == nullptr) {
          refresh_selected_device();
          return;
        }

        auto state = device->adapter->state();
        state.buttons.clear();
        status = device->adapter->set_state(state);
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

    void set_selected_battery_from_controls() {
      const auto state_selection = ::SendMessageW(battery_state_combo_, CB_GETCURSEL, 0, 0);
      if (state_selection == CB_ERR || state_selection < 0 || static_cast<std::size_t>(state_selection) >= battery_choices.size()) {
        return;
      }

      lvh::GamepadBattery battery;
      battery.state = battery_choices[static_cast<std::size_t>(state_selection)].state;
      battery.percentage = static_cast<std::uint8_t>(std::clamp<LRESULT>(::SendMessageW(battery_slider_, TBM_GETPOS, 0, 0), 0, 100));

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
        show_error(widen(status.message()));
      }
      refresh_selected_device();
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

    std::optional<lvh::DeviceProfile> current_combo_profile() const {
      const auto selection = ::SendMessageW(profile_combo_, CB_GETCURSEL, 0, 0);
      if (selection == CB_ERR || selection < 0 || static_cast<std::size_t>(selection) >= profile_choices.size()) {
        return std::nullopt;
      }

      return profile_for_choice(profile_choices[static_cast<std::size_t>(selection)]);
    }

    bool update_visible_controls_for_profile(const lvh::DeviceProfile &profile) {
      auto changed = false;
      for (std::size_t index = 0; index < button_choices.size(); ++index) {
        const auto visible = lvh::supports_gamepad_button(profile, button_choices[index].button);
        changed = changed || visible_buttons_[index] != visible;
        visible_buttons_[index] = visible;
      }
      changed = changed || battery_controls_visible_ != profile.capabilities.supports_battery;
      battery_controls_visible_ = profile.capabilities.supports_battery;
      return changed;
    }

    std::wstring profile_feature_summary(const lvh::DeviceProfile &profile) const {
      const auto support = lvh::gamepad_profile_support(profile);
      std::wostringstream stream;
      stream << L"Features: battery " << yes_no(support.supports_battery)
             << L" | rumble " << yes_no(lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::rumble))
             << L" | trigger rumble " << yes_no(lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::trigger_rumble))
             << L" | RGB LED " << yes_no(lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::rgb_led))
             << L" | adaptive triggers " << yes_no(lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::adaptive_triggers))
             << L" | raw output " << yes_no(lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::raw_report));
      return stream.str();
    }

    std::wstring output_summary(const ControlledGamepad &device, const lvh::DeviceProfile &profile) const {
      std::wostringstream stream;
      stream << L"Output: ";
      if (device.outputs.empty()) {
        stream << L"no reports received";
      } else {
        auto wrote = false;
        if (device.latest_rumble) {
          stream << L"rumble low=" << device.latest_rumble->low_frequency_rumble << L" high=" << device.latest_rumble->high_frequency_rumble;
          wrote = true;
        }
        if (device.latest_trigger_rumble) {
          stream << (wrote ? L" | " : L"") << L"trigger rumble L=" << device.latest_trigger_rumble->left_trigger_rumble << L" R=" << device.latest_trigger_rumble->right_trigger_rumble;
          wrote = true;
        }
        if (device.latest_rgb_led) {
          stream << (wrote ? L" | " : L"") << L"RGB " << static_cast<unsigned>(device.latest_rgb_led->red) << L"," << static_cast<unsigned>(device.latest_rgb_led->green)
                 << L"," << static_cast<unsigned>(device.latest_rgb_led->blue);
          wrote = true;
        }
        if (device.latest_adaptive_triggers) {
          stream << (wrote ? L" | " : L"") << L"adaptive flags=" << static_cast<unsigned>(device.latest_adaptive_triggers->adaptive_trigger_flags);
          wrote = true;
        }
        if (!wrote && device.latest_raw_report) {
          stream << L"raw report";
          wrote = true;
        }
        if (!wrote) {
          stream << L"reports received";
        }
      }

      if (!lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::rumble) && !lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::rgb_led) &&
          !lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::adaptive_triggers) && !lvh::supports_gamepad_output(profile, lvh::GamepadOutputKind::trigger_rumble)) {
        stream << L" | profile has no normalized feedback categories";
      }
      return stream.str();
    }

    void refresh_selected_device() {
      std::lock_guard lock {mutex_};
      auto *device = selected_device_locked();
      auto relayout_needed = false;
      const auto enabled = device != nullptr;
      ::EnableWindow(reset_button_, enabled);
      ::EnableWindow(remove_selected_button_, enabled);
      ::EnableWindow(remove_all_button_, !devices_.empty());
      for (auto *slider : axis_sliders_) {
        ::EnableWindow(slider, enabled);
      }

      if (device == nullptr) {
        if (const auto profile = current_combo_profile()) {
          relayout_needed = update_visible_controls_for_profile(*profile);
          ::SetWindowTextW(feature_text_, profile_feature_summary(*profile).c_str());
        } else {
          relayout_needed = std::any_of(visible_buttons_.begin(), visible_buttons_.end(), [](bool visible) {
                              return visible;
                            }) ||
                            battery_controls_visible_;
          visible_buttons_.fill(false);
          battery_controls_visible_ = false;
          ::SetWindowTextW(feature_text_, L"");
        }

        for (std::size_t index = 0; index < button_controls_.size(); ++index) {
          ::EnableWindow(button_controls_[index], FALSE);
          set_button_visual(index, false);
        }
        ::EnableWindow(battery_state_combo_, FALSE);
        ::EnableWindow(battery_slider_, FALSE);
        ::EnableWindow(clear_battery_button_, FALSE);
        ::SetWindowTextW(state_text_, L"No device selected. Create a gamepad to begin testing.");
        ::SetWindowTextW(output_summary_text_, L"Output: no selected device.");
        ::SendMessageW(nodes_list_, LB_RESETCONTENT, 0, 0);
        ::SendMessageW(output_list_, LB_RESETCONTENT, 0, 0);
        reset_sliders();
        refresh_battery_controls({}, false);
        if (relayout_needed) {
          relayout_and_redraw();
        }
        return;
      }

      const auto *gamepad = device->adapter->gamepad();
      const auto &profile = gamepad->profile();
      const auto state = device->adapter->state();
      relayout_needed = update_visible_controls_for_profile(profile);

      std::wostringstream state_text;
      state_text << device->profile_label << L" #" << gamepad->device_id() << L"\r\n"
                 << device_type_name(profile.device_type) << L" | " << widen(profile.name) << L"\r\n"
                 << L"L(" << state.left_stick.x << L", " << state.left_stick.y << L") "
                 << L"R(" << state.right_stick.x << L", " << state.right_stick.y << L") "
                 << L"LT " << state.left_trigger << L" RT " << state.right_trigger << L"\r\n";
      if (state.battery) {
        state_text << L"Battery " << battery_state_name(state.battery->state) << L" " << static_cast<unsigned>(state.battery->percentage) << L"% | ";
      } else {
        state_text << L"Battery unset | ";
      }
      state_text
        << gamepad->submit_count() << L" submits";
      ::SetWindowTextW(state_text_, state_text.str().c_str());
      ::SetWindowTextW(feature_text_, profile_feature_summary(profile).c_str());
      ::SetWindowTextW(output_summary_text_, output_summary(*device, profile).c_str());

      for (std::size_t index = 0; index < button_choices.size(); ++index) {
        ::EnableWindow(button_controls_[index], visible_buttons_[index]);
        set_button_visual(index, state.buttons.test(button_choices[index].button));
      }

      ::SendMessageW(axis_sliders_[0], TBM_SETPOS, TRUE, axis_to_slider(state.left_stick.x));
      ::SendMessageW(axis_sliders_[1], TBM_SETPOS, TRUE, axis_to_slider(state.left_stick.y));
      ::SendMessageW(axis_sliders_[2], TBM_SETPOS, TRUE, axis_to_slider(state.right_stick.x));
      ::SendMessageW(axis_sliders_[3], TBM_SETPOS, TRUE, axis_to_slider(state.right_stick.y));
      ::SendMessageW(axis_sliders_[4], TBM_SETPOS, TRUE, trigger_to_slider(state.left_trigger));
      ::SendMessageW(axis_sliders_[5], TBM_SETPOS, TRUE, trigger_to_slider(state.right_trigger));
      refresh_battery_controls(state, device->adapter->support().supports_battery);

      refresh_nodes(*gamepad);
      refresh_outputs(*device);
      if (relayout_needed) {
        relayout_and_redraw();
      }
    }

    void reset_sliders() {
      for (std::size_t index = 0; index < axis_sliders_.size(); ++index) {
        ::SendMessageW(axis_sliders_[index], TBM_SETPOS, TRUE, 0);
      }
    }

    void set_button_visual(std::size_t index, bool pressed) {
      if (index >= button_controls_.size()) {
        return;
      }
      ::SendMessageW(button_controls_[index], BM_SETSTATE, pressed ? TRUE : FALSE, 0);
    }

    void refresh_battery_controls(const lvh::GamepadState &state, bool supported) {
      const auto enabled = selected_device_locked() != nullptr && supported;
      ::EnableWindow(battery_state_combo_, enabled);
      ::EnableWindow(battery_slider_, enabled);
      ::EnableWindow(clear_battery_button_, enabled && state.battery.has_value());

      if (state.battery) {
        ::SendMessageW(battery_state_combo_, CB_SETCURSEL, battery_choice_index(state.battery->state), 0);
        ::SendMessageW(battery_slider_, TBM_SETPOS, TRUE, state.battery->percentage);
        return;
      }

      ::SendMessageW(battery_state_combo_, CB_SETCURSEL, battery_choice_index(lvh::GamepadBatteryState::full), 0);
      ::SendMessageW(battery_slider_, TBM_SETPOS, TRUE, 100);
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

        switch (output.kind) {
          using enum lvh::GamepadOutputKind;

          case rumble:
            iter->second.latest_rumble = output;
            break;
          case trigger_rumble:
            iter->second.latest_trigger_rumble = output;
            break;
          case rgb_led:
            iter->second.latest_rgb_led = output;
            break;
          case adaptive_triggers:
            iter->second.latest_adaptive_triggers = output;
            break;
          case raw_report:
            iter->second.latest_raw_report = output;
            break;
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
        adapters = take_all_adapters_locked();
      }

      close_adapters(adapters, false);
    }

    std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> take_all_adapters_locked() {
      std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> adapters;
      for (auto &[id, device] : devices_) {
        adapters.push_back(std::move(device.adapter));
      }
      devices_.clear();
      selected_id_ = 0;
      return adapters;
    }

    void close_adapters(std::vector<std::unique_ptr<lvh::GamepadStateAdapter>> &adapters, bool report_errors) {
      std::optional<std::wstring> first_error;
      for (auto &adapter : adapters) {
        if (adapter) {
          if (const auto status = adapter->close(); !status.ok() && report_errors && !first_error) {
            first_error = widen(status.message());
          }
        }
      }

      if (first_error) {
        show_error(*first_error);
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
    HWND remove_selected_button_ = nullptr;
    HWND remove_all_button_ = nullptr;
    HWND lock_buttons_check_ = nullptr;
    HWND state_text_ = nullptr;
    HWND feature_text_ = nullptr;
    HWND battery_label_ = nullptr;
    HWND battery_state_combo_ = nullptr;
    HWND battery_slider_ = nullptr;
    HWND clear_battery_button_ = nullptr;
    HWND output_summary_text_ = nullptr;
    HWND nodes_label_ = nullptr;
    HWND output_label_ = nullptr;
    HWND nodes_list_ = nullptr;
    HWND output_list_ = nullptr;
    std::array<HWND, button_choices.size()> button_controls_ {};
    std::array<HWND, axis_choices.size()> axis_labels_ {};
    std::array<HWND, axis_choices.size()> axis_sliders_ {};
    std::array<bool, button_choices.size()> visible_buttons_ {};
    std::array<bool, button_choices.size()> momentary_pointer_pressed_ {};
    std::array<bool, button_choices.size()> momentary_key_pressed_ {};
    std::unique_ptr<lvh::Runtime> runtime_;
    std::mutex mutex_;
    std::map<lvh::DeviceId, ControlledGamepad> devices_;
    lvh::DeviceId selected_id_ = 0;
    std::uint64_t next_metadata_index_ = 0;
    std::uint64_t next_output_sequence_ = 1;
    bool battery_controls_visible_ = false;
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
