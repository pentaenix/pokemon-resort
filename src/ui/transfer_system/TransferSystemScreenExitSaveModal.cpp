#include "ui/TransferSystemScreen.hpp"

#include "core/SaveBridgeClient.hpp"
#include "resort/domain/ExportedPokemon.hpp"
#include "resort/domain/ImportedPokemon.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"
#include "resort/services/PokemonResortService.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace pr {

namespace {
constexpr const char* kDefaultResortProfileId = "default";
constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";
constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

double smoothTowards(double current, double target, double smoothing, double dt) {
    // Standard critically damped-ish smoothing used elsewhere in the UI.
    const double k = std::max(1.0, smoothing);
    const double step = 1.0 - std::exp(-k * dt);
    return current + (target - current) * step;
}

std::string escapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char raw : s) {
        const char c = static_cast<char>(raw);
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out.push_back(' ');
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string quoted(const std::string& s) { return std::string{"\""} + escapeJsonString(s) + "\""; }

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

bool looksLikeSha256Hex(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (const char ch : value) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

bool transferSaveSupportsBoxNameProjection(const TransferSaveSelection& selection) {
    return selection.game_key != "pokemon_red" &&
           selection.game_key != "pokemon_blue" &&
           selection.game_key != "pokemon_yellow" &&
           selection.game_key != "pokemon_gold" &&
           selection.game_key != "pokemon_silver" &&
           selection.game_key != "pokemon_crystal";
}

bool writeTransferSaveProjectionV2(
    const std::string& projection_path,
    const std::vector<TransferSaveSelection::PcBox>& boxes,
    std::size_t slots_per_box,
    bool include_box_names,
    std::string& out_error) {
    out_error.clear();
    try {
        if (slots_per_box == 0) {
            for (const auto& box : boxes) {
                if (box.native_slot_count > 0) {
                    slots_per_box = static_cast<std::size_t>(std::min(30, box.native_slot_count));
                    break;
                }
                if (!box.slots.empty()) {
                    slots_per_box = box.slots.size();
                    break;
                }
            }
        }
        if (slots_per_box == 0) {
            out_error = "Could not determine save PC slot count";
            return false;
        }

        for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
            const auto& slots = boxes[bi].slots;
            for (std::size_t si = slots_per_box; si < slots.size(); ++si) {
                const PcSlotSpecies& sl = slots[si];
                if (sl.present && sl.species_id > 0) {
                    out_error = "PC box " + std::to_string(bi) +
                        " has Pokemon in UI-only padded slot " + std::to_string(si) +
                        "; this save supports only " + std::to_string(slots_per_box) +
                        " slots per box";
                    return false;
                }
            }
        }

        std::ostringstream json;
        json << "{\n"
             << "  \"projection_schema\": 2,\n";
        if (include_box_names) {
            json << "  \"box_names\": [";
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                if (i) {
                    json << ", ";
                }
                json << quoted(boxes[i].name);
            }
            json << "],\n";
        }
        json << "  \"pc_boxes\": [\n";
        for (std::size_t bi = 0; bi < boxes.size(); ++bi) {
            if (bi) {
                json << ",\n";
            }
            json << "    {\"slots\": [";
            const auto& slots = boxes[bi].slots;
            for (std::size_t si = 0; si < slots_per_box; ++si) {
                if (si) {
                    json << ",";
                }
                if (si >= slots.size()) {
                    json << "null";
                    continue;
                }
                const PcSlotSpecies& sl = slots[si];
                const bool empty_slot = !sl.present || sl.species_id <= 0;
                if (empty_slot) {
                    json << "null";
                } else {
                    json << '{'
                         << "\"raw_payload_base64\":" << quoted(sl.bridge_box_payload_base64) << ','
                         << "\"raw_hash_sha256\":" << quoted(sl.bridge_box_payload_hash_sha256) << '}';
                }
            }
            json << "]}";
        }
        json << "\n  ]\n}\n";

        std::ofstream out(projection_path, std::ios::trunc);
        if (!out) {
            out_error = "Could not open projection file for writing";
            return false;
        }
        out << json.str();
        out.flush();
        if (!out) {
            out_error = "Failed while writing projection file";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        out_error = ex.what();
        return false;
    }
}

std::size_t transferSaveSlotsPerGameBox(const TransferSaveSelection& selection) {
    if (!selection.pc_boxes.empty()) {
        for (const auto& box : selection.pc_boxes) {
            if (box.native_slot_count > 0) {
                return static_cast<std::size_t>(std::min(30, box.native_slot_count));
            }
            if (!box.slots.empty()) {
                return box.slots.size();
            }
        }
    }
    if (!selection.box1_slots.empty()) {
        return selection.box1_slots.size();
    }
    return 0;
}
} // namespace

