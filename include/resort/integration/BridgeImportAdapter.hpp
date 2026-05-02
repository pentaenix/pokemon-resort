#pragma once

#include "resort/domain/ImportedPokemon.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pr {
struct PcSlotSpecies;
}

namespace pr::resort {

struct BridgeImportParseResult {
    bool success = false;
    std::vector<ImportedPokemon> pokemon;
    std::string error;
};

/// Parses the native-side contract for import-grade bridge output.
/// This deliberately rejects transfer-ticket summaries because canonical imports require exact raw payload bytes.
BridgeImportParseResult parseBridgeImportPayload(const std::string& json_text);

/// Builds an `ImportedPokemon` from a probed game PC slot plus merged bridge bytes (`bridge_box_payload_*`).
/// Returns empty when merge/import data is missing or invalid.
std::optional<ImportedPokemon> importedPokemonFromGamePcSlot(const PcSlotSpecies& slot, std::uint16_t source_game);

} // namespace pr::resort
