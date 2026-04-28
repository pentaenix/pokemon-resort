#pragma once

#include "ui/TransferSaveSelection.hpp"

#include <string>
#include <vector>

namespace pr {

/// Attaches `bridge_box_payload_*` from a successful `importSaveWithBridge` stdout payload to the matching
/// PC slots (`source_location.area == "box"`). Does not modify party or Resort slots.
bool mergeBridgeImportIntoGamePcBoxes(
    const std::string& bridge_import_stdout_json,
    std::vector<TransferSaveSelection::PcBox>& pc_boxes,
    std::string* error_message = nullptr);

} // namespace pr
