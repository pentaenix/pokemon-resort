#include "ui/TransferSystemScreen.hpp"

#include "core/Assets.hpp"
#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace pr {

using transfer_system::detail::drawRoundedOutlineScanlines;
using transfer_system::detail::fillRoundedRingScanlines;
using transfer_system::detail::setDrawColor;

void TransferSystemScreen::drawBoxRenameFocusRing(SDL_Renderer* renderer) const {
    if (!selection_cursor_style_.enabled || !box_rename_modal_open_ || box_rename_editing_) {
        return;
    }
    SDL_Rect r{};
    switch (box_rename_focus_slot_) {
        case BoxRenameFocusSlot::Field:
            r = box_rename_text_field_rect_virt_;
            break;
        case BoxRenameFocusSlot::Cancel:
            r = box_rename_cancel_button_rect_virt_;
            break;
        case BoxRenameFocusSlot::Confirm:
            r = box_rename_ok_button_rect_virt_;
            break;
    }
    if (r.w <= 0 || r.h <= 0) {
        return;
    }
    const double pulse =
        (std::sin(ui_state_.elapsedSeconds() * selection_cursor_style_.beat_speed * 3.14159265358979323846 * 2.0) + 1.0) *
        0.5;
    const int pad = selection_cursor_style_.padding + static_cast<int>(std::lround(selection_cursor_style_.beat_magnitude * pulse));
    const int inner_x = r.x - pad;
    const int inner_y = r.y - pad;
    int inner_w = r.w + pad * 2;
    int inner_h = r.h + pad * 2;
    const int min_w = std::max(0, selection_cursor_style_.min_width);
    const int min_h = std::max(0, selection_cursor_style_.min_height);
    int draw_x = inner_x;
    int draw_y = inner_y;
    if (inner_w < min_w || inner_h < min_h) {
        const int cx = inner_x + inner_w / 2;
        const int cy = inner_y + inner_h / 2;
        inner_w = std::max(inner_w, min_w);
        inner_h = std::max(inner_h, min_h);
        draw_x = cx - inner_w / 2;
        draw_y = cy - inner_h / 2;
    }
    const int corner =
        std::clamp(selection_cursor_style_.corner_radius + pad, 0, std::max(0, std::min(inner_w, inner_h) / 2));
    Color c = selection_cursor_style_.color;
    c.a = std::clamp(selection_cursor_style_.alpha, 0, 255);
    drawRoundedOutlineScanlines(renderer, draw_x, draw_y, inner_w, inner_h, corner, c, selection_cursor_style_.thickness);
}

