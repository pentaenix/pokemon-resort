#pragma once

#include "resort/domain/ImportedPokemon.hpp"

#include <string>
#include <vector>

namespace pr::resort {

struct BridgeImportParseResult {
    bool success = false;
    std::vector<ImportedPokemon> pokemon;
    std::string error;
};

/// Parses the native-side contract for import-grade bridge output.
/// This deliberately rejects transfer-ticket summaries because canonical imports require exact raw payload bytes.
BridgeImportParseResult parseBridgeImportPayload(const std::string& json_text);

} // namespace pr::resort
