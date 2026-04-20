#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/services/PokemonResortService.hpp"

#include <string>
#include <vector>

namespace pr::resort {

struct BridgeSaveImportOptions {
    std::string profile_id = "default";
    int start_box = 0;
    int start_slot = 0;
    BoxPlacementPolicy placement_policy = BoxPlacementPolicy::RejectIfOccupied;
    std::size_t max_pokemon = 0; // 0 means import every Pokemon emitted by the bridge.
};

struct BridgeSaveImportResult {
    bool success = false;
    std::size_t imported_count = 0;
    std::vector<ImportResult> imports;
    std::string bridge_command;
    std::string error;
};

class BridgeImportService {
public:
    BridgeImportService(
        PokemonResortService& resort_service,
        std::string project_root,
        const char* argv0);

    BridgeSaveImportResult importSave(
        const std::string& save_path,
        const BridgeSaveImportOptions& options);

private:
    PokemonResortService& resort_service_;
    std::string project_root_;
    const char* argv0_ = nullptr;
};

} // namespace pr::resort
