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

} // namespace pr
