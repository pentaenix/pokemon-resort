#include "ui/TransferSystemScreen.hpp"
#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

using transfer_system::detail::setDrawColor;

void TransferSystemScreen::drawBackground(SDL_Renderer* renderer) const {
    if (!background_.texture) {
        return;
    }

    SDL_SetTextureBlendMode(background_.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(background_.texture.get(), 255);
    SDL_SetTextureColorMod(background_.texture.get(), 255, 255, 255);

    const double safe_scale = std::max(0.01, background_animation_.scale);
    const int width = std::max(1, static_cast<int>(std::round(
        static_cast<double>(background_.width) * safe_scale)));
    const int height = std::max(1, static_cast<int>(std::round(
        static_cast<double>(background_.height) * safe_scale)));

    if (!background_animation_.enabled ||
        (background_animation_.speed_x == 0.0 && background_animation_.speed_y == 0.0)) {
        SDL_Rect dst{0, 0, width, height};
        SDL_RenderCopy(renderer, background_.texture.get(), nullptr, &dst);
        return;
    }

    const int screen_width = window_config_.virtual_width;
    const int screen_height = window_config_.virtual_height;
    const int offset_x = static_cast<int>(std::floor(background_animation_.speed_x * ui_state_.elapsedSeconds())) % width;
    const int offset_y = static_cast<int>(std::floor(background_animation_.speed_y * ui_state_.elapsedSeconds())) % height;
    const int start_x = offset_x > 0 ? offset_x - width : offset_x;
    const int start_y = offset_y > 0 ? offset_y - height : offset_y;

    for (int y = start_y; y < screen_height; y += height) {
        for (int x = start_x; x < screen_width; x += width) {
            SDL_Rect dst{x, y, width, height};
            SDL_RenderCopy(renderer, background_.texture.get(), nullptr, &dst);
        }
    }
}

void TransferSystemScreen::render(SDL_Renderer* renderer) {
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    drawBackground(renderer);

    if (resort_box_viewport_) {
        const bool resort_dropdown_visible = box_name_dropdown_style_.enabled && !resort_pc_boxes_.empty() &&
            resort_box_browser_.dropdownExpandT() > 1e-6;
        if (resort_dropdown_visible) {
            resort_box_viewport_->renderBelowNamePlate(renderer);
            drawResortBoxNameDropdownChrome(renderer);
            resort_box_viewport_->renderNamePlate(renderer);
            drawResortBoxNameDropdownList(renderer);
        } else {
            resort_box_viewport_->render(renderer);
        }
    }

    if (game_save_box_viewport_) {
        const bool game_dropdown_visible = box_name_dropdown_style_.enabled && !game_pc_boxes_.empty() &&
            game_box_browser_.dropdownExpandT() > 1e-6;
        if (game_dropdown_visible) {
            game_save_box_viewport_->renderBelowNamePlate(renderer);
            drawGameBoxNameDropdownChrome(renderer);
            game_save_box_viewport_->renderNamePlate(renderer);
            drawGameBoxNameDropdownList(renderer);
        } else {
            game_save_box_viewport_->render(renderer);
        }
    }

    drawMiniPreview(renderer);
    drawToolCarousel(renderer);
    drawPillToggle(renderer);
    drawBottomBanner(renderer);
    drawSelectionCursor(renderer);
    drawMultiSelectionDrag(renderer);
    drawKeyboardMultiMarquee(renderer);
    drawHeldBoxSpaceBox(renderer);
    drawHeldPokemon(renderer);
    drawHeldMultiPokemon(renderer);
    drawHeldItem(renderer);
    drawItemActionMenu(renderer);
    drawPokemonActionMenu(renderer);
    drawBoxRenameModal(renderer);

    if (!ui_state_.exitInProgress() && ui_state_.fadeInSeconds() > 1e-6) {
        const double t = std::clamp(ui_state_.elapsedSeconds() / ui_state_.fadeInSeconds(), 0.0, 1.0);
        const int a = static_cast<int>(std::lround(255.0 * (1.0 - t)));
        if (a > 0) {
            setDrawColor(renderer, Color{0, 0, 0, a});
            SDL_Rect r{0, 0, window_config_.virtual_width, window_config_.virtual_height};
            SDL_RenderFillRect(renderer, &r);
        }
    }

    if (ui_state_.exitInProgress() && ui_state_.fadeOutSeconds() > 1e-6) {
        const double t = std::clamp(ui_state_.exitFadeSeconds() / ui_state_.fadeOutSeconds(), 0.0, 1.0);
        const int a = static_cast<int>(std::lround(255.0 * t));
        setDrawColor(renderer, Color{0, 0, 0, a});
        SDL_Rect r{0, 0, window_config_.virtual_width, window_config_.virtual_height};
        SDL_RenderFillRect(renderer, &r);
    }
}

} // namespace pr

