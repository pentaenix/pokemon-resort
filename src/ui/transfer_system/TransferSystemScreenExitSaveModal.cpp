#include "ui/TransferSystemScreen.hpp"

#include "core/bridge/SaveBridgeClient.hpp"
#include "core/config/Json.hpp"
#include "resort/domain/ExportedPokemon.hpp"
#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/PkmFormat.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"
#include "resort/openhome/OpenHomeMovementBridge.hpp"
#include "resort/openhome/OpenHomeStorageBridge.hpp"
#include "resort/services/PokemonResortService.hpp"
#include "core/bridge/BridgeImportMerge.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

namespace pr {

namespace {
constexpr const char* kDefaultResortProfileId = "default";
constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";
constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr const char* kOpenHomeJournalFileName = "pkr_openhome_journal.json";

std::string escapeJsonString(const std::string& s);

struct OpenHomeJournalPull {
    int source_box = -1;
    int source_slot = -1;
    std::string openhome_id;
};

std::filesystem::path openHomeJournalPath(const std::string& save_directory) {
    namespace fs = std::filesystem;
    return fs::path(save_directory) / "resort-openhome-storage" / kOpenHomeJournalFileName;
}

bool writeOpenHomeJournal(
    const std::string& save_directory,
    const std::string& source_save_path,
    const std::vector<OpenHomeJournalPull>& pulls) {
    namespace fs = std::filesystem;
    const fs::path p = openHomeJournalPath(save_directory);
    std::error_code mkdir_error;
    fs::create_directories(p.parent_path(), mkdir_error);

    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        return false;
    }
    f << "{\n";
    f << "  \"version\": 1,\n";
    f << "  \"state\": \"in_progress\",\n";
    f << "  \"sourceSavePath\": \"" << escapeJsonString(source_save_path) << "\",\n";
    f << "  \"pulls\": [\n";
    for (std::size_t i = 0; i < pulls.size(); ++i) {
        const auto& pull = pulls[i];
        f << "    {\"sourceBox\": " << pull.source_box
          << ", \"sourceSlot\": " << pull.source_slot
          << ", \"openhomeId\": \"" << escapeJsonString(pull.openhome_id) << "\"}";
        if (i + 1 < pulls.size()) {
            f << ",";
        }
        f << "\n";
    }
    f << "  ]\n";
    f << "}\n";
    return static_cast<bool>(f);
}

std::vector<OpenHomeJournalPull> readOpenHomeJournalPulls(
    const std::string& save_directory,
    std::string* out_source_save_path) {
    namespace fs = std::filesystem;
    std::vector<OpenHomeJournalPull> out;
    const fs::path p = openHomeJournalPath(save_directory);
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return out;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    try {
        const pr::JsonValue root = pr::parseJsonText(buffer.str());
        if (out_source_save_path) {
            if (const pr::JsonValue* s = root.get("sourceSavePath"); s && s->isString()) {
                *out_source_save_path = s->asString();
            }
        }
        const pr::JsonValue* pulls = root.get("pulls");
        if (!pulls || !pulls->isArray()) {
            return out;
        }
        for (const pr::JsonValue& item : pulls->asArray()) {
            if (!item.isObject()) continue;
            OpenHomeJournalPull pull;
            if (const pr::JsonValue* b = item.get("sourceBox"); b && b->isNumber()) {
                pull.source_box = static_cast<int>(b->asNumber());
            }
            if (const pr::JsonValue* s = item.get("sourceSlot"); s && s->isNumber()) {
                pull.source_slot = static_cast<int>(s->asNumber());
            }
            if (const pr::JsonValue* id = item.get("openhomeId"); id && id->isString()) {
                pull.openhome_id = id->asString();
            }
            if (pull.source_box >= 0 && pull.source_slot >= 0 && !pull.openhome_id.empty()) {
                out.push_back(std::move(pull));
            }
        }
    } catch (...) {
        // best effort
    }
    return out;
}

void clearOpenHomeJournalBestEffort(const std::string& save_directory) {
    namespace fs = std::filesystem;
    std::error_code rm_error;
    fs::remove(openHomeJournalPath(save_directory), rm_error);
}

// If a Save+Exit attempt succeeded in OpenHome (placed mons into Home) but failed later,
// the user's real save is still unchanged (we stage save writes). However, the Home placement
// can "swap" into a later save operation via displacement. This rollback clears the placement
// by pushing the OpenHome IDs back into the original slots on a temporary staged copy of the save,
// then discarding that copy.
void rollbackIncompleteOpenHomeJournalToPreventSwap(
    const std::string& project_root,
    const std::string& save_directory,
    const std::string& source_save_path) {
    namespace fs = std::filesystem;
    std::string journal_save_path;
    const std::vector<OpenHomeJournalPull> pulls =
        readOpenHomeJournalPulls(save_directory, &journal_save_path);
    if (pulls.empty()) {
        return;
    }
    if (!journal_save_path.empty() && journal_save_path != source_save_path) {
        // Don't touch unrelated saves.
        return;
    }

    resort::openhome::OpenHomeCliMovementBridge bridge(
        fs::path(project_root),
        fs::path(save_directory) / "resort-openhome-storage");

    for (const auto& pull : pulls) {
        const fs::path reconcile_path = fs::path(source_save_path).string() + ".pkr_openhome_reconcile";
        std::error_code copy_error;
        fs::copy_file(source_save_path, reconcile_path, fs::copy_options::overwrite_existing, copy_error);
        if (copy_error) {
            std::cerr << "Warning: OpenHome rollback could not stage save: " << copy_error.message() << '\n';
            break;
        }
        resort::openhome::OpenHomePushToGameRequest request;
        request.openhome_id = pull.openhome_id;
        request.destination.save_path = reconcile_path;
        request.destination.box = pull.source_box;
        request.destination.slot = pull.source_slot;
        const resort::openhome::OpenHomePushToGameResult result = bridge.pushPokemonToGame(request);
        std::error_code rm_error;
        fs::remove(reconcile_path, rm_error);
        if (!result.success) {
            std::cerr << "Warning: OpenHome rollback push-to-game failed for " << pull.openhome_id << ": "
                      << (result.error ? result.error->message : std::string("unknown error")) << '\n';
            break;
        }
    }
    clearOpenHomeJournalBestEffort(save_directory);
}

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

std::string asciiLowerCopy(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool asciiStartsWithInsensitive(std::string_view haystack, std::string_view needle) {
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t i = 0; i < needle.size(); ++i) {
        const int a = std::tolower(static_cast<int>(static_cast<unsigned char>(haystack[i])));
        const int b = std::tolower(static_cast<int>(static_cast<unsigned char>(needle[i])));
        if (a != b) {
            return false;
        }
    }
    return true;
}

/// `--save-type` for `resortBridge` (`SaveClass.saveTypeID`). If empty, the bridge guesses from bytes and
/// may mis-detect (Gen 2 checksum coincidence on later-gen files).
std::string openHomeResortExplicitSaveType(const TransferSaveSelection& sel) {
    if (!sel.pkhex_save_type.empty()) {
        const std::string lo = asciiLowerCopy(sel.pkhex_save_type);
        if (asciiStartsWithInsensitive(lo, "sav7")) {
            return "SM/USUM";
        }
        // PKHeX: SAV8SWSH (Sword/Shield).
        if (asciiStartsWithInsensitive(lo, "sav8swsh")) {
            return "SwShSAV";
        }
    }
    const std::string gk = asciiLowerCopy(sel.game_key);
    static constexpr const char* kGen7[] = {
        "pokemon_sun",
        "pokemon_moon",
        "pokemon_ultra_sun",
        "pokemon_ultra_moon",
        "7_s",
        "7_m",
        "7_us",
        "7_um",
        "7_sn",
        "7_mn",
    };
    for (const char* k : kGen7) {
        if (gk == k) {
            return "SM/USUM";
        }
    }
    static constexpr const char* kGen8SwSh[] = {
        "pokemon_sword",
        "pokemon_shield",
        "8_sw",
        "8_sh",
        "8_sword",
        "8_shield",
    };
    for (const char* k : kGen8SwSh) {
        if (gk == k) {
            return "SwShSAV";
        }
    }
    return {};
}

std::string quoted(const std::string& s) { return std::string{"\""} + escapeJsonString(s) + "\""; }

std::optional<std::uint32_t> targetPidFromSnapshotNotes(const std::string& notes_json) {
    if (notes_json.empty()) {
        return std::nullopt;
    }
    try {
        const JsonValue root = parseJsonText(notes_json);
        const JsonValue* target_pid = root.get("target_pid");
        if (!target_pid || !target_pid->isNumber()) {
            return std::nullopt;
        }
        const double value = target_pid->asNumber();
        if (value < 0.0 || value > 4294967295.0) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(std::llround(value));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

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

bool slotIsOpenHomeBacked(const PcSlotSpecies& slot);

bool slotHasImportGradePayload(const PcSlotSpecies& slot) {
    return !slot.bridge_box_payload_base64.empty() &&
           looksLikeSha256Hex(slot.bridge_box_payload_hash_sha256);
}

bool slotRequiresPreserveBoxProjection(const PcSlotSpecies& slot) {
    return slotIsOpenHomeBacked(slot) && !slotHasImportGradePayload(slot);
}

bool transferSaveSupportsBoxNameProjection(const TransferSaveSelection& selection) {
    return selection.game_key != "pokemon_red" &&
           selection.game_key != "pokemon_blue" &&
           selection.game_key != "pokemon_yellow" &&
           selection.game_key != "pokemon_gold" &&
           selection.game_key != "pokemon_silver" &&
           selection.game_key != "pokemon_crystal";
}

bool isGen12GameKey(const std::string& game_key) {
    return game_key == "pokemon_red" || game_key == "pokemon_blue" || game_key == "pokemon_yellow" ||
           game_key == "pokemon_gold" || game_key == "pokemon_silver" || game_key == "pokemon_crystal";
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
                } else if (slotRequiresPreserveBoxProjection(sl)) {
                    // Some OpenHome-backed slots only exist as direct staged-save writes and do not have import-grade
                    // payloads attached. Preserve the staged bytes for those slots; otherwise prefer the explicit raw
                    // payload so moved/swapped slots are written to their new location instead of preserving stale bytes.
                    json << "{\"preserve_box_slot\":true}";
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
    if (isGen12GameKey(selection.game_key)) {
        return 20;
    }
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

bool slotIsOpenHomeBacked(const PcSlotSpecies& slot) {
    return resort::openhome::isValidOpenHomeId(slot.home_tracker);
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
        if (!had_changes) {
            if (!saveGameBoxEditsOverlayAndClearDirty()) {
                ui_state_.requestErrorSfx();
                return;
            }
            successful_save_exit_requested_ = false;
            closeExitSaveModal();
            ui_state_.startExit();
            return;
        }
        deferred_save_for_successful_exit_pending_ = true;
        successful_save_exit_requested_ = true;
        closeExitSaveModal();
        return;
    } else if (row == 1) {
        game_boxes_dirty_ = false;
        resort_boxes_dirty_ = false;
    }
    closeExitSaveModal();
    ui_state_.startExit();
}

bool TransferSystemScreen::runDeferredSaveForSuccessfulExit() {
    if (!deferred_save_for_successful_exit_pending_) {
        return true;
    }
    deferred_save_for_successful_exit_pending_ = false;
    return saveGameBoxEditsOverlayAndClearDirty();
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

void TransferSystemScreen::noteCrossPanelGameToResortMoves(int count) {
    if (count > 0) {
        cross_panel_game_to_resort_moves_ += count;
    }
}

void TransferSystemScreen::noteCrossPanelResortToGameMoves(int count) {
    if (count > 0) {
        cross_panel_resort_to_game_moves_ += count;
    }
}

std::string TransferSystemScreen::successfulSaveQuickPassMessageKey() const {
    const bool into_resort = cross_panel_game_to_resort_moves_ > 0;
    const bool into_game = cross_panel_resort_to_game_moves_ > 0;
    if (into_resort && into_game) {
        return "message_transport_pokemon_bidirectional";
    }
    if (into_resort) {
        return "message_transport_pokemon_in";
    }
    if (into_game) {
        return "message_transport_pokemon_out";
    }
    return "message_pokemon_manage";
}

bool TransferSystemScreen::preparePendingResortMirrorPayloadsForSave() {
    if (!resort_service_) {
        return true;
    }
    pending_prepared_mirror_exports_.clear();
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
            if (!slotIsOpenHomeBacked(slot) && resort_service_) {
                if (const auto linked_openhome_id = resort_service_->getOpenHomeIdForPokemon(slot.resort_pkrid);
                    linked_openhome_id && resort::openhome::isValidOpenHomeId(*linked_openhome_id)) {
                    slot.home_tracker = *linked_openhome_id;
                }
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
            const std::filesystem::path bridge_project_req =
                std::filesystem::temp_directory_path() /
                ("pr_prepare_proj_" + slot.resort_pkrid + ".json");
            // Mirror slots often retain the Pokémon's prior encoding (`pk3`) even while the loaded save is Gen 4.
            // PKHeX write-projection requires payloads matching the *save file* (`pk4`). Prefer import snapshot format.
            const std::string inferred_write_target_format =
                bridge_import_source_game_ ? resort::pkmStorageFormatNameForGameId(*bridge_import_source_game_)
                                           : std::string{};
            const std::string write_target_format = !inferred_write_target_format.empty()
                                                      ? inferred_write_target_format
                                                      : (bridge_import_storage_format_name_.empty()
                                                             ? slot.format
                                                             : bridge_import_storage_format_name_);
            const std::optional<resort::PokemonSnapshot> snapshot =
                resort_service_->prepareLatestRawSnapshotForGameWrite(
                    slot.resort_pkrid,
                    bridge_import_source_game_,
                    write_target_format,
                    project_root_,
                    bridge_argv0_,
                    bridge_project_req);
            if (!snapshot || snapshot->raw_bytes.empty() || snapshot->raw_hash_sha256.empty()) {
                std::cerr << "Warning: cannot save Resort mirror: missing compatible raw snapshot for "
                          << slot.resort_pkrid << '\n';
                return false;
            }
            slot.bridge_box_payload_base64 = encodeBase64(snapshot->raw_bytes);
            slot.bridge_box_payload_hash_sha256 = snapshot->raw_hash_sha256;
            slot.format = snapshot->format_name;
            slot.pid = targetPidFromSnapshotNotes(snapshot->notes_json).value_or(slot.pid.value_or(0));
            if (slot.pid && *slot.pid == 0) {
                slot.pid.reset();
            }
            resort::ExportContext ctx;
            ctx.target_game = *bridge_import_source_game_;
            ctx.target_format_name = write_target_format;
            ctx.managed_mirror = true;
            ctx.bridge_project_root = project_root_;
            ctx.bridge_argv0 = bridge_argv0_;
            PendingPreparedMirrorExport pending;
            pending.pkrid = slot.resort_pkrid;
            pending.context = ctx;
            pending.raw_payload = snapshot->raw_bytes;
            pending.raw_hash = snapshot->raw_hash_sha256;
            pending.format_name = snapshot->format_name;
            pending.transport_pid = slot.pid;
            pending_prepared_mirror_exports_.push_back(std::move(pending));
            std::cerr << kTempTransferLog
                      << " Save prepare Resort mirror payload pkrid=" << slot.resort_pkrid
                      << " snapshot_id=" << snapshot->snapshot_id
                      << " format=" << snapshot->format_name
                      << " transport_pid=" << (slot.pid ? std::to_string(*slot.pid) : std::string("null"))
                      << " raw_bytes=" << snapshot->raw_bytes.size()
                      << " hash=" << snapshot->raw_hash_sha256 << '\n';
        }
    }
    return true;
}

bool TransferSystemScreen::hasPendingOpenHomeMovement() const {
    if (!pending_openhome_pulls_.empty()) {
        return true;
    }
    for (const auto& box : game_pc_boxes_) {
        for (const PcSlotSpecies& slot : box.slots) {
            if (!slot.occupied() || slot.resort_pkrid.empty()) {
                continue;
            }
            if (slotIsOpenHomeBacked(slot)) {
                return true;
            }
            if (resort_service_) {
                if (const auto linked_openhome_id = resort_service_->getOpenHomeIdForPokemon(slot.resort_pkrid);
                    linked_openhome_id && resort::openhome::isValidOpenHomeId(*linked_openhome_id)) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool TransferSystemScreen::commitPendingOpenHomeMovementBeforeSave(const std::string& save_path_override) {
    namespace fs = std::filesystem;
    if (!hasPendingOpenHomeMovement()) {
        return true;
    }
    if (save_path_override.empty()) {
        std::cerr << "Warning: cannot commit OpenHome movement: missing staged save path\n";
        return false;
    }

    // Journal all successful OpenHome pulls so we can roll back safely if anything fails after side effects
    // (or if the app crashes mid-save). Rollback is implemented by pushing each OpenHome ID back into the
    // same save slot on a temporary staged copy, then discarding that staged save; this clears the Home placement
    // without mutating the user's real save.
    std::vector<OpenHomeJournalPull> journal_pulls;
    resort::openhome::OpenHomeCliMovementBridge bridge(
        fs::path(project_root_),
        fs::path(save_directory_) / "resort-openhome-storage");

    auto findFirstEmptyHomeSlot = [&]() -> std::optional<resort::openhome::OpenHomeHomeSlot> {
        try {
            resort::openhome::OpenHomeStorageBridge storage(fs::path(save_directory_) / "resort-openhome-storage");
            const resort::openhome::OpenHomeBankData banks = storage.loadHomeBanks();
            for (const auto& bank : banks.banks) {
                for (const auto& box : bank.boxes) {
                    // Home boxes are sparse maps; first missing slot index is free.
                    for (int si = 0; si < 30; ++si) {
                        if (box.identifiers_by_slot.find(si) == box.identifiers_by_slot.end()) {
                            resort::openhome::OpenHomeHomeSlot out;
                            out.bank = bank.index;
                            out.box = box.index;
                            out.slot = si;
                            return out;
                        }
                    }
                }
            }
        } catch (const std::exception& ex) {
            std::cerr << "Warning: could not load OpenHome banks to find empty slot: " << ex.what() << '\n';
        }
        return std::nullopt;
    };

    std::vector<const PendingOpenHomePull*> ordered_pulls;
    ordered_pulls.reserve(pending_openhome_pulls_.size());
    for (const PendingOpenHomePull& pending : pending_openhome_pulls_) {
        ordered_pulls.push_back(&pending);
    }
    std::sort(ordered_pulls.begin(), ordered_pulls.end(), [](const PendingOpenHomePull* a, const PendingOpenHomePull* b) {
        if (a->source_box != b->source_box) {
            return a->source_box < b->source_box;
        }
        return a->source_slot > b->source_slot;
    });

    for (const PendingOpenHomePull* pending_ptr : ordered_pulls) {
        const PendingOpenHomePull& pending = *pending_ptr;
        PcSlotSpecies* target_slot = nullptr;
        int target_box = -1;
        int target_index = -1;
        for (std::size_t bi = 0; bi < resort_pc_boxes_.size() && !target_slot; ++bi) {
            auto& slots = resort_pc_boxes_[bi].slots;
            for (std::size_t si = 0; si < slots.size(); ++si) {
                PcSlotSpecies& candidate = slots[si];
                if (candidate.occupied() &&
                    candidate.bridge_box_payload_hash_sha256 == pending.raw_hash &&
                    candidate.resort_pkrid.empty()) {
                    target_slot = &candidate;
                    target_box = static_cast<int>(bi);
                    target_index = static_cast<int>(si);
                    break;
                }
            }
        }
        if (!target_slot) {
            continue;
        }

        // Gen 1 boxes pack toward slot 0 on each save write. Pull higher slots first within each
        // box so queued lower-slot coordinates remain valid without a PKHeX bridge re-probe.
        int staged_source_box = pending.source_box;
        int staged_source_slot = pending.source_slot;

        resort::openhome::OpenHomePullToHomeRequest request;
        request.source.save_path = save_path_override;
        request.source.save_type = openHomeResortExplicitSaveType(transfer_selection_);
        request.source.box = staged_source_box;
        request.source.slot = staged_source_slot;
        if (const auto empty = findFirstEmptyHomeSlot()) {
            request.destination = *empty;
        } else {
            // Fallback: preserve old behavior if banks cannot be read, but this may swap.
            request.destination.bank = 0;
            request.destination.box = target_box;
            request.destination.slot = target_index;
        }

        const resort::openhome::OpenHomePullToHomeResult result = bridge.pullPokemonToHome(request);
        if (!result.success) {
            std::cerr << "Warning: OpenHome pull-to-home failed: "
                      << (result.error ? result.error->message : std::string("unknown error")) << '\n';
            if (!journal_pulls.empty()) {
                (void)writeOpenHomeJournal(save_directory_, transfer_selection_.source_path, journal_pulls);
            }
            return false;
        }
        target_slot->home_tracker = result.openhome_id;
        pending_openhome_import_payloads_.push_back(PendingOpenHomeImportPayload{
            target_box,
            target_index,
            request.destination.bank,
            request.destination.box,
            request.destination.slot,
            result.payload
        });
        journal_pulls.push_back(OpenHomeJournalPull{
            staged_source_box,
            staged_source_slot,
            result.openhome_id
        });
        std::cerr << kTempTransferLog
                  << " OpenHome pull-to-home openhomeId=" << result.openhome_id
                  << " source_box=" << staged_source_box
                  << " source_slot=" << staged_source_slot
                  << " (queue_box=" << pending.source_box << " queue_slot=" << pending.source_slot << ")"
                  << " home_bank=" << request.destination.bank
                  << " home_box=" << request.destination.box
                  << " home_slot=" << request.destination.slot << '\n';
    }

    for (std::size_t bi = 0; bi < game_pc_boxes_.size(); ++bi) {
        auto& slots = game_pc_boxes_[bi].slots;
        for (std::size_t si = 0; si < slots.size(); ++si) {
            PcSlotSpecies& slot = slots[si];
            if (!slot.occupied() || slot.resort_pkrid.empty()) {
                continue;
            }
            std::string openhome_id = slot.home_tracker;
            if (!resort::openhome::isValidOpenHomeId(openhome_id) && resort_service_) {
                if (const auto linked_openhome_id = resort_service_->getOpenHomeIdForPokemon(slot.resort_pkrid);
                    linked_openhome_id && resort::openhome::isValidOpenHomeId(*linked_openhome_id)) {
                    openhome_id = *linked_openhome_id;
                    slot.home_tracker = openhome_id;
                }
            }
            if (!resort::openhome::isValidOpenHomeId(openhome_id)) {
                continue;
            }
            resort::openhome::OpenHomePushToGameRequest request;
            request.openhome_id = openhome_id;
            request.destination.save_path = save_path_override;
            request.destination.box = static_cast<int>(bi);
            request.destination.slot = static_cast<int>(si);
            const resort::openhome::OpenHomePushToGameResult result = bridge.pushPokemonToGame(request);
            if (!result.success) {
                std::cerr << "Warning: OpenHome push-to-game failed: "
                          << (result.error ? result.error->message : std::string("unknown error")) << '\n';
                return false;
            }
            if (resort_service_) {
                if (result.updated_payload && !result.updated_payload->serialized_identity_or_ohpkm.empty()) {
                    resort_service_->linkOpenHomePayloadToPokemon(slot.resort_pkrid, *result.updated_payload);
                }
                resort_service_->recordPokemonInGamePlacement(
                    slot.resort_pkrid,
                    openhome_id,
                    bridge_import_source_game_,
                    transfer_selection_.source_path,
                    static_cast<int>(bi),
                    static_cast<int>(si));
                resort_service_->removePokemonFromBoxes(kDefaultResortProfileId, slot.resort_pkrid);
            }
            std::cerr << kTempTransferLog
                      << " OpenHome push-to-game openhomeId=" << openhome_id
                      << " pkrid=" << slot.resort_pkrid
                      << " game_box=" << bi
                      << " game_slot=" << si << '\n';
        }
    }

    if (!journal_pulls.empty()) {
        (void)writeOpenHomeJournal(save_directory_, transfer_selection_.source_path, journal_pulls);
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
            if (resort::openhome::isValidOpenHomeId(slot.home_tracker)) {
                auto payload_it = std::find_if(
                    pending_openhome_import_payloads_.begin(),
                    pending_openhome_import_payloads_.end(),
                    [&](const PendingOpenHomeImportPayload& pending) {
                        return pending.resort_box == static_cast<int>(bi) &&
                               pending.resort_slot == static_cast<int>(si) &&
                               pending.payload.openhome_id == slot.home_tracker;
                    });
                if (payload_it != pending_openhome_import_payloads_.end() &&
                    !payload_it->payload.serialized_identity_or_ohpkm.empty()) {
                    resort_service_->linkOpenHomePayloadToPokemon(result.pkrid, payload_it->payload);
                    resort_service_->recordPokemonHomePlacement(
                        result.pkrid,
                        slot.home_tracker,
                        payload_it->home_bank,
                        payload_it->home_box,
                        payload_it->home_slot);
                    std::cerr << kTempTransferLog
                              << " Save pre-commit linked OpenHome payload pkrid=" << result.pkrid
                              << " openhomeId=" << slot.home_tracker
                              << " box=" << bi
                              << " slot=" << si << '\n';
                }
            }
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

    for (const auto& pending : pending_prepared_mirror_exports_) {
        const resort::ExportResult exported = resort_service_->commitPreparedMirrorExport(
            pending.pkrid,
            pending.context,
            pending.raw_payload,
            pending.raw_hash,
            pending.format_name,
            pending.transport_pid);
        if (!exported.success) {
            std::cerr << "Warning: could not commit prepared Resort mirror export after save: "
                      << exported.error << '\n';
            return false;
        }
        std::cerr << kTempTransferLog
                  << " Save commit prepared Resort->Game mirror pkrid=" << exported.pkrid
                  << " mirror_session_id=" << exported.mirror_session_id
                  << " snapshot_id=" << exported.snapshot_id
                  << " transport_pid="
                  << (exported.transport_pid ? std::to_string(*exported.transport_pid) : std::string("null"))
                  << '\n';
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
    pending_prepared_mirror_exports_.clear();
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
    if (hasPendingOpenHomeMovement()) {
        namespace fs = std::filesystem;
        if (transfer_selection_.source_path.empty()) {
            std::cerr << "Warning: cannot save OpenHome movement: missing source save path\n";
            return false;
        }
        const fs::path source_path = transfer_selection_.source_path;
        const fs::path staged_path = source_path.string() + ".pkr_openhome_staged";
        std::error_code copy_error;
        fs::copy_file(source_path, staged_path, fs::copy_options::overwrite_existing, copy_error);
        if (copy_error) {
            std::cerr << "Warning: could not stage save for OpenHome movement: " << copy_error.message() << '\n';
            return false;
        }

        const bool openhome_ok = commitPendingOpenHomeMovementBeforeSave(staged_path.string());
        if (!openhome_ok) {
            std::error_code rm_error;
            fs::remove(staged_path, rm_error);
            // If any OpenHome pulls succeeded before failure, roll back Home placements using the journal.
            rollbackIncompleteOpenHomeJournalToPreventSwap(project_root_, save_directory_, source_path.string());
            return false;
        }
        if (!commitPendingGameToResortImportsBeforeSave()) {
            std::error_code rm_error;
            fs::remove(staged_path, rm_error);
            rollbackIncompleteOpenHomeJournalToPreventSwap(project_root_, save_directory_, source_path.string());
            return false;
        }
        if (game_boxes_dirty_) {
            // OpenHome push/pull mutates specific staged slots, but box swaps and non-OpenHome Resort->Game
            // placements still need the full projection pass so the staged save matches the in-memory box order.
            for (const auto& box : game_pc_boxes_) {
                for (const auto& slot : box.slots) {
                    if (!slot.occupied()) {
                        continue;
                    }
                    if (slotIsOpenHomeBacked(slot)) {
                        continue;
                    }
                    if (slot.bridge_box_payload_base64.empty() || !looksLikeSha256Hex(slot.bridge_box_payload_hash_sha256)) {
                        std::cerr
                            << "Cannot save to real save: missing PKHeX import payload for one or more PC Pokémon. "
                               "Ensure the import bridge ran when opening this screen (check console warnings).\n";
                        std::error_code rm_error;
                        fs::remove(staged_path, rm_error);
                        rollbackIncompleteOpenHomeJournalToPreventSwap(project_root_, save_directory_, source_path.string());
                        return false;
                    }
                }
            }

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
                std::error_code rm_error;
                fs::remove(staged_path, rm_error);
                rollbackIncompleteOpenHomeJournalToPreventSwap(project_root_, save_directory_, source_path.string());
                return false;
            }

            const SaveBridgeProbeResult result = writeProjectionWithBridge(
                project_root_,
                bridge_argv0_,
                staged_path.string(),
                projection_path.string());
            if (!result.launched || !result.success) {
                std::cerr << "Warning: bridge write-projection failed. exit_code=" << result.exit_code << ' '
                          << formatBridgeRunFailureMessage(result) << '\n';
                std::error_code rm_error;
                fs::remove(staged_path, rm_error);
                rollbackIncompleteOpenHomeJournalToPreventSwap(project_root_, save_directory_, source_path.string());
                return false;
            }
        }

        if (!commitPendingResortStorageChangesAfterSave()) {
            std::error_code rm_error;
            fs::remove(staged_path, rm_error);
            rollbackIncompleteOpenHomeJournalToPreventSwap(project_root_, save_directory_, source_path.string());
            return false;
        }

        // All commits succeeded; atomically replace the user's real save with the staged OpenHome result.
        std::error_code rename_error;
        fs::rename(staged_path, source_path, rename_error);
        if (rename_error) {
            // If rename fails (e.g. cross-device), fall back to copy+remove.
            std::error_code copy_back_error;
            fs::copy_file(staged_path, source_path, fs::copy_options::overwrite_existing, copy_back_error);
            std::error_code rm_error;
            fs::remove(staged_path, rm_error);
            if (copy_back_error) {
                std::cerr << "Warning: OpenHome movement succeeded but failed to write staged save back to source: "
                          << copy_back_error.message() << '\n';
                return false;
            }
        }
        pending_openhome_pulls_.clear();
        pending_openhome_import_payloads_.clear();
        clearOpenHomeJournalBestEffort(save_directory_);
        std::error_code overlay_rm_error;
        fs::remove(fs::path(save_directory_) / "transfer_box_edits.json", overlay_rm_error);
        game_boxes_dirty_ = false;
        resort_boxes_dirty_ = false;
        return true;
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
            if (slotIsOpenHomeBacked(slot)) {
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
