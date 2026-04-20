#include "resort/persistence/SnapshotRepository.hpp"

namespace pr::resort {

SnapshotRepository::SnapshotRepository(SqliteConnection& connection)
    : connection_(connection) {}

void SnapshotRepository::insert(const PokemonSnapshot& snapshot) {
    auto stmt = connection_.prepare(R"sql(
INSERT INTO pokemon_snapshots (
    snapshot_id, pkrid, kind, format_name, game_id, captured_at,
    raw_bytes, raw_hash_sha256, parsed_json, notes_json
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
)sql");
    stmt.bindText(1, snapshot.snapshot_id);
    stmt.bindText(2, snapshot.pkrid);
    stmt.bindInt(3, static_cast<int>(snapshot.kind));
    stmt.bindText(4, snapshot.format_name);
    if (snapshot.game_id) stmt.bindInt(5, *snapshot.game_id); else stmt.bindNull(5);
    stmt.bindInt64(6, snapshot.captured_at_unix);
    if (snapshot.raw_bytes.empty()) {
        stmt.bindBlob(7, "", 0);
    } else {
        stmt.bindBlob(7, snapshot.raw_bytes.data(), static_cast<int>(snapshot.raw_bytes.size()));
    }
    stmt.bindText(8, snapshot.raw_hash_sha256);
    stmt.bindBlob(9, snapshot.parsed_json.data(), static_cast<int>(snapshot.parsed_json.size()));
    stmt.bindBlob(10, snapshot.notes_json.data(), static_cast<int>(snapshot.notes_json.size()));
    stmt.stepDone();
}

} // namespace pr::resort
