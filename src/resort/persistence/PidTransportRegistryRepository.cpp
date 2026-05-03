#include "resort/persistence/PidTransportRegistryRepository.hpp"

namespace pr::resort {

PidTransportRegistryRepository::PidTransportRegistryRepository(SqliteConnection& connection)
    : connection_(connection) {}

void PidTransportRegistryRepository::insertActiveMapping(
    std::uint32_t temp_pid,
    const std::string& pkrid,
    std::uint32_t original_pid,
    int source_constraint_gen,
    int target_constraint_gen,
    std::int64_t created_at_unix,
    const std::string& mirror_session_id) {
    auto stmt = connection_.prepare(R"sql(
INSERT INTO pid_transport_registry (
    temp_pid, pkrid, original_pid, source_constraint_gen, target_constraint_gen,
    created_at_unix, mirror_session_id, active
) VALUES (?, ?, ?, ?, ?, ?, ?, 1)
)sql");
    stmt.bindInt64(1, static_cast<long long>(temp_pid));
    stmt.bindText(2, pkrid);
    stmt.bindInt64(3, static_cast<long long>(original_pid));
    stmt.bindInt(4, source_constraint_gen);
    stmt.bindInt(5, target_constraint_gen);
    stmt.bindInt64(6, created_at_unix);
    stmt.bindText(7, mirror_session_id);
    stmt.stepDone();
}

std::optional<PidTransportActiveMapping> PidTransportRegistryRepository::findActiveByTempPidAndTargetGen(
    std::uint32_t temp_pid,
    int target_constraint_gen) const {
    auto stmt = connection_.prepare(R"sql(
SELECT pkrid, mirror_session_id
FROM pid_transport_registry
WHERE temp_pid = ?
  AND target_constraint_gen = ?
  AND active = 1
LIMIT 1
)sql");
    stmt.bindInt64(1, static_cast<long long>(temp_pid));
    stmt.bindInt(2, target_constraint_gen);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    PidTransportActiveMapping out;
    out.pkrid = stmt.columnText(0);
    out.mirror_session_id = stmt.columnText(1);
    return out;
}

void PidTransportRegistryRepository::deactivateByMirrorSession(const std::string& mirror_session_id) {
    auto stmt = connection_.prepare(
        "UPDATE pid_transport_registry SET active = 0 WHERE mirror_session_id = ? AND active = 1");
    stmt.bindText(1, mirror_session_id);
    stmt.stepDone();
}

} // namespace pr::resort
