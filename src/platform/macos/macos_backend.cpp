/**
 * @file src/platform/macos/macos_backend.cpp
 * @brief macOS CoreGraphics backend definitions.
 */

// standard includes
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

// platform includes
#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hidsystem/IOLLEvent.h>

// local includes
#include "core/backend.hpp"

namespace lvh::detail {
  namespace macos {

    constexpr auto multi_click_delay = std::chrono::milliseconds(500);  ///< Maximum double-click gap.
    constexpr int wheel_delta = 120;  ///< Windows-style high-resolution wheel delta.
    constexpr double default_scrollwheel_scaling = 0.3125;  ///< Default macOS scroll speed slider position.
    constexpr int default_scroll_lines_per_detent = 5;  ///< Logical lines represented by one default wheel detent.

    /**
     * @brief Mapping from portable Windows-style key code to macOS virtual key code.
     */
    struct KeyCodeMap {
      KeyboardKeyCode portable_key_code;  ///< Portable key code received from the consumer.
      int macos_key_code;  ///< macOS virtual key code sent through CoreGraphics.
    };

    /**
     * @brief Build the portable Windows-style key code to macOS virtual key code map.
     *
     * @return Portable key code map.
     */
    constexpr std::array<KeyCodeMap, 167> make_key_code_map() {
      std::array<KeyCodeMap, 167> result {};
      result[0] = {0x08 /* VKEY_BACK */, kVK_Delete};
      result[1] = {0x09 /* VKEY_TAB */, kVK_Tab};
      result[2] = {0x0A /* VKEY_BACKTAB */, 0x21E4};
      result[3] = {0x0C /* VKEY_CLEAR */, kVK_ANSI_KeypadClear};
      result[4] = {0x0D /* VKEY_RETURN */, kVK_Return};
      result[5] = {0x10 /* VKEY_SHIFT */, kVK_Shift};
      result[6] = {0x11 /* VKEY_CONTROL */, kVK_Control};
      result[7] = {0x12 /* VKEY_MENU */, kVK_Option};
      result[8] = {0x13 /* VKEY_PAUSE */, -1};
      result[9] = {0x14 /* VKEY_CAPITAL */, kVK_CapsLock};
      result[10] = {0x15 /* VKEY_KANA */, kVK_JIS_Kana};
      result[11] = {0x15 /* VKEY_HANGUL */, -1};
      result[12] = {0x17 /* VKEY_JUNJA */, -1};
      result[13] = {0x18 /* VKEY_FINAL */, -1};
      result[14] = {0x19 /* VKEY_HANJA */, -1};
      result[15] = {0x19 /* VKEY_KANJI */, -1};
      result[16] = {0x1B /* VKEY_ESCAPE */, kVK_Escape};
      result[17] = {0x1C /* VKEY_CONVERT */, -1};
      result[18] = {0x1D /* VKEY_NONCONVERT */, -1};
      result[19] = {0x1E /* VKEY_ACCEPT */, -1};
      result[20] = {0x1F /* VKEY_MODECHANGE */, -1};
      result[21] = {0x20 /* VKEY_SPACE */, kVK_Space};
      result[22] = {0x21 /* VKEY_PRIOR */, kVK_PageUp};
      result[23] = {0x22 /* VKEY_NEXT */, kVK_PageDown};
      result[24] = {0x23 /* VKEY_END */, kVK_End};
      result[25] = {0x24 /* VKEY_HOME */, kVK_Home};
      result[26] = {0x25 /* VKEY_LEFT */, kVK_LeftArrow};
      result[27] = {0x26 /* VKEY_UP */, kVK_UpArrow};
      result[28] = {0x27 /* VKEY_RIGHT */, kVK_RightArrow};
      result[29] = {0x28 /* VKEY_DOWN */, kVK_DownArrow};
      result[30] = {0x29 /* VKEY_SELECT */, -1};
      result[31] = {0x2A /* VKEY_PRINT */, -1};
      result[32] = {0x2B /* VKEY_EXECUTE */, -1};
      result[33] = {0x2C /* VKEY_SNAPSHOT */, -1};
      result[34] = {0x2D /* VKEY_INSERT */, kVK_Help};
      result[35] = {0x2E /* VKEY_DELETE */, kVK_ForwardDelete};
      result[36] = {0x2F /* VKEY_HELP */, kVK_Help};
      result[37] = {0x30 /* VKEY_0 */, kVK_ANSI_0};
      result[38] = {0x31 /* VKEY_1 */, kVK_ANSI_1};
      result[39] = {0x32 /* VKEY_2 */, kVK_ANSI_2};
      result[40] = {0x33 /* VKEY_3 */, kVK_ANSI_3};
      result[41] = {0x34 /* VKEY_4 */, kVK_ANSI_4};
      result[42] = {0x35 /* VKEY_5 */, kVK_ANSI_5};
      result[43] = {0x36 /* VKEY_6 */, kVK_ANSI_6};
      result[44] = {0x37 /* VKEY_7 */, kVK_ANSI_7};
      result[45] = {0x38 /* VKEY_8 */, kVK_ANSI_8};
      result[46] = {0x39 /* VKEY_9 */, kVK_ANSI_9};
      result[47] = {0x41 /* VKEY_A */, kVK_ANSI_A};
      result[48] = {0x42 /* VKEY_B */, kVK_ANSI_B};
      result[49] = {0x43 /* VKEY_C */, kVK_ANSI_C};
      result[50] = {0x44 /* VKEY_D */, kVK_ANSI_D};
      result[51] = {0x45 /* VKEY_E */, kVK_ANSI_E};
      result[52] = {0x46 /* VKEY_F */, kVK_ANSI_F};
      result[53] = {0x47 /* VKEY_G */, kVK_ANSI_G};
      result[54] = {0x48 /* VKEY_H */, kVK_ANSI_H};
      result[55] = {0x49 /* VKEY_I */, kVK_ANSI_I};
      result[56] = {0x4A /* VKEY_J */, kVK_ANSI_J};
      result[57] = {0x4B /* VKEY_K */, kVK_ANSI_K};
      result[58] = {0x4C /* VKEY_L */, kVK_ANSI_L};
      result[59] = {0x4D /* VKEY_M */, kVK_ANSI_M};
      result[60] = {0x4E /* VKEY_N */, kVK_ANSI_N};
      result[61] = {0x4F /* VKEY_O */, kVK_ANSI_O};
      result[62] = {0x50 /* VKEY_P */, kVK_ANSI_P};
      result[63] = {0x51 /* VKEY_Q */, kVK_ANSI_Q};
      result[64] = {0x52 /* VKEY_R */, kVK_ANSI_R};
      result[65] = {0x53 /* VKEY_S */, kVK_ANSI_S};
      result[66] = {0x54 /* VKEY_T */, kVK_ANSI_T};
      result[67] = {0x55 /* VKEY_U */, kVK_ANSI_U};
      result[68] = {0x56 /* VKEY_V */, kVK_ANSI_V};
      result[69] = {0x57 /* VKEY_W */, kVK_ANSI_W};
      result[70] = {0x58 /* VKEY_X */, kVK_ANSI_X};
      result[71] = {0x59 /* VKEY_Y */, kVK_ANSI_Y};
      result[72] = {0x5A /* VKEY_Z */, kVK_ANSI_Z};
      result[73] = {0x5B /* VKEY_LWIN */, kVK_Command};
      result[74] = {0x5C /* VKEY_RWIN */, kVK_RightCommand};
      result[75] = {0x5D /* VKEY_APPS */, kVK_RightCommand};
      result[76] = {0x5F /* VKEY_SLEEP */, -1};
      result[77] = {0x60 /* VKEY_NUMPAD0 */, kVK_ANSI_Keypad0};
      result[78] = {0x61 /* VKEY_NUMPAD1 */, kVK_ANSI_Keypad1};
      result[79] = {0x62 /* VKEY_NUMPAD2 */, kVK_ANSI_Keypad2};
      result[80] = {0x63 /* VKEY_NUMPAD3 */, kVK_ANSI_Keypad3};
      result[81] = {0x64 /* VKEY_NUMPAD4 */, kVK_ANSI_Keypad4};
      result[82] = {0x65 /* VKEY_NUMPAD5 */, kVK_ANSI_Keypad5};
      result[83] = {0x66 /* VKEY_NUMPAD6 */, kVK_ANSI_Keypad6};
      result[84] = {0x67 /* VKEY_NUMPAD7 */, kVK_ANSI_Keypad7};
      result[85] = {0x68 /* VKEY_NUMPAD8 */, kVK_ANSI_Keypad8};
      result[86] = {0x69 /* VKEY_NUMPAD9 */, kVK_ANSI_Keypad9};
      result[87] = {0x6A /* VKEY_MULTIPLY */, kVK_ANSI_KeypadMultiply};
      result[88] = {0x6B /* VKEY_ADD */, kVK_ANSI_KeypadPlus};
      result[89] = {0x6C /* VKEY_SEPARATOR */, -1};
      result[90] = {0x6D /* VKEY_SUBTRACT */, kVK_ANSI_KeypadMinus};
      result[91] = {0x6E /* VKEY_DECIMAL */, kVK_ANSI_KeypadDecimal};
      result[92] = {0x6F /* VKEY_DIVIDE */, kVK_ANSI_KeypadDivide};
      result[93] = {0x70 /* VKEY_F1 */, kVK_F1};
      result[94] = {0x71 /* VKEY_F2 */, kVK_F2};
      result[95] = {0x72 /* VKEY_F3 */, kVK_F3};
      result[96] = {0x73 /* VKEY_F4 */, kVK_F4};
      result[97] = {0x74 /* VKEY_F5 */, kVK_F5};
      result[98] = {0x75 /* VKEY_F6 */, kVK_F6};
      result[99] = {0x76 /* VKEY_F7 */, kVK_F7};
      result[100] = {0x77 /* VKEY_F8 */, kVK_F8};
      result[101] = {0x78 /* VKEY_F9 */, kVK_F9};
      result[102] = {0x79 /* VKEY_F10 */, kVK_F10};
      result[103] = {0x7A /* VKEY_F11 */, kVK_F11};
      result[104] = {0x7B /* VKEY_F12 */, kVK_F12};
      result[105] = {0x7C /* VKEY_F13 */, kVK_F13};
      result[106] = {0x7D /* VKEY_F14 */, kVK_F14};
      result[107] = {0x7E /* VKEY_F15 */, kVK_F15};
      result[108] = {0x7F /* VKEY_F16 */, kVK_F16};
      result[109] = {0x80 /* VKEY_F17 */, kVK_F17};
      result[110] = {0x81 /* VKEY_F18 */, kVK_F18};
      result[111] = {0x82 /* VKEY_F19 */, kVK_F19};
      result[112] = {0x83 /* VKEY_F20 */, kVK_F20};
      result[113] = {0x84 /* VKEY_F21 */, -1};
      result[114] = {0x85 /* VKEY_F22 */, -1};
      result[115] = {0x86 /* VKEY_F23 */, -1};
      result[116] = {0x87 /* VKEY_F24 */, -1};
      result[117] = {0x90 /* VKEY_NUMLOCK */, -1};
      result[118] = {0x91 /* VKEY_SCROLL */, -1};
      result[119] = {0xA0 /* VKEY_LSHIFT */, kVK_Shift};
      result[120] = {0xA1 /* VKEY_RSHIFT */, kVK_RightShift};
      result[121] = {0xA2 /* VKEY_LCONTROL */, kVK_Control};
      result[122] = {0xA3 /* VKEY_RCONTROL */, kVK_RightControl};
      result[123] = {0xA4 /* VKEY_LMENU */, kVK_Option};
      result[124] = {0xA5 /* VKEY_RMENU */, kVK_RightOption};
      result[125] = {0xA6 /* VKEY_BROWSER_BACK */, -1};
      result[126] = {0xA7 /* VKEY_BROWSER_FORWARD */, -1};
      result[127] = {0xA8 /* VKEY_BROWSER_REFRESH */, -1};
      result[128] = {0xA9 /* VKEY_BROWSER_STOP */, -1};
      result[129] = {0xAA /* VKEY_BROWSER_SEARCH */, -1};
      result[130] = {0xAB /* VKEY_BROWSER_FAVORITES */, -1};
      result[131] = {0xAC /* VKEY_BROWSER_HOME */, -1};
      result[132] = {0xAD /* VKEY_VOLUME_MUTE */, -1};
      result[133] = {0xAE /* VKEY_VOLUME_DOWN */, -1};
      result[134] = {0xAF /* VKEY_VOLUME_UP */, -1};
      result[135] = {0xB0 /* VKEY_MEDIA_NEXT_TRACK */, -1};
      result[136] = {0xB1 /* VKEY_MEDIA_PREV_TRACK */, -1};
      result[137] = {0xB2 /* VKEY_MEDIA_STOP */, -1};
      result[138] = {0xB3 /* VKEY_MEDIA_PLAY_PAUSE */, -1};
      result[139] = {0xB4 /* VKEY_MEDIA_LAUNCH_MAIL */, -1};
      result[140] = {0xB5 /* VKEY_MEDIA_LAUNCH_MEDIA_SELECT */, -1};
      result[141] = {0xB6 /* VKEY_MEDIA_LAUNCH_APP1 */, -1};
      result[142] = {0xB7 /* VKEY_MEDIA_LAUNCH_APP2 */, -1};
      result[143] = {0xBA /* VKEY_OEM_1 */, kVK_ANSI_Semicolon};
      result[144] = {0xBB /* VKEY_OEM_PLUS */, kVK_ANSI_Equal};
      result[145] = {0xBC /* VKEY_OEM_COMMA */, kVK_ANSI_Comma};
      result[146] = {0xBD /* VKEY_OEM_MINUS */, kVK_ANSI_Minus};
      result[147] = {0xBE /* VKEY_OEM_PERIOD */, kVK_ANSI_Period};
      result[148] = {0xBF /* VKEY_OEM_2 */, kVK_ANSI_Slash};
      result[149] = {0xC0 /* VKEY_OEM_3 */, kVK_ANSI_Grave};
      result[150] = {0xDB /* VKEY_OEM_4 */, kVK_ANSI_LeftBracket};
      result[151] = {0xDC /* VKEY_OEM_5 */, kVK_ANSI_Backslash};
      result[152] = {0xDD /* VKEY_OEM_6 */, kVK_ANSI_RightBracket};
      result[153] = {0xDE /* VKEY_OEM_7 */, kVK_ANSI_Quote};
      result[154] = {0xDF /* VKEY_OEM_8 */, -1};
      result[155] = {0xE2 /* VKEY_OEM_102 */, -1};
      result[156] = {0xE5 /* VKEY_PROCESSKEY */, -1};
      result[157] = {0xE7 /* VKEY_PACKET */, -1};
      result[158] = {0xF6 /* VKEY_ATTN */, -1};
      result[159] = {0xF7 /* VKEY_CRSEL */, -1};
      result[160] = {0xF8 /* VKEY_EXSEL */, -1};
      result[161] = {0xF9 /* VKEY_EREOF */, -1};
      result[162] = {0xFA /* VKEY_PLAY */, -1};
      result[163] = {0xFB /* VKEY_ZOOM */, -1};
      result[164] = {0xFC /* VKEY_NONAME */, -1};
      result[165] = {0xFD /* VKEY_PA1 */, -1};
      result[166] = {0xFE /* VKEY_OEM_CLEAR */, kVK_ANSI_KeypadClear};
      return result;
    }

