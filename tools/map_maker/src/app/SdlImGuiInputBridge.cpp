#include "mapmaker/app/SdlImGuiInputBridge.hpp"

#include <bgfx/bgfx.h>
#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace pr::mapmaker {
namespace {

ImGuiKey imguiKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_TAB: return ImGuiKey_Tab;
        case SDLK_LEFT: return ImGuiKey_LeftArrow;
        case SDLK_RIGHT: return ImGuiKey_RightArrow;
        case SDLK_UP: return ImGuiKey_UpArrow;
        case SDLK_DOWN: return ImGuiKey_DownArrow;
        case SDLK_PAGEUP: return ImGuiKey_PageUp;
        case SDLK_PAGEDOWN: return ImGuiKey_PageDown;
        case SDLK_HOME: return ImGuiKey_Home;
        case SDLK_END: return ImGuiKey_End;
        case SDLK_INSERT: return ImGuiKey_Insert;
        case SDLK_DELETE: return ImGuiKey_Delete;
        case SDLK_BACKSPACE: return ImGuiKey_Backspace;
        case SDLK_SPACE: return ImGuiKey_Space;
        case SDLK_RETURN: return ImGuiKey_Enter;
        case SDLK_ESCAPE: return ImGuiKey_Escape;
        case SDLK_QUOTE: return ImGuiKey_Apostrophe;
        case SDLK_COMMA: return ImGuiKey_Comma;
        case SDLK_MINUS: return ImGuiKey_Minus;
        case SDLK_PERIOD: return ImGuiKey_Period;
        case SDLK_SLASH: return ImGuiKey_Slash;
        case SDLK_SEMICOLON: return ImGuiKey_Semicolon;
        case SDLK_EQUALS: return ImGuiKey_Equal;
        case SDLK_LEFTBRACKET: return ImGuiKey_LeftBracket;
        case SDLK_BACKSLASH: return ImGuiKey_Backslash;
        case SDLK_RIGHTBRACKET: return ImGuiKey_RightBracket;
        case SDLK_BACKQUOTE: return ImGuiKey_GraveAccent;
        case SDLK_CAPSLOCK: return ImGuiKey_CapsLock;
        case SDLK_SCROLLLOCK: return ImGuiKey_ScrollLock;
        case SDLK_NUMLOCKCLEAR: return ImGuiKey_NumLock;
        case SDLK_PRINTSCREEN: return ImGuiKey_PrintScreen;
        case SDLK_PAUSE: return ImGuiKey_Pause;
        case SDLK_KP_0: return ImGuiKey_Keypad0;
        case SDLK_KP_1: return ImGuiKey_Keypad1;
        case SDLK_KP_2: return ImGuiKey_Keypad2;
        case SDLK_KP_3: return ImGuiKey_Keypad3;
        case SDLK_KP_4: return ImGuiKey_Keypad4;
        case SDLK_KP_5: return ImGuiKey_Keypad5;
        case SDLK_KP_6: return ImGuiKey_Keypad6;
        case SDLK_KP_7: return ImGuiKey_Keypad7;
        case SDLK_KP_8: return ImGuiKey_Keypad8;
        case SDLK_KP_9: return ImGuiKey_Keypad9;
        case SDLK_KP_PERIOD: return ImGuiKey_KeypadDecimal;
        case SDLK_KP_DIVIDE: return ImGuiKey_KeypadDivide;
        case SDLK_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
        case SDLK_KP_MINUS: return ImGuiKey_KeypadSubtract;
        case SDLK_KP_PLUS: return ImGuiKey_KeypadAdd;
        case SDLK_KP_ENTER: return ImGuiKey_KeypadEnter;
        case SDLK_KP_EQUALS: return ImGuiKey_KeypadEqual;
        case SDLK_LCTRL: return ImGuiKey_LeftCtrl;
        case SDLK_LSHIFT: return ImGuiKey_LeftShift;
        case SDLK_LALT: return ImGuiKey_LeftAlt;
        case SDLK_LGUI: return ImGuiKey_LeftSuper;
        case SDLK_RCTRL: return ImGuiKey_RightCtrl;
        case SDLK_RSHIFT: return ImGuiKey_RightShift;
        case SDLK_RALT: return ImGuiKey_RightAlt;
        case SDLK_RGUI: return ImGuiKey_RightSuper;
        case SDLK_APPLICATION: return ImGuiKey_Menu;
        case SDLK_0: return ImGuiKey_0;
        case SDLK_1: return ImGuiKey_1;
        case SDLK_2: return ImGuiKey_2;
        case SDLK_3: return ImGuiKey_3;
        case SDLK_4: return ImGuiKey_4;
        case SDLK_5: return ImGuiKey_5;
        case SDLK_6: return ImGuiKey_6;
        case SDLK_7: return ImGuiKey_7;
        case SDLK_8: return ImGuiKey_8;
        case SDLK_9: return ImGuiKey_9;
        case SDLK_a: return ImGuiKey_A;
        case SDLK_b: return ImGuiKey_B;
        case SDLK_c: return ImGuiKey_C;
        case SDLK_d: return ImGuiKey_D;
        case SDLK_e: return ImGuiKey_E;
        case SDLK_f: return ImGuiKey_F;
        case SDLK_g: return ImGuiKey_G;
        case SDLK_h: return ImGuiKey_H;
        case SDLK_i: return ImGuiKey_I;
        case SDLK_j: return ImGuiKey_J;
        case SDLK_k: return ImGuiKey_K;
        case SDLK_l: return ImGuiKey_L;
        case SDLK_m: return ImGuiKey_M;
        case SDLK_n: return ImGuiKey_N;
        case SDLK_o: return ImGuiKey_O;
        case SDLK_p: return ImGuiKey_P;
        case SDLK_q: return ImGuiKey_Q;
        case SDLK_r: return ImGuiKey_R;
        case SDLK_s: return ImGuiKey_S;
        case SDLK_t: return ImGuiKey_T;
        case SDLK_u: return ImGuiKey_U;
        case SDLK_v: return ImGuiKey_V;
        case SDLK_w: return ImGuiKey_W;
        case SDLK_x: return ImGuiKey_X;
        case SDLK_y: return ImGuiKey_Y;
        case SDLK_z: return ImGuiKey_Z;
        case SDLK_F1: return ImGuiKey_F1;
        case SDLK_F2: return ImGuiKey_F2;
        case SDLK_F3: return ImGuiKey_F3;
        case SDLK_F4: return ImGuiKey_F4;
        case SDLK_F5: return ImGuiKey_F5;
        case SDLK_F6: return ImGuiKey_F6;
        case SDLK_F7: return ImGuiKey_F7;
        case SDLK_F8: return ImGuiKey_F8;
        case SDLK_F9: return ImGuiKey_F9;
        case SDLK_F10: return ImGuiKey_F10;
        case SDLK_F11: return ImGuiKey_F11;
        case SDLK_F12: return ImGuiKey_F12;
        default: return ImGuiKey_None;
    }
}

