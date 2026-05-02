#include "ui/TransferSystemScreen.hpp"
#include "ui/transfer_system/detail/SdlScanlineDraw.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

using transfer_system::detail::setDrawColor;

void TransferSystemScreen::drawHeldPokemon(SDL_Renderer* renderer) {
    const auto* held = pokemon_move_.held();
    if (!held || !sprite_assets_) {
        return;
    }

    int cx = held->pointer.x;
    int cy = held->pointer.y;
    if (held->input_mode == transfer_system::PokemonMoveController::InputMode::Keyboard) {
        if (const auto bounds = focus_.currentBounds()) {
            cx = bounds->x + bounds->w / 2;
            cy = bounds->y + bounds->h / 2 - 12;
        }
    }
    cx = std::clamp(cx, 28, std::max(28, window_config_.virtual_width - 28));
    cy = std::clamp(cy, 28, std::max(28, window_config_.virtual_height - 28));
    cy += box_viewport_style_.sprite_offset_y;

    cx += held_sprite_shake_offset_px_;

    TextureHandle tex = held_move_sprite_tex_;
    if (!tex.texture || tex.width <= 0 || tex.height <= 0) {
        tex = sprite_assets_->loadPokemonTexture(renderer, held->pokemon);
    }
    if (!tex.texture || tex.width <= 0 || tex.height <= 0) {
        return;
    }
    const double scale = std::clamp(
        box_viewport_style_.sprite_scale * pokemon_action_menu_style_.held_sprite_scale_multiplier,
        0.01,
        32.0);
    const int w = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.width) * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.height) * scale)));
    const int dx0 = cx - w / 2;
    const int dy0 = cy - h / 2;

    if (pokemon_action_menu_style_.held_sprite_shadow_enabled) {
        const Color& sc = pokemon_action_menu_style_.held_sprite_shadow_color;
        SDL_SetTextureColorMod(tex.texture.get(), sc.r, sc.g, sc.b);
        SDL_SetTextureAlphaMod(tex.texture.get(), sc.a);
        const int shy = pokemon_action_menu_style_.held_sprite_shadow_offset_y;
        SDL_Rect shadow_dst{dx0, dy0 + shy, w, h};
        SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &shadow_dst);
    }

    SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(tex.texture.get(), 255);
    SDL_Rect dst{dx0, dy0, w, h};
    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
}