    /**
     * @brief Portable Windows-style key code to macOS virtual key code map.
     */
    constexpr auto key_code_map = make_key_code_map();

    /**
     * @brief Resolve a portable key code to a macOS virtual key code.
     *
     * @param key_code Portable key code.
     * @return macOS virtual key code when supported.
     */
    inline std::optional<CGKeyCode> macos_key_code(KeyboardKeyCode key_code) {
      const auto position = std::ranges::lower_bound(key_code_map, key_code, {}, &KeyCodeMap::portable_key_code);

      if (position == key_code_map.end() || position->portable_key_code != key_code || position->macos_key_code < 0) {
        return std::nullopt;
      }

      return static_cast<CGKeyCode>(position->macos_key_code);
    }

    /**
     * @brief macOS modifier flags split into generic and device-specific masks.
     */
    struct ModifierFlags {
      CGEventFlags generic {};  ///< Device-independent CoreGraphics modifier flag.
      CGEventFlags device {};  ///< Left/right device-specific flag.
      CGEventFlags all_devices {};  ///< All left/right device flags for this modifier.
    };

    /**
     * @brief Resolve modifier masks associated with a macOS key.
     *
     * @param key macOS virtual key code.
     * @param flags Output modifier flag masks.
     * @return `true` when the key represents a modifier.
     */
    inline bool modifier_flags_for_key(CGKeyCode key, ModifierFlags &flags) {
      switch (key) {
        case kVK_Shift:
          flags = {kCGEventFlagMaskShift, NX_DEVICELSHIFTKEYMASK, NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK};
          return true;
        case kVK_RightShift:
          flags = {kCGEventFlagMaskShift, NX_DEVICERSHIFTKEYMASK, NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK};
          return true;
        case kVK_Command:
          flags = {kCGEventFlagMaskCommand, NX_DEVICELCMDKEYMASK, NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK};
          return true;
        case kVK_RightCommand:
          flags = {kCGEventFlagMaskCommand, NX_DEVICERCMDKEYMASK, NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK};
          return true;
        case kVK_Option:
          flags = {kCGEventFlagMaskAlternate, NX_DEVICELALTKEYMASK, NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK};
          return true;
        case kVK_RightOption:
          flags = {kCGEventFlagMaskAlternate, NX_DEVICERALTKEYMASK, NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK};
          return true;
        case kVK_Control:
          flags = {kCGEventFlagMaskControl, NX_DEVICELCTLKEYMASK, NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK};
          return true;
        case kVK_RightControl:
          flags = {kCGEventFlagMaskControl, NX_DEVICERCTLKEYMASK, NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK};
          return true;
        default:
          return false;
      }
    }