void updateModifiers(SDL_Keymod modifiers) {
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & KMOD_CTRL) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifiers & KMOD_SHIFT) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (modifiers & KMOD_ALT) != 0);
    io.AddKeyEvent(ImGuiMod_Super, (modifiers & KMOD_GUI) != 0);
}

} // namespace

void SdlImGuiInputBridge::handleEvent(const SDL_Event& event) {
    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
        case SDL_MOUSEMOTION:
            mouse_x_ = event.motion.x;
            mouse_y_ = event.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            mouse_x_ = event.button.x;
            mouse_y_ = event.button.y;
            const bool down = event.type == SDL_MOUSEBUTTONDOWN;
            const std::uint8_t mask = event.button.button == SDL_BUTTON_LEFT ? IMGUI_MBUT_LEFT
                : event.button.button == SDL_BUTTON_RIGHT ? IMGUI_MBUT_RIGHT
                : event.button.button == SDL_BUTTON_MIDDLE ? IMGUI_MBUT_MIDDLE : 0;
            mouse_buttons_ = down ? static_cast<std::uint8_t>(mouse_buttons_ | mask)
                                  : static_cast<std::uint8_t>(mouse_buttons_ & ~mask);
            break;
        }
        case SDL_MOUSEWHEEL: {
            double delta = static_cast<double>(event.wheel.preciseY);
            if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) delta = -delta;
            scroll_accumulator_ += delta;
            scroll_accumulator_ = std::clamp(
                scroll_accumulator_,
                static_cast<double>(std::numeric_limits<int>::min()),
                static_cast<double>(std::numeric_limits<int>::max()));
            scroll_total_ = static_cast<int>(std::lround(scroll_accumulator_));
            break;
        }
        case SDL_TEXTINPUT:
            pending_text_ += event.text.text;
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const ImGuiKey key = imguiKey(event.key.keysym.sym);
            if (key != ImGuiKey_None) {
                io.AddKeyEvent(key, event.type == SDL_KEYDOWN);
                io.SetKeyEventNativeData(key, event.key.keysym.scancode, event.key.keysym.sym);
            }
            updateModifiers(static_cast<SDL_Keymod>(event.key.keysym.mod));
            break;
        }
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) io.AddFocusEvent(true);
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                // SDL may not deliver button-up after focus changes. Clear the
                // mirrored state so a drag cannot remain stuck on return.
                mouse_buttons_ = 0;
                io.AddFocusEvent(false);
            }
            break;
        default:
            break;
    }
}

void SdlImGuiInputBridge::beginFrame(
    int window_width, int window_height, std::uint16_t view_id) {
    updateModifiers(SDL_GetModState());
    // Input events must enter ImGui's queue before NewFrame(), which is called
    // by imguiBeginFrame. Adding text afterward delays it by a full frame.
    if (!pending_text_.empty()) {
        ImGui::GetIO().AddInputCharactersUTF8(pending_text_.c_str());
        pending_text_.clear();
    }
    imguiBeginFrame(
        mouse_x_, mouse_y_, mouse_buttons_, scroll_total_,
        static_cast<std::uint16_t>(std::max(1, window_width)),
        static_cast<std::uint16_t>(std::max(1, window_height)),
        -1,
        static_cast<bgfx::ViewId>(view_id));
}

bool SdlImGuiInputBridge::wantsKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }
bool SdlImGuiInputBridge::wantsPointer() const { return ImGui::GetIO().WantCaptureMouse; }
bool SdlImGuiInputBridge::wantsTextInput() const { return ImGui::GetIO().WantTextInput; }

} // namespace pr::mapmaker