void TransferSystemScreen::openExitSaveModal() {
    if (!exit_save_modal_style_.enabled) {
        return;
    }
    exit_save_modal_open_ = true;
    exit_save_modal_target_open_ = true;
    exit_save_modal_selected_row_ = 0;
    exit_save_modal_reveal_ = std::max(exit_save_modal_reveal_, 0.001);
    syncExitSaveModalLayout();
    ui_state_.requestButtonSfx();
}

void TransferSystemScreen::closeExitSaveModal() {
    if (!exit_save_modal_open_) {
        return;
    }
    exit_save_modal_target_open_ = false;
}

void TransferSystemScreen::updateExitSaveModal(double dt) {
    const double target = exit_save_modal_target_open_ ? 1.0 : 0.0;
    const double smoothing = target > exit_save_modal_reveal_ ? exit_save_modal_style_.enter_smoothing : exit_save_modal_style_.exit_smoothing;
    exit_save_modal_reveal_ = smoothTowards(exit_save_modal_reveal_, target, smoothing, dt);
    if (!exit_save_modal_target_open_ && exit_save_modal_reveal_ < 0.001) {
        exit_save_modal_open_ = false;
        exit_save_modal_target_open_ = false;
        exit_save_modal_reveal_ = 0.0;
    }
    if (exit_save_modal_reveal_ > 0.001) {
        syncExitSaveModalLayout();
    }
}

void TransferSystemScreen::syncExitSaveModalLayout() {
    const auto& s = exit_save_modal_style_;
    const int row_h = std::max(1, s.row_height);
    const int gap = std::max(0, s.row_gap);
    const int pad_x = std::max(0, s.padding_x);
    const int pad_top = std::max(0, s.padding_top);
    const int pad_bottom = std::max(0, s.padding_bottom);
    const int w = std::max(1, s.width);
    const int h = pad_top + (row_h * 3) + (gap * 2) + pad_bottom;

    // Anchor the card above the info banner (top-left), like other modals.
    const int screen_w = window_config_.virtual_width;
    const int screen_h = window_config_.virtual_height;
    int banner_y0 = screen_h;
    if (info_banner_style_.enabled) {
        const int stats_h = std::max(0, info_banner_style_.info_height);
        const int top_line_h = std::max(0, info_banner_style_.separator_height);
        const int total_h = stats_h + top_line_h;
        const int off = static_cast<int>(std::lround((1.0 - ui_state_.bottomBannerReveal()) * static_cast<double>(total_h)));
        banner_y0 = screen_h - total_h + off;
    }
    const int y = std::max(0, banner_y0 - s.gap_above_info_banner - h);

    // Smooth enter from the left side.
    const double t = std::clamp(exit_save_modal_reveal_, 0.0, 1.0);
    const int shown_x = std::clamp(s.shown_x, 0, std::max(0, screen_w - w));
    const int hidden_x = -w - std::max(0, s.offscreen_pad);
    const int x = static_cast<int>(std::lround(static_cast<double>(hidden_x) + (static_cast<double>(shown_x - hidden_x) * t)));
    exit_save_modal_card_rect_virt_ = SDL_Rect{x, y, w, h};

    int ry = y + pad_top;
    for (int i = 0; i < 3; ++i) {
        exit_save_modal_row_rects_virt_[static_cast<std::size_t>(i)] =
            SDL_Rect{x + pad_x, ry, w - pad_x * 2, row_h};
        ry += row_h + gap;
    }
}

void TransferSystemScreen::stepExitSaveModalSelection(int delta) {
    exit_save_modal_selected_row_ = std::clamp(exit_save_modal_selected_row_ + delta, 0, 2);
    ui_state_.requestUiMoveSfx();
}