    /**
     * @brief Convert a scroll-wheel slider value to logical lines per detent.
     *
     * @param scale macOS scroll-wheel scaling preference.
     * @return Logical lines represented by one wheel detent.
     */
    inline int scroll_lines_per_detent(double scale) {
      if (!std::isfinite(scale)) {
        scale = default_scrollwheel_scaling;
      }

      const auto scroll_scale = std::clamp(scale, 0.0, 1.0);
      constexpr double lines_per_scroll_scale = (default_scroll_lines_per_detent - 1.0) / default_scrollwheel_scaling;
      return std::max(1, static_cast<int>(std::ceil(1.0 + scroll_scale * lines_per_scroll_scale)));
    }

    /**
     * @brief Read the macOS scroll-wheel preference.
     *
     * @param scrollwheel_scaling Output raw scaling preference.
     * @return Logical lines represented by one wheel detent.
     */
    inline int read_scroll_lines_per_detent(double &scrollwheel_scaling) {
      double scale = default_scrollwheel_scaling;
      const auto value = CFPreferencesCopyValue(
        CFSTR("com.apple.scrollwheel.scaling"),
        kCFPreferencesAnyApplication,
        kCFPreferencesCurrentUser,
        kCFPreferencesAnyHost
      );
      if (value) {
        if (CFGetTypeID(value) == CFNumberGetTypeID()) {
          CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &scale);
        } else if (CFGetTypeID(value) == CFStringGetTypeID()) {
          scale = CFStringGetDoubleValue(static_cast<CFStringRef>(value));
        }
        CFRelease(value);
      }

