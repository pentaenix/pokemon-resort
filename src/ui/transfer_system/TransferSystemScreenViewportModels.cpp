#include "ui/TransferSystemScreen.hpp"

#include <algorithm>

namespace pr {

BoxViewportModel TransferSystemScreen::gameBoxViewportModelAt(int box_index) const {
    BoxViewportModel incoming;
    if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
        return incoming;
    }
    incoming.box_name = game_pc_boxes_[static_cast<std::size_t>(box_index)].name;
    const auto& slots = game_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    const int accessible_slots = gameSaveSlotsPerBox();
    incoming.visible_slot_count = accessible_slots;
    incoming.slot_columns = accessible_slots <= 20 ? 5 : 6;
    for (std::size_t i = 0; i < incoming.slot_sprites.size() && i < slots.size(); ++i) {
        if (static_cast<int>(i) >= accessible_slots) {
            incoming.disabled_slots[i] = true;
            continue;
        }
        const auto& pc = slots[i];
        if (!pc.occupied() || !sprite_assets_ || !renderer_) {
            incoming.slot_sprites[i] = std::nullopt;
            continue;
        }
        TextureHandle texture = sprite_assets_->loadPokemonTexture(renderer_, pc);
        incoming.slot_sprites[i] = texture.texture ? std::optional<TextureHandle>(std::move(texture)) : std::nullopt;
        if (pc.held_item_id > 0) {
            TextureHandle item = sprite_assets_->loadItemTexture(renderer_, pc.held_item_id);
            incoming.held_item_sprites[i] =
                item.texture ? std::optional<TextureHandle>(std::move(item)) : std::nullopt;
        }
    }
    for (std::size_t i = static_cast<std::size_t>(std::max(0, accessible_slots));
         i < incoming.disabled_slots.size();
         ++i) {
        incoming.disabled_slots[i] = true;
    }
    return incoming;
}

BoxViewportModel TransferSystemScreen::gameBoxSpaceViewportModelAt(int row_offset) const {
    BoxViewportModel m;
    m.box_name = "BOX SPACE";

    const int base_box_index = std::max(0, row_offset) * 6;
    const int hide_box_index =
        (held_move_.heldBox() && !pokemon_move_.active() &&
         held_move_.heldBox()->source_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Game)
        ? held_move_.heldBox()->source_box_index
        : -1;
    for (int i = 0; i < 30; ++i) {
        const int box_index = base_box_index + i;
        if (box_index < 0 || box_index >= static_cast<int>(game_pc_boxes_.size())) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }
        if (hide_box_index >= 0 && box_index == hide_box_index) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        const auto& box = game_pc_boxes_[static_cast<std::size_t>(box_index)];
        int occupied = 0;
        const int total = gameSaveSlotsPerBox();
        for (int slot_index = 0; slot_index < total && slot_index < static_cast<int>(box.slots.size()); ++slot_index) {
            if (box.slots[static_cast<std::size_t>(slot_index)].occupied()) {
                ++occupied;
            }
        }

        if (total <= 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        if (occupied == 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_empty_tex_.texture ? std::optional<TextureHandle>(box_space_empty_tex_) : std::nullopt;
        } else if (occupied >= total) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_full_tex_.texture ? std::optional<TextureHandle>(box_space_full_tex_) : std::nullopt;
        } else {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_noempty_tex_.texture ? std::optional<TextureHandle>(box_space_noempty_tex_) : std::nullopt;
        }
    }
    for (int i = 0; i < 30; ++i) {
        const bool use_wiggle = box_space_interaction_panel_ == BoxSpaceInteractionPanel::Game;
        m.slot_wiggle_dx[static_cast<std::size_t>(i)] =
            use_wiggle ? box_space_slot_wiggle_dx_[static_cast<std::size_t>(i)] : 0;
    }
    return m;
}

BoxViewportModel TransferSystemScreen::resortBoxViewportModelAt(int box_index) const {
    BoxViewportModel incoming;
    if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
        return incoming;
    }
    incoming.box_name = resort_pc_boxes_[static_cast<std::size_t>(box_index)].name;
    const auto& slots = resort_pc_boxes_[static_cast<std::size_t>(box_index)].slots;
    for (std::size_t i = 0; i < incoming.slot_sprites.size() && i < slots.size(); ++i) {
        const auto& pc = slots[i];
        if (!pc.occupied() || !sprite_assets_ || !renderer_) {
            incoming.slot_sprites[i] = std::nullopt;
            continue;
        }
        TextureHandle texture = sprite_assets_->loadPokemonTexture(renderer_, pc);
        incoming.slot_sprites[i] = texture.texture ? std::optional<TextureHandle>(std::move(texture)) : std::nullopt;
        if (pc.held_item_id > 0) {
            TextureHandle item = sprite_assets_->loadItemTexture(renderer_, pc.held_item_id);
            incoming.held_item_sprites[i] =
                item.texture ? std::optional<TextureHandle>(std::move(item)) : std::nullopt;
        }
    }
    return incoming;
}

BoxViewportModel TransferSystemScreen::resortBoxSpaceViewportModelAt(int row_offset) const {
    BoxViewportModel m;
    m.box_name = "BOX SPACE";

    const int base_box_index = std::max(0, row_offset) * 6;
    const int hide_box_index =
        (held_move_.heldBox() && !pokemon_move_.active() &&
         held_move_.heldBox()->source_panel == transfer_system::move::HeldMoveController::PokemonSlotRef::Panel::Resort)
        ? held_move_.heldBox()->source_box_index
        : -1;
    for (int i = 0; i < 30; ++i) {
        const int box_index = base_box_index + i;
        if (box_index < 0 || box_index >= static_cast<int>(resort_pc_boxes_.size())) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }
        if (hide_box_index >= 0 && box_index == hide_box_index) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        const auto& box = resort_pc_boxes_[static_cast<std::size_t>(box_index)];
        int occupied = 0;
        int total = 0;
        for (const auto& slot : box.slots) {
            ++total;
            if (slot.occupied()) {
                ++occupied;
            }
        }

        if (total <= 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] = std::nullopt;
            continue;
        }

        if (occupied == 0) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_empty_tex_.texture ? std::optional<TextureHandle>(box_space_empty_tex_) : std::nullopt;
        } else if (occupied >= total) {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_full_tex_.texture ? std::optional<TextureHandle>(box_space_full_tex_) : std::nullopt;
        } else {
            m.slot_sprites[static_cast<std::size_t>(i)] =
                box_space_noempty_tex_.texture ? std::optional<TextureHandle>(box_space_noempty_tex_) : std::nullopt;
        }
    }
    for (int i = 0; i < 30; ++i) {
        const bool use_wiggle = box_space_interaction_panel_ == BoxSpaceInteractionPanel::Resort;
        m.slot_wiggle_dx[static_cast<std::size_t>(i)] =
            use_wiggle ? box_space_slot_wiggle_dx_[static_cast<std::size_t>(i)] : 0;
    }
    return m;
}

} // namespace pr

