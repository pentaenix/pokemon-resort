#include "ui/TransferSystemScreen.hpp"

#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace pr {

using transfer_system::detail::fillRoundedRectScanlines;

namespace {
// Keep local to avoid introducing a new shared math utility in this phase.
void approachExponential(double& v, double target, double dt, double lambda) {
    if (lambda <= 1e-9) {
        v = target;
        return;
    }
    const double alpha = 1.0 - std::exp(-lambda * dt);
    v += (target - v) * alpha;
    if (std::fabs(target - v) < 0.0005) {
        v = target;
    }
}
} // namespace

bool TransferSystemScreen::shouldShowMiniPreviewForBox(int box_index, MiniPreviewContext context) const {
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return false;
    }

    switch (context) {
        case MiniPreviewContext::Dropdown:
            return true;
        case MiniPreviewContext::MouseHover:
        case MiniPreviewContext::BoxSpaceFocus:
            return gameBoxHasPreviewContent(box_index);
    }

    return false;
}

bool TransferSystemScreen::shouldShowMiniPreviewForResortBox(int box_index, MiniPreviewContext context) const {
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return false;
    }

    switch (context) {
        case MiniPreviewContext::Dropdown:
            return true;
        case MiniPreviewContext::MouseHover:
        case MiniPreviewContext::BoxSpaceFocus:
            return resortBoxHasPreviewContent(box_index);
    }

    return false;
}

void TransferSystemScreen::syncBoxSpaceMiniPreviewHoverFromPointer(int logical_x, int logical_y) {
    if (const std::optional<FocusNodeId> picked = focusNodeAtPointer(logical_x, logical_y)) {
        if (*picked >= 2000 && *picked <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
            const int cell = *picked - 2000;
            const int idx = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (idx >= 0 && idx < static_cast<int>(game_pc_boxes_.size()) &&
                gameBoxHasPreviewContent(idx)) {
                mouse_hover_mini_preview_box_index_ = idx;
                mini_preview_model_from_resort_ = false;
            }
        } else if (*picked >= 1000 && *picked <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
            const int cell = *picked - 1000;
            const int idx = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (idx >= 0 && idx < static_cast<int>(resort_pc_boxes_.size()) &&
                resortBoxHasPreviewContent(idx)) {
                mouse_hover_mini_preview_box_index_ = idx;
                mini_preview_model_from_resort_ = true;
            }
        }
    }
}

void TransferSystemScreen::syncBoxSpaceMiniPreviewFromFocusedBoxSpaceCell() {
    const FocusNodeId cur = focus_.current();
    if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode() && game_save_box_viewport_) {
        const int cell = cur - 2000;
        const int idx = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
        if (idx >= 0 && idx < static_cast<int>(game_pc_boxes_.size()) && gameBoxHasPreviewContent(idx)) {
            mouse_hover_mini_preview_box_index_ = idx;
            mini_preview_model_from_resort_ = false;
        }
        return;
    }
    if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode() && resort_box_viewport_) {
        const int cell = cur - 1000;
        const int idx = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
        if (idx >= 0 && idx < static_cast<int>(resort_pc_boxes_.size()) && resortBoxHasPreviewContent(idx)) {
            mouse_hover_mini_preview_box_index_ = idx;
            mini_preview_model_from_resort_ = true;
        }
    }
}

