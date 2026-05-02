#include "ui/TransferSystemScreen.hpp"

#include "core/BridgeImportMerge.hpp"
#include "core/SaveBridgeClient.hpp"
#include "core/TransferBoxEditsStore.hpp"
#include "core/PokeSpriteAssets.hpp"
#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/ExportedPokemon.hpp"
#include "resort/domain/ResortTypes.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"
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
constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";

std::string encodeBase64(const std::vector<unsigned char>& bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = (i + 1) < bytes.size() ? bytes[i + 1] : 0;
        const unsigned int b2 = (i + 2) < bytes.size() ? bytes[i + 2] : 0;
        out.push_back(kBase64Alphabet[(b0 >> 2) & 0x3f]);
        out.push_back(kBase64Alphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0f)]);
        out.push_back((i + 1) < bytes.size() ? kBase64Alphabet[((b1 & 0x0f) << 2) | ((b2 >> 6) & 0x03)] : '=');
        out.push_back((i + 2) < bytes.size() ? kBase64Alphabet[b2 & 0x3f] : '=');
    }
    return out;
}

std::string firstNonEmptyGameSlotFormat(const std::vector<TransferSaveSelection::PcBox>& boxes) {
    for (const auto& box : boxes) {
        for (const auto& slot : box.slots) {
            if (slot.occupied() && !slot.format.empty()) {
                return slot.format;
            }
        }
    }
    return {};
}

void getPillTrackBounds(const GameTransferPillToggleStyle& st, int screen_w, int& tx, int& ty, int& tw, int& th) {
    const int right_col_x = screen_w - 40 - BoxViewport::kViewportWidth;
    tx = right_col_x + (BoxViewport::kViewportWidth - st.track_width) / 2;
    ty = kBoxViewportY - st.gap_above_boxes - st.track_height;
    tw = st.track_width;
    th = st.track_height;
}
}

