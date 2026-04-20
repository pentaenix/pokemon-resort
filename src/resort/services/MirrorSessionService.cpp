#include "resort/services/MirrorSessionService.hpp"

#include "resort/domain/Ids.hpp"

#include <stdexcept>

namespace pr::resort {

MirrorSessionService::MirrorSessionService(
    SqliteConnection& connection,
    PokemonRepository& pokemon,
    MirrorSessionRepository& mirrors,
    HistoryRepository& history)
    : connection_(connection),
      pokemon_(pokemon),
      mirrors_(mirrors),
      history_(history) {}

MirrorSession MirrorSessionService::openMirrorSession(
    const std::string& pkrid,
    std::uint16_t target_game,
    const MirrorOpenContext& context) {
    const auto pokemon = pokemon_.findById(pkrid);
    if (!pokemon) {
        throw std::runtime_error("Cannot open mirror for missing Pokemon: " + pkrid);
    }

    const long long now = unixNow();
    MirrorSession session;
    session.mirror_session_id = generateId("mir");
    session.pkrid = pkrid;
    session.target_game = target_game;
    session.status = MirrorStatus::Active;
    session.created_at_unix = now;
    session.beacon_tid16 = context.beacon_tid16;
    session.beacon_ot_name = context.beacon_ot_name;
    session.sent_species_id = pokemon->hot.species_id;
    session.sent_form_id = pokemon->hot.form_id;
    session.sent_lineage_root = pokemon->hot.lineage_root_species == 0
        ? pokemon->hot.species_id
        : pokemon->hot.lineage_root_species;
    session.sent_level = pokemon->hot.level;
    session.sent_exp = pokemon->hot.exp;
    session.original_ot_name = pokemon->hot.ot_name;
    session.original_tid16 = pokemon->hot.tid16;
    session.original_sid16 = pokemon->hot.sid16;
    session.original_game = pokemon->hot.origin_game;
    session.projection_json = context.projection_metadata_json;

    mirrors_.insert(session);

    PokemonHistoryEvent event;
    event.event_id = generateId("hist");
    event.pkrid = pkrid;
    event.event_type = HistoryEventType::MirrorOpened;
    event.timestamp_unix = now;
    event.mirror_session_id = session.mirror_session_id;
    event.diff_json = "{\"event\":\"mirror_opened\",\"target_game\":" + std::to_string(target_game) + "}";
    history_.insert(event);

    return session;
}

std::optional<MirrorSession> MirrorSessionService::getById(const std::string& mirror_session_id) const {
    return mirrors_.findById(mirror_session_id);
}

std::optional<MirrorSession> MirrorSessionService::getActiveForPokemon(const std::string& pkrid) const {
    return mirrors_.findActiveForPokemon(pkrid);
}

std::optional<MirrorSession> MirrorSessionService::findActiveByBeacon(
    std::uint16_t target_game,
    std::uint16_t beacon_tid16,
    const std::string& beacon_ot_name) const {
    return mirrors_.findActiveByBeacon(target_game, beacon_tid16, beacon_ot_name);
}

void MirrorSessionService::closeReturned(
    const std::string& mirror_session_id,
    long long returned_at_unix) {
    auto session = mirrors_.findById(mirror_session_id);
    if (!session) {
        throw std::runtime_error("Mirror session not found: " + mirror_session_id);
    }
    session->status = MirrorStatus::Returned;
    session->returned_at_unix = returned_at_unix;
    mirrors_.update(*session);

    PokemonHistoryEvent event;
    event.event_id = generateId("hist");
    event.pkrid = session->pkrid;
    event.event_type = HistoryEventType::MirrorReturned;
    event.timestamp_unix = returned_at_unix;
    event.mirror_session_id = session->mirror_session_id;
    event.diff_json = "{\"event\":\"mirror_returned\"}";
    history_.insert(event);
}

} // namespace pr::resort