void TransferSystemScreen::updateMiniPreview(double dt) {
    const bool have_game_preview = game_save_box_viewport_ && !game_pc_boxes_.empty();
    const bool have_resort_preview = resort_box_viewport_ && !resort_pc_boxes_.empty();
    if (!mini_preview_style_.enabled || (!have_game_preview && !have_resort_preview)) {
        mini_preview_target_ = 0.0;
        approachExponential(mini_preview_t_, mini_preview_target_, dt, std::max(1.0, mini_preview_style_.enter_smoothing));
        // Keep the last rendered model while we animate out; clear once fully hidden.
        if (mini_preview_t_ <= 1e-3) {
            mini_preview_box_index_ = -1;
            mini_preview_model_from_resort_ = false;
        }
        return;
    }

    if ((pokemon_move_.active() || multi_pokemon_move_.active()) &&
        (game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode())) {
        const FocusNodeId cur = focus_.current();
        if ((cur >= 2000 && cur <= 2029) || (cur >= 1000 && cur <= 1029)) {
            syncBoxSpaceMiniPreviewFromFocusedBoxSpaceCell();
        } else {
            syncBoxSpaceMiniPreviewHoverFromPointer(last_pointer_position_.x, last_pointer_position_.y);
        }
    }

    int wanted = -1;
    bool wanted_from_resort = false;
    const bool game_dropdown_visible =
        box_name_dropdown_style_.enabled && game_pc_boxes_.size() >= 2 && game_box_browser_.dropdownExpandT() > 0.08;
    const bool resort_dropdown_visible =
        box_name_dropdown_style_.enabled && resort_pc_boxes_.size() >= 2 && resort_box_browser_.dropdownExpandT() > 0.08;
    if (game_dropdown_visible) {
        const int hi = game_box_browser_.dropdownHighlightIndex();
        if (hi > 0) {
            const int candidate = hi - 1;
            if (candidate >= 0 && candidate < static_cast<int>(game_pc_boxes_.size()) &&
                shouldShowMiniPreviewForBox(candidate, MiniPreviewContext::Dropdown)) {
                wanted = candidate;
                wanted_from_resort = false;
            }
        }
    } else if (resort_dropdown_visible) {
        const int hi = resort_box_browser_.dropdownHighlightIndex();
        if (hi > 0) {
            const int candidate = hi - 1;
            if (candidate >= 0 && candidate < static_cast<int>(resort_pc_boxes_.size()) &&
                shouldShowMiniPreviewForResortBox(candidate, MiniPreviewContext::Dropdown)) {
                wanted = candidate;
                wanted_from_resort = true;
            }
        }
    } else if (selection_cursor_hidden_after_mouse_) {
        if (mini_preview_model_from_resort_) {
            if (shouldShowMiniPreviewForResortBox(mouse_hover_mini_preview_box_index_, MiniPreviewContext::MouseHover)) {
                wanted = mouse_hover_mini_preview_box_index_;
                wanted_from_resort = true;
            }
        } else if (shouldShowMiniPreviewForBox(mouse_hover_mini_preview_box_index_, MiniPreviewContext::MouseHover)) {
            wanted = mouse_hover_mini_preview_box_index_;
            wanted_from_resort = false;
        }
    } else if (game_box_browser_.gameBoxSpaceMode() || resort_box_browser_.gameBoxSpaceMode()) {
        const FocusNodeId cur = focus_.current();
        if (cur >= 2000 && cur <= 2029 && game_box_browser_.gameBoxSpaceMode()) {
            const int cell = cur - 2000;
            const int candidate = game_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (shouldShowMiniPreviewForBox(candidate, MiniPreviewContext::BoxSpaceFocus)) {
                wanted = candidate;
                wanted_from_resort = false;
            }
        } else if (cur >= 1000 && cur <= 1029 && resort_box_browser_.gameBoxSpaceMode()) {
            const int cell = cur - 1000;
            const int candidate = resort_box_browser_.gameBoxSpaceRowOffset() * 6 + cell;
            if (shouldShowMiniPreviewForResortBox(candidate, MiniPreviewContext::BoxSpaceFocus)) {
                wanted = candidate;
                wanted_from_resort = true;
            }
        }
    }

    mini_preview_target_ = (wanted >= 0) ? 1.0 : 0.0;
    approachExponential(mini_preview_t_, mini_preview_target_, dt, std::max(1.0, mini_preview_style_.enter_smoothing));

    if (wanted >= 0 &&
        (wanted != mini_preview_box_index_ || wanted_from_resort != mini_preview_model_from_resort_)) {
        mini_preview_box_index_ = wanted;
        mini_preview_model_from_resort_ = wanted_from_resort;
        mini_preview_model_ =
            wanted_from_resort ? resortBoxViewportModelAt(wanted) : gameBoxViewportModelAt(wanted);
    }
    // When no target, animate out smoothly using the last cached model; clear once hidden.
    if (wanted < 0 && mini_preview_t_ <= 1e-3) {
        mini_preview_box_index_ = -1;
        mini_preview_model_from_resort_ = false;
    }
}

namespace {
std::optional<SDL_Rect> computeCenteredScaledRectClampedToBounds(
    const TextureHandle& tex,
    int cx,
    int cy,
    const SDL_Rect& clamp_bounds,
    double desired_scale) {
    if (!tex.texture || clamp_bounds.w <= 0 || clamp_bounds.h <= 0) {
        return std::nullopt;
    }
    desired_scale = std::max(0.01, desired_scale);
    const double dw0 = static_cast<double>(tex.width) * desired_scale;
    const double dh0 = static_cast<double>(tex.height) * desired_scale;
    const double sx = static_cast<double>(clamp_bounds.w) / std::max(1.0, dw0);
    const double sy = static_cast<double>(clamp_bounds.h) / std::max(1.0, dh0);
    const double clamp_scale = std::min(1.0, std::min(sx, sy));
    const int dw = std::max(1, static_cast<int>(std::round(dw0 * clamp_scale)));
    const int dh = std::max(1, static_cast<int>(std::round(dh0 * clamp_scale)));
    SDL_Rect dst{cx - dw / 2, cy - dh / 2, dw, dh};
    dst.x = std::clamp(dst.x, clamp_bounds.x, clamp_bounds.x + clamp_bounds.w - dst.w);
    dst.y = std::clamp(dst.y, clamp_bounds.y, clamp_bounds.y + clamp_bounds.h - dst.h);
    return dst;
}

void drawTextureRect(SDL_Renderer* renderer, const TextureHandle& tex, const SDL_Rect& dst) {
    if (!tex.texture || dst.w <= 0 || dst.h <= 0) {
        return;
    }
    SDL_SetTextureBlendMode(tex.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex.texture.get(), 255);
    SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
}
} // namespace

