#include "ui/TransferSystemScreen.hpp"

#include "core/PokeSpriteAssets.hpp"
#include "resort/domain/ResortTypes.hpp"
#include "resort/services/PokemonResortService.hpp"
#include "ui/transfer_system/TransferSystemFocusGraph.hpp"

#include <SDL.h>

#include <array>
#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace pr {

namespace {
constexpr const char* kDefaultResortProfileId = "default";
constexpr int kBoxViewportY = 100;

void getPillTrackBounds(const GameTransferPillToggleStyle& st, int screen_w, int& tx, int& ty, int& tw, int& th) {
    const int right_col_x = screen_w - 40 - BoxViewport::kViewportWidth;
    tx = right_col_x + (BoxViewport::kViewportWidth - st.track_width) / 2;
    ty = kBoxViewportY - st.gap_above_boxes - st.track_height;
    tw = st.track_width;
    th = st.track_height;
}
}

void TransferSystemScreen::cachePillLabelTextures(SDL_Renderer* renderer) {
    const Color black{0, 0, 0, 255};
    const Color white{255, 255, 255, 255};
    pill_label_pokemon_black_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", black);
    pill_label_items_black_ = renderTextTexture(renderer, pill_font_.get(), "Items", black);
    pill_label_pokemon_white_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", white);
    pill_label_items_white_ = renderTextTexture(renderer, pill_font_.get(), "Items", white);
}

void TransferSystemScreen::initializeResortPcBoxesFromStorage(SDL_Renderer* renderer) {
    resort_pc_boxes_.clear();
    if (!resort_service_) {
        resort_pc_boxes_.reserve(static_cast<std::size_t>(resort_pc_box_count_));
        for (int b = 0; b < resort_pc_box_count_; ++b) {
            TransferSaveSelection::PcBox box;
            box.name = "RESORT " + std::to_string(b + 1);
            box.slots.assign(30, PcSlotSpecies{});
            resort_pc_boxes_.push_back(std::move(box));
        }
        return;
    }

    resort_service_->ensureProfile(kDefaultResortProfileId);
    const auto headers = resort_service_->listProfileBoxes(kDefaultResortProfileId);
    if (headers.empty()) {
        resort_pc_boxes_.reserve(static_cast<std::size_t>(resort_pc_box_count_));
        for (int b = 0; b < resort_pc_box_count_; ++b) {
            TransferSaveSelection::PcBox box;
            box.name = "RESORT " + std::to_string(b + 1);
            box.slots.assign(30, PcSlotSpecies{});
            resort_pc_boxes_.push_back(std::move(box));
        }
        return;
    }

    resort_pc_boxes_.reserve(headers.size());
    for (const auto& header : headers) {
        const int box_id = header.first;
        TransferSaveSelection::PcBox box;
        box.name = "RESORT " + std::to_string(box_id + 1);
        box.slots.assign(30, PcSlotSpecies{});
        const std::vector<resort::PokemonSlotView> views =
            resort_service_->getBoxSlotViews(kDefaultResortProfileId, box_id);
        for (const auto& view : views) {
            if (view.slot_index >= 0 && view.slot_index < 30) {
                box.slots[static_cast<std::size_t>(view.slot_index)] =
                    pcSlotFromResortSlotView(view, box_id, view.slot_index);
            }
        }
        resort_pc_boxes_.push_back(std::move(box));
    }

    if (sprite_assets_ && renderer) {
        for (const auto& b : resort_pc_boxes_) {
            for (const auto& slot : b.slots) {
                if (slot.occupied()) {
                    (void)sprite_assets_->loadPokemonTexture(renderer, slot);
                }
            }
        }
    }
}