      if (!std::isfinite(scale)) {
        scale = default_scrollwheel_scaling;
      }
      scrollwheel_scaling = scale;
      return scroll_lines_per_detent(scale);
    }

    /**
     * @brief Convert a high-resolution wheel delta to CoreGraphics scroll pixels.
     *
     * @param high_resolution_distance Wheel delta in high-resolution units.
     * @param pixels_per_line Pixel distance represented by one logical line.
     * @param lines_per_detent Logical lines represented by one wheel detent.
     * @return Pixel distance to send to CoreGraphics.
     */
    inline int scroll_pixels(std::int32_t high_resolution_distance, int pixels_per_line, int lines_per_detent) {
      const auto scaled_pixels = static_cast<std::int64_t>(high_resolution_distance) *
                                 std::max(1, pixels_per_line) *
                                 std::max(1, lines_per_detent);
      return static_cast<int>(scaled_pixels / wheel_delta);
    }

    /**
     * @brief Shared macOS backend state.
     */
    class MacosInputState {
    public:
      MacosInputState():
          display {CGMainDisplayID()},
          display_scaling {display_scaling_for(display)},
          source {CGEventSourceCreate(kCGEventSourceStateHIDSystemState)},
          keyboard_source {CGEventSourceCreate(kCGEventSourceStatePrivate)},
          mouse_event {source ? CGEventCreate(source) : nullptr},
          scroll_lines_per_detent_value {read_scroll_lines_per_detent(scrollwheel_scaling)} {}