std::optional<int> TransferSystemScreen::exitSaveModalRowAtPoint(int logical_x, int logical_y) const {
    auto in = [](int px, int py, const SDL_Rect& r) {
        return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h;
    };
    for (int i = 0; i < 3; ++i) {
        if (in(logical_x, logical_y, exit_save_modal_row_rects_virt_[static_cast<std::size_t>(i)])) {
            return i;
        }
    }
    return std::nullopt;
}

void TransferSystemScreen::activateExitSaveModalRow(int row) {
    row = std::clamp(row, 0, 2);
    // 0: save + exit, 1: exit without saving, 2: continue
    if (row == 2) {
        // Return to box ops immediately (no lingering input capture).
        exit_save_modal_open_ = false;
        exit_save_modal_target_open_ = false;
        exit_save_modal_reveal_ = 0.0;
        ui_state_.requestButtonSfx();
        return;
    }
    if (row == 0) {
        const bool had_changes = game_boxes_dirty_ || resort_boxes_dirty_;
        if (!saveGameBoxEditsOverlayAndClearDirty()) {
            ui_state_.requestErrorSfx();
            return;
        }
        successful_save_exit_requested_ = had_changes;
        closeExitSaveModal();
        if (!had_changes) {
            ui_state_.startExit();
        }
        return;
    } else if (row == 1) {
        game_boxes_dirty_ = false;
        resort_boxes_dirty_ = false;
    }
    closeExitSaveModal();
    ui_state_.startExit();
}

bool TransferSystemScreen::handleExitSaveModalPointerPressed(int logical_x, int logical_y) {
    if (!exit_save_modal_open_) {
        return false;
    }
    syncExitSaveModalLayout();
    if (const auto row = exitSaveModalRowAtPoint(logical_x, logical_y)) {
        exit_save_modal_selected_row_ = *row;
        ui_state_.requestButtonSfx();
        activateExitSaveModalRow(*row);
        return true;
    }
    // Click outside: keep modal open.
    return true;
}

void TransferSystemScreen::markGameBoxesDirty() {
    game_boxes_dirty_ = true;
}

void TransferSystemScreen::markResortBoxesDirty() {
    resort_boxes_dirty_ = true;
}

bool TransferSystemScreen::preparePendingResortMirrorPayloadsForSave() {
    if (!resort_service_) {
        return true;
    }
    if (!bridge_import_source_game_.has_value()) {
        for (const auto& box : resort_pc_boxes_) {
            for (const auto& slot : box.slots) {
                if (slot.occupied() && slot.resort_pkrid.empty()) {
                    std::cerr << "Warning: cannot prepare Resort import: missing bridge import source_game\n";
                    return false;
                }
            }
        }
    }

    for (auto& box : game_pc_boxes_) {
        for (auto& slot : box.slots) {
            if (!slot.occupied() || slot.resort_pkrid.empty()) {
                continue;
            }
            if (resort_service_->getActiveMirrorForPokemon(slot.resort_pkrid).has_value()) {
                if (resort_service_->getPokemonLocation(kDefaultResortProfileId, slot.resort_pkrid).has_value()) {
                    std::cerr << kTempTransferLog
                              << " Save prepare found stale active mirror for boxed Pokemon; export will retire it pkrid="
                              << slot.resort_pkrid << '\n';
                } else {
                    std::cerr << "Warning: cannot save Resort mirror: Pokemon already has an active mirror: "
                              << slot.resort_pkrid << '\n';
                    return false;
                }
            }
            const std::optional<resort::PokemonSnapshot> snapshot =
                resort_service_->prepareLatestRawSnapshotForGameWrite(
                    slot.resort_pkrid,
                    bridge_import_source_game_,
                    slot.format);
            if (!snapshot || snapshot->raw_bytes.empty() || snapshot->raw_hash_sha256.empty()) {
                std::cerr << "Warning: cannot save Resort mirror: missing compatible raw snapshot for "
                          << slot.resort_pkrid << '\n';
                return false;
            }
            slot.bridge_box_payload_base64 = encodeBase64(snapshot->raw_bytes);
            slot.bridge_box_payload_hash_sha256 = snapshot->raw_hash_sha256;
            slot.format = snapshot->format_name;
            std::cerr << kTempTransferLog
                      << " Save prepare Resort mirror payload pkrid=" << slot.resort_pkrid
                      << " snapshot_id=" << snapshot->snapshot_id
                      << " format=" << snapshot->format_name
                      << " raw_bytes=" << snapshot->raw_bytes.size()
                      << " hash=" << snapshot->raw_hash_sha256 << '\n';
        }
    }
    return true;
}

