#include "resort/persistence/MirrorSessionRepository.hpp"

namespace pr::resort {

namespace {

template <typename T>
void bindOptionalInt(SqliteStatement& stmt, int index, const std::optional<T>& value) {
    if (value) {
        stmt.bindInt(index, static_cast<int>(*value));
    } else {
        stmt.bindNull(index);
    }
}

void bindOptionalText(SqliteStatement& stmt, int index, const std::optional<std::string>& value) {
    if (value) {
        stmt.bindText(index, *value);
    } else {
        stmt.bindNull(index);
    }
}

std::optional<unsigned short> optionalU16(const SqliteStatement& stmt, int index) {
    if (stmt.columnIsNull(index)) {
        return std::nullopt;
    }
    return static_cast<unsigned short>(stmt.columnInt(index));
}

std::optional<long long> optionalI64(const SqliteStatement& stmt, int index) {
    if (stmt.columnIsNull(index)) {
        return std::nullopt;
    }
    return stmt.columnInt64(index);
}

std::optional<std::string> optionalText(const SqliteStatement& stmt, int index) {
    if (stmt.columnIsNull(index)) {
        return std::nullopt;
    }
    return stmt.columnText(index);
}

MirrorSession sessionFromCurrentRow(const SqliteStatement& stmt) {
    MirrorSession session;
    session.mirror_session_id = stmt.columnText(0);
    session.pkrid = stmt.columnText(1);
    session.target_game = static_cast<unsigned short>(stmt.columnInt(2));
    session.status = static_cast<MirrorStatus>(stmt.columnInt(3));
    session.created_at_unix = stmt.columnInt64(4);
    session.returned_at_unix = optionalI64(stmt, 5);
    session.beacon_tid16 = optionalU16(stmt, 6);
    session.beacon_ot_name = optionalText(stmt, 7);
    session.sent_species_id = static_cast<unsigned short>(stmt.columnInt(8));
    session.sent_form_id = static_cast<unsigned short>(stmt.columnInt(9));
    session.sent_lineage_root = static_cast<unsigned short>(stmt.columnInt(10));
    session.sent_level = static_cast<unsigned char>(stmt.columnInt(11));
    session.sent_exp = static_cast<unsigned int>(stmt.columnInt64(12));
    session.original_ot_name = optionalText(stmt, 13);
    session.original_tid16 = optionalU16(stmt, 14);
    session.original_sid16 = optionalU16(stmt, 15);
    session.original_game = optionalU16(stmt, 16);
    session.sent_dv16 = optionalU16(stmt, 17);
    session.projection_json = stmt.columnBlobAsString(18);
    return session;
}

void bindSession(SqliteStatement& stmt, const MirrorSession& session) {
    stmt.bindText(1, session.mirror_session_id);
    stmt.bindText(2, session.pkrid);
    stmt.bindInt(3, session.target_game);
    stmt.bindInt(4, static_cast<int>(session.status));
    stmt.bindInt64(5, session.created_at_unix);
    if (session.returned_at_unix) stmt.bindInt64(6, *session.returned_at_unix); else stmt.bindNull(6);
    bindOptionalInt(stmt, 7, session.beacon_tid16);
    bindOptionalText(stmt, 8, session.beacon_ot_name);
    stmt.bindInt(9, session.sent_species_id);
    stmt.bindInt(10, session.sent_form_id);
    stmt.bindInt(11, session.sent_lineage_root);
    stmt.bindInt(12, session.sent_level);
    stmt.bindInt64(13, session.sent_exp);
    bindOptionalText(stmt, 14, session.original_ot_name);
    bindOptionalInt(stmt, 15, session.original_tid16);
    bindOptionalInt(stmt, 16, session.original_sid16);
    bindOptionalInt(stmt, 17, session.original_game);
    bindOptionalInt(stmt, 18, session.sent_dv16);
    stmt.bindBlob(19, session.projection_json.data(), static_cast<int>(session.projection_json.size()));
}

} // namespace

MirrorSessionRepository::MirrorSessionRepository(SqliteConnection& connection)
    : connection_(connection) {}

void MirrorSessionRepository::insert(const MirrorSession& session) {
    auto stmt = connection_.prepare(R"sql(
INSERT INTO mirror_sessions (
    mirror_session_id, pkrid, target_game, status, created_at, returned_at,
    beacon_tid16, beacon_ot_name, sent_species_id, sent_form_id, sent_lineage_root,
    sent_level, sent_exp, original_ot_name, original_tid16, original_sid16,
    original_game, sent_dv16, projection_json
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
)sql");
    bindSession(stmt, session);
    stmt.stepDone();
}

void MirrorSessionRepository::update(const MirrorSession& session) {
    auto stmt = connection_.prepare(R"sql(
UPDATE mirror_sessions SET
    pkrid = ?,
    target_game = ?,
    status = ?,
    created_at = ?,
    returned_at = ?,
    beacon_tid16 = ?,
    beacon_ot_name = ?,
    sent_species_id = ?,
    sent_form_id = ?,
    sent_lineage_root = ?,
    sent_level = ?,
    sent_exp = ?,
    original_ot_name = ?,
    original_tid16 = ?,
    original_sid16 = ?,
    original_game = ?,
    sent_dv16 = ?,
    projection_json = ?
WHERE mirror_session_id = ?
)sql");
    stmt.bindText(1, session.pkrid);
    stmt.bindInt(2, session.target_game);
    stmt.bindInt(3, static_cast<int>(session.status));
    stmt.bindInt64(4, session.created_at_unix);
    if (session.returned_at_unix) stmt.bindInt64(5, *session.returned_at_unix); else stmt.bindNull(5);
    bindOptionalInt(stmt, 6, session.beacon_tid16);
    bindOptionalText(stmt, 7, session.beacon_ot_name);
    stmt.bindInt(8, session.sent_species_id);
    stmt.bindInt(9, session.sent_form_id);
    stmt.bindInt(10, session.sent_lineage_root);
    stmt.bindInt(11, session.sent_level);
    stmt.bindInt64(12, session.sent_exp);
    bindOptionalText(stmt, 13, session.original_ot_name);
    bindOptionalInt(stmt, 14, session.original_tid16);
    bindOptionalInt(stmt, 15, session.original_sid16);
    bindOptionalInt(stmt, 16, session.original_game);
    bindOptionalInt(stmt, 17, session.sent_dv16);
    stmt.bindBlob(18, session.projection_json.data(), static_cast<int>(session.projection_json.size()));
    stmt.bindText(19, session.mirror_session_id);
    stmt.stepDone();
}

std::optional<MirrorSession> MirrorSessionRepository::findById(const std::string& mirror_session_id) const {
    auto stmt = connection_.prepare(R"sql(
SELECT mirror_session_id, pkrid, target_game, status, created_at, returned_at,
       beacon_tid16, beacon_ot_name, sent_species_id, sent_form_id, sent_lineage_root,
       sent_level, sent_exp, original_ot_name, original_tid16, original_sid16,
       original_game, sent_dv16, projection_json
FROM mirror_sessions
WHERE mirror_session_id = ?
)sql");
    stmt.bindText(1, mirror_session_id);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return sessionFromCurrentRow(stmt);
}

std::optional<MirrorSession> MirrorSessionRepository::findActiveForPokemon(const std::string& pkrid) const {
    auto stmt = connection_.prepare(R"sql(
SELECT mirror_session_id, pkrid, target_game, status, created_at, returned_at,
       beacon_tid16, beacon_ot_name, sent_species_id, sent_form_id, sent_lineage_root,
       sent_level, sent_exp, original_ot_name, original_tid16, original_sid16,
       original_game, sent_dv16, projection_json
FROM mirror_sessions
WHERE pkrid = ? AND status = ?
ORDER BY created_at DESC
LIMIT 1
)sql");
    stmt.bindText(1, pkrid);
    stmt.bindInt(2, static_cast<int>(MirrorStatus::Active));
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return sessionFromCurrentRow(stmt);
}

std::optional<MirrorSession> MirrorSessionRepository::findActiveByBeacon(
    std::uint16_t target_game,
    std::uint16_t beacon_tid16,
    const std::string& beacon_ot_name) const {
    auto stmt = connection_.prepare(R"sql(
SELECT mirror_session_id, pkrid, target_game, status, created_at, returned_at,
       beacon_tid16, beacon_ot_name, sent_species_id, sent_form_id, sent_lineage_root,
       sent_level, sent_exp, original_ot_name, original_tid16, original_sid16,
       original_game, sent_dv16, projection_json
FROM mirror_sessions
WHERE target_game = ?
  AND status = ?
  AND beacon_tid16 = ?
  AND beacon_ot_name = ?
ORDER BY created_at DESC
LIMIT 1
)sql");
    stmt.bindInt(1, target_game);
    stmt.bindInt(2, static_cast<int>(MirrorStatus::Active));
    stmt.bindInt(3, beacon_tid16);
    stmt.bindText(4, beacon_ot_name);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return sessionFromCurrentRow(stmt);
}

std::vector<MirrorSession> MirrorSessionRepository::findActiveCandidatesByBeacon(
    std::uint16_t target_game,
    std::uint16_t beacon_tid16,
    const std::string& beacon_ot_name) const {
    auto stmt = connection_.prepare(R"sql(
SELECT mirror_session_id, pkrid, target_game, status, created_at, returned_at,
       beacon_tid16, beacon_ot_name, sent_species_id, sent_form_id, sent_lineage_root,
       sent_level, sent_exp, original_ot_name, original_tid16, original_sid16,
       original_game, sent_dv16, projection_json
FROM mirror_sessions
WHERE target_game = ?
  AND status = ?
  AND (
      (beacon_tid16 = ? AND beacon_ot_name = ?)
      OR (original_tid16 = ? AND original_ot_name = ?)
  )
ORDER BY created_at DESC
)sql");
    stmt.bindInt(1, target_game);
    stmt.bindInt(2, static_cast<int>(MirrorStatus::Active));
    stmt.bindInt(3, beacon_tid16);
    stmt.bindText(4, beacon_ot_name);
    stmt.bindInt(5, beacon_tid16);
    stmt.bindText(6, beacon_ot_name);

    std::vector<MirrorSession> sessions;
    while (stmt.stepRow()) {
        sessions.push_back(sessionFromCurrentRow(stmt));
    }
    return sessions;
}

} // namespace pr::resort
