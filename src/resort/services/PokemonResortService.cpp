#include "resort/services/PokemonResortService.hpp"

#include "resort/domain/Ids.hpp"
#include "resort/persistence/Migrations.hpp"

namespace pr::resort {

std::filesystem::path defaultResortProfilePath(const std::filesystem::path& app_save_directory) {
    return app_save_directory / "profile.resort.db";
}

PokemonResortService::PokemonResortService(const std::filesystem::path& profile_path)
    : connection_(std::make_unique<SqliteConnection>(profile_path)) {
    runResortMigrations(*connection_);
    pokemon_ = std::make_unique<PokemonRepository>(*connection_);
    boxes_ = std::make_unique<BoxRepository>(*connection_);
    snapshots_ = std::make_unique<SnapshotRepository>(*connection_);
    history_ = std::make_unique<HistoryRepository>(*connection_);
    mirrors_ = std::make_unique<MirrorSessionRepository>(*connection_);
    box_views_ = std::make_unique<BoxViewService>(*boxes_);
    matcher_ = std::make_unique<PokemonMatcher>(*pokemon_, *mirrors_);
    merge_ = std::make_unique<PokemonMergeService>();
    mirror_sessions_ = std::make_unique<MirrorSessionService>(
        *connection_,
        *pokemon_,
        *mirrors_,
        *history_);
    imports_ = std::make_unique<PokemonImportService>(
        *connection_,
        *pokemon_,
        *boxes_,
        *snapshots_,
        *history_,
        *matcher_,
        *merge_,
        *mirror_sessions_);
    exports_ = std::make_unique<PokemonExportService>(
        *connection_,
        *pokemon_,
        *snapshots_,
        *history_,
        *mirror_sessions_);
}

PokemonResortService::~PokemonResortService() = default;

void PokemonResortService::ensureProfile(const std::string& profile_id) {
    SqliteTransaction tx(*connection_);
    boxes_->ensureDefaultBoxes(profile_id);
    tx.commit();
}

ImportResult PokemonResortService::importParsedPokemon(
    const ImportedPokemon& imported,
    const ImportContext& context) {
    ensureProfile(context.profile_id);
    return imports_->importParsedPokemon(imported, context);
}

ExportResult PokemonResortService::exportPokemon(
    const std::string& pkrid,
    const ExportContext& context) {
    return exports_->exportPokemon(pkrid, context);
}

std::optional<ResortPokemon> PokemonResortService::getPokemonById(const std::string& pkrid) const {
    return pokemon_->findById(pkrid);
}

bool PokemonResortService::pokemonExists(const std::string& pkrid) const {
    return pokemon_->exists(pkrid);
}

std::vector<PokemonSlotView> PokemonResortService::getBoxSlotViews(
    const std::string& profile_id,
    int box_id) const {
    return box_views_->getBoxSlotViews(profile_id, box_id);
}

std::optional<BoxLocation> PokemonResortService::getPokemonLocation(
    const std::string& profile_id,
    const std::string& pkrid) const {
    return boxes_->findPokemonLocation(profile_id, pkrid);
}

MirrorSession PokemonResortService::openMirrorSession(
    const std::string& pkrid,
    std::uint16_t target_game,
    const MirrorOpenContext& context) {
    SqliteTransaction tx(*connection_);
    MirrorSession session = mirror_sessions_->openMirrorSession(pkrid, target_game, context);
    tx.commit();
    return session;
}

std::optional<MirrorSession> PokemonResortService::getMirrorSession(
    const std::string& mirror_session_id) const {
    return mirror_sessions_->getById(mirror_session_id);
}

std::optional<MirrorSession> PokemonResortService::getActiveMirrorForPokemon(
    const std::string& pkrid) const {
    return mirror_sessions_->getActiveForPokemon(pkrid);
}

void PokemonResortService::closeMirrorSessionReturned(const std::string& mirror_session_id) {
    SqliteTransaction tx(*connection_);
    mirror_sessions_->closeReturned(mirror_session_id, unixNow());
    tx.commit();
}

} // namespace pr::resort
