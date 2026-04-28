#include "ui/TransferSystemScreen.hpp"
#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

using transfer_system::detail::fillRoundedRectScanlines;
using transfer_system::detail::fillRoundedRingScanlines;

void TransferSystemScreen::drawPokemonActionMenu(SDL_Renderer* renderer) const {
    if (!pokemon_action_menu_.visible() || pokemon_action_menu_.transitionT() <= 1e-3) {
        return;
    }

    const auto& labels = transfer_system::PokemonActionMenuController::labels();
    const auto& st = pokemon_action_menu_style_;
    const SDL_Rect final = pokemonActionMenuFinalRect();
    const SDL_Rect& anchor = pokemon_action_menu_.anchorRect();
    const double t = std::clamp(pokemon_action_menu_.transitionT(), 0.0, 1.0);
    const double scale = 0.06 + 0.94 * t;
    const int anchor_cx = anchor.x + anchor.w / 2;
    const int anchor_cy = anchor.y + anchor.h / 2;
    const int final_cx = final.x + final.w / 2;
    const int final_cy = final.y + final.h / 2;
    const int cx = static_cast<int>(std::lround(anchor_cx + (final_cx - anchor_cx) * t));
    const int cy = static_cast<int>(std::lround(anchor_cy + (final_cy - anchor_cy) * t));
    const int w = std::max(1, static_cast<int>(std::lround(final.w * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(final.h * scale)));
    const SDL_Rect rect{cx - w / 2, cy - h / 2, w, h};

    auto faded = [t](Color c) {
        c.a = std::clamp(static_cast<int>(std::lround(static_cast<double>(c.a) * t)), 0, 255);
        return c;
    };

    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const int radius = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(st.corner_radius) * scale)),
        0,
        std::min(rect.w, rect.h) / 2);
    const int stroke = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(st.border_thickness) * scale)),
        1,
        std::max(1, std::min(rect.w, rect.h) / 2));
    fillRoundedRingScanlines(
        renderer,
        rect.x,
        rect.y,
        rect.w,
        rect.h,
        radius,
        stroke,
        faded(st.border_color),
        faded(st.background_color));

    if (!pokemon_action_menu_font_.get() || t < 0.55) {
        return;
    }

    const int text_alpha =
        std::clamp(static_cast<int>(std::lround(255.0 * std::clamp((t - 0.55) / 0.45, 0.0, 1.0))), 0, 255);
    Color text_color = st.text_color;
    text_color.a = text_alpha;
    const int row_h = std::max(1, st.row_height);
    const int pad_y = std::max(0, st.padding_y);
    for (int i = 0; i < static_cast<int>(labels.size()); ++i) {
        const int row_y = final.y + pad_y + i * row_h;
        if (i == pokemon_action_menu_.selectedRow()) {
            Color selected = st.selected_row_color;
            selected.a = std::clamp(static_cast<int>(std::lround(static_cast<double>(selected.a) * t)), 0, 255);
            const int rr = std::clamp(st.corner_radius / 2, 0, row_h / 2);
            fillRoundedRectScanlines(renderer, final.x + 10, row_y + 3, final.w - 20, row_h - 6, rr, selected);
        }
        TextureHandle label =
            renderTextTexture(renderer, pokemon_action_menu_font_.get(), labels[static_cast<std::size_t>(i)], text_color);
        if (!label.texture) {
            continue;
        }
        SDL_Rect dst{final.x + 28, row_y + (row_h - label.height) / 2, label.width, label.height};
        SDL_RenderCopy(renderer, label.texture.get(), nullptr, &dst);
    }
}

void TransferSystemScreen::drawItemActionMenu(SDL_Renderer* renderer) const {
    if (!item_action_menu_.visible() || item_action_menu_.transitionT() <= 1e-3) {
        return;
    }

    const auto& st = pokemon_action_menu_style_;
    const SDL_Rect final = item_action_menu_.finalRect(
        st,
        window_config_.virtual_width,
        pokemonActionMenuBottomLimitY());
    const SDL_Rect& anchor = item_action_menu_.anchorRect();
    const double t = std::clamp(item_action_menu_.transitionT(), 0.0, 1.0);
    const double scale = 0.06 + 0.94 * t;
    const int anchor_cx = anchor.x + anchor.w / 2;
    const int anchor_cy = anchor.y + anchor.h / 2;
    const int final_cx = final.x + final.w / 2;
    const int final_cy = final.y + final.h / 2;
    const int cx = static_cast<int>(std::lround(anchor_cx + (final_cx - anchor_cx) * t));
    const int cy = static_cast<int>(std::lround(anchor_cy + (final_cy - anchor_cy) * t));
    const int w = std::max(1, static_cast<int>(std::lround(final.w * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(final.h * scale)));
    const SDL_Rect rect{cx - w / 2, cy - h / 2, w, h};

    auto faded = [t](Color c) {
        c.a = std::clamp(static_cast<int>(std::lround(static_cast<double>(c.a) * t)), 0, 255);
        return c;
    };

    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const int radius = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(st.corner_radius) * scale)),
        0,
        std::min(rect.w, rect.h) / 2);
    const int stroke = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(st.border_thickness) * scale)),
        1,
        std::max(1, std::min(rect.w, rect.h) / 2));
    // Yellow border: reuse selection cursor color.
    fillRoundedRingScanlines(
        renderer,
        rect.x,
        rect.y,
        rect.w,
        rect.h,
        radius,
        stroke,
        faded(selection_cursor_style_.color),
        faded(st.background_color));

    if (!pokemon_action_menu_font_.get() || t < 0.55) {
        return;
    }

    const int text_alpha =
        std::clamp(static_cast<int>(std::lround(255.0 * std::clamp((t - 0.55) / 0.45, 0.0, 1.0))), 0, 255);
    Color text_color = st.text_color;
    text_color.a = text_alpha;
    const int row_h = std::max(1, st.row_height);
    const int pad_y = std::max(0, st.padding_y);
    for (int i = 0; i < item_action_menu_.rowCount(); ++i) {
        const int row_y = final.y + pad_y + i * row_h;
        if (i == item_action_menu_.selectedRow()) {
            Color selected = st.selected_row_color;
            selected.a = std::clamp(static_cast<int>(std::lround(static_cast<double>(selected.a) * t)), 0, 255);
            const int rr = std::clamp(st.corner_radius / 2, 0, row_h / 2);
            fillRoundedRectScanlines(renderer, final.x + 10, row_y + 3, final.w - 20, row_h - 6, rr, selected);
        }
        const std::string& row_label = item_action_menu_.labelAt(i);
        TextureHandle label =
            renderTextTexture(renderer, pokemon_action_menu_font_.get(), row_label, text_color);
        if (!label.texture) {
            continue;
        }
        SDL_Rect dst{final.x + 28, row_y + (row_h - label.height) / 2, label.width, label.height};
        SDL_RenderCopy(renderer, label.texture.get(), nullptr, &dst);
    }
}

} // namespace pr