PcSlotSpecies TransferSystemScreen::pcSlotFromResortSlotView(
    const resort::PokemonSlotView& view,
    int box_id,
    int slot_index) const {
    PcSlotSpecies s{};
    s.present = true;
    s.resort_pkrid = view.pkrid;
    s.area = "resort";
    s.box_index = box_id;
    s.slot_index = slot_index;
    s.species_id = static_cast<int>(view.species_id);
    s.form = static_cast<int>(view.form_id);
    s.level = static_cast<int>(view.level);
    s.is_shiny = view.shiny;
    s.gender = static_cast<int>(view.gender);
    if (!view.display_name.empty() && view.display_name != view.pkrid) {
        s.nickname = view.display_name;
    }
    if (view.held_item_id.has_value()) {
        s.held_item_id = static_cast<int>(*view.held_item_id);
    }
    if (sprite_assets_) {
        PokemonSpriteRequest rq{};
        rq.species_id = s.species_id;
        rq.gender = s.gender;
        rq.is_shiny = s.is_shiny;
        const ResolvedPokemonSprite r = sprite_assets_->resolvePokemon(rq);
        s.slug = r.species_slug;
        s.form_key = r.resolved_form_key;
    }
    return s;
}

void TransferSystemScreen::persistResortPokemonDropToStorage(
    const transfer_system::PokemonMoveController::SlotRef& target,
    const transfer_system::PokemonMoveController::SlotRef& return_slot,
    const bool target_was_occupied,
    const bool swap_into_hand,
    const std::string& held_pkrid,
    const std::string& target_pkrid_before) {
    using Move = transfer_system::PokemonMoveController;
    if (!resort_service_ || swap_into_hand) {
        return;
    }
    if (target.panel != Move::Panel::Resort || return_slot.panel != Move::Panel::Resort) {
        return;
    }
    try {
        if (!target_was_occupied && !held_pkrid.empty()) {
            resort_service_->movePokemonToSlot(
                resort::BoxLocation{kDefaultResortProfileId, target.box_index, target.slot_index},
                held_pkrid,
                resort::BoxPlacementPolicy::RejectIfOccupied);
        } else if (target_was_occupied && !held_pkrid.empty() && !target_pkrid_before.empty()) {
            resort_service_->swapResortSlotContents(
                resort::BoxLocation{kDefaultResortProfileId, return_slot.box_index, return_slot.slot_index},
                resort::BoxLocation{kDefaultResortProfileId, target.box_index, target.slot_index});
        }
    } catch (const std::exception& ex) {
        std::cerr << "Warning: could not persist Resort box move to profile.resort.db: " << ex.what() << '\n';
    }
}

