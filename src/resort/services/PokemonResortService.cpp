#include "resort/services/PokemonResortService.hpp"

#include "core/crypto/Sha256.hpp"
#include "resort/domain/Ids.hpp"
#include "resort/integration/Gen12DvBytes.hpp"
#include "resort/persistence/Migrations.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <filesystem>
#include <iostream>
#include <random>

namespace pr::resort {

namespace {

constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";

thread_local std::mt19937 kGen12PrepareWriteDvRng{std::random_device{}()};

} // namespace

PokemonResortService::PokemonResortService(const std::filesystem::path& profile_path)
    : profile_path_(profile_path),
      connection_(std::make_unique<SqliteConnection>(profile_path)) {
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
        *boxes_,
        *snapshots_,
        *history_,
        *mirror_sessions_);
}

PokemonResortService::~PokemonResortService() = default;

ResetProfileResult PokemonResortService::resetProfileToEmpty(
    const std::string& profile_id,
    const std::string& backup_path) {
    ResetProfileResult result;
    ensureProfile(profile_id);

    if (!backup_path.empty()) {
        try {
            std::filesystem::copy_file(
                profile_path_,
                backup_path,
                std::filesystem::copy_options::overwrite_existing);
            result.backup_path = backup_path;
        } catch (const std::exception& ex) {
            result.error = std::string("Failed to create backup: ") + ex.what();
            return result;
        }
    }

    try {
        SqliteTransaction tx(*connection_);
        // Clear placements first so UI comes up empty even if a later delete fails.
        auto clear_slots = connection_->prepare("UPDATE box_slots SET pkrid = NULL WHERE profile_id = ?");
        clear_slots.bindText(1, profile_id);
        clear_slots.stepDone();

        // Delete all canonical Pokemon. Cascades remove snapshots/history/mirrors; box_slots FK is SET NULL.
        // Resort currently stores one canonical universe per DB file, so wiping all Pokemon is expected.
        connection_->exec("DELETE FROM pokemon");

        // Ensure the profile has an empty box lattice (idempotent).
        boxes_->ensureDefaultBoxes(profile_id);
        tx.commit();
        result.success = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        return result;
    }
}

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

std::optional<PokemonSnapshot> PokemonResortService::getLatestRawSnapshotForPokemon(
    const std::string& pkrid,
    std::optional<std::uint16_t> game_id,
    const std::string& format_name) const {
    return snapshots_->findLatestRawForPokemon(pkrid, game_id, format_name);
}