void TransferSystemScreen::cachePillLabelTextures(SDL_Renderer* renderer) {
    const Color selected = pill_style_.label_selected_color;
    const Color unselected = pill_style_.label_unselected_color;
    // Reuse the existing texture slots: "black" = selected, "white" = unselected.
    pill_label_pokemon_black_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", selected);
    pill_label_items_black_ = renderTextTexture(renderer, pill_font_.get(), "Items", selected);
    pill_label_pokemon_white_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", unselected);
    pill_label_items_white_ = renderTextTexture(renderer, pill_font_.get(), "Items", unselected);
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
        box.name = header.second.empty() ? "RESORT " + std::to_string(box_id + 1) : header.second;
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

bool TransferSystemScreen::persistResortPokemonDropToStorage(
    const transfer_system::PokemonMoveController::SlotRef& target,
    const transfer_system::PokemonMoveController::SlotRef& return_slot,
    const bool target_was_occupied,
    const bool swap_into_hand,
    const std::string& held_pkrid,
    const std::string& target_pkrid_before) {
    using Move = transfer_system::PokemonMoveController;
    if (!resort_service_) {
        return true;
    }
    if (target.panel == Move::Panel::Resort && return_slot.panel == Move::Panel::Game) {
        std::cerr << kTempTransferLog
                  << " UI Game->Resort drop deferred until Save+Exit source_box=" << return_slot.box_index
                  << " source_slot=" << return_slot.slot_index
                  << " target_box=" << target.box_index
                  << " target_slot=" << target.slot_index
                  << " target_was_occupied=" << (target_was_occupied ? "true" : "false")
                  << " target_pkrid_before=" << target_pkrid_before << '\n';
        markResortBoxesDirty();
        std::cerr << kTempTransferLog << " UI storage untouched; Game->Resort commit pending Save+Exit\n";
        return true;
    }
    if (target.panel == Move::Panel::Game && return_slot.panel == Move::Panel::Resort && !held_pkrid.empty()) {
        std::cerr << kTempTransferLog
                  << " UI Resort->Game mirror send deferred until Save+Exit pkrid=" << held_pkrid
                  << " resort_box=" << return_slot.box_index
                  << " resort_slot=" << return_slot.slot_index
                  << " game_box=" << target.box_index
                  << " game_slot=" << target.slot_index
                  << " target_was_occupied=" << (target_was_occupied ? "true" : "false")
                  << " game_target_pkrid_before=" << target_pkrid_before << '\n';
        markGameBoxesDirty();
        markResortBoxesDirty();
        std::cerr << kTempTransferLog << " UI storage untouched; Resort->Game commit pending Save+Exit\n";
        return true;
    }
    if (target.panel == Move::Panel::Resort || return_slot.panel == Move::Panel::Resort) {
        markResortBoxesDirty();
        std::cerr << kTempTransferLog
                  << " UI Resort box mutation deferred until Save+Exit held_pkrid=" << held_pkrid
                  << " swap_into_hand=" << (swap_into_hand ? "true" : "false") << '\n';
    }
    return true;
}

void TransferSystemScreen::enter(const TransferSaveSelection& selection, SDL_Renderer* renderer, int initial_game_box_index) {
    closeBoxRenameModal(false);
    ui_state_.enter();
    transfer_selection_ = selection;
    bridge_import_source_game_.reset();
    initializeResortPcBoxesFromStorage(renderer);
    resort_box_browser_.enter(static_cast<int>(resort_pc_boxes_.size()), 0);
    pokemon_move_.clear();
    multi_pokemon_move_.clear();
    held_move_sprite_tex_ = {};
    pickup_sfx_requested_ = false;
    putdown_sfx_requested_ = false;
    successful_save_exit_requested_ = false;
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
        for (auto& b : game_pc_boxes_) {
            if (b.native_slot_count <= 0) {
                b.native_slot_count = static_cast<int>(std::min<std::size_t>(30, b.slots.size()));
            }
            if (b.slots.size() < 30) {
                b.slots.resize(30);
            } else if (b.slots.size() > 30) {
                b.slots.resize(30);
            }
        }
    } else if (!selection.box1_slots.empty()) {
        TransferSaveSelection::PcBox b;
        b.name = "BOX 1";
        b.slots = selection.box1_slots;
        b.native_slot_count = static_cast<int>(std::min<std::size_t>(30, b.slots.size()));
        if (b.slots.size() < 30) {
            b.slots.resize(30);
        } else if (b.slots.size() > 30) {
            b.slots.resize(30);
        }
        game_pc_boxes_.push_back(std::move(b));
    }

    // Apply persisted overlay edits (prototype external-save editing). Must run **before** the bridge import merge:
    // otherwise replacing `game_pc_boxes_` here would wipe import-grade payloads and stable identity fields
    // (`pid`, `encryption_constant`, etc.) that `mergeBridgeImportIntoGamePcBoxes` attaches for matching on return.
    game_boxes_dirty_ = false;
    {
        std::string overlay_error;
        if (const auto overlay = loadTransferBoxEditsOverlay(save_directory_, selection.source_path, selection.game_key, &overlay_error)) {
            if (!overlay->pc_boxes.empty()) {
                std::vector<int> native_slot_counts;
                native_slot_counts.reserve(game_pc_boxes_.size());
                for (const auto& box : game_pc_boxes_) {
                    native_slot_counts.push_back(box.native_slot_count);
                }
                game_pc_boxes_ = overlay->pc_boxes;
                for (std::size_t i = 0; i < game_pc_boxes_.size(); ++i) {
                    auto& box = game_pc_boxes_[i];
                    if (box.native_slot_count <= 0 && i < native_slot_counts.size()) {
                        box.native_slot_count = native_slot_counts[i];
                    }
                    if (box.native_slot_count <= 0) {
                        box.native_slot_count = static_cast<int>(std::min<std::size_t>(30, box.slots.size()));
                    }
                    if (box.slots.size() < 30) {
                        box.slots.resize(30);
                    } else if (box.slots.size() > 30) {
                        box.slots.resize(30);
                    }
                }
            }
        } else if (!overlay_error.empty()) {
            std::cerr << "Warning: could not load transfer box edits overlay: " << overlay_error << '\n';
        }
    }

    // Import-grade encrypted PKM payloads (per PC slot) + hot identity for Resort matching. Merge onto the current
    // in-memory box layout (probe + optional overlay) so slot indices line up with what the player edits.
    {
        std::string import_merge_error;
        const SaveBridgeProbeResult import_result =
            importSaveWithBridge(project_root_, bridge_argv0_, transfer_selection_.source_path);
        if (!import_result.launched || !import_result.success) {
            std::cerr << "Warning: PKHeX import bridge did not return usable data; writing Pokémon moves to the real save "
                         "will be disabled until import succeeds. exit_code="
                      << import_result.exit_code << '\n';
        } else if (!mergeBridgeImportIntoGamePcBoxes(import_result.stdout_text, game_pc_boxes_, &import_merge_error)) {
            std::cerr << "Warning: could not merge import payloads into PC slots: " << import_merge_error << '\n';
        } else {
            std::uint16_t sg = 0;
            std::string sg_err;
            if (parseBridgeImportFirstPokemonSourceGame(import_result.stdout_text, &sg, &sg_err)) {
                bridge_import_source_game_ = sg;
            } else {
                std::cerr << "Warning: could not read source_game from bridge import: " << sg_err << '\n';
            }
        }
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
            const int accessible_slots = gameSaveSlotsPerBox();
            m.visible_slot_count = accessible_slots;
            m.slot_columns = accessible_slots <= 20 ? 5 : 6;
            for (std::size_t i = 0; i < m.slot_sprites.size() && i < b.slots.size(); ++i) {
                if (static_cast<int>(i) >= accessible_slots) {
                    m.disabled_slots[i] = true;
                    continue;
                }
                const auto& slot = b.slots[i];
                m.slot_sprites[i] = sprite_for(slot);
                if (slot.occupied() && slot.held_item_id > 0 && sprite_assets_ && renderer_) {
                    TextureHandle item = sprite_assets_->loadItemTexture(renderer_, slot.held_item_id);
                    m.held_item_sprites[i] =
                        item.texture ? std::optional<TextureHandle>(std::move(item)) : std::nullopt;
                }
            }
            for (std::size_t i = static_cast<std::size_t>(std::max(0, accessible_slots));
                 i < m.disabled_slots.size();
                 ++i) {
                m.disabled_slots[i] = true;
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