void TransferSystemScreen::enter(const TransferSaveSelection& selection, SDL_Renderer* renderer, int initial_game_box_index) {
    closeBoxRenameModal(false);
    ui_state_.enter();
    transfer_selection_ = selection;
    initializeResortPcBoxesFromStorage(renderer);
    resort_box_browser_.enter(static_cast<int>(resort_pc_boxes_.size()), 0);
    pokemon_move_.clear();
    multi_pokemon_move_.clear();
    held_move_sprite_tex_ = {};
    pickup_sfx_requested_ = false;
    putdown_sfx_requested_ = false;
    selection_cursor_hidden_after_mouse_ = false;
    speech_hover_active_ = false;
    dropdown_lmb_down_in_panel_ = false;
    dropdown_lmb_drag_accum_ = 0.0;
    dropdown_labels_dirty_ = false;
    dropdown_item_textures_.clear();
    current_game_key_ = selection.game_key;
    selection_game_title_ = selection.game_title;
    speech_bubble_label_cache_.clear();
    speech_bubble_label_tex_ = {};
    mouse_hover_mini_preview_box_index_ = -1;
    mouse_hover_focus_node_ = -1;
    last_pointer_position_ = SDL_Point{0, 0};
    box_space_drag_active_ = false;
    box_space_drag_last_y_ = 0;
    box_space_drag_accum_ = 0.0;
    box_space_pressed_cell_ = -1;
    multi_select_drag_active_ = false;
    multi_select_drag_rect_ = SDL_Rect{0, 0, 0, 0};
    // Focus graph is rebuilt at end of enter() once bounds are valid.

    if (resort_box_viewport_) {
        // Match game column: controller `resort_box_browser_.enter()` clears Box Space, but the viewport
        // keeps `box_space_active_` / header mode until we reset it (otherwise the footer stays green).
        resort_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        resort_box_viewport_->setBoxSpaceActive(false);
        resort_box_viewport_->setModel(resortBoxViewportModel());
        resort_box_viewport_->reloadResortIcon(renderer);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setModel(BoxViewportModel{});
        game_save_box_viewport_->reloadGameIcon(renderer, selection.game_key);
        game_save_box_viewport_->setHeaderMode(BoxViewport::HeaderMode::Normal, false);
        game_save_box_viewport_->setBoxSpaceActive(false);
    }

    // Capture the PC box list (right box only). Prefer full `pc_boxes`; fall back to legacy box1.
    game_pc_boxes_.clear();
    if (!selection.pc_boxes.empty()) {
        game_pc_boxes_ = selection.pc_boxes;
    } else if (!selection.box1_slots.empty()) {
        TransferSaveSelection::PcBox b;
        b.name = "BOX 1";
        b.slots = selection.box1_slots;
        if (b.slots.size() < 30) {
            b.slots.resize(30);
        } else if (b.slots.size() > 30) {
            b.slots.resize(30);
        }
        game_pc_boxes_.push_back(std::move(b));
    }

    game_box_browser_.enter(static_cast<int>(game_pc_boxes_.size()), initial_game_box_index);

    auto sprite_for = [&](const PcSlotSpecies& slot) -> std::optional<TextureHandle> {
        if (!slot.occupied() || !sprite_assets_) {
            return std::nullopt;
        }
        TextureHandle texture = sprite_assets_->loadPokemonTexture(renderer, slot);
        return texture.texture ? std::optional<TextureHandle>(std::move(texture)) : std::nullopt;
    };

    // Preload all sprites referenced by the save boxes so navigation is smooth (no IO in input handlers).
    {
        for (const auto& b : game_pc_boxes_) {
            for (const auto& slot : b.slots) {
                if (slot.occupied()) {
                    (void)sprite_for(slot);
                }
            }
        }
    }

    auto build_box_model = [&](int box_index) -> BoxViewportModel {
        BoxViewportModel m;
        if (box_index >= 0 && static_cast<std::size_t>(box_index) < game_pc_boxes_.size()) {
            const auto& b = game_pc_boxes_[static_cast<std::size_t>(box_index)];
            m.box_name = b.name;
            for (std::size_t i = 0; i < m.slot_sprites.size() && i < b.slots.size(); ++i) {
                const auto& slot = b.slots[i];
                m.slot_sprites[i] = sprite_for(slot);
                if (slot.occupied() && slot.held_item_id > 0 && sprite_assets_ && renderer_) {
                    TextureHandle item = sprite_assets_->loadItemTexture(renderer_, slot.held_item_id);
                    m.held_item_sprites[i] =
                        item.texture ? std::optional<TextureHandle>(std::move(item)) : std::nullopt;
                }
            }
        }
        return m;
    };

    if (game_save_box_viewport_ && !game_pc_boxes_.empty()) {
        game_save_box_viewport_->setModel(build_box_model(game_box_browser_.gameBoxIndex()));
    }
    // Build focus nodes (automatic spatial navigation + overrides).
    {
        std::vector<FocusNode> nodes;
        nodes.reserve(120);
        auto add = [&](FocusNode n) { nodes.push_back(std::move(n)); };

        // Resort grid slots.
        if (resort_box_viewport_) {
            for (int i = 0; i < 30; ++i) {
                add(FocusNode{
                    1000 + i,
                    [this, i]() -> std::optional<SDL_Rect> {
                        if (!resort_box_viewport_) return std::nullopt;
                        SDL_Rect r;
                        if (!resort_box_viewport_->getSlotBounds(i, r)) return std::nullopt;
                        return r;
                    },
                    [this]() { (void)activateFocusedResortSlot(); },
                    nullptr});
            }
            add(FocusNode{1101, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getPrevArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceResortBox(-1); },
                          nullptr});
            add(FocusNode{1102, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getNamePlateBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
            add(FocusNode{1103, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getNextArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceResortBox(1); },
                          nullptr});
            add(FocusNode{1110, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getFooterBoxSpaceBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() {
                              setResortBoxSpaceMode(!resort_box_browser_.gameBoxSpaceMode());
                              ui_state_.requestButtonSfx();
                              closeResortBoxDropdown();
                          },
                          nullptr});
            add(FocusNode{1111, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getFooterGameIconBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
            add(FocusNode{1112, [this]() -> std::optional<SDL_Rect> {
                              if (!resort_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return resort_box_viewport_->getResortScrollArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { stepResortBoxSpaceRowDown(); },
                          nullptr});
        }

        // Game grid slots.
        if (game_save_box_viewport_) {
            for (int i = 0; i < 30; ++i) {
                add(FocusNode{
                    2000 + i,
                    [this, i]() -> std::optional<SDL_Rect> {
                        if (!game_save_box_viewport_) return std::nullopt;
                        SDL_Rect r;
                        if (!game_save_box_viewport_->getSlotBounds(i, r)) return std::nullopt;
                        return r;
                    },
                    [this]() {
                        (void)activateFocusedGameSlot();
                    },
                    nullptr});
            }
            add(FocusNode{2101, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getPrevArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceGameBox(-1); },
                          nullptr});
            add(FocusNode{2102, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getNamePlateBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {},
                          nullptr});
            add(FocusNode{2103, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getNextArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { advanceGameBox(1); },
                          nullptr});
            add(FocusNode{2112, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getBoxSpaceScrollArrowBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() { stepGameBoxSpaceRowDown(); },
                          nullptr});
            add(FocusNode{2110, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getFooterBoxSpaceBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          [this]() {
                              setGameBoxSpaceMode(!game_box_browser_.gameBoxSpaceMode());
                              ui_state_.requestButtonSfx();
                              closeGameBoxDropdown();
                          },
                          nullptr});
            add(FocusNode{2111, [this]() -> std::optional<SDL_Rect> {
                              if (!game_save_box_viewport_) return std::nullopt;
                              SDL_Rect r;
                              return game_save_box_viewport_->getFooterGameIconBounds(r) ? std::optional<SDL_Rect>(r) : std::nullopt;
                          },
                          []() {}, nullptr});
        }

        // Tool carousel.
        if (exit_button_enabled_) {
            add(FocusNode{
                5000,
                [this]() -> std::optional<SDL_Rect> {
                    const int bs = carousel_style_.viewport_height;
                    const int bx = carousel_style_.offset_from_left_wall;
                    const int by = exitButtonScreenY();
                    if (bs <= 0) return std::nullopt;
                    return SDL_Rect{bx, by, bs, bs};
                },
                [this]() {
                    ui_state_.requestButtonSfx();
                    onBackPressed();
                },
                nullptr});
        }
        add(FocusNode{
            3000,
            [this]() -> std::optional<SDL_Rect> {
                const int vx = carousel_style_.offset_from_left_wall +
                    (exit_button_enabled_ ? (carousel_style_.viewport_height + exit_button_gap_pixels_) : 0);
                const int vy = carouselScreenY();
                return SDL_Rect{vx, vy, carousel_style_.viewport_width, carousel_style_.viewport_height};
            },
            []() {},
            [this](int dx, int dy) -> bool {
                if (dx != 0) {
                    cycleToolCarousel(dx);
                    return true;
                }
                (void)dy;
                return false;
            }});

        // Pill toggle.
        add(FocusNode{
            4000,
            [this]() -> std::optional<SDL_Rect> {
                int tx=0,ty=0,tw=0,th=0;
                getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
                const int enter_off =
                    static_cast<int>(std::lround((1.0 - ui_state_.uiEnter()) * static_cast<double>(-(th + 24))));
                ty += enter_off;
                return SDL_Rect{tx, ty, tw, th};
            },
            [this]() { togglePillTarget(); },
            [this](int dx, int dy) -> bool {
                if (dx != 0) {
                    togglePillTarget();
                    return true;
                }
                (void)dy;
                return false;
            }});

        transfer_system::applyTransferSystemFocusEdges(nodes);
        focus_.setExplicitNavigationOnly(true);
        focus_.setNodes(std::move(nodes));
        focus_.setCurrent(1000); // resort slot 1 (top-left)
    }
    syncBoxViewportPositions();
}

} // namespace pr

