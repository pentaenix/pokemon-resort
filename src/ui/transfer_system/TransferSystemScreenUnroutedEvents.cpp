#include "ui/TransferSystemScreen.hpp"

#include "ui/transfer_system/detail/StringUtil.hpp"

#include <SDL.h>

namespace pr {

bool TransferSystemScreen::capturesUnroutedKeyboardFocus() const {
    return box_rename_modal_open_ && box_rename_editing_;
}

bool TransferSystemScreen::handleUnroutedSdlEvent(const SDL_Event& event) {
    if (!box_rename_modal_open_ || !box_rename_editing_) {
        return false;
    }
    constexpr std::size_t kMaxBytes = 120;
    if (event.type == SDL_TEXTINPUT) {
        std::string chunk(event.text.text);
        while (box_rename_text_utf8_.size() + chunk.size() > kMaxBytes && !chunk.empty()) {
            transfer_system::detail::utf8_pop_back_last(chunk);
        }
        box_rename_text_utf8_ += chunk;
        box_rename_ime_utf8_.clear();
        return true;
    }
    if (event.type == SDL_TEXTEDITING) {
        box_rename_ime_utf8_ = std::string(event.edit.text);
        return true;
    }
    if (event.type == SDL_KEYDOWN) {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE && event.key.repeat == 0) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            ui_state_.requestButtonSfx();
            return true;
        }
        if ((key == SDLK_RETURN || key == SDLK_KP_ENTER) && event.key.repeat == 0) {
            box_rename_editing_ = false;
            SDL_StopTextInput();
            box_rename_focus_slot_ = BoxRenameFocusSlot::Confirm;
            ui_state_.requestButtonSfx();
            return true;
        }
        if (key == SDLK_BACKSPACE) {
            transfer_system::detail::utf8_pop_back_last(box_rename_text_utf8_);
            box_rename_ime_utf8_.clear();
            return true;
        }
        if (key == SDLK_DELETE && event.key.repeat == 0) {
            box_rename_ime_utf8_.clear();
            return true;
        }
        return true;
    }
    return false;
}

} // namespace pr