      MacosInputState(const MacosInputState &) = delete;
      MacosInputState &operator=(const MacosInputState &) = delete;
      MacosInputState(MacosInputState &&) noexcept = delete;
      MacosInputState &operator=(MacosInputState &&) noexcept = delete;

      ~MacosInputState() {
        if (mouse_event) {
          CFRelease(mouse_event);
        }
        if (keyboard_source) {
          CFRelease(keyboard_source);
        }
        if (source) {
          CFRelease(source);
        }
      }

      /**
       * @brief Compute the coordinate scaling factor for a display.
       *
       * @param display_id CoreGraphics display identifier.
       * @return Coordinate scaling factor.
       */
      static CGFloat display_scaling_for(CGDirectDisplayID display_id) {
        const auto mode = CGDisplayCopyDisplayMode(display_id);
        if (!mode) {
          return 1.0;
        }

        const auto logical_width = CGDisplayModeGetPixelWidth(mode);
        if (logical_width == 0) {
          CFRelease(mode);
          return 1.0;
        }

        const auto scaling = static_cast<CGFloat>(CGDisplayPixelsWide(display_id)) / static_cast<CGFloat>(logical_width);
        CFRelease(mode);
        return scaling;
      }

      CGDirectDisplayID display {};  ///< CoreGraphics identifier for the target display.
      CGFloat display_scaling = 1.0;  ///< Scaling factor from logical to physical display pixels.
      CGEventSourceRef source {};  ///< CoreGraphics event source for mouse and scroll events.
      CGEventSourceRef keyboard_source {};  ///< CoreGraphics event source for keyboard events.
      CGEventRef mouse_event {};  ///< Reusable CoreGraphics mouse event.
      double scrollwheel_scaling = default_scrollwheel_scaling;  ///< Raw macOS scroll-wheel scaling preference.
      int scroll_lines_per_detent_value = default_scroll_lines_per_detent;  ///< Logical lines per wheel detent.
      CGEventFlags keyboard_flags {};  ///< Active modifier flags applied to mouse events.
      std::mutex keyboard_mutex;  ///< Guards shared keyboard modifier state.
    };

