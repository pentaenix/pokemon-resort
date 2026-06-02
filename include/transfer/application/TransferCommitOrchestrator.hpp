#pragma once

#include "resort/domain/ExportedPokemon.hpp"
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

struct PendingPreparedMirrorExport {
    std::string pkrid;
    resort::ExportContext context;
    std::vector<unsigned char> raw_payload;
    std::string raw_hash;
    std::string format_name;
    std::optional<std::uint32_t> transport_pid;
};

struct PendingOpenHomeImportPayload {
    int resort_box = -1;
    int resort_slot = -1;
    int home_bank = 0;
    int home_box = 0;
    int home_slot = 0;
    resort::openhome::OpenHomePokemonPayload payload;
};

class TransferCommitOrchestrator {
public:
    explicit TransferCommitOrchestrator(resort::PokemonResortService& resort_service);

    bool commitGameToResortImportsBeforeSave(
        std::vector<TransferSaveSelection::PcBox>& resort_pc_boxes,
        std::optional<std::uint16_t> bridge_import_source_game,
        const std::vector<PendingOpenHomeImportPayload>& pending_openhome_import_payloads) const;

    bool commitResortStorageChangesAfterSave(
        const std::vector<TransferSaveSelection::PcBox>& resort_pc_boxes,
        std::optional<std::uint16_t> bridge_import_source_game,
        std::vector<PendingPreparedMirrorExport>& pending_prepared_mirror_exports) const;

private:
    resort::PokemonResortService& resort_service_;
};

} // namespace pr::transfer::application
