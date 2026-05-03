#pragma once

#include "resort/domain/ExportedPokemon.hpp"
#include "resort/persistence/BoxRepository.hpp"
#include "resort/persistence/HistoryRepository.hpp"
#include "resort/persistence/PidTransportRegistryRepository.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SnapshotRepository.hpp"
#include "resort/persistence/SqliteConnection.hpp"
#include "resort/services/MirrorSessionService.hpp"

namespace pr::resort {

class MirrorProjectionService;

class PokemonExportService {
public:
    PokemonExportService(
        SqliteConnection& connection,
        PokemonRepository& pokemon,
        BoxRepository& boxes,
        SnapshotRepository& snapshots,
        HistoryRepository& history,
        MirrorSessionService& mirror_sessions,
        MirrorProjectionService& projection,
        PidTransportRegistryRepository& pid_transport);

    ExportResult exportPokemon(const std::string& pkrid, const ExportContext& context);
    ExportResult commitPreparedMirrorExport(
        const std::string& pkrid,
        const ExportContext& context,
        const std::vector<unsigned char>& raw_payload,
        const std::string& raw_hash,
        const std::string& format_name,
        std::optional<std::uint32_t> transport_pid);

private:
    SqliteConnection& connection_;
    PokemonRepository& pokemon_;
    BoxRepository& boxes_;
    SnapshotRepository& snapshots_;
    HistoryRepository& history_;
    MirrorSessionService& mirror_sessions_;
    MirrorProjectionService& projection_;
    PidTransportRegistryRepository& pid_transport_;
};

} // namespace pr::resort
