#include "resort/services/PokemonResortService.hpp"

#include "core/crypto/Sha256.hpp"
#include "resort/diagnostics/ResortTransferLog.hpp"
#include "resort/domain/Ids.hpp"
#include "resort/domain/PkmFormat.hpp"
#include "resort/integration/Gen12DvBytes.hpp"
#include "resort/services/MirrorProjectionService.hpp"
#include "resort/persistence/Migrations.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <string_view>

namespace pr::resort {

namespace {

constexpr const char* kTempTransferLog = "[TEMP_TRANSFER_LOG_DELETE]";

thread_local std::mt19937 kGen12PrepareWriteDvRng{std::random_device{}()};

std::string jsonEscapeMinimal(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string transferImportJsonLine(const ImportResult& r, const ImportContext& ctx, const ResortPokemon* canonical) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    std::ostringstream o;
    o << "{\"schema_version\":1,\"ts_ms\":" << ms << ",\"event\":\"import\""
      << ",\"profile_id\":\"" << jsonEscapeMinimal(ctx.profile_id) << "\""
      << ",\"success\":true"
      << ",\"pkrid\":\"" << jsonEscapeMinimal(r.pkrid) << "\""
      << ",\"snapshot_id\":\"" << jsonEscapeMinimal(r.snapshot_id) << "\""
      << ",\"created\":" << (r.created ? "true" : "false") << ",\"merged\":" << (r.merged ? "true" : "false")
      << ",\"match_reason\":\"" << jsonEscapeMinimal(r.match_reason) << "\"";
    if (canonical) {
        o << ",\"canonical_shiny\":" << (canonical->hot.shiny ? "true" : "false")
          << ",\"canonical_species_id\":" << canonical->hot.species_id
          << ",\"canonical_origin_game\":" << canonical->hot.origin_game;
    }
    o << "}";
    return o.str();
}

std::string transferBridgeProjectJsonLine(
    const std::string& pkrid,
    const MirrorProjectDecodedResult& projected) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    std::ostringstream o;
    o << "{\"schema_version\":1,\"ts_ms\":" << ms << ",\"event\":\"bridge_project\""
      << ",\"pkrid\":\"" << jsonEscapeMinimal(pkrid) << "\""
      << ",\"source_snapshot_id\":\"" << jsonEscapeMinimal(projected.source_snapshot_id) << "\""
      << ",\"source_format\":\"" << jsonEscapeMinimal(projected.source_format_name) << "\""
      << ",\"target_format\":\"" << jsonEscapeMinimal(projected.target_format_name) << "\""
      << ",\"lossy\":" << (projected.bridge_lossy ? "true" : "false")
      << ",\"lost_categories\":[";
    for (std::size_t i = 0; i < projected.bridge_lost_categories.size(); ++i) {
        if (i > 0) {
            o << ',';
        }
        o << '"' << jsonEscapeMinimal(projected.bridge_lost_categories[i]) << '"';
    }
    o << "],\"loss_notes\":[";
    for (std::size_t i = 0; i < projected.bridge_loss_notes.size() && i < 12; ++i) {
        if (i > 0) {
            o << ',';
        }
        o << '"' << jsonEscapeMinimal(projected.bridge_loss_notes[i]) << '"';
    }
    o << "]}";
    return o.str();
}

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
    openhome_links_ = std::make_unique<OpenHomeLinkRepository>(*connection_);
    pid_transport_ = std::make_unique<PidTransportRegistryRepository>(*connection_);
    box_views_ = std::make_unique<BoxViewService>(*boxes_);
    matcher_ = std::make_unique<PokemonMatcher>(*pokemon_, *mirrors_, *pid_transport_);
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
        *mirror_sessions_,
        *pid_transport_);
    projection_ = std::make_unique<MirrorProjectionService>(*pokemon_, *snapshots_);
    exports_ = std::make_unique<PokemonExportService>(
        *connection_,
        *pokemon_,
        *boxes_,
        *snapshots_,
        *history_,
        *mirror_sessions_,
        *projection_,
        *pid_transport_);
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
    ImportResult result = imports_->importParsedPokemon(imported, context);
    if (result.success) {
        const auto canonical = pokemon_->findById(result.pkrid);
        appendResortTransferJsonl(
            profile_path_,
            transferImportJsonLine(result, context, canonical ? &*canonical : nullptr));
    }
    return result;
}

