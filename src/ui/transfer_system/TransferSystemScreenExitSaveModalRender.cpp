#include "ui/TransferSystemScreen.hpp"

#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace pr {

using transfer_system::detail::fillRoundedRingScanlines;
using transfer_system::detail::setDrawColor;

void TransferSystemScreen::drawExitSaveModal(SDL_Renderer* renderer) const {
    if ((!exit_save_modal_open_ && exit_save_modal_reveal_ < 0.001) || !renderer) {
        return;
    }
    const auto& s = exit_save_modal_style_;
    const double t = std::clamp(exit_save_modal_reveal_, 0.0, 1.0);

    // Dim background (data-driven).
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (s.dim_background) {
        Color dim = s.dim_color;
        dim.a = static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(static_cast<double>(s.dim_alpha) * t)), 0, 255));
        setDrawColor(renderer, dim);
        SDL_Rect full{0, 0, window_config_.virtual_width, window_config_.virtual_height};
        SDL_RenderFillRect(renderer, &full);
    }

    const SDL_Rect card = exit_save_modal_card_rect_virt_;
    const int rad = std::max(0, s.corner_radius);
    const int stroke = std::max(0, s.border_thickness);
    Color card_fill = s.card_fill;
    card_fill.a = static_cast<std::uint8_t>(std::clamp(s.card_fill_alpha, 0, 255));
    Color card_border = s.card_border;
    card_border.a = static_cast<std::uint8_t>(std::clamp(s.card_border_alpha, 0, 255));
    fillRoundedRingScanlines(
        renderer,
        card.x,
        card.y,
        card.w,
        card.h,
        rad,
        stroke,
        card_border,
        card_fill);

    // Buttons.
    const std::string labels[3] = {
        "Save changes and exit",
        "Exit without saving changes",
        "Continue box operations",
    };

    for (int i = 0; i < 3; ++i) {
        const SDL_Rect r = exit_save_modal_row_rects_virt_[static_cast<std::size_t>(i)];
        const bool selected = (i == exit_save_modal_selected_row_);
        Color bg = selected ? s.selected_row_fill : s.row_fill;
        bg.a = static_cast<std::uint8_t>(std::clamp(selected ? s.selected_row_fill_alpha : s.row_fill_alpha, 0, 255));
        Color border = selected ? s.selected_row_border : s.row_border;
        border.a = static_cast<std::uint8_t>(std::clamp(selected ? s.selected_row_border_alpha : s.row_border_alpha, 0, 255));
        fillRoundedRingScanlines(renderer, r.x, r.y, r.w, r.h, 10, 2, border, bg);
        if (exit_save_modal_font_.get()) {
            Color text = s.text_color;
            text.a = static_cast<std::uint8_t>(std::clamp(s.text_alpha, 0, 255));
            TextureHandle tex = renderTextTexture(renderer, exit_save_modal_font_.get(), labels[i], text);
            if (tex.texture) {
                SDL_Rect dst{
                    r.x + 18,
                    r.y + r.h / 2 - tex.height / 2,
                    tex.width,
                    tex.height};
                SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
            }
        }
    }
}

} // namespace pr