    /**
     * @brief Backend keyboard backed by CoreGraphics keyboard events.
     */
    class MacosKeyboard final: public BackendKeyboard {
    public:
      explicit MacosKeyboard(std::shared_ptr<MacosInputState> state):
          state_ {std::move(state)} {}

      ~MacosKeyboard() override {
        static_cast<void>(close());
      }

      OperationStatus submit(const KeyboardEvent &event) override {
        using enum ErrorCode;

        if (!open_) {
          return OperationStatus::failure(device_closed, "macOS keyboard is closed");
        }

        const auto key = macos_key_code(event.key_code);
        if (!key) {
          return OperationStatus::success();
        }

        if (!state_->keyboard_source) {
          return OperationStatus::failure(backend_failure, "macOS keyboard event source is unavailable");
        }

        const auto keyboard_event = CGEventCreateKeyboardEvent(state_->keyboard_source, *key, event.pressed);
        if (!keyboard_event) {
          return OperationStatus::failure(backend_failure, "create macOS keyboard event");
        }

        std::lock_guard lock {state_->keyboard_mutex};
        CGEventSetIntegerValueField(keyboard_event, kCGKeyboardEventKeycode, *key);

        ModifierFlags modifier_flags;
        if (modifier_flags_for_key(*key, modifier_flags)) {
          if (event.pressed) {
            state_->keyboard_flags |= modifier_flags.generic | modifier_flags.device;
          } else {
            state_->keyboard_flags &= ~modifier_flags.device;
            if ((state_->keyboard_flags & modifier_flags.all_devices) == 0) {
              state_->keyboard_flags &= ~modifier_flags.generic;
            }
          }
          CGEventSetType(keyboard_event, kCGEventFlagsChanged);
        } else {
          CGEventSetType(keyboard_event, event.pressed ? kCGEventKeyDown : kCGEventKeyUp);
        }

        CGEventSetFlags(keyboard_event, state_->keyboard_flags);
        CGEventPost(kCGSessionEventTap, keyboard_event);
        CFRelease(keyboard_event);
        return OperationStatus::success();
      }

      OperationStatus type_text(const KeyboardTextEvent &event) override {
        using enum ErrorCode;

        if (!open_) {
          return OperationStatus::failure(device_closed, "macOS keyboard is closed");
        }
        if (event.text.empty()) {
          return OperationStatus::success();
        }

        return OperationStatus::failure(unsupported_profile, "macOS keyboard text input is not implemented");
      }

      OperationStatus close() override {
        open_ = false;
        return OperationStatus::success();
      }

    private:
      std::shared_ptr<MacosInputState> state_;
      bool open_ = true;
    };

    /**
     * @brief macOS mouse button metadata.
     */
    struct MacosMouseButton {
      CGMouseButton button {};  ///< CoreGraphics button code.
      std::size_t index = 0;  ///< Local button-state index.
      CGEventType down_event {};  ///< CoreGraphics down event type.
      CGEventType up_event {};  ///< CoreGraphics up event type.
    };

    /**
     * @brief Translate a portable mouse button to CoreGraphics metadata.
     *
     * @param button Portable mouse button.
     * @return CoreGraphics button metadata when supported.
     */
    inline std::optional<MacosMouseButton> macos_mouse_button(MouseButton button) {
      switch (button) {
        using enum MouseButton;

        case left:
          return MacosMouseButton {kCGMouseButtonLeft, 0, kCGEventLeftMouseDown, kCGEventLeftMouseUp};
        case right:
          return MacosMouseButton {kCGMouseButtonRight, 1, kCGEventRightMouseDown, kCGEventRightMouseUp};
        case middle:
          return MacosMouseButton {kCGMouseButtonCenter, 2, kCGEventOtherMouseDown, kCGEventOtherMouseUp};
        case side:
        case extra:
          return std::nullopt;
      }

      return std::nullopt;
    }

    /**
     * @brief Backend mouse backed by CoreGraphics mouse and scroll events.
     */
    class MacosMouse final: public BackendMouse {
    public:
      explicit MacosMouse(std::shared_ptr<MacosInputState> state):
          state_ {std::move(state)} {}

      ~MacosMouse() override {
        static_cast<void>(close());
      }

