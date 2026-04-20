#pragma once

#include "resort/domain/ExportedPokemon.hpp"
#include "resort/persistence/HistoryRepository.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SnapshotRepository.hpp"
#include "resort/persistence/SqliteConnection.hpp"
#include "resort/services/MirrorSessionService.hpp"

namespace pr::resort {

class PokemonExportService {
public:
    PokemonExportService(
        SqliteConnection& connection,
        PokemonRepository& pokemon,
        SnapshotRepository& snapshots,
        HistoryRepository& history,
        MirrorSessionService& mirror_sessions);

    ExportResult exportPokemon(const std::string& pkrid, const ExportContext& context);

private:
    SqliteConnection& connection_;
    PokemonRepository& pokemon_;
    SnapshotRepository& snapshots_;
    HistoryRepository& history_;
    MirrorSessionService& mirror_sessions_;
};

} // namespace pr::resort
