#include "ui/TransferSystemScreen.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

namespace {

void setDrawColor(SDL_Renderer* renderer, const Color& c) {
    SDL_SetRenderDrawColor(
        renderer,
        static_cast<Uint8>(c.r),
        static_cast<Uint8>(c.g),
        static_cast<Uint8>(c.b),
        static_cast<Uint8>(c.a));
}

void fillRoundedRectScanlines(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, const Color& c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    setDrawColor(renderer, c);
    for (int yy = y; yy < y + h; ++yy) {
        int x0 = x;
        int x1 = x + w;
        if (yy < y + radius) {
            const int dy = yy - (y + radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        } else if (yy >= y + h - radius) {
            const int dy = yy - (y + h - radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        }
        if (x1 > x0) {
            SDL_Rect row{x0, yy, x1 - x0, 1};
            SDL_RenderFillRect(renderer, &row);
        }
    }
}

void fillRoundedRingScanlines(
    SDL_Renderer* renderer,
    int x,
    int y,
    int w,
    int h,
    int outer_radius,
    int stroke,
    const Color& border,
    const Color& inner_fill) {
    if (w <= 0 || h <= 0 || stroke <= 0 || stroke * 2 >= w || stroke * 2 >= h) {
        return;
    }
    fillRoundedRectScanlines(renderer, x, y, w, h, outer_radius, border);
    const int inner_r = std::max(0, outer_radius - stroke);
    fillRoundedRectScanlines(renderer, x + stroke, y + stroke, w - 2 * stroke, h - 2 * stroke, inner_r, inner_fill);
}

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
    const int vx = carousel_style_.offset_from_left_wall;
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    if (vw <= 0 || vh <= 0) {
        return;
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
        SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
        SDL_SetTextureAlphaMod(tex.texture.get(), 255);
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

void TransferSystemScreen::drawGameBoxNameDropdownChrome(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.empty() || game_box_browser_.dropdownExpandT() <= 1e-6) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int rad =
        std::clamp(box_name_dropdown_style_.panel_corner_radius, 0, std::min(outer.w, outer.h) / 2);

    fillRoundedRingScanlines(
        renderer,
        outer.x,
        outer.y,
        outer.w,
        outer.h,
        rad,
        stroke,
        box_name_dropdown_style_.panel_border_color,
        box_name_dropdown_style_.panel_color);
}

void TransferSystemScreen::drawGameBoxNameDropdownList(SDL_Renderer* renderer) const {
    if (!box_name_dropdown_style_.enabled || game_pc_boxes_.empty() || game_box_browser_.dropdownExpandT() <= 1e-6) {
        return;
    }
    if (dropdown_labels_dirty_) {
        const_cast<TransferSystemScreen*>(this)->rebuildDropdownItemTextures(renderer);
    }
    if (static_cast<int>(dropdown_item_textures_.size()) != static_cast<int>(game_pc_boxes_.size())) {
        return;
    }

    SDL_Rect outer{};
    int list_h = 0;
    int list_clip_y = 0;
    if (!computeGameBoxDropdownOuterRect(outer, static_cast<float>(game_box_browser_.dropdownExpandT()), list_h, list_clip_y)) {
        return;
    }

    const int stroke = std::max(1, box_name_dropdown_style_.panel_border_thickness);
    const int inner_x = outer.x + stroke;
    const int inner_w = outer.w - stroke * 2;
    const int rh = std::max(1, game_box_browser_.dropdownRowHeightPx());
    const int n = static_cast<int>(game_pc_boxes_.size());

    SDL_Rect clip{inner_x, list_clip_y, inner_w, list_h};
    SDL_RenderSetClipRect(renderer, &clip);

    const Color tint = box_name_dropdown_style_.selected_row_tint;
    for (int i = 0; i < n; ++i) {
        const int row_top = list_clip_y + i * rh - static_cast<int>(std::lround(game_box_browser_.dropdownScrollPx()));
        if (row_top + rh < list_clip_y || row_top > list_clip_y + list_h) {
            continue;
        }
        if (i == game_box_browser_.dropdownHighlightIndex()) {
            const int rr = std::max(0, std::min(8, rh / 4));
            fillRoundedRectScanlines(renderer, inner_x, row_top, inner_w, rh, rr, tint);
        }
        if (i < static_cast<int>(dropdown_item_textures_.size()) && dropdown_item_textures_[i].texture) {
            const TextureHandle& tex = dropdown_item_textures_[i];
            const int tcx = inner_x + inner_w / 2 - tex.width / 2;
            const int tcy = row_top + (rh - tex.height) / 2;
            SDL_Rect dst{tcx, tcy, tex.width, tex.height};
            SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
        }
    }

    SDL_RenderSetClipRect(renderer, nullptr);
}

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
        resort_box_viewport_->render(renderer);
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
