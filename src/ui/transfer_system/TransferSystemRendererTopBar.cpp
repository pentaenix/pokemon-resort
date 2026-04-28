#include "ui/TransferSystemScreen.hpp"
#include "ui/transfer_system/GameBoxBrowserController.hpp"
#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

using transfer_system::detail::fillRoundedRectScanlines;
using transfer_system::detail::fillRoundedRingScanlines;

namespace {

int carouselIconClipInset(const GameTransferToolCarouselStyle& st, int vw, int vh, int radius) {
    if (st.viewport_clip_inset > 0) {
        return std::clamp(st.viewport_clip_inset, 1, std::max(1, std::min(vw, vh) / 2 - 1));
    }
    return std::clamp(radius, 1, std::max(1, std::min(vw, vh) / 2 - 1));
}

void getPillTrackBounds(const GameTransferPillToggleStyle& st, int screen_w, int& tx, int& ty, int& tw, int& th) {
    constexpr int kBoxViewportY = 100;
    const int right_col_x = screen_w - 40 - BoxViewport::kViewportWidth;
    tx = right_col_x + (BoxViewport::kViewportWidth - st.track_width) / 2;
    ty = kBoxViewportY - st.gap_above_boxes - st.track_height;
    tw = st.track_width;
    th = st.track_height;
}

} // namespace

void TransferSystemScreen::drawToolCarousel(SDL_Renderer* renderer) const {
    const int vx = carousel_style_.offset_from_left_wall +
        (exit_button_enabled_ ? (carousel_style_.viewport_height + exit_button_gap_pixels_) : 0);
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    if (vw <= 0 || vh <= 0) {
        return;
    }

    if (exit_button_enabled_) {
        // Exit button: square, same height as tool belt, to the left of the carousel.
        const int bx = carousel_style_.offset_from_left_wall;
        const int by = exitButtonScreenY();
        const int bs = vh;
        const int radius = std::clamp(carousel_style_.viewport_corner_radius, 0, bs / 2);
        fillRoundedRectScanlines(renderer, bx, by, bs, bs, radius, carousel_style_.viewport_color);

        if (exit_button_icon_.texture) {
            SDL_SetTextureColorMod(
                exit_button_icon_.texture.get(),
                static_cast<Uint8>(std::clamp(exit_button_icon_mod_color_.r, 0, 255)),
                static_cast<Uint8>(std::clamp(exit_button_icon_mod_color_.g, 0, 255)),
                static_cast<Uint8>(std::clamp(exit_button_icon_mod_color_.b, 0, 255)));
            SDL_SetTextureAlphaMod(exit_button_icon_.texture.get(), static_cast<Uint8>(std::clamp(exit_button_icon_mod_color_.a, 0, 255)));
            const int pad = std::max(6, bs / 6);
            const int base = std::max(1, bs - 2 * pad);
            const double s = std::clamp(exit_button_icon_scale_, 0.05, 4.0);
            const int sz = std::clamp(static_cast<int>(std::lround(static_cast<double>(base) * s)), 1, base);
            const int dx = bx + (bs - sz) / 2;
            const int dy = by + (bs - sz) / 2;
            SDL_Rect dst{dx, dy, sz, sz};
            SDL_RenderCopy(renderer, exit_button_icon_.texture.get(), nullptr, &dst);
        }
    }

    const int radius =
        std::clamp(carousel_style_.viewport_corner_radius, 0, std::min(vw, vh) / 2);

    const int sel_i = ui_state_.selectedToolIndex();
    const int cy = vy + vh / 2;
    const int icon = std::max(1, carousel_style_.icon_size);

    const int scroll = static_cast<int>(std::lround(ui_state_.carouselSlideOffsetX()));
    const int focus_cx = vx + carousel_style_.slot_center_middle;
    const int pitch_l = carousel_style_.slot_center_middle - carousel_style_.slot_center_left;
    const int pitch_r = carousel_style_.slot_center_right - carousel_style_.slot_center_middle;

    auto strip_center_x_at_k = [&](int k) -> int {
        if (carousel_style_.belt_spacing_pixels > 0) {
            return focus_cx + k * carousel_style_.belt_spacing_pixels + scroll;
        }
        if (carousel_style_.slide_span_pixels > 0) {
            return focus_cx + k * carousel_style_.slide_span_pixels + scroll;
        }
        if (k == 0) {
            return focus_cx + scroll;
        }
        if (k < 0) {
            return focus_cx + k * pitch_l + scroll;
        }
        return focus_cx + k * pitch_r + scroll;
    };

    auto strip_tool_at_k = [&](int k) -> int {
        return ((sel_i + k) % 4 + 4) % 4;
    };

    fillRoundedRectScanlines(renderer, vx, vy, vw, vh, radius, carousel_style_.viewport_color);

    const int clip_inset = carouselIconClipInset(carousel_style_, vw, vh, radius);
    const SDL_Rect viewport_clip{vx, vy, vw, vh};
    SDL_Rect inner_clip{vx + clip_inset, vy + clip_inset, vw - 2 * clip_inset, vh - 2 * clip_inset};
    if (inner_clip.w < icon * 2 || inner_clip.h < icon) {
        inner_clip = viewport_clip;
    }

    const int fs = std::max(carousel_style_.selection_frame_size, icon + 2);
    const int stroke = std::clamp(carousel_style_.selection_stroke, 1, fs / 2);
    int fr = carousel_style_.selector_corner_radius;
    if (fr <= 0) {
        fr = std::clamp(radius, 0, fs / 2);
    } else {
        fr = std::clamp(fr, 0, fs / 2);
    }

    auto draw_icon = [&](int tool_i, int center_x) {
        const TextureHandle& tex = tool_icons_[static_cast<std::size_t>(tool_i)];
        if (!tex.texture || tex.width <= 0) {
            return;
        }
        SDL_SetTextureColorMod(
            tex.texture.get(),
            static_cast<Uint8>(std::clamp(carousel_style_.icon_mod_color.r, 0, 255)),
            static_cast<Uint8>(std::clamp(carousel_style_.icon_mod_color.g, 0, 255)),
            static_cast<Uint8>(std::clamp(carousel_style_.icon_mod_color.b, 0, 255)));
        SDL_SetTextureAlphaMod(tex.texture.get(), static_cast<Uint8>(std::clamp(carousel_style_.icon_mod_color.a, 0, 255)));
        const int half = icon / 2;
        SDL_Rect dst{center_x - half, cy - half, icon, icon};
        SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
    };

    SDL_RenderSetClipRect(renderer, &inner_clip);
    for (int k = -2; k <= 2; ++k) {
        draw_icon(strip_tool_at_k(k), strip_center_x_at_k(k));
    }

    SDL_RenderSetClipRect(renderer, &viewport_clip);
    const int fx = focus_cx - fs / 2;
    const int fy = cy - fs / 2;
    fillRoundedRingScanlines(
        renderer,
        fx,
        fy,
        fs,
        fs,
        fr,
        stroke,
        carouselFrameColorForIndex(sel_i),
        carousel_style_.viewport_color);

    int punch_cx = strip_center_x_at_k(0);
    int punch_tool = strip_tool_at_k(0);
    int best_d = std::abs(punch_cx - focus_cx);
    for (int k = -2; k <= 2; ++k) {
        const int cx = strip_center_x_at_k(k);
        const int d = std::abs(cx - focus_cx);
        if (d < best_d) {
            best_d = d;
            punch_cx = cx;
            punch_tool = strip_tool_at_k(k);
        }
    }
    SDL_RenderSetClipRect(renderer, &inner_clip);
    draw_icon(punch_tool, punch_cx);

    SDL_RenderSetClipRect(renderer, nullptr);
}

