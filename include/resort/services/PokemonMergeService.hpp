#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/MatchMerge.hpp"
#include "resort/domain/ResortTypes.hpp"

namespace pr::resort {

class PokemonMergeService {
public:
    PokemonMergeResult mergeImported(
        ResortPokemon& canonical,
        const ImportedPokemon& imported,
        long long updated_at_unix) const;
};

} // namespace pr::resort
