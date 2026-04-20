#include "resort/persistence/HistoryRepository.hpp"

namespace pr::resort {

HistoryRepository::HistoryRepository(SqliteConnection& connection)
    : connection_(connection) {}

void HistoryRepository::insert(const PokemonHistoryEvent& event) {
    auto stmt = connection_.prepare(R"sql(
INSERT INTO pokemon_history (
    event_id, pkrid, event_type, timestamp, source_snapshot_id,
    mirror_session_id, diff_json
) VALUES (?, ?, ?, ?, ?, ?, ?)
)sql");
    stmt.bindText(1, event.event_id);
    stmt.bindText(2, event.pkrid);
    stmt.bindInt(3, static_cast<int>(event.event_type));
    stmt.bindInt64(4, event.timestamp_unix);
    if (event.source_snapshot_id) stmt.bindText(5, *event.source_snapshot_id); else stmt.bindNull(5);
    if (event.mirror_session_id) stmt.bindText(6, *event.mirror_session_id); else stmt.bindNull(6);
    stmt.bindBlob(7, event.diff_json.data(), static_cast<int>(event.diff_json.size()));
    stmt.stepDone();
}

} // namespace pr::resort