void TransferSystemScreen::drawPillToggle(SDL_Renderer* renderer) const {
    int track_x = 0;
    int track_y = 0;
    int track_w = 0;
    int track_h = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, track_x, track_y, track_w, track_h);
    const int enter_off =
        static_cast<int>(std::lround((1.0 - ui_state_.uiEnter()) * static_cast<double>(-(track_h + 24))));
    track_y += enter_off;

    const int pad = std::max(0, pill_style_.pill_inset);
    const int inner_x = track_x + pad;
    const int inner_y = track_y + pad;
    const int inner_w = std::max(0, track_w - 2 * pad);
    const int inner_h = std::max(0, track_h - 2 * pad);

    const int track_radius = std::min(track_h / 2, track_w / 2);
    fillRoundedRectScanlines(renderer, track_x, track_y, track_w, track_h, track_radius, pill_style_.track_color);

    const int pill_w = std::min(pill_style_.pill_width, inner_w);
    const int pill_h = std::min(pill_style_.pill_height, inner_h);
    const int max_travel = std::max(0, inner_w - pill_w);
    const int pill_x = inner_x + static_cast<int>(std::round(ui_state_.sliderT() * static_cast<double>(max_travel)));
    const int pill_y = inner_y + (inner_h - pill_h) / 2;
    const int pill_radius = std::max(4, std::min(pill_h / 2, pill_w / 2));

    const int mid_x = inner_x + inner_w / 2;
    const int pokemon_cx = inner_x + inner_w / 4;
    const int items_cx = inner_x + (3 * inner_w) / 4;
    const int label_cy = inner_y + inner_h / 2;

    if (pill_label_pokemon_white_.texture) {
        SDL_Rect dr{
            pokemon_cx - pill_label_pokemon_white_.width / 2,
            label_cy - pill_label_pokemon_white_.height / 2,
            pill_label_pokemon_white_.width,
            pill_label_pokemon_white_.height};
        SDL_RenderCopy(renderer, pill_label_pokemon_white_.texture.get(), nullptr, &dr);
    }
    if (pill_label_items_white_.texture) {
        SDL_Rect dr{
            items_cx - pill_label_items_white_.width / 2,
            label_cy - pill_label_items_white_.height / 2,
            pill_label_items_white_.width,
            pill_label_items_white_.height};
        SDL_RenderCopy(renderer, pill_label_items_white_.texture.get(), nullptr, &dr);
    }

    fillRoundedRectScanlines(renderer, pill_x, pill_y, pill_w, pill_h, pill_radius, pill_style_.pill_color);

    const int pill_cx = pill_x + pill_w / 2;
    const bool pokemon_selected = pill_cx < mid_x;
    if (pokemon_selected && pill_label_pokemon_black_.texture) {
        SDL_Rect dr{
            pokemon_cx - pill_label_pokemon_black_.width / 2,
            label_cy - pill_label_pokemon_black_.height / 2,
            pill_label_pokemon_black_.width,
            pill_label_pokemon_black_.height};
        SDL_RenderCopy(renderer, pill_label_pokemon_black_.texture.get(), nullptr, &dr);
    } else if (!pokemon_selected && pill_label_items_black_.texture) {
        SDL_Rect dr{
            items_cx - pill_label_items_black_.width / 2,
            label_cy - pill_label_items_black_.height / 2,
            pill_label_items_black_.width,
            pill_label_items_black_.height};
        SDL_RenderCopy(renderer, pill_label_items_black_.texture.get(), nullptr, &dr);
    }
}

} // namespace pr

