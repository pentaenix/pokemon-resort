#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/ResortTypes.hpp"

#include <string>
#include <vector>

namespace pr::resort {

/// Heuristic conflict flags for a mirror return or merge-from-import (see
/// `docs/transfer_system/MIRROR_PROJECTION_ARCHITECTURE.md` — suspicious changes).
struct ImportConflictReport {
    std::vector<std::string> flags;
    bool quarantine_recommended = false;
};

/// Pure policy helper: inspect incoming import evidence against canonical state **before** merge.
class MirrorReturnAnalysis {
public:
    static ImportConflictReport analyzePreMerge(
        const ResortPokemon& canonical,
        const ImportedPokemon& incoming);
};

} // namespace pr::resort