      OperationStatus submit(const MouseEvent &event) override {
        using enum MouseEventKind;

        std::lock_guard lock {mutex_};
        if (!open_) {
          return OperationStatus::failure(ErrorCode::device_closed, "macOS mouse is closed");
        }
        if (!state_->source || !state_->mouse_event) {
          return OperationStatus::failure(ErrorCode::backend_failure, "macOS mouse event source is unavailable");
        }

        switch (event.kind) {
          case relative_motion:
            return submit_relative_motion(event.x, event.y);
          case absolute_motion:
            return submit_absolute_motion(event);
          case button:
            return submit_button(event);
          case vertical_scroll:
            return submit_scroll(event.high_resolution_scroll, 0);
          case horizontal_scroll:
            return submit_scroll(0, event.high_resolution_scroll);
        }

        return OperationStatus::failure(ErrorCode::invalid_argument, "unsupported macOS mouse event kind");
      }

      OperationStatus close() override {
        std::lock_guard lock {mutex_};
        open_ = false;
        return OperationStatus::success();
      }

    private:
      CGPoint current_location() const {
        const auto snapshot_event = CGEventCreate(state_->source);
        if (!snapshot_event) {
          const auto display_bounds = CGDisplayBounds(state_->display);
          return display_bounds.origin;
        }

        const auto current = CGEventGetLocation(snapshot_event);
        CFRelease(snapshot_event);
        return current;
      }

      CGEventType event_type_for_current_buttons() const {
        if (mouse_down_[0]) {
          return kCGEventLeftMouseDragged;
        }
        if (mouse_down_[1]) {
          return kCGEventOtherMouseDragged;
        }
        if (mouse_down_[2]) {
          return kCGEventRightMouseDragged;
        }
        return kCGEventMouseMoved;
      }

      OperationStatus post_mouse(
        CGMouseButton button,
        CGEventType type,
        CGPoint raw_location,
        CGPoint previous_location,
        int click_count
      ) const {
        const auto display_bounds = CGDisplayBounds(state_->display);
        const auto location = CGPoint {
          std::clamp(raw_location.x, display_bounds.origin.x, display_bounds.origin.x + display_bounds.size.width - 1),
          std::clamp(raw_location.y, display_bounds.origin.y, display_bounds.origin.y + display_bounds.size.height - 1)
        };

        const auto event = state_->mouse_event;
        CGEventSetType(event, type);
        CGEventSetLocation(event, location);
        CGEventSetIntegerValueField(event, kCGMouseEventButtonNumber, button);
        CGEventSetIntegerValueField(event, kCGMouseEventClickState, click_count);
        CGEventSetDoubleValueField(event, kCGMouseEventDeltaX, raw_location.x - previous_location.x);
        CGEventSetDoubleValueField(event, kCGMouseEventDeltaY, raw_location.y - previous_location.y);

        {
          std::lock_guard lock {state_->keyboard_mutex};
          CGEventSetFlags(event, state_->keyboard_flags);
        }

        CGEventPost(kCGHIDEventTap, event);
        CGWarpMouseCursorPosition(location);
        return OperationStatus::success();
      }

      OperationStatus submit_relative_motion(std::int32_t delta_x, std::int32_t delta_y) {
        const auto current = current_location();
        const auto location = CGPoint {current.x + delta_x, current.y + delta_y};
        return post_mouse(kCGMouseButtonLeft, event_type_for_current_buttons(), location, current, 0);
      }

      OperationStatus submit_absolute_motion(const MouseEvent &event) {
        const auto display_bounds = CGDisplayBounds(state_->display);
        const auto location = CGPoint {
          event.has_fractional_absolute_coordinates ? display_bounds.origin.x + event.absolute_x * display_bounds.size.width :
                                                      static_cast<CGFloat>(event.x) * state_->display_scaling + display_bounds.origin.x,
          event.has_fractional_absolute_coordinates ? display_bounds.origin.y + event.absolute_y * display_bounds.size.height :
                                                      static_cast<CGFloat>(event.y) * state_->display_scaling + display_bounds.origin.y
        };
        return post_mouse(kCGMouseButtonLeft, event_type_for_current_buttons(), location, current_location(), 0);
      }

      OperationStatus submit_button(const MouseEvent &event) {
        using enum ErrorCode;

        const auto button = macos_mouse_button(event.button);
        if (!button) {
          return OperationStatus::failure(unsupported_profile, "macOS mouse backend supports only left, middle, and right buttons");
        }

        mouse_down_[button->index] = event.pressed;
        const auto now = std::chrono::steady_clock::now();
        const auto release_index = event.pressed ? 0U : 1U;
        const auto mouse_position = current_location();
        const auto click_count = now < last_mouse_event_[button->index][release_index] + multi_click_delay ? 2 : 1;

        last_mouse_event_[button->index][release_index] = now;
        return post_mouse(button->button, event.pressed ? button->down_event : button->up_event, mouse_position, mouse_position, click_count);
      }