bool TransferSystemScreen::commitPendingGameToResortImportsBeforeSave() {
    if (!resort_service_ || !resort_boxes_dirty_) {
        return true;
    }
    if (!bridge_import_source_game_.has_value()) {
        std::cerr << "Warning: cannot commit Game->Resort imports: missing bridge import source_game\n";
        return false;
    }

    int imported_count = 0;
    for (std::size_t bi = 0; bi < resort_pc_boxes_.size(); ++bi) {
        auto& slots = resort_pc_boxes_[bi].slots;
        for (std::size_t si = 0; si < slots.size(); ++si) {
            PcSlotSpecies& slot = slots[si];
            if (!slot.occupied() || !slot.resort_pkrid.empty()) {
                continue;
            }
            const std::optional<resort::ImportedPokemon> imported =
                resort::importedPokemonFromGamePcSlot(slot, *bridge_import_source_game_);
            if (!imported.has_value()) {
                std::cerr << "Warning: cannot pre-commit Game->Resort import: missing import-grade payload\n";
                return false;
            }
            resort::ImportContext ctx;
            ctx.profile_id = kDefaultResortProfileId;
            ctx.target_location = resort::BoxLocation{
                kDefaultResortProfileId,
                static_cast<int>(bi),
                static_cast<int>(si)
            };
            ctx.placement_policy = resort::BoxPlacementPolicy::ReplaceOccupied;
            const resort::ImportResult result = resort_service_->importParsedPokemon(*imported, ctx);
            if (!result.success) {
                std::cerr << "Warning: could not pre-commit Game->Resort import before save write: "
                          << result.error << '\n';
                return false;
            }
            slot.resort_pkrid = result.pkrid;
            ++imported_count;
            std::cerr << kTempTransferLog
                      << " Save pre-commit Game->Resort import pkrid=" << result.pkrid
                      << " snapshot_id=" << result.snapshot_id
                      << " box=" << bi
                      << " slot=" << si
                      << " created=" << (result.created ? "true" : "false")
                      << " merged=" << (result.merged ? "true" : "false") << '\n';
        }
    }
    if (imported_count > 0) {
        std::cerr << kTempTransferLog
                  << " Save pre-commit Game->Resort imports complete count=" << imported_count << '\n';
    }
    return true;
}

bool TransferSystemScreen::commitPendingResortStorageChangesAfterSave() {
    if (!resort_service_ || !resort_boxes_dirty_) {
        return true;
    }
    if (!bridge_import_source_game_.has_value()) {
        std::cerr << "Warning: cannot commit Resort changes: missing bridge import source_game\n";
        return false;
    }

    for (const auto& box : game_pc_boxes_) {
        for (const auto& slot : box.slots) {
            if (!slot.occupied() || slot.resort_pkrid.empty()) {
                continue;
            }
            resort::ExportContext ctx;
            ctx.target_game = *bridge_import_source_game_;
            ctx.target_format_name = slot.format;
            ctx.managed_mirror = true;
            const resort::ExportResult exported = resort_service_->exportPokemon(slot.resort_pkrid, ctx);
            if (!exported.success) {
                std::cerr << "Warning: could not commit Resort mirror export after save: "
                          << exported.error << '\n';
                return false;
            }
            std::cerr << kTempTransferLog
                      << " Save commit Resort->Game mirror pkrid=" << exported.pkrid
                      << " mirror_session_id=" << exported.mirror_session_id
                      << " snapshot_id=" << exported.snapshot_id << '\n';
        }
    }

    for (std::size_t bi = 0; bi < resort_pc_boxes_.size(); ++bi) {
        auto& slots = resort_pc_boxes_[bi].slots;
        for (std::size_t si = 0; si < slots.size(); ++si) {
            PcSlotSpecies& slot = slots[si];
            if (!slot.occupied() || !slot.resort_pkrid.empty()) {
                continue;
            }
            std::cerr << "Warning: refusing to finish save with uncommitted Game->Resort Pokemon at Resort box "
                      << bi << " slot " << si << '\n';
            return false;
        }
    }

    for (std::size_t bi = 0; bi < resort_pc_boxes_.size(); ++bi) {
        resort_service_->renameResortBox(kDefaultResortProfileId, static_cast<int>(bi), resort_pc_boxes_[bi].name);
    }

    for (std::size_t bi = 0; bi < resort_pc_boxes_.size(); ++bi) {
        const auto& slots = resort_pc_boxes_[bi].slots;
        for (std::size_t si = 0; si < slots.size(); ++si) {
            const PcSlotSpecies& slot = slots[si];
            if (!slot.occupied() || slot.resort_pkrid.empty()) {
                continue;
            }
            resort_service_->movePokemonToSlot(
                resort::BoxLocation{kDefaultResortProfileId, static_cast<int>(bi), static_cast<int>(si)},
                slot.resort_pkrid,
                resort::BoxPlacementPolicy::ReplaceOccupied);
        }
    }
    std::cerr << kTempTransferLog << " Save commit Resort storage complete\n";
    return true;
}