std::optional<PokemonSnapshot> PokemonResortService::prepareLatestRawSnapshotForGameWrite(
    const std::string& pkrid,
    std::optional<std::uint16_t> game_id,
    const std::string& format_name) {
    auto snapshot = snapshots_->findLatestRawForPokemon(pkrid, game_id, format_name);
    if (!snapshot || snapshot->raw_bytes.empty() || !isGen12StorageFormat(snapshot->format_name)) {
        return snapshot;
    }

    const auto raw_dv = readPk12Dv16FromRaw(snapshot->raw_bytes, snapshot->format_name);
    if (!raw_dv) {
        return snapshot;
    }

    auto canonical = pokemon_->findById(pkrid);
    if (!canonical) {
        return snapshot;
    }

    if (*raw_dv != 0 && canonical->hot.dv16 && *canonical->hot.dv16 != 0) {
        return snapshot;
    }

    std::optional<std::uint16_t> use_dv;
    if (canonical->hot.dv16 && *canonical->hot.dv16 != 0) {
        use_dv = *canonical->hot.dv16;
    } else if (*raw_dv != 0) {
        use_dv = *raw_dv;
    } else {
        use_dv = randomNonZeroGen12Dv16(kGen12PrepareWriteDvRng);
    }

    bool raw_patched = false;
    if (*raw_dv == 0) {
        if (!patchPk12DvBytes(snapshot->raw_bytes, snapshot->format_name, *use_dv)) {
            std::cerr << "Warning: failed to patch Gen 1/2 DV bytes before save write pkrid=" << pkrid << '\n';
            return snapshot;
        }
        snapshot->raw_hash_sha256 = pr::sha256HexLowercase(snapshot->raw_bytes);
        raw_patched = true;
    }

    bool canonical_updated = false;
    if (!canonical->hot.dv16 || *canonical->hot.dv16 == 0) {
        canonical->hot.dv16 = *use_dv;
        canonical->revision += 1;
        canonical->updated_at_unix = unixNow();
        canonical_updated = true;
    }

    if (!raw_patched && !canonical_updated) {
        return snapshot;
    }

    SqliteTransaction tx(*connection_);
    if (canonical_updated) {
        pokemon_->updateAfterMerge(*canonical);
        std::cerr << kTempTransferLog << " Save prepare persisted Gen12 DV repair to canonical "
                  << formatGen12Dv16ForLog(*use_dv)
                  << " pkrid=" << pkrid << '\n';
    }
    if (raw_patched) {
        PokemonSnapshot patched = *snapshot;
        patched.snapshot_id = generateId("snap");
        patched.kind = SnapshotKind::CanonicalCheckpoint;
        patched.captured_at_unix = unixNow();
        patched.notes_json = "{\"schema_version\":1,\"reason\":\"gen12_dv_repair_before_game_write\"}";
        snapshots_->insert(patched);
        snapshot = patched;
        std::cerr << kTempTransferLog << " Save prepare patched Gen12 DV raw payload for write "
                  << formatGen12Dv16ForLog(*use_dv)
                  << " snapshot_id=" << snapshot->snapshot_id
                  << " pkrid=" << pkrid << '\n';
    }
    tx.commit();

    return snapshot;
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

std::vector<std::pair<int, std::string>> PokemonResortService::listProfileBoxes(const std::string& profile_id) const {
    return boxes_->listBoxes(profile_id);
}

void PokemonResortService::movePokemonToSlot(
    const BoxLocation& destination,
    const std::string& pkrid,
    BoxPlacementPolicy policy) {
    SqliteTransaction tx(*connection_);
    boxes_->placePokemon(destination, pkrid, policy);
    tx.commit();
}

RecoveryResult PokemonResortService::recoverPokemonToFirstAvailableSlot(
    const std::string& profile_id,
    const std::string& pkrid) {
    RecoveryResult result;
    if (!pokemon_->exists(pkrid)) {
        result.error = "Cannot recover Pokemon: pkrid does not exist";
        return result;
    }

    ensureProfile(profile_id);
    SqliteTransaction tx(*connection_);
    if (const auto current = boxes_->findPokemonLocation(profile_id, pkrid)) {
        result.success = true;
        result.location = *current;
        result.already_boxed = true;
        tx.commit();
        return result;
    }

    const auto destination = boxes_->findFirstEmptySlot(profile_id);
    if (!destination) {
        result.error = "Cannot recover Pokemon: no empty Resort box slots";
        return result;
    }

    boxes_->placePokemon(*destination, pkrid, BoxPlacementPolicy::RejectIfOccupied);
    if (const auto active = mirror_sessions_->getActiveForPokemon(pkrid)) {
        mirror_sessions_->closeReturned(active->mirror_session_id, unixNow());
        result.closed_active_mirror = true;
    }

    PokemonHistoryEvent event;
    event.event_id = generateId("hist");
    event.pkrid = pkrid;
    event.event_type = HistoryEventType::MovedBox;
    event.timestamp_unix = unixNow();
    event.diff_json = "{\"reason\":\"emergency_recover\",\"profile_id\":\"" + profile_id +
        "\",\"box_id\":" + std::to_string(destination->box_id) +
        ",\"slot_index\":" + std::to_string(destination->slot_index) + "}";
    history_->insert(event);

    result.success = true;
    result.location = *destination;
    tx.commit();
    return result;
}

void PokemonResortService::swapResortBoxContents(const std::string& profile_id, int box_a, int box_b) {
    SqliteTransaction tx(*connection_);
    boxes_->swapBoxContents(profile_id, box_a, box_b);
    tx.commit();
}

void PokemonResortService::renameResortBox(const std::string& profile_id, int box_id, const std::string& name) {
    SqliteTransaction tx(*connection_);
    boxes_->renameBox(profile_id, box_id, name);
    tx.commit();
}

void PokemonResortService::swapResortSlotContents(const BoxLocation& a, const BoxLocation& b) {
    SqliteTransaction tx(*connection_);
    boxes_->swapSlotContents(a, b);
    tx.commit();
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