void TransferSystemScreen::drawMiniPreview(SDL_Renderer* renderer) const {
    if (!mini_preview_style_.enabled || mini_preview_t_ <= 1e-3 || mini_preview_box_index_ < 0) {
        return;
    }
#ifdef PR_ENABLE_TEST_HOOKS
    debug_mini_preview_first_sprite_rect_.reset();
    debug_mini_preview_cell_size_ = SDL_Point{0, 0};
#endif

    const Color kBorder = box_viewport_style_.viewport_border_color;
    const Color kFill = box_viewport_style_.slot_background_color;

    const int w = std::max(1, mini_preview_style_.width);
    const int h = std::max(1, mini_preview_style_.height);
    const int pad = std::max(0, mini_preview_style_.edge_pad);
    const int y = std::max(0, mini_preview_style_.offset_y);

    // Game-side preview: slide in from the left. Resort preview: same vertical band, from the right edge.
    const int vw = window_config_.virtual_width;
    int hidden_x = -w - pad;
    int shown_x = pad;
    if (mini_preview_model_from_resort_) {
        hidden_x = vw + pad;
        shown_x = vw - w - pad;
    }
    const int x = static_cast<int>(std::lround(hidden_x + (shown_x - hidden_x) * mini_preview_t_));

    const int r = std::clamp(mini_preview_style_.corner_radius, 0, std::min(w, h) / 2);
    const int stroke = std::clamp(mini_preview_style_.border_thickness, 1, std::min(w, h) / 2);

    fillRoundedRectScanlines(renderer, x, y, w, h, r, kBorder);
    fillRoundedRectScanlines(
        renderer,
        x + stroke,
        y + stroke,
        w - 2 * stroke,
        h - 2 * stroke,
        std::max(0, r - stroke),
        kFill);

    const int inner_x = x + stroke + 10;
    const int inner_y = y + stroke + 10;
    const int inner_w = std::max(1, w - 2 * stroke - 20);
    const int inner_h = std::max(1, h - 2 * stroke - 20);

    const int visible_slots = std::clamp(mini_preview_model_.visible_slot_count, 0, 30);
    const int cols = std::clamp(mini_preview_model_.slot_columns, 1, 6);
    const int rows = std::max(1, (std::max(1, visible_slots) + cols - 1) / cols);
    const int gap = 3;
    const int cell_w = std::max(6, (inner_w - (cols - 1) * gap) / cols);
    const int cell_h = std::max(6, (inner_h - (rows - 1) * gap) / rows);
#ifdef PR_ENABLE_TEST_HOOKS
    debug_mini_preview_cell_size_ = SDL_Point{cell_w, cell_h};
#endif
    const int grid_w = cols * cell_w + (cols - 1) * gap;
    const int grid_h = rows * cell_h + (rows - 1) * gap;
    const int gx = inner_x + (inner_w - grid_w) / 2;
    const int gy = inner_y + (inner_h - grid_h) / 2;
    const SDL_Rect preview_clip{inner_x, inner_y, inner_w, inner_h};

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const int sx = gx + col * (cell_w + gap);
            const int sy = gy + row * (cell_h + gap);
            fillRoundedRectScanlines(renderer, sx, sy, cell_w, cell_h, 4, kFill);

            const std::size_t idx = static_cast<std::size_t>(row * cols + col);
            if (static_cast<int>(idx) >= visible_slots) {
                continue;
            }
            if (idx < mini_preview_model_.slot_sprites.size()) {
                const auto& slot = mini_preview_model_.slot_sprites[idx];
                if (slot.has_value() && slot->texture) {
                    const auto dst = computeCenteredScaledRectClampedToBounds(
                        *slot,
                        sx + cell_w / 2,
                        sy + cell_h / 2,
                        preview_clip,
                        mini_preview_style_.sprite_scale);
                    if (dst) {
#ifdef PR_ENABLE_TEST_HOOKS
                        if (idx == 0) {
                            debug_mini_preview_first_sprite_rect_ = *dst;
                        }
#endif
                        drawTextureRect(renderer, *slot, *dst);
                    }
                }
            }
        }
    }
}

} // namespace pr

