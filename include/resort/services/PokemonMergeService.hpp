#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/MatchMerge.hpp"
#include "resort/domain/ResortTypes.hpp"

namespace pr::resort {

/// `FullReplaceFromImport`: trust decoded PKM hot fields (first-time / non-mirror matched import).
/// `MirrorReturnGameplaySync`: Resort stays source of truth for identity & encounter provenance; apply only
/// progression from the external save (see `PokemonMergeFieldPolicy.hpp` for static vs mutable `PokemonHot`
/// fields and mirror warm JSON stripping).
enum class ImportMergeKind {
    FullReplaceFromImport,
    MirrorReturnGameplaySync,
};

class PokemonMergeService {
public:
    PokemonMergeResult mergeImported(
        ResortPokemon& canonical,
        const ImportedPokemon& imported,
        long long updated_at_unix,
        ImportMergeKind kind = ImportMergeKind::FullReplaceFromImport) const;
};

} // namespace pr::resort
