#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/HistoryRepository.hpp"
#include "resort/persistence/MirrorSessionRepository.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <optional>
#include <string>

namespace pr::resort {

struct MirrorOpenContext {
    std::optional<std::uint16_t> beacon_tid16;
    std::optional<std::string> beacon_ot_name;
    std::string projection_metadata_json = "{\"schema_version\":1}";
};

class MirrorSessionService {
public:
    MirrorSessionService(
        SqliteConnection& connection,
        PokemonRepository& pokemon,
        MirrorSessionRepository& mirrors,
        HistoryRepository& history);

    MirrorSession openMirrorSession(
        const std::string& pkrid,
        std::uint16_t target_game,
        const MirrorOpenContext& context);

    std::optional<MirrorSession> getById(const std::string& mirror_session_id) const;
    std::optional<MirrorSession> getActiveForPokemon(const std::string& pkrid) const;
    std::optional<MirrorSession> findActiveByBeacon(
        std::uint16_t target_game,
        std::uint16_t beacon_tid16,
        const std::string& beacon_ot_name) const;

    void closeReturned(const std::string& mirror_session_id, long long returned_at_unix);

private:
    SqliteConnection& connection_;
    PokemonRepository& pokemon_;
    MirrorSessionRepository& mirrors_;
    HistoryRepository& history_;
};

} // namespace pr::resort