void TransferSystemScreen::drawBoxRenameModal(SDL_Renderer* renderer) {
    if (!box_rename_modal_open_ || !renderer) {
        return;
    }
    TTF_Font* body_f = box_rename_modal_body_font_.get();
    if (!body_f) {
        body_f = dropdown_item_font_.get();
    }
    if (!body_f) {
        return;
    }

    syncBoxRenameModalLayout();

    const int vw = window_config_.virtual_width;
    const int vh = window_config_.virtual_height;
    // Match exit-save modal dim feel (tunable via transfer_save.json).
    setDrawColor(renderer, Color{0, 0, 0, 140});
    SDL_Rect dim_rect{0, 0, vw, vh};
    SDL_RenderFillRect(renderer, &dim_rect);

    const int card_x = box_rename_card_rect_virt_.x;
    const int card_y = box_rename_card_rect_virt_.y;
    const int card_w = box_rename_card_rect_virt_.w;
    const int card_h = box_rename_card_rect_virt_.h;
    fillRoundedRingScanlines(
        renderer,
        card_x,
        card_y,
        card_w,
        card_h,
        18,
        2,
        box_viewport_style_.viewport_border_color,
        box_viewport_style_.slot_background_color);

    const int input_x = box_rename_text_field_rect_virt_.x;
    const int field_y = box_rename_text_field_rect_virt_.y;
    const int input_inner_w = box_rename_text_field_rect_virt_.w;
    const int row_h = box_rename_text_field_rect_virt_.h;

    fillRoundedRingScanlines(
        renderer,
        input_x,
        field_y,
        input_inner_w,
        row_h,
        12,
        2,
        box_viewport_style_.viewport_border_color,
        box_viewport_style_.name_plate_background_color);

    const Color btn_border = box_viewport_style_.viewport_border_color;
    const Color btn_fill = box_viewport_style_.viewport_background_color;
    fillRoundedRingScanlines(
        renderer,
        box_rename_cancel_button_rect_virt_.x,
        box_rename_cancel_button_rect_virt_.y,
        box_rename_cancel_button_rect_virt_.w,
        box_rename_cancel_button_rect_virt_.h,
        10,
        2,
        btn_border,
        btn_fill);
    fillRoundedRingScanlines(
        renderer,
        box_rename_ok_button_rect_virt_.x,
        box_rename_ok_button_rect_virt_.y,
        box_rename_ok_button_rect_virt_.w,
        box_rename_ok_button_rect_virt_.h,
        10,
        2,
        btn_border,
        btn_fill);

    const Color icon_col = selection_cursor_style_.speech_bubble.text_color;
    TextureHandle x_tex = renderTextTexture(renderer, body_f, "\xc3\x97", icon_col);
    if (x_tex.texture) {
        const SDL_Rect& br = box_rename_cancel_button_rect_virt_;
        SDL_Rect xd{
            br.x + (br.w - x_tex.width) / 2,
            br.y + (br.h - x_tex.height) / 2,
            x_tex.width,
            x_tex.height};
        SDL_RenderCopy(renderer, x_tex.texture.get(), nullptr, &xd);
    }
    TextureHandle check_tex = renderTextTexture(renderer, body_f, "\xe2\x9c\x93", icon_col);
    if (check_tex.texture) {
        const SDL_Rect& br = box_rename_ok_button_rect_virt_;
        SDL_Rect chk{
            br.x + (br.w - check_tex.width) / 2,
            br.y + (br.h - check_tex.height) / 2,
            check_tex.width,
            check_tex.height};
        SDL_RenderCopy(renderer, check_tex.texture.get(), nullptr, &chk);
    }

    const std::string display = box_rename_text_utf8_ + box_rename_ime_utf8_;
    const Color text_col = display.empty() ? box_name_dropdown_style_.panel_border_color : selection_cursor_style_.speech_bubble.text_color;
    TextureHandle value_tex =
        display.empty() ? renderTextTexture(renderer, body_f, "Enter name", text_col)
                          : renderTextTexture(renderer, body_f, display, text_col);
    const int text_pad = 14;
    const int max_text_w = input_inner_w - text_pad * 2;
    if (value_tex.texture) {
        const int ty = field_y + (row_h - value_tex.height) / 2;
        SDL_Rect vd{input_x + text_pad, ty, std::min(value_tex.width, max_text_w), value_tex.height};
        SDL_RenderCopy(renderer, value_tex.texture.get(), nullptr, &vd);

        if (box_rename_editing_) {
            const bool caret_on =
                std::fmod(box_rename_caret_blink_phase_, 1.06) < 0.53 || !box_rename_ime_utf8_.empty();
            if (caret_on && !display.empty()) {
                int tw = 0;
                int th = 0;
                if (TTF_SizeUTF8(body_f, display.c_str(), &tw, &th) == 0) {
                    const int cx_caret = input_x + text_pad + std::min(tw, max_text_w);
                    setDrawColor(renderer, selection_cursor_style_.speech_bubble.text_color);
                    SDL_Rect caret{cx_caret, field_y + 12, 3, row_h - 24};
                    SDL_RenderFillRect(renderer, &caret);
                }
            }
        }
    }

    drawBoxRenameFocusRing(renderer);

    if (box_rename_editing_) {
        SDL_SetTextInputRect(&box_rename_text_field_rect_virt_);
    }
}

} // namespace pr

