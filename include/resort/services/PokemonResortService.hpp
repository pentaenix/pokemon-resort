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
#include "resort/services/MirrorProjectionService.hpp"
#include "resort/services/MirrorSessionService.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pr::resort {

struct RecoveryResult {
    bool success = false;
    std::string error;
    BoxLocation location;
    bool already_boxed = false;
    bool closed_active_mirror = false;
};

struct ResetProfileResult {
    bool success = false;
    std::string error;
    std::string backup_path;
};

class PokemonResortService {
public:
    explicit PokemonResortService(const std::filesystem::path& profile_path);
    ~PokemonResortService();

    PokemonResortService(const PokemonResortService&) = delete;
    PokemonResortService& operator=(const PokemonResortService&) = delete;

    /// Idempotently inserts default empty box/slot rows if missing (`INSERT OR IGNORE`). Schema creation runs in the constructor via migrations.
    void ensureProfile(const std::string& profile_id);

    ImportResult importParsedPokemon(const ImportedPokemon& imported, const ImportContext& context);
    ExportResult exportPokemon(const std::string& pkrid, const ExportContext& context);

    std::optional<ResortPokemon> getPokemonById(const std::string& pkrid) const;
    bool pokemonExists(const std::string& pkrid) const;
    std::optional<PokemonSnapshot> getLatestRawSnapshotForPokemon(
        const std::string& pkrid,
        std::optional<std::uint16_t> game_id = std::nullopt,
        const std::string& format_name = {}) const;
    std::optional<PokemonSnapshot> prepareLatestRawSnapshotForGameWrite(
        const std::string& pkrid,
        std::optional<std::uint16_t> game_id = std::nullopt,
        const std::string& format_name = {},
        const std::string& bridge_project_root = {},
        const char* bridge_argv0 = nullptr,
        const std::filesystem::path& bridge_project_request_path = {});
    std::vector<PokemonSlotView> getBoxSlotViews(const std::string& profile_id, int box_id) const;
    std::optional<BoxLocation> getPokemonLocation(const std::string& profile_id, const std::string& pkrid) const;

    std::vector<std::pair<int, std::string>> listProfileBoxes(const std::string& profile_id) const;

    /// Updates `box_slots` for one Pokémon (clears prior slot rows via `BoxRepository::placePokemon`).
    void movePokemonToSlot(const BoxLocation& destination, const std::string& pkrid, BoxPlacementPolicy policy);

    /// Emergency recovery: places an existing canonical Pokemon in the first empty Resort slot.
    /// Never overwrites another slot occupant.
    RecoveryResult recoverPokemonToFirstAvailableSlot(
        const std::string& profile_id,
        const std::string& pkrid);

    /// DANGEROUS: wipes all Resort canonical Pokemon, snapshots, history, mirrors, and slot placements.
    /// Keeps the box headers for the profile and leaves the profile in a "fresh" state (empty slots).
    /// If `backup_path` is non-empty, copies the SQLite file before modifying it.
    ResetProfileResult resetProfileToEmpty(
        const std::string& profile_id,
        const std::string& backup_path = {});

    /// Swaps all slot occupants between two Resort PC boxes (same profile).
    void swapResortBoxContents(const std::string& profile_id, int box_a, int box_b);

    void renameResortBox(const std::string& profile_id, int box_id, const std::string& name);

    void swapResortSlotContents(const BoxLocation& a, const BoxLocation& b);
    MirrorSession openMirrorSession(
        const std::string& pkrid,
        std::uint16_t target_game,
        const MirrorOpenContext& context);
    std::optional<MirrorSession> getMirrorSession(const std::string& mirror_session_id) const;
    std::optional<MirrorSession> getActiveMirrorForPokemon(const std::string& pkrid) const;
    void closeMirrorSessionReturned(const std::string& mirror_session_id);

private:
    std::filesystem::path profile_path_;
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
    std::unique_ptr<MirrorProjectionService> projection_;
    std::unique_ptr<PokemonExportService> exports_;
};

} // namespace pr::resort
