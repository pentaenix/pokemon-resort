#include "transfer/application/OpenHomeMovementOrchestrator.hpp"

#include "core/config/Json.hpp"
#include "resort/openhome/OpenHomeIdentity.hpp"
#include "resort/openhome/OpenHomeMovementBridge.hpp"
#include "resort/openhome/OpenHomeStorageBridge.hpp"
#include "resort/services/PokemonResortService.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace pr::transfer::application {

namespace {

constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";
constexpr const char* kDefaultResortProfileId = "default";

std::string escapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0F];
                    out += hex[c & 0x0F];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

struct OpenHomeJournalPull {
    int source_box = -1;
    int source_slot = -1;
    std::string openhome_id;
};

std::filesystem::path openHomeJournalPath(const std::string& save_directory) {
    namespace fs = std::filesystem;
    return fs::path(save_directory) / "resort-openhome-storage" / "pkr_openhome_journal.json";
}

bool writeOpenHomeBatchOpsFile(const std::filesystem::path& path, const std::string& json_body) {
    namespace fs = std::filesystem;
    std::error_code mk_err;
    fs::create_directories(path.parent_path(), mk_err);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(json_body.data(), static_cast<std::streamsize>(json_body.size()));
    return static_cast<bool>(out);
}

std::vector<resort::openhome::OpenHomeHomeSlot> findEmptyOpenHomeHomeSlots(
    const std::filesystem::path& openhome_storage_root,
    std::size_t need_count) {
    std::vector<resort::openhome::OpenHomeHomeSlot> out;
    if (need_count == 0) {
        return out;
    }
    try {
        resort::openhome::OpenHomeStorageBridge storage(openhome_storage_root);
        const resort::openhome::OpenHomeBankData banks = storage.loadHomeBanks();
        for (const auto& bank : banks.banks) {
            for (const auto& box : bank.boxes) {
                for (int si = 0; si < 30; ++si) {
                    if (box.identifiers_by_slot.find(si) == box.identifiers_by_slot.end()) {
                        resort::openhome::OpenHomeHomeSlot slot;
                        slot.bank = bank.index;
                        slot.box = box.index;
                        slot.slot = si;
                        out.push_back(slot);
                        if (out.size() >= need_count) {
                            return out;
                        }
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "Warning: could not enumerate OpenHome home slots: " << ex.what() << '\n';
    }
    return out;
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

} // namespace

OpenHomeMovementOrchestrator::OpenHomeMovementOrchestrator(
    const std::string& project_root,
    const std::string& save_directory,
    const TransferSaveSelection& transfer_selection,
    std::optional<std::uint16_t> bridge_import_source_game,
    resort::PokemonResortService* resort_service)
    : project_root_(project_root),
      save_directory_(save_directory),
      transfer_selection_(transfer_selection),
      bridge_import_source_game_(bridge_import_source_game),
      resort_service_(resort_service) {}

bool OpenHomeMovementOrchestrator::commitPendingOpenHomeMovementBeforeSave(
    const std::string& save_path_override,
    const std::string& explicit_save_type,
    std::vector<PendingOpenHomePullInput>& pending_openhome_pulls,
    std::vector<TransferSaveSelection::PcBox>& resort_pc_boxes,
    std::vector<TransferSaveSelection::PcBox>& game_pc_boxes,
    std::vector<PendingOpenHomeImportPayloadOutput>& pending_openhome_import_payloads) const {
    namespace fs = std::filesystem;
    if (save_path_override.empty()) {
        std::cerr << "Warning: cannot commit OpenHome movement: missing staged save path\n";
        return false;
    }

    std::vector<OpenHomeJournalPull> journal_pulls;
    resort::openhome::OpenHomeCliMovementBridge bridge(
        fs::path(project_root_),
        fs::path(save_directory_) / "resort-openhome-storage");

    const bool trace_save_perf = std::getenv("PDSM_TRACE_SAVE_PERF") != nullptr &&
        std::string(std::getenv("PDSM_TRACE_SAVE_PERF")) == "1";
    const auto perf_wall_start = std::chrono::steady_clock::now();
    int openhome_processes = 0;
    int openhome_batch_pull_size = 0;
    int openhome_batch_push_size = 0;

    std::vector<const PendingOpenHomePullInput*> ordered_pulls;
    ordered_pulls.reserve(pending_openhome_pulls.size());
    for (const PendingOpenHomePullInput& pending : pending_openhome_pulls) {
        ordered_pulls.push_back(&pending);
    }
    std::sort(ordered_pulls.begin(), ordered_pulls.end(), [](const PendingOpenHomePullInput* a, const PendingOpenHomePullInput* b) {
        if (a->source_box != b->source_box) {
            return a->source_box < b->source_box;
        }
        return a->source_slot > b->source_slot;
    });

    struct ExecPull {
        const PendingOpenHomePullInput* pending;
        PcSlotSpecies* target_slot = nullptr;
        int target_box = -1;
        int target_index = -1;
        int staged_source_box = 0;
        int staged_source_slot = 0;
    };
    std::vector<ExecPull> exec_pulls;
    exec_pulls.reserve(ordered_pulls.size());
    for (const PendingOpenHomePullInput* pending_ptr : ordered_pulls) {
        const PendingOpenHomePullInput& pending = *pending_ptr;
        PcSlotSpecies* target_slot = nullptr;
        int target_box = -1;
        int target_index = -1;
        for (std::size_t bi = 0; bi < resort_pc_boxes.size() && !target_slot; ++bi) {
            auto& slots = resort_pc_boxes[bi].slots;
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
        ExecPull row;
        row.pending = pending_ptr;
        row.target_slot = target_slot;
        row.target_box = target_box;
        row.target_index = target_index;
        row.staged_source_box = pending.source_box;
        row.staged_source_slot = pending.source_slot;
        exec_pulls.push_back(row);
    }

    if (!exec_pulls.empty()) {
        const fs::path openhome_root = fs::path(save_directory_) / "resort-openhome-storage";
        const std::vector<resort::openhome::OpenHomeHomeSlot> home_slots =
            findEmptyOpenHomeHomeSlots(openhome_root, exec_pulls.size());
        if (home_slots.size() < exec_pulls.size()) {
            std::cerr << "Warning: OpenHome batch pull needs " << exec_pulls.size()
                      << " empty home slots but only " << home_slots.size() << " are available\n";
            return false;
        }
        std::ostringstream ops_json;
        ops_json << '[';
        for (std::size_t i = 0; i < exec_pulls.size(); ++i) {
            if (i > 0) {
                ops_json << ',';
            }
            const ExecPull& e = exec_pulls[i];
            const resort::openhome::OpenHomeHomeSlot& h = home_slots[i];
            ops_json << "{\"box\":" << e.staged_source_box << ",\"slot\":" << e.staged_source_slot
                     << ",\"home_bank\":" << h.bank << ",\"home_box\":" << h.box << ",\"home_slot\":" << h.slot
                     << "}";
        }
        ops_json << ']';
        const fs::path ops_path = fs::path(save_directory_) / "pkr_openhome_batch_pull_ops.json";
        if (!writeOpenHomeBatchOpsFile(ops_path, ops_json.str())) {
            std::cerr << "Warning: could not write OpenHome batch pull ops file\n";
            return false;
        }

        resort::openhome::OpenHomeBatchPullToHomeRequest batch_req;
        batch_req.save_path = save_path_override;
        batch_req.save_type = explicit_save_type;
        batch_req.ops_json_path = ops_path;
        batch_req.write_source_save = true;
        const resort::openhome::OpenHomeBatchPullToHomeResult batch_result = bridge.batchPullPokemonToHome(batch_req);
        ++openhome_processes;
        openhome_batch_pull_size = static_cast<int>(exec_pulls.size());
        if (trace_save_perf) {
            std::cerr << kTempTransferLog << " openhome_batch_pull_size=" << openhome_batch_pull_size
                      << " bridge_perf_save_loads=" << batch_result.perf.save_loads
                      << " bridge_perf_save_writes=" << batch_result.perf.save_writes
                      << " bridge_perf_total_ms=" << batch_result.perf.total_ms << '\n';
        }
        if (!batch_result.success) {
            std::cerr << "Warning: OpenHome batch-pull-to-home failed: "
                      << (batch_result.error ? batch_result.error->message : std::string("unknown error")) << '\n';
            if (!journal_pulls.empty()) {
                (void)writeOpenHomeJournal(save_directory_, transfer_selection_.source_path, journal_pulls);
            }
            return false;
        }
        if (batch_result.pulls.size() != exec_pulls.size()) {
            std::cerr << "Warning: OpenHome batch-pull-to-home result count mismatch\n";
            if (!journal_pulls.empty()) {
                (void)writeOpenHomeJournal(save_directory_, transfer_selection_.source_path, journal_pulls);
            }
            return false;
        }
        for (std::size_t i = 0; i < exec_pulls.size(); ++i) {
            const ExecPull& e = exec_pulls[i];
            const resort::openhome::OpenHomeBatchPullItemResult& pr = batch_result.pulls[i];
            e.target_slot->home_tracker = pr.openhome_id;
            pending_openhome_import_payloads.push_back(PendingOpenHomeImportPayloadOutput{
                e.target_box,
                e.target_index,
                pr.home_bank,
                pr.home_box,
                pr.home_slot,
                pr.payload
            });
            journal_pulls.push_back(OpenHomeJournalPull{
                e.staged_source_box,
                e.staged_source_slot,
                pr.openhome_id
            });
            std::cerr << kTempTransferLog
                      << " OpenHome batch-pull openhomeId=" << pr.openhome_id
                      << " source_box=" << e.staged_source_box
                      << " source_slot=" << e.staged_source_slot
                      << " (queue_box=" << e.pending->source_box << " queue_slot=" << e.pending->source_slot << ")"
                      << " home_bank=" << pr.home_bank
                      << " home_box=" << pr.home_box
                      << " home_slot=" << pr.home_slot << '\n';
        }
    }

    struct PushEntry {
        std::string openhome_id;
        std::string resort_pkrid;
        int game_box = 0;
        int game_slot = 0;
    };
    std::vector<PushEntry> push_entries;
    for (std::size_t bi = 0; bi < game_pc_boxes.size(); ++bi) {
        auto& slots = game_pc_boxes[bi].slots;
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
            PushEntry pe;
            pe.openhome_id = std::move(openhome_id);
            pe.resort_pkrid = slot.resort_pkrid;
            pe.game_box = static_cast<int>(bi);
            pe.game_slot = static_cast<int>(si);
            push_entries.push_back(std::move(pe));
        }
    }

    if (!push_entries.empty()) {
        std::ostringstream ops_json;
        ops_json << '[';
        for (std::size_t i = 0; i < push_entries.size(); ++i) {
            if (i > 0) {
                ops_json << ',';
            }
            const PushEntry& pe = push_entries[i];
            ops_json << "{\"openhome_id\":\"" << escapeJsonString(pe.openhome_id) << "\",\"box\":" << pe.game_box
                     << ",\"slot\":" << pe.game_slot << "}";
        }
        ops_json << ']';
        const fs::path ops_path = fs::path(save_directory_) / "pkr_openhome_batch_push_ops.json";
        if (!writeOpenHomeBatchOpsFile(ops_path, ops_json.str())) {
            std::cerr << "Warning: could not write OpenHome batch push ops file\n";
            return false;
        }
        resort::openhome::OpenHomeBatchPushToGameRequest batch_req;
        batch_req.save_path = save_path_override;
        batch_req.save_type = explicit_save_type;
        batch_req.ops_json_path = ops_path;
        batch_req.write_target_save = true;
        const resort::openhome::OpenHomeBatchPushToGameResult batch_result = bridge.batchPushPokemonToGame(batch_req);
        ++openhome_processes;
        openhome_batch_push_size = static_cast<int>(push_entries.size());
        if (trace_save_perf) {
            std::cerr << kTempTransferLog << " openhome_batch_push_size=" << openhome_batch_push_size
                      << " bridge_perf_save_loads=" << batch_result.perf.save_loads
                      << " bridge_perf_save_writes=" << batch_result.perf.save_writes
                      << " bridge_perf_total_ms=" << batch_result.perf.total_ms << '\n';
        }
        if (!batch_result.success) {
            std::cerr << "Warning: OpenHome batch-push-to-game failed: "
                      << (batch_result.error ? batch_result.error->message : std::string("unknown error")) << '\n';
            return false;
        }
        if (batch_result.pushes.size() != push_entries.size()) {
            std::cerr << "Warning: OpenHome batch-push-to-game result count mismatch\n";
            return false;
        }
        if (resort_service_) {
            std::vector<resort::OpenHomePushToGamePlacementItem> placement_items;
            placement_items.reserve(push_entries.size());
            for (std::size_t i = 0; i < push_entries.size(); ++i) {
                resort::OpenHomePushToGamePlacementItem item;
                item.pkrid = push_entries[i].resort_pkrid;
                item.openhome_id = push_entries[i].openhome_id;
                item.game_box = push_entries[i].game_box;
                item.game_slot = push_entries[i].game_slot;
                const resort::openhome::OpenHomePokemonPayload& pl = batch_result.pushes[i].payload;
                if (!pl.serialized_identity_or_ohpkm.empty()) {
                    item.updated_payload = pl;
                }
                placement_items.push_back(std::move(item));
            }
            resort_service_->applyOpenHomePushToGamePlacementsBatch(
                kDefaultResortProfileId,
                bridge_import_source_game_,
                save_path_override,
                placement_items);
        }
        for (std::size_t i = 0; i < push_entries.size(); ++i) {
            const PushEntry& pe = push_entries[i];
            std::cerr << kTempTransferLog
                      << " OpenHome batch-push openhomeId=" << pe.openhome_id
                      << " pkrid=" << pe.resort_pkrid
                      << " game_box=" << pe.game_box
                      << " game_slot=" << pe.game_slot << '\n';
        }
    }

    if (!journal_pulls.empty()) {
        (void)writeOpenHomeJournal(save_directory_, transfer_selection_.source_path, journal_pulls);
    }
    if (trace_save_perf) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - perf_wall_start);
        std::cerr << kTempTransferLog << " save_perf_openhome total_ms=" << elapsed.count()
                  << " openhome_processes=" << openhome_processes
                  << " openhome_batch_pull_size=" << openhome_batch_pull_size
                  << " openhome_batch_push_size=" << openhome_batch_push_size
                  << " placement_transactions=" << (push_entries.empty() ? 0 : 1) << '\n';
    }

    return true;
}

} // namespace pr::transfer::application
