#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/persistence/BoxRepository.hpp"
#include "resort/persistence/HistoryRepository.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SnapshotRepository.hpp"
#include "resort/persistence/SqliteConnection.hpp"
#include "resort/services/PokemonMatcher.hpp"
#include "resort/services/PokemonMergeService.hpp"
#include "resort/services/MirrorSessionService.hpp"

namespace pr::resort {

class PokemonImportService {
public:
    PokemonImportService(
        SqliteConnection& connection,
        PokemonRepository& pokemon,
        BoxRepository& boxes,
        SnapshotRepository& snapshots,
        HistoryRepository& history,
        PokemonMatcher& matcher,
        PokemonMergeService& merge,
        MirrorSessionService& mirror_sessions);

    ImportResult importParsedPokemon(const ImportedPokemon& imported, const ImportContext& context);

private:
    SqliteConnection& connection_;
    PokemonRepository& pokemon_;
    BoxRepository& boxes_;
    SnapshotRepository& snapshots_;
    HistoryRepository& history_;
    PokemonMatcher& matcher_;
    PokemonMergeService& merge_;
    MirrorSessionService& mirror_sessions_;
};

} // namespace pr::resort
