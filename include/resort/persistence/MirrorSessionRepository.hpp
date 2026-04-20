#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <optional>
#include <string>

namespace pr::resort {

class MirrorSessionRepository {
public:
    explicit MirrorSessionRepository(SqliteConnection& connection);

    void insert(const MirrorSession& session);
    void update(const MirrorSession& session);
    std::optional<MirrorSession> findById(const std::string& mirror_session_id) const;
    std::optional<MirrorSession> findActiveForPokemon(const std::string& pkrid) const;
    std::optional<MirrorSession> findActiveByBeacon(
        std::uint16_t target_game,
        std::uint16_t beacon_tid16,
        const std::string& beacon_ot_name) const;

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
