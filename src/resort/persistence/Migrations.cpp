#include "resort/persistence/Migrations.hpp"

#include <stdexcept>

namespace pr::resort {

namespace {

int currentVersion(SqliteConnection& connection) {
    connection.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)");
    auto stmt = connection.prepare("SELECT version FROM schema_version ORDER BY version DESC LIMIT 1");
    if (stmt.stepRow()) {
        return stmt.columnInt(0);
    }
    return 0;
}

void setVersion(SqliteConnection& connection, int version) {
    connection.exec("DELETE FROM schema_version");
    auto stmt = connection.prepare("INSERT INTO schema_version (version) VALUES (?)");
    stmt.bindInt(1, version);
    stmt.stepDone();
}

void migrateTo1(SqliteConnection& connection) {
    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS pokemon (
    pkrid                   TEXT PRIMARY KEY,
    origin_fingerprint      TEXT NOT NULL,
    revision                INTEGER NOT NULL,
    created_at              INTEGER NOT NULL,
    updated_at              INTEGER NOT NULL,
    species_id              INTEGER NOT NULL,
    form_id                 INTEGER NOT NULL,
    nickname                TEXT,
    is_nicknamed            INTEGER NOT NULL,
    level                   INTEGER NOT NULL,
    exp                     INTEGER NOT NULL,
    gender                  INTEGER,
    shiny                   INTEGER,
    ability_id              INTEGER,
    ability_slot            INTEGER,
    held_item_id            INTEGER,
    move1_id                INTEGER,
    move2_id                INTEGER,
    move3_id                INTEGER,
    move4_id                INTEGER,
    move1_pp                INTEGER,
    move2_pp                INTEGER,
    move3_pp                INTEGER,
    move4_pp                INTEGER,
    move1_ppups             INTEGER,
    move2_ppups             INTEGER,
    move3_ppups             INTEGER,
    move4_ppups             INTEGER,
    hp_current              INTEGER NOT NULL,
    hp_max                  INTEGER NOT NULL,
    status_flags            INTEGER NOT NULL,
    ot_name                 TEXT NOT NULL,
    tid16                   INTEGER,
    sid16                   INTEGER,
    tid32                   INTEGER,
    origin_game             INTEGER NOT NULL,
    language                INTEGER,
    met_location_id         INTEGER,
    met_level               INTEGER,
    met_date                INTEGER,
    ball_id                 INTEGER,
    pid                     INTEGER,
    encryption_constant     INTEGER,
    home_tracker            TEXT,
    lineage_root_species    INTEGER NOT NULL,
    identity_strength       INTEGER NOT NULL,
    warm_json               BLOB NOT NULL,
    suspended_json          BLOB NOT NULL
);
)sql");

    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS boxes (
    profile_id      TEXT NOT NULL,
    box_id          INTEGER NOT NULL,
    name            TEXT NOT NULL,
    wallpaper_id    INTEGER,
    sort_key        INTEGER NOT NULL,
    PRIMARY KEY (profile_id, box_id)
);
)sql");

    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS box_slots (
    profile_id      TEXT NOT NULL,
    box_id          INTEGER NOT NULL,
    slot_index      INTEGER NOT NULL,
    pkrid           TEXT,
    PRIMARY KEY (profile_id, box_id, slot_index),
    FOREIGN KEY (profile_id, box_id) REFERENCES boxes(profile_id, box_id) ON DELETE CASCADE,
    FOREIGN KEY (pkrid) REFERENCES pokemon(pkrid) ON DELETE SET NULL
);
)sql");

    connection.exec(R"sql(
CREATE UNIQUE INDEX IF NOT EXISTS idx_box_slots_profile_pkrid
ON box_slots(profile_id, pkrid)
WHERE pkrid IS NOT NULL;
)sql");

    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS mirror_sessions (
    mirror_session_id       TEXT PRIMARY KEY,
    pkrid                   TEXT NOT NULL,
    target_game             INTEGER NOT NULL,
    status                  INTEGER NOT NULL,
    created_at              INTEGER NOT NULL,
    returned_at             INTEGER,
    beacon_tid16            INTEGER,
    beacon_ot_name          TEXT,
    sent_species_id         INTEGER NOT NULL,
    sent_form_id            INTEGER NOT NULL,
    sent_lineage_root       INTEGER NOT NULL,
    sent_level              INTEGER NOT NULL,
    sent_exp                INTEGER NOT NULL,
    original_ot_name        TEXT,
    original_tid16          INTEGER,
    original_sid16          INTEGER,
    original_game           INTEGER,
    projection_json         BLOB NOT NULL,
    FOREIGN KEY (pkrid) REFERENCES pokemon(pkrid) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED
);
)sql");

    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS pokemon_snapshots (
    snapshot_id             TEXT PRIMARY KEY,
    pkrid                   TEXT NOT NULL,
    kind                    INTEGER NOT NULL,
    format_name             TEXT NOT NULL,
    game_id                 INTEGER,
    captured_at             INTEGER NOT NULL,
    raw_bytes               BLOB NOT NULL,
    raw_hash_sha256         TEXT NOT NULL,
    parsed_json             BLOB,
    notes_json              BLOB,
    FOREIGN KEY (pkrid) REFERENCES pokemon(pkrid) ON DELETE CASCADE
);
)sql");

    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS pokemon_history (
    event_id                TEXT PRIMARY KEY,
    pkrid                   TEXT NOT NULL,
    event_type              INTEGER NOT NULL,
    timestamp               INTEGER NOT NULL,
    source_snapshot_id      TEXT,
    mirror_session_id       TEXT,
    diff_json               BLOB NOT NULL,
    FOREIGN KEY (pkrid) REFERENCES pokemon(pkrid) ON DELETE CASCADE DEFERRABLE INITIALLY DEFERRED,
    FOREIGN KEY (source_snapshot_id) REFERENCES pokemon_snapshots(snapshot_id) ON DELETE SET NULL DEFERRABLE INITIALLY DEFERRED,
    FOREIGN KEY (mirror_session_id) REFERENCES mirror_sessions(mirror_session_id) ON DELETE SET NULL DEFERRABLE INITIALLY DEFERRED
);
)sql");

    connection.exec("CREATE INDEX IF NOT EXISTS idx_box_slots_profile_box ON box_slots(profile_id, box_id, slot_index)");
    connection.exec("CREATE INDEX IF NOT EXISTS idx_snapshots_pkrid ON pokemon_snapshots(pkrid)");
    connection.exec("CREATE INDEX IF NOT EXISTS idx_history_pkrid ON pokemon_history(pkrid, timestamp)");
    connection.exec("CREATE INDEX IF NOT EXISTS idx_mirror_active_pkrid ON mirror_sessions(pkrid, status)");
    connection.exec("CREATE INDEX IF NOT EXISTS idx_mirror_active_beacon ON mirror_sessions(target_game, beacon_tid16, beacon_ot_name, status)");
}

} // namespace

void runResortMigrations(SqliteConnection& connection) {
    SqliteTransaction tx(connection);
    int version = currentVersion(connection);
    if (version > kCurrentResortSchemaVersion) {
        throw std::runtime_error("Resort profile schema is newer than this build supports");
    }
    if (version < 1) {
        migrateTo1(connection);
        setVersion(connection, 1);
    }
    tx.commit();
}

} // namespace pr::resort
