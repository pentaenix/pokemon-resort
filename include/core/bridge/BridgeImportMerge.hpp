#pragma once

#include "ui/TransferSaveSelection.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pr {

/// Attaches `bridge_box_payload_*` from a successful `importSaveWithBridge` stdout payload to the matching
/// PC slots (`source_location.area == "box"`). Does not modify party or Resort slots.
bool mergeBridgeImportIntoGamePcBoxes(
    const std::string& bridge_import_stdout_json,
    std::vector<TransferSaveSelection::PcBox>& pc_boxes,
    std::string* error_message = nullptr);

/// Reads `source_game` from the first entry in `pokemon` (bridge import schema 1).
bool parseBridgeImportFirstPokemonSourceGame(
    const std::string& bridge_import_stdout_json,
    std::uint16_t* out_source_game,
    std::string* error_message = nullptr);

/// Reads `format_name` from the first entry (PKHeX entity type lowercased, e.g. `pk4`). Matches the save's
/// native encrypted PKM layout for PC writes — use this for cross-gen projection targets instead of `PcSlotSpecies::format`
/// when mirror slots still carry a Pokémon's source encoding (e.g. `pk3`).
bool parseBridgeImportFirstPokemonFormatName(
    const std::string& bridge_import_stdout_json,
    std::string* out_format_name,
    std::string* error_message = nullptr);

} // namespace pr