      OperationStatus submit_scroll(std::int32_t high_resolution_vertical, std::int32_t high_resolution_horizontal) const {
        const auto source_pixels_per_line = CGEventSourceGetPixelsPerLine(state_->source);
        const auto pixels_per_line = source_pixels_per_line > 0 ? static_cast<int>(source_pixels_per_line + 0.5) : 10;
        const auto vertical = scroll_pixels(high_resolution_vertical, pixels_per_line, state_->scroll_lines_per_detent_value);
        const auto horizontal = scroll_pixels(high_resolution_horizontal, pixels_per_line, state_->scroll_lines_per_detent_value);
        if (vertical == 0 && horizontal == 0) {
          return OperationStatus::success();
        }

        const auto event = CGEventCreateScrollWheelEvent(state_->source, kCGScrollEventUnitPixel, 2, vertical, horizontal);
        if (!event) {
          return OperationStatus::failure(ErrorCode::backend_failure, "create macOS scroll event");
        }

        CGEventSetIntegerValueField(event, kCGScrollWheelEventIsContinuous, 1);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        return OperationStatus::success();
      }

      std::shared_ptr<MacosInputState> state_;
      std::array<bool, 3> mouse_down_ {};
      std::array<std::array<std::chrono::steady_clock::time_point, 2>, 3> last_mouse_event_ {};
      std::mutex mutex_;
      bool open_ = true;
    };

    /**
     * @brief macOS platform backend.
     */
    class MacosBackend final: public Backend {
    public:
      MacosBackend() {
        capabilities_.backend_name = "macos-coregraphics";
        capabilities_.supports_keyboard = true;
        capabilities_.supports_mouse = true;
      }

      const BackendCapabilities &capabilities() const override {
        return capabilities_;
      }

      BackendGamepadCreationResult create_gamepad(DeviceId /*id*/, const CreateGamepadOptions & /*options*/) override {
        return {OperationStatus::failure(ErrorCode::unsupported_profile, "macOS gamepad backend is not implemented"), nullptr};
      }

      BackendKeyboardCreationResult create_keyboard(DeviceId /*id*/, const CreateKeyboardOptions &options) override {
        if (options.profile.device_type != DeviceType::keyboard) {
          return {OperationStatus::failure(ErrorCode::unsupported_profile, "device profile is not a keyboard"), nullptr};
        }
        if (!state_->keyboard_source) {
          return {OperationStatus::failure(ErrorCode::backend_failure, "macOS keyboard event source is unavailable"), nullptr};
        }

        return {OperationStatus::success(), std::make_unique<MacosKeyboard>(state_)};
      }

      BackendMouseCreationResult create_mouse(DeviceId /*id*/, const CreateMouseOptions &options) override {
        if (options.profile.device_type != DeviceType::mouse) {
          return {OperationStatus::failure(ErrorCode::unsupported_profile, "device profile is not a mouse"), nullptr};
        }
        if (!state_->source || !state_->mouse_event) {
          return {OperationStatus::failure(ErrorCode::backend_failure, "macOS mouse event source is unavailable"), nullptr};
        }

        return {OperationStatus::success(), std::make_unique<MacosMouse>(state_)};
      }

      BackendTouchscreenCreationResult create_touchscreen(
        DeviceId /*id*/,
        const CreateTouchscreenOptions & /*options*/
      ) override {
        return {OperationStatus::failure(ErrorCode::unsupported_profile, "macOS touchscreen backend is not implemented"), nullptr};
      }

      BackendTrackpadCreationResult create_trackpad(DeviceId /*id*/, const CreateTrackpadOptions & /*options*/) override {
        return {OperationStatus::failure(ErrorCode::unsupported_profile, "macOS trackpad backend is not implemented"), nullptr};
      }

      BackendPenTabletCreationResult create_pen_tablet(
        DeviceId /*id*/,
        const CreatePenTabletOptions & /*options*/
      ) override {
        return {OperationStatus::failure(ErrorCode::unsupported_profile, "macOS pen tablet backend is not implemented"), nullptr};
      }

    private:
      BackendCapabilities capabilities_;
      std::shared_ptr<MacosInputState> state_ {std::make_shared<MacosInputState>()};
    };

  }  // namespace macos

  std::unique_ptr<Backend> create_platform_backend() {
    return std::make_unique<macos::MacosBackend>();
  }

}  // namespace lvh::detail
