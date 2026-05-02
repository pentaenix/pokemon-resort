#include "resort/persistence/SnapshotRepository.hpp"

namespace pr::resort {

namespace {

PokemonSnapshot snapshotFromCurrentRow(const SqliteStatement& stmt) {
    PokemonSnapshot snapshot;
    snapshot.snapshot_id = stmt.columnText(0);
    snapshot.pkrid = stmt.columnText(1);
    snapshot.kind = static_cast<SnapshotKind>(stmt.columnInt(2));
    snapshot.format_name = stmt.columnText(3);
    if (!stmt.columnIsNull(4)) {
        snapshot.game_id = static_cast<std::uint16_t>(stmt.columnInt(4));
    }
    snapshot.captured_at_unix = stmt.columnInt64(5);
    const std::string raw = stmt.columnBlobAsString(6);
    snapshot.raw_bytes.assign(raw.begin(), raw.end());
    snapshot.raw_hash_sha256 = stmt.columnText(7);
    snapshot.parsed_json = stmt.columnBlobAsString(8);
    snapshot.notes_json = stmt.columnBlobAsString(9);
    return snapshot;
}

} // namespace

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

std::optional<PokemonSnapshot> SnapshotRepository::findLatestRawForPokemon(
    const std::string& pkrid,
    std::optional<std::uint16_t> game_id,
    const std::string& format_name) const {
    auto stmt = connection_.prepare(R"sql(
SELECT snapshot_id, pkrid, kind, format_name, game_id, captured_at,
       raw_bytes, raw_hash_sha256, parsed_json, notes_json
FROM pokemon_snapshots
WHERE pkrid = ?
  AND kind != ?
  AND (? IS NULL OR game_id = ?)
  AND (? = '' OR lower(format_name) = lower(?))
ORDER BY captured_at DESC,
         CASE WHEN kind = ? THEN 0 ELSE 1 END,
         snapshot_id DESC
LIMIT 1
)sql");
    stmt.bindText(1, pkrid);
    stmt.bindInt(2, static_cast<int>(SnapshotKind::ExportProjection));
    if (game_id) {
        stmt.bindInt(3, *game_id);
        stmt.bindInt(4, *game_id);
    } else {
        stmt.bindNull(3);
        stmt.bindNull(4);
    }
    stmt.bindText(5, format_name);
    stmt.bindText(6, format_name);
    stmt.bindInt(7, static_cast<int>(SnapshotKind::CanonicalCheckpoint));
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return snapshotFromCurrentRow(stmt);
}

} // namespace pr::resort
