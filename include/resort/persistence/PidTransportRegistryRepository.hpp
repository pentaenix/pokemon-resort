#pragma once

#include "resort/persistence/SqliteConnection.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace pr::resort {

struct PidTransportActiveMapping {
    std::string pkrid;
    std::string mirror_session_id;
};

class PidTransportRegistryRepository {
public:
    explicit PidTransportRegistryRepository(SqliteConnection& connection);

    void insertActiveMapping(
        std::uint32_t temp_pid,
        const std::string& pkrid,
        std::uint32_t original_pid,
        int source_constraint_gen,
        int target_constraint_gen,
        std::int64_t created_at_unix,
        const std::string& mirror_session_id);

    std::optional<PidTransportActiveMapping> findActiveByTempPidAndTargetGen(
        std::uint32_t temp_pid,
        int target_constraint_gen) const;

    void deactivateByMirrorSession(const std::string& mirror_session_id);

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