void TransferSystemScreen::drawHeldMultiPokemon(SDL_Renderer* renderer) {
    if (!multi_pokemon_move_.active() || !sprite_assets_) {
        return;
    }

    const auto anchor = heldMultiPokemonAnchorSlot();
    const auto target_slots = anchor ? multi_pokemon_move_.targetSlotsFor(*anchor) : std::nullopt;

    constexpr int kFallbackSlotW = 76;
    constexpr int kFallbackSlotH = 76;
    constexpr int kFallbackGapX = 10;
    constexpr int kFallbackGapY = 10;

    const double scale = std::clamp(
        box_viewport_style_.sprite_scale * pokemon_action_menu_style_.held_sprite_scale_multiplier,
        0.01,
        32.0);

    for (std::size_t i = 0; i < multi_pokemon_move_.entries().size(); ++i) {
        const auto& entry = multi_pokemon_move_.entries()[i];
        int cx = multi_pokemon_move_.pointer().x + entry.col_offset * (kFallbackSlotW + kFallbackGapX);
        int cy = multi_pokemon_move_.pointer().y + entry.row_offset * (kFallbackSlotH + kFallbackGapY);

        if (target_slots && i < target_slots->size()) {
            SDL_Rect bounds{};
            const auto& ref = (*target_slots)[i];
            const bool has_bounds = ref.panel == transfer_system::PokemonMoveController::Panel::Game
                ? (game_save_box_viewport_ && game_save_box_viewport_->getSlotBounds(ref.slot_index, bounds))
                : (resort_box_viewport_ && resort_box_viewport_->getSlotBounds(ref.slot_index, bounds));
            if (has_bounds) {
                cx = bounds.x + bounds.w / 2;
                cy = bounds.y + bounds.h / 2 + box_viewport_style_.sprite_offset_y;
            }
        } else if (multi_pokemon_move_.inputMode() == transfer_system::MultiPokemonMoveController::InputMode::Keyboard) {
            if (const auto bounds = focus_.currentBounds()) {
                cx = bounds->x + bounds->w / 2 + entry.col_offset * (kFallbackSlotW + kFallbackGapX);
                cy = bounds->y + bounds->h / 2 + entry.row_offset * (kFallbackSlotH + kFallbackGapY) + box_viewport_style_.sprite_offset_y;
            }
        } else {
            cy += box_viewport_style_.sprite_offset_y;
        }

        const float collapse = box_space_multi_collapse_t_;
        if (collapse > 0.002f && ui_state_.selectedToolIndex() == 0 && !target_slots.has_value()) {
            int ax = multi_pokemon_move_.pointer().x;
            int ay = multi_pokemon_move_.pointer().y;
            if (multi_pokemon_move_.inputMode() == transfer_system::MultiPokemonMoveController::InputMode::Keyboard) {
                if (const auto bounds = focus_.currentBounds()) {
                    ax = bounds->x + bounds->w / 2;
                    ay = bounds->y + bounds->h / 2 + box_viewport_style_.sprite_offset_y;
                }
            }
            cx = static_cast<int>(
                std::lround(static_cast<double>(cx) * static_cast<double>(1.f - collapse) +
                            static_cast<double>(ax) * static_cast<double>(collapse)));
            cy = static_cast<int>(
                std::lround(static_cast<double>(cy) * static_cast<double>(1.f - collapse) +
                            static_cast<double>(ay) * static_cast<double>(collapse)));
        }

        if (held_sprite_shake_timer_ > 0.0) {
            cx += static_cast<int>(
                std::lround(std::sin(held_sprite_shake_phase_ + static_cast<double>(i) * 1.85) * 7.0));
        }

        TextureHandle tex = sprite_assets_->loadPokemonTexture(renderer, entry.pokemon);
        if (!tex.texture || tex.width <= 0 || tex.height <= 0) {
            continue;
        }

        const int w = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.width) * scale)));
        const int h = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.height) * scale)));
        const int dx0 = cx - w / 2;
        const int dy0 = cy - h / 2;

        if (pokemon_action_menu_style_.held_sprite_shadow_enabled) {
            const Color& sc = pokemon_action_menu_style_.held_sprite_shadow_color;
            SDL_SetTextureColorMod(tex.texture.get(), sc.r, sc.g, sc.b);
            SDL_SetTextureAlphaMod(tex.texture.get(), sc.a);
            const int shy = pokemon_action_menu_style_.held_sprite_shadow_offset_y;
            SDL_Rect shadow_dst{dx0, dy0 + shy, w, h};
            SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &shadow_dst);
        }

        SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
        SDL_SetTextureAlphaMod(tex.texture.get(), 255);
        SDL_Rect dst{dx0, dy0, w, h};
        SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
    }
}

void TransferSystemScreen::drawHeldItem(SDL_Renderer* renderer) {
    const auto* held = held_move_.heldItem();
    if (!held || !sprite_assets_) {
        return;
    }

    int cx = held->pointer.x;
    int cy = held->pointer.y;
    if (held->input_mode == transfer_system::move::HeldMoveController::InputMode::Keyboard) {
        if (const auto bounds = focus_.currentBounds()) {
            cx = bounds->x + bounds->w / 2;
            cy = bounds->y + bounds->h / 2 - 12;
        }
    }
    cx = std::clamp(cx, 28, std::max(28, window_config_.virtual_width - 28));
    cy = std::clamp(cy, 28, std::max(28, window_config_.virtual_height - 28));
    cy += box_viewport_style_.sprite_offset_y;

    cx += held_sprite_shake_offset_px_;

    TextureHandle tex = sprite_assets_->loadItemTexture(renderer, held->item_id, ItemIconUsage::Held);
    if (!tex.texture || tex.width <= 0 || tex.height <= 0) {
        return;
    }

    const int target_size = std::max(8, box_viewport_style_.item_tool_item_size);
    const double scale = std::clamp(static_cast<double>(target_size) / std::max(1, tex.width), 0.05, 16.0);
    const int w = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.width) * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.height) * scale)));
    const SDL_Rect dst{cx - w / 2, cy - h / 2, w, h};

    // Silhouette shadow (tinted sprite copy), similar to held Pokemon.
    const int shadow_y = dst.y + std::max(6, h / 2);
    const SDL_Rect shadow_dst{dst.x + 2, shadow_y, dst.w, dst.h};
    SDL_SetTextureColorMod(tex.texture.get(), 0, 0, 0);
    SDL_SetTextureAlphaMod(tex.texture.get(), 110);
    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &shadow_dst);
    SDL_SetTextureAlphaMod(tex.texture.get(), 255);
    SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);

    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
}