ExportResult PokemonResortService::exportPokemon(
    const std::string& pkrid,
    const ExportContext& context) {
    return exports_->exportPokemon(pkrid, context);
}

ExportResult PokemonResortService::commitPreparedMirrorExport(
    const std::string& pkrid,
    const ExportContext& context,
    const std::vector<unsigned char>& raw_payload,
    const std::string& raw_hash,
    const std::string& format_name,
    std::optional<std::uint32_t> transport_pid) {
    return exports_->commitPreparedMirrorExport(
        pkrid,
        context,
        raw_payload,
        raw_hash,
        format_name,
        transport_pid);
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
    const std::string& format_name,
    const std::string& bridge_project_root,
    const char* bridge_argv0,
    const std::filesystem::path& bridge_project_request_path) {
    const std::string inferred_format = game_id ? pkmStorageFormatNameForGameId(*game_id) : std::string{};
    const std::string target_format = format_name.empty() ? inferred_format : format_name;
    // Start from the latest raw only as a fallback container. When a bridge is available below,
    // projection source selection intentionally prefers target-format snapshots, then the most advanced
    // available snapshot, and overlays current canonical Resort state as the payload goes out.
    auto snapshot = snapshots_->findLatestRawForPokemon(pkrid, std::nullopt, {});
    if (!snapshot || snapshot->raw_bytes.empty() || snapshot->raw_hash_sha256.empty()) {
        return std::nullopt;
    }

    if (!target_format.empty() &&
        (bridge_project_root.empty() || bridge_argv0 == nullptr || bridge_project_request_path.empty()) &&
        !pkmFormatNamesEqual(snapshot->format_name, target_format)) {
        if (auto same_format = snapshots_->findLatestRawForPokemon(pkrid, game_id, target_format)) {
            if (!same_format->raw_bytes.empty() && !same_format->raw_hash_sha256.empty()) {
                snapshot = std::move(same_format);
            } else {
                return std::nullopt;
            }
        } else if (auto same_format_any_game =
                       snapshots_->findLatestRawForPokemon(pkrid, std::nullopt, target_format)) {
            if (!same_format_any_game->raw_bytes.empty() && !same_format_any_game->raw_hash_sha256.empty()) {
                snapshot = std::move(same_format_any_game);
            } else {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    } else if (!target_format.empty() &&
               !bridge_project_root.empty() &&
               bridge_argv0 != nullptr &&
               !bridge_project_request_path.empty()) {
        MirrorBridgeProjectInput input;
        input.pkrid = pkrid;
        input.target_game = game_id.value_or(0);
        input.target_format_name = target_format;
        input.allow_lossy_projection = true;
        const MirrorProjectDecodedResult projected = projection_->projectLatestSnapshotToTargetDecoded(
            input, bridge_project_root, bridge_argv0, bridge_project_request_path);
        if (!projected.success) {
            std::cerr << "Warning: cross-gen prepare failed pkrid=" << pkrid
                      << " err=" << projected.error << '\n';
            return std::nullopt;
        }
        appendResortTransferJsonl(profile_path_, transferBridgeProjectJsonLine(pkrid, projected));
        PokemonSnapshot next = *snapshot;
        next.snapshot_id = generateId("snap");
        next.kind = SnapshotKind::CanonicalCheckpoint;
        next.captured_at_unix = unixNow();
        next.format_name = projected.target_format_name;
        next.raw_bytes = projected.raw_bytes;
        next.raw_hash_sha256 = projected.raw_hash_sha256;
        next.notes_json = std::string("{\"schema_version\":1,\"reason\":\"projection_for_game_write\",\"target_pid\":") +
            (projected.bridge_target_pid ? std::to_string(*projected.bridge_target_pid) : std::string("null")) +
            "}";
        snapshot = std::move(next);
    }

    if (!isGen12StorageFormat(snapshot->format_name)) {
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

void PokemonResortService::linkOpenHomePayloadToPokemon(
    const std::string& pkrid,
    const openhome::OpenHomePokemonPayload& payload) {
    SqliteTransaction tx(*connection_);
    openhome_links_->upsertPayloadForPokemon(pkrid, payload, unixNow());
    tx.commit();
}

std::optional<std::string> PokemonResortService::getOpenHomeIdForPokemon(const std::string& pkrid) const {
    return openhome_links_->findOpenHomeIdForPokemon(pkrid);
}

std::optional<std::string> PokemonResortService::getPokemonIdForOpenHomeId(const std::string& openhome_id) const {
    return openhome_links_->findPokemonForOpenHomeId(openhome_id);
}

void PokemonResortService::recordPokemonInGamePlacement(
    const std::string& pkrid,
    const std::string& openhome_id,
    std::optional<std::uint16_t> game_id,
    const std::string& save_path,
    int box_index,
    int slot_index) {
    PokemonPlacementRecord placement;
    placement.pkrid = pkrid;
    placement.kind = PokemonPlacementKind::InGameSave;
    placement.game_id = game_id;
    placement.save_path = save_path;
    placement.box_index = box_index;
    placement.slot_index = slot_index;
    placement.openhome_id = openhome_id;
    placement.updated_at_unix = unixNow();
    SqliteTransaction tx(*connection_);
    openhome_links_->recordPlacement(placement);
    tx.commit();
}

void PokemonResortService::recordPokemonHomePlacement(
    const std::string& pkrid,
    const std::string& openhome_id,
    int bank,
    int box,
    int slot) {
    PokemonPlacementRecord placement;
    placement.pkrid = pkrid;
    placement.kind = PokemonPlacementKind::HomeBank;
    placement.box_index = box;
    placement.slot_index = slot;
    placement.openhome_id = openhome_id;
    placement.profile_id = "bank:" + std::to_string(bank);
    placement.updated_at_unix = unixNow();
    SqliteTransaction tx(*connection_);
    openhome_links_->recordPlacement(placement);
    tx.commit();
}

void PokemonResortService::applyOpenHomePushToGamePlacementsBatch(
    const std::string& profile_id,
    const std::optional<std::uint16_t> game_id,
    const std::string& save_path,
    const std::vector<OpenHomePushToGamePlacementItem>& items) {
    if (items.empty()) {
        return;
    }
    SqliteTransaction tx(*connection_);
    for (const auto& item : items) {
        if (item.updated_payload && !item.updated_payload->serialized_identity_or_ohpkm.empty()) {
            openhome_links_->upsertPayloadForPokemon(item.pkrid, *item.updated_payload, unixNow());
        }
        PokemonPlacementRecord placement;
        placement.pkrid = item.pkrid;
        placement.kind = PokemonPlacementKind::InGameSave;
        placement.game_id = game_id;
        placement.save_path = save_path;
        placement.box_index = item.game_box;
        placement.slot_index = item.game_slot;
        placement.openhome_id = item.openhome_id;
        placement.updated_at_unix = unixNow();
        openhome_links_->recordPlacement(placement);
        boxes_->removePokemon(profile_id, item.pkrid);
    }
    tx.commit();
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
    PokemonPlacementRecord placement;
    placement.pkrid = pkrid;
    placement.kind = PokemonPlacementKind::ResortBox;
    placement.profile_id = destination.profile_id;
    placement.box_index = destination.box_id;
    placement.slot_index = destination.slot_index;
    placement.openhome_id = openhome_links_->findOpenHomeIdForPokemon(pkrid).value_or("");
    placement.updated_at_unix = unixNow();
    openhome_links_->recordPlacement(placement);
    tx.commit();
}

void PokemonResortService::removePokemonFromBoxes(const std::string& profile_id, const std::string& pkrid) {
    SqliteTransaction tx(*connection_);
    boxes_->removePokemon(profile_id, pkrid);
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
