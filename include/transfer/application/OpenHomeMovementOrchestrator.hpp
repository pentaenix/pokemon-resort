#pragma once

#include "core/domain/PcSlotSpecies.hpp"
#include "resort/openhome/OpenHomePokemonPayload.hpp"
#include "ui/TransferSaveSelection.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pr::resort {
class PokemonResortService;
}

namespace pr::transfer::application {

struct PendingOpenHomePullInput {
    int source_box = -1;
    int source_slot = -1;
    std::string raw_hash;
};

struct PendingOpenHomeImportPayloadOutput {
    int resort_box = -1;
    int resort_slot = -1;
    int home_bank = 0;
    int home_box = 0;
    int home_slot = 0;
    resort::openhome::OpenHomePokemonPayload payload;
};

class OpenHomeMovementOrchestrator {
public:
    OpenHomeMovementOrchestrator(
        const std::string& project_root,
        const std::string& save_directory,
        const TransferSaveSelection& transfer_selection,
        std::optional<std::uint16_t> bridge_import_source_game,
        resort::PokemonResortService* resort_service);

    bool commitPendingOpenHomeMovementBeforeSave(
        const std::string& save_path_override,
        const std::string& explicit_save_type,
        std::vector<PendingOpenHomePullInput>& pending_openhome_pulls,
        std::vector<TransferSaveSelection::PcBox>& resort_pc_boxes,
        std::vector<TransferSaveSelection::PcBox>& game_pc_boxes,
        std::vector<PendingOpenHomeImportPayloadOutput>& pending_openhome_import_payloads) const;

private:
    std::string project_root_;
    std::string save_directory_;
    const TransferSaveSelection& transfer_selection_;
    std::optional<std::uint16_t> bridge_import_source_game_;
    resort::PokemonResortService* resort_service_;
};

} // namespace pr::transfer::application
