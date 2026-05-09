#include "resort/persistence/Migrations.hpp"

#include <string>
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
    dv16                    INTEGER,
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
    sent_dv16               INTEGER,
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

void addColumnIfMissing(SqliteConnection& connection, const char* table, const char* column, const char* definition) {
    auto stmt = connection.prepare(std::string("PRAGMA table_info(") + table + ")");
    while (stmt.stepRow()) {
        if (stmt.columnText(1) == column) {
            return;
        }
    }
    connection.exec(std::string("ALTER TABLE ") + table + " ADD COLUMN " + definition);
}

void migrateTo2(SqliteConnection& connection) {
    addColumnIfMissing(connection, "pokemon", "dv16", "dv16 INTEGER");
    addColumnIfMissing(connection, "mirror_sessions", "sent_dv16", "sent_dv16 INTEGER");
}

void migrateTo3(SqliteConnection& connection) {
    addColumnIfMissing(connection, "mirror_sessions", "mirror_canonical_pid", "mirror_canonical_pid INTEGER");
    addColumnIfMissing(connection, "mirror_sessions", "transport_pid", "transport_pid INTEGER");
    connection.exec(
        "CREATE INDEX IF NOT EXISTS idx_mirror_transport_pid ON mirror_sessions(transport_pid, target_game, "
        "status) WHERE transport_pid IS NOT NULL");
}

void migrateTo4(SqliteConnection& connection) {
    addColumnIfMissing(connection, "pokemon", "original_pid", "original_pid INTEGER");
    addColumnIfMissing(connection, "pokemon", "pid_history_json", "pid_history_json TEXT NOT NULL DEFAULT '[]'");
    connection.exec("UPDATE pokemon SET original_pid = pid WHERE original_pid IS NULL AND pid IS NOT NULL");
    connection.exec("UPDATE pokemon SET pid_history_json = '[]' WHERE pid_history_json IS NULL OR pid_history_json = ''");

    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS pid_transport_registry (
    row_id INTEGER PRIMARY KEY AUTOINCREMENT,
    temp_pid INTEGER NOT NULL,
    pkrid TEXT NOT NULL,
    original_pid INTEGER NOT NULL,
    source_constraint_gen INTEGER NOT NULL,
    target_constraint_gen INTEGER NOT NULL,
    created_at_unix INTEGER NOT NULL,
    mirror_session_id TEXT,
    active INTEGER NOT NULL DEFAULT 1,
    FOREIGN KEY (pkrid) REFERENCES pokemon(pkrid) ON DELETE CASCADE,
    FOREIGN KEY (mirror_session_id) REFERENCES mirror_sessions(mirror_session_id) ON DELETE SET NULL
);
)sql");
    connection.exec(
        "CREATE UNIQUE INDEX IF NOT EXISTS idx_pid_transport_active_temp ON pid_transport_registry(temp_pid) "
        "WHERE active = 1");
    connection.exec("CREATE INDEX IF NOT EXISTS idx_pid_transport_pkrid ON pid_transport_registry(pkrid)");
}

void migrateTo5(SqliteConnection& connection) {
    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS openhome_payloads (
    openhome_id             TEXT PRIMARY KEY,
    pkrid                   TEXT NOT NULL UNIQUE,
    payload_format_version  TEXT NOT NULL,
    ohpkm_bytes             BLOB NOT NULL,
    updated_at              INTEGER NOT NULL,
    FOREIGN KEY (pkrid) REFERENCES pokemon(pkrid) ON DELETE CASCADE
);
)sql");

    connection.exec(R"sql(
CREATE TABLE IF NOT EXISTS pokemon_placements (
    pkrid                   TEXT PRIMARY KEY,
    placement_kind          TEXT NOT NULL,
    profile_id              TEXT,
    game_id                 INTEGER,
    save_path               TEXT,
    box_index               INTEGER,
    slot_index              INTEGER,
    openhome_id             TEXT,
    updated_at              INTEGER NOT NULL,
    FOREIGN KEY (pkrid) REFERENCES pokemon(pkrid) ON DELETE CASCADE,
    FOREIGN KEY (openhome_id) REFERENCES openhome_payloads(openhome_id) ON DELETE SET NULL
);
)sql");

    connection.exec("CREATE UNIQUE INDEX IF NOT EXISTS idx_openhome_payloads_pkrid ON openhome_payloads(pkrid)");
    connection.exec("CREATE INDEX IF NOT EXISTS idx_pokemon_placements_kind ON pokemon_placements(placement_kind)");
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
        version = 1;
    }
    if (version < 2) {
        migrateTo2(connection);
        setVersion(connection, 2);
        version = 2;
    }
    if (version < 3) {
        migrateTo3(connection);
        setVersion(connection, 3);
        version = 3;
    }
    if (version < 4) {
        migrateTo4(connection);
        setVersion(connection, 4);
        version = 4;
    }
    if (version < 5) {
        migrateTo5(connection);
        setVersion(connection, 5);
    }
    tx.commit();
}

} // namespace pr::resort
