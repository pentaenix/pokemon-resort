#include "transfer/application/TransferCommitOrchestrator.hpp"

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"
#include "resort/services/PokemonResortService.hpp"

#include <algorithm>
#include <iostream>

namespace pr::transfer::application {

namespace {
constexpr const char* kDefaultResortProfileId = "default";
constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";
}

TransferCommitOrchestrator::TransferCommitOrchestrator(resort::PokemonResortService& resort_service)
    : resort_service_(resort_service) {}

bool TransferCommitOrchestrator::commitGameToResortImportsBeforeSave(
    std::vector<TransferSaveSelection::PcBox>& resort_pc_boxes,
    std::optional<std::uint16_t> bridge_import_source_game,
    const std::vector<PendingOpenHomeImportPayload>& pending_openhome_import_payloads) const {
    if (!bridge_import_source_game.has_value()) {
        std::cerr << "Warning: cannot commit Game->Resort imports: missing bridge import source_game\n";
        return false;
    }

    int imported_count = 0;
    for (std::size_t bi = 0; bi < resort_pc_boxes.size(); ++bi) {
        auto& slots = resort_pc_boxes[bi].slots;
        for (std::size_t si = 0; si < slots.size(); ++si) {
            PcSlotSpecies& slot = slots[si];
            if (!slot.occupied() || !slot.resort_pkrid.empty()) {
                continue;
            }
            const std::optional<resort::ImportedPokemon> imported =
                resort::importedPokemonFromGamePcSlot(slot, *bridge_import_source_game);
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
            const resort::ImportResult result = resort_service_.importParsedPokemon(*imported, ctx);
            if (!result.success) {
                std::cerr << "Warning: could not pre-commit Game->Resort import before save write: "
                          << result.error << '\n';
                return false;
            }
            slot.resort_pkrid = result.pkrid;
            if (resort::openhome::isValidOpenHomeId(slot.home_tracker)) {
                auto payload_it = std::find_if(
                    pending_openhome_import_payloads.begin(),
                    pending_openhome_import_payloads.end(),
                    [&](const PendingOpenHomeImportPayload& pending) {
                        return pending.resort_box == static_cast<int>(bi) &&
                               pending.resort_slot == static_cast<int>(si) &&
                               pending.payload.openhome_id == slot.home_tracker;
                    });
                if (payload_it != pending_openhome_import_payloads.end() &&
                    !payload_it->payload.serialized_identity_or_ohpkm.empty()) {
                    resort_service_.linkOpenHomePayloadToPokemon(result.pkrid, payload_it->payload);
                    resort_service_.recordPokemonHomePlacement(
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

bool TransferCommitOrchestrator::commitResortStorageChangesAfterSave(
    const std::vector<TransferSaveSelection::PcBox>& resort_pc_boxes,
    std::optional<std::uint16_t> bridge_import_source_game,
    std::vector<PendingPreparedMirrorExport>& pending_prepared_mirror_exports) const {
    if (!bridge_import_source_game.has_value()) {
        std::cerr << "Warning: cannot commit Resort changes: missing bridge import source_game\n";
        return false;
    }

    for (const auto& pending : pending_prepared_mirror_exports) {
        const resort::ExportResult exported = resort_service_.commitPreparedMirrorExport(
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

    for (std::size_t bi = 0; bi < resort_pc_boxes.size(); ++bi) {
        const auto& slots = resort_pc_boxes[bi].slots;
        for (std::size_t si = 0; si < slots.size(); ++si) {
            const PcSlotSpecies& slot = slots[si];
            if (!slot.occupied() || !slot.resort_pkrid.empty()) {
                continue;
            }
            std::cerr << "Warning: refusing to finish save with uncommitted Game->Resort Pokemon at Resort box "
                      << bi << " slot " << si << '\n';
            return false;
        }
    }

    for (std::size_t bi = 0; bi < resort_pc_boxes.size(); ++bi) {
        resort_service_.renameResortBox(kDefaultResortProfileId, static_cast<int>(bi), resort_pc_boxes[bi].name);
    }

    for (std::size_t bi = 0; bi < resort_pc_boxes.size(); ++bi) {
        const auto& slots = resort_pc_boxes[bi].slots;
        for (std::size_t si = 0; si < slots.size(); ++si) {
            const PcSlotSpecies& slot = slots[si];
            if (!slot.occupied() || slot.resort_pkrid.empty()) {
                continue;
            }
            resort_service_.movePokemonToSlot(
                resort::BoxLocation{kDefaultResortProfileId, static_cast<int>(bi), static_cast<int>(si)},
                slot.resort_pkrid,
                resort::BoxPlacementPolicy::ReplaceOccupied);
        }
    }
    std::cerr << kTempTransferLog << " Save commit Resort storage complete\n";
    pending_prepared_mirror_exports.clear();
    return true;
}

} // namespace pr::transfer::application
