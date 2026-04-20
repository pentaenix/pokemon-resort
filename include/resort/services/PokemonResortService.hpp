#pragma once

#include "resort/domain/ImportedPokemon.hpp"
#include "resort/domain/ExportedPokemon.hpp"
#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/BoxRepository.hpp"
#include "resort/persistence/HistoryRepository.hpp"
#include "resort/persistence/MirrorSessionRepository.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SnapshotRepository.hpp"
#include "resort/persistence/SqliteConnection.hpp"
#include "resort/services/BoxViewService.hpp"
#include "resort/services/PokemonImportService.hpp"
#include "resort/services/PokemonExportService.hpp"
#include "resort/services/PokemonMatcher.hpp"
#include "resort/services/PokemonMergeService.hpp"
#include "resort/services/MirrorSessionService.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pr::resort {

std::filesystem::path defaultResortProfilePath(const std::filesystem::path& app_save_directory);

class PokemonResortService {
public:
    explicit PokemonResortService(const std::filesystem::path& profile_path);
    ~PokemonResortService();

    PokemonResortService(const PokemonResortService&) = delete;
    PokemonResortService& operator=(const PokemonResortService&) = delete;

    void ensureProfile(const std::string& profile_id);

    ImportResult importParsedPokemon(const ImportedPokemon& imported, const ImportContext& context);
    ExportResult exportPokemon(const std::string& pkrid, const ExportContext& context);

    std::optional<ResortPokemon> getPokemonById(const std::string& pkrid) const;
    bool pokemonExists(const std::string& pkrid) const;
    std::vector<PokemonSlotView> getBoxSlotViews(const std::string& profile_id, int box_id) const;
    std::optional<BoxLocation> getPokemonLocation(const std::string& profile_id, const std::string& pkrid) const;
    MirrorSession openMirrorSession(
        const std::string& pkrid,
        std::uint16_t target_game,
        const MirrorOpenContext& context);
    std::optional<MirrorSession> getMirrorSession(const std::string& mirror_session_id) const;
    std::optional<MirrorSession> getActiveMirrorForPokemon(const std::string& pkrid) const;
    void closeMirrorSessionReturned(const std::string& mirror_session_id);

private:
    std::unique_ptr<SqliteConnection> connection_;
    std::unique_ptr<PokemonRepository> pokemon_;
    std::unique_ptr<BoxRepository> boxes_;
    std::unique_ptr<SnapshotRepository> snapshots_;
    std::unique_ptr<HistoryRepository> history_;
    std::unique_ptr<MirrorSessionRepository> mirrors_;
    std::unique_ptr<BoxViewService> box_views_;
    std::unique_ptr<PokemonMatcher> matcher_;
    std::unique_ptr<PokemonMergeService> merge_;
    std::unique_ptr<MirrorSessionService> mirror_sessions_;
    std::unique_ptr<PokemonImportService> imports_;
    std::unique_ptr<PokemonExportService> exports_;
};

} // namespace pr::resort