bool TransferSystemScreen::saveGameBoxEditsOverlayAndClearDirty() {
    if (!game_boxes_dirty_ && !resort_boxes_dirty_) {
        game_boxes_dirty_ = false;
        resort_boxes_dirty_ = false;
        return true;
    }
    if (!preparePendingResortMirrorPayloadsForSave()) {
        return false;
    }
    if (!commitPendingGameToResortImportsBeforeSave()) {
        return false;
    }
    if (!game_boxes_dirty_) {
        if (!commitPendingResortStorageChangesAfterSave()) {
            return false;
        }
        resort_boxes_dirty_ = false;
        return true;
    }
    if (save_directory_.empty()) {
        game_boxes_dirty_ = false;
        resort_boxes_dirty_ = false;
        return true;
    }
    // Require import-grade payloads for every occupied PC slot so we never write guessed PKM bytes.
    for (const auto& box : game_pc_boxes_) {
        for (const auto& slot : box.slots) {
            if (!slot.occupied()) {
                continue;
            }
            if (slot.bridge_box_payload_base64.empty() || !looksLikeSha256Hex(slot.bridge_box_payload_hash_sha256)) {
                std::cerr
                    << "Cannot save to real save: missing PKHeX import payload for one or more PC Pokémon. "
                       "Ensure the import bridge ran when opening this screen (check console warnings).\n";
                return false;
            }
        }
    }

    namespace fs = std::filesystem;
    const fs::path dir(save_directory_);
    std::error_code mkdir_error;
    fs::create_directories(dir, mkdir_error);

    const fs::path projection_path = dir / "transfer_write_projection.json";
    std::string projection_error;
    if (!writeTransferSaveProjectionV2(
            projection_path.string(),
            game_pc_boxes_,
            transferSaveSlotsPerGameBox(transfer_selection_),
            transferSaveSupportsBoxNameProjection(transfer_selection_),
            projection_error)) {
        std::cerr << "Warning: failed to write bridge projection: " << projection_error << '\n';
        return false;
    }

    const SaveBridgeProbeResult result = writeProjectionWithBridge(
        project_root_,
        bridge_argv0_,
        transfer_selection_.source_path,
        projection_path.string());
    if (!result.launched || !result.success) {
        std::cerr << "Warning: bridge write-projection failed. exit_code=" << result.exit_code << ' '
                  << formatBridgeRunFailureMessage(result) << '\n';
        return false;
    }

    if (!commitPendingResortStorageChangesAfterSave()) {
        return false;
    }

    // Keep the overlay cleared once the real save is updated (avoid double-applying on next launch).
    std::error_code rm_error;
    fs::remove(dir / "transfer_box_edits.json", rm_error);
    game_boxes_dirty_ = false;
    resort_boxes_dirty_ = false;
    return true;
}

} // namespace pr