void TransferSystemScreen::drawMultiSelectionDrag(SDL_Renderer* renderer) const {
    if (!multi_select_drag_active_ || multi_select_drag_rect_.w <= 0 || multi_select_drag_rect_.h <= 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    Color fill = carousel_style_.frame_multiple;
    fill.a = 54;
    Color border = carousel_style_.frame_multiple;
    border.a = 220;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &multi_select_drag_rect_);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    for (int i = 0; i < 3; ++i) {
        SDL_Rect r{
            multi_select_drag_rect_.x + i,
            multi_select_drag_rect_.y + i,
            std::max(0, multi_select_drag_rect_.w - i * 2),
            std::max(0, multi_select_drag_rect_.h - i * 2)};
        if (r.w > 0 && r.h > 0) {
            SDL_RenderDrawRect(renderer, &r);
        }
    }
}

void TransferSystemScreen::drawKeyboardMultiMarquee(SDL_Renderer* renderer) const {
    if (!keyboard_multi_marquee_active_) {
        return;
    }
    const SDL_Rect box = keyboardMultiMarqueeScreenRect();
    if (box.w <= 0 || box.h <= 0) {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    Color fill = carousel_style_.frame_multiple;
    fill.a = 54;
    Color border = carousel_style_.frame_multiple;
    border.a = 220;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &box);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    for (int i = 0; i < 3; ++i) {
        SDL_Rect r{box.x + i, box.y + i, std::max(0, box.w - i * 2), std::max(0, box.h - i * 2)};
        if (r.w > 0 && r.h > 0) {
            SDL_RenderDrawRect(renderer, &r);
        }
    }
}

void TransferSystemScreen::drawHeldBoxSpaceBox(SDL_Renderer* renderer) {
    const auto* m = held_move_.heldBox();
    if (!m || pokemon_move_.active()) {
        return;
    }
    const bool from_game = m->source_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game;
    if (from_game) {
        if (!game_save_box_viewport_ || !game_box_browser_.gameBoxSpaceMode()) {
            return;
        }
    } else {
        if (!resort_box_viewport_ || !resort_box_browser_.gameBoxSpaceMode()) {
            return;
        }
    }
    const int src = m->source_box_index;
    if (src < 0 ||
        (from_game ? src >= static_cast<int>(game_pc_boxes_.size())
                   : src >= static_cast<int>(resort_pc_boxes_.size()))) {
        return;
    }

    int cx = m->pointer.x;
    int cy = m->pointer.y;
    if (m->input_mode == transfer_system::move::HeldMoveController::InputMode::Keyboard) {
        if (const auto bounds = focus_.currentBounds()) {
            cx = bounds->x + bounds->w / 2;
            cy = bounds->y + bounds->h / 2;
        }
    }
    cx = std::clamp(cx, 28, std::max(28, window_config_.virtual_width - 28));
    cy = std::clamp(cy, 28, std::max(28, window_config_.virtual_height - 28));

    const auto& box = from_game ? game_pc_boxes_[static_cast<std::size_t>(src)]
                                : resort_pc_boxes_[static_cast<std::size_t>(src)];
    int occupied = 0;
    const int total = from_game ? gameSaveSlotsPerBox() : static_cast<int>(box.slots.size());
    for (int i = 0; i < total && i < static_cast<int>(box.slots.size()); ++i) {
        if (box.slots[static_cast<std::size_t>(i)].occupied()) ++occupied;
    }
    const TextureHandle* tex = nullptr;
    if (total <= 0) {
        return;
    }
    if (occupied == 0) {
        tex = &box_space_empty_tex_;
    } else if (occupied >= total) {
        tex = &box_space_full_tex_;
    } else {
        tex = &box_space_noempty_tex_;
    }
    if (!tex || !tex->texture || tex->width <= 0 || tex->height <= 0) {
        return;
    }

    const double scale = std::clamp(box_viewport_style_.box_space_sprite_scale, 0.01, 32.0);
    const int w = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex->width) * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex->height) * scale)));
    const int dx0 = cx - w / 2;
    const int dy0 = cy - h / 2;

    if (pokemon_action_menu_style_.held_sprite_shadow_enabled) {
        const Color& sc = pokemon_action_menu_style_.held_sprite_shadow_color;
        SDL_SetTextureColorMod(tex->texture.get(), sc.r, sc.g, sc.b);
        SDL_SetTextureAlphaMod(tex->texture.get(), sc.a);
        const int shy = pokemon_action_menu_style_.held_sprite_shadow_offset_y;
        SDL_Rect shadow_dst{dx0, dy0 + shy, w, h};
        SDL_RenderCopy(renderer, tex->texture.get(), nullptr, &shadow_dst);
        SDL_SetTextureColorMod(tex->texture.get(), 255, 255, 255);
        SDL_SetTextureAlphaMod(tex->texture.get(), 255);
    }

    SDL_Rect dst{dx0, dy0, w, h};
    SDL_RenderCopy(renderer, tex->texture.get(), nullptr, &dst);
}

} // namespace pr

