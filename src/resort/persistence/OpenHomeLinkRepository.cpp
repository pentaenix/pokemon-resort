#include "resort/persistence/OpenHomeLinkRepository.hpp"

#include "core/domain/PcSlotOpenHomeProfileKey.hpp"
#include "resort/openhome/OpenHomeIdentity.hpp"

#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace pr::resort {

namespace {

const char* placementKindToString(PokemonPlacementKind kind) {
    switch (kind) {
        case PokemonPlacementKind::ResortBox: return "RESORT_BOX";
        case PokemonPlacementKind::InGameSave: return "IN_GAME_SAVE";
        case PokemonPlacementKind::HomeBank: return "HOME_BANK";
    }
    return "RESORT_BOX";
}

PokemonPlacementKind placementKindFromString(const std::string& value) {
    if (value == "IN_GAME_SAVE") {
        return PokemonPlacementKind::InGameSave;
    }
    if (value == "HOME_BANK") {
        return PokemonPlacementKind::HomeBank;
    }
    return PokemonPlacementKind::ResortBox;
}

std::optional<std::string> optionalText(const SqliteStatement& stmt, int index) {
    if (stmt.columnIsNull(index)) {
        return std::nullopt;
    }
    return stmt.columnText(index);
}

std::optional<std::uint16_t> optionalU16(const SqliteStatement& stmt, int index) {
    if (stmt.columnIsNull(index)) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(stmt.columnInt(index));
}

} // namespace

OpenHomeLinkRepository::OpenHomeLinkRepository(SqliteConnection& connection)
    : connection_(connection) {}

void OpenHomeLinkRepository::upsertPayloadForPokemon(
    const std::string& pkrid,
    const openhome::OpenHomePokemonPayload& payload,
    std::int64_t updated_at_unix) {
    if (pkrid.empty()) {
        throw std::runtime_error("OpenHome link requires pkrid");
    }
    if (!openhome::isValidOpenHomeId(payload.openhome_id)) {
        throw std::runtime_error("OpenHome link requires valid openhome_id");
    }
    if (payload.serialized_identity_or_ohpkm.empty()) {
        throw std::runtime_error("OpenHome link requires OHPKM payload bytes");
    }

    auto clear_previous = connection_.prepare(
        "DELETE FROM openhome_payloads WHERE pkrid = ? AND openhome_id <> ?");
    clear_previous.bindText(1, pkrid);
    clear_previous.bindText(2, payload.openhome_id);
    clear_previous.stepDone();

    auto stmt = connection_.prepare(R"sql(
INSERT INTO openhome_payloads (
    openhome_id, pkrid, payload_format_version, ohpkm_bytes, updated_at
) VALUES (?, ?, ?, ?, ?)
ON CONFLICT(openhome_id) DO UPDATE SET
    pkrid = excluded.pkrid,
    payload_format_version = excluded.payload_format_version,
    ohpkm_bytes = excluded.ohpkm_bytes,
    updated_at = excluded.updated_at
)sql");
    stmt.bindText(1, payload.openhome_id);
    stmt.bindText(2, pkrid);
    stmt.bindText(3, payload.openhome_format_version.empty() ? "OHPKM" : payload.openhome_format_version);
    stmt.bindBlob(
        4,
        payload.serialized_identity_or_ohpkm.data(),
        static_cast<int>(payload.serialized_identity_or_ohpkm.size()));
    stmt.bindInt64(5, updated_at_unix);
    stmt.stepDone();

    auto link_stmt = connection_.prepare("UPDATE pokemon SET home_tracker = ? WHERE pkrid = ?");
    link_stmt.bindText(1, payload.openhome_id);
    link_stmt.bindText(2, pkrid);
    link_stmt.stepDone();
}

std::optional<std::string> OpenHomeLinkRepository::findOpenHomeIdForPokemon(const std::string& pkrid) const {
    auto stmt = connection_.prepare("SELECT openhome_id FROM openhome_payloads WHERE pkrid = ? LIMIT 1");
    stmt.bindText(1, pkrid);
    if (stmt.stepRow()) {
        return stmt.columnText(0);
    }
    auto legacy = connection_.prepare("SELECT home_tracker FROM pokemon WHERE pkrid = ? AND home_tracker IS NOT NULL LIMIT 1");
    legacy.bindText(1, pkrid);
    if (legacy.stepRow()) {
        return legacy.columnText(0);
    }
    return std::nullopt;
}

std::unordered_map<std::string, std::string> OpenHomeLinkRepository::findOpenHomeIdsForPokemon(
    const std::vector<std::string>& pkrids) const {
    std::unordered_map<std::string, std::string> out;
    std::vector<std::string> uniq;
    uniq.reserve(pkrids.size());
    std::unordered_set<std::string> seen;
    for (const std::string& p : pkrids) {
        if (p.empty() || seen.count(p)) {
            continue;
        }
        seen.insert(p);
        uniq.push_back(p);
    }
    if (uniq.empty()) {
        return out;
    }

    auto run_in_query = [&](const char* sql_prefix, const char* sql_suffix, const std::vector<std::string>& keys) {
        if (keys.empty()) {
            return;
        }
        std::ostringstream oss;
        oss << sql_prefix;
        for (std::size_t i = 0; i < keys.size(); ++i) {
            oss << (i == 0 ? "?" : ",?");
        }
        oss << sql_suffix;
        auto stmt = connection_.prepare(oss.str());
        for (std::size_t i = 0; i < keys.size(); ++i) {
            stmt.bindText(static_cast<int>(i) + 1, keys[i]);
        }
        while (stmt.stepRow()) {
            out[stmt.columnText(0)] = stmt.columnText(1);
        }
    };

    run_in_query("SELECT pkrid, openhome_id FROM openhome_payloads WHERE pkrid IN (", ")", uniq);

    std::vector<std::string> missing;
    missing.reserve(uniq.size());
    for (const std::string& p : uniq) {
        if (!out.count(p)) {
            missing.push_back(p);
        }
    }
    run_in_query(
        "SELECT pkrid, home_tracker FROM pokemon WHERE pkrid IN (",
        ") AND home_tracker IS NOT NULL AND length(home_tracker) > 0",
        missing);
    return out;
}

void OpenHomeLinkRepository::loadPidEcOtOpenHomeProfileMatchKeys(std::unordered_set<std::string>& out) const {
    auto insert_if_valid = [&](std::uint32_t pid,
                               std::uint32_t ec,
                               const std::optional<std::uint16_t>& tid,
                               const std::optional<std::uint16_t>& sid,
                               const std::string& ot,
                               const std::string& openhome_candidate) {
        if (!openhome::isValidOpenHomeId(openhome_candidate)) {
            return;
        }
        const std::string k = pr::openHomeProfileMatchKey(pid, ec, tid, sid, ot);
        if (!k.empty()) {
            out.insert(k);
        }
    };

    {
        auto stmt = connection_.prepare(R"sql(
SELECT p.pid, p.encryption_constant, p.tid16, p.sid16, p.ot_name, op.openhome_id
FROM pokemon p
INNER JOIN openhome_payloads op ON op.pkrid = p.pkrid
WHERE p.pid IS NOT NULL AND p.encryption_constant IS NOT NULL
)sql");
        while (stmt.stepRow()) {
            const auto pid = static_cast<std::uint32_t>(stmt.columnInt64(0));
            const auto ec = static_cast<std::uint32_t>(stmt.columnInt64(1));
            insert_if_valid(pid, ec, optionalU16(stmt, 2), optionalU16(stmt, 3), stmt.columnText(4), stmt.columnText(5));
        }
    }

    {
        auto stmt = connection_.prepare(R"sql(
SELECT p.pid, p.encryption_constant, p.tid16, p.sid16, p.ot_name, p.home_tracker
FROM pokemon p
LEFT JOIN openhome_payloads op ON op.pkrid = p.pkrid
WHERE op.pkrid IS NULL
  AND p.pid IS NOT NULL AND p.encryption_constant IS NOT NULL
  AND p.home_tracker IS NOT NULL AND length(p.home_tracker) > 0
)sql");
        while (stmt.stepRow()) {
            const auto pid = static_cast<std::uint32_t>(stmt.columnInt64(0));
            const auto ec = static_cast<std::uint32_t>(stmt.columnInt64(1));
            insert_if_valid(pid, ec, optionalU16(stmt, 2), optionalU16(stmt, 3), stmt.columnText(4), stmt.columnText(5));
        }
    }
}

std::optional<std::string> OpenHomeLinkRepository::findPokemonForOpenHomeId(const std::string& openhome_id) const {
    auto stmt = connection_.prepare("SELECT pkrid FROM openhome_payloads WHERE openhome_id = ? LIMIT 1");
    stmt.bindText(1, openhome_id);
    if (stmt.stepRow()) {
        return stmt.columnText(0);
    }
    auto legacy = connection_.prepare("SELECT pkrid FROM pokemon WHERE home_tracker = ? LIMIT 1");
    legacy.bindText(1, openhome_id);
    if (legacy.stepRow()) {
        return legacy.columnText(0);
    }
    return std::nullopt;
}

void OpenHomeLinkRepository::recordPlacement(const PokemonPlacementRecord& placement) {
    if (placement.pkrid.empty()) {
        throw std::runtime_error("Pokemon placement requires pkrid");
    }
    auto stmt = connection_.prepare(R"sql(
INSERT INTO pokemon_placements (
    pkrid, placement_kind, profile_id, game_id, save_path, box_index, slot_index, openhome_id, updated_at
) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
ON CONFLICT(pkrid) DO UPDATE SET
    placement_kind = excluded.placement_kind,
    profile_id = excluded.profile_id,
    game_id = excluded.game_id,
    save_path = excluded.save_path,
    box_index = excluded.box_index,
    slot_index = excluded.slot_index,
    openhome_id = excluded.openhome_id,
    updated_at = excluded.updated_at
)sql");
    stmt.bindText(1, placement.pkrid);
    stmt.bindText(2, placementKindToString(placement.kind));
    if (!placement.profile_id.empty()) stmt.bindText(3, placement.profile_id); else stmt.bindNull(3);
    if (placement.game_id) stmt.bindInt(4, *placement.game_id); else stmt.bindNull(4);
    if (!placement.save_path.empty()) stmt.bindText(5, placement.save_path); else stmt.bindNull(5);
    if (placement.box_index >= 0) stmt.bindInt(6, placement.box_index); else stmt.bindNull(6);
    if (placement.slot_index >= 0) stmt.bindInt(7, placement.slot_index); else stmt.bindNull(7);
    if (!placement.openhome_id.empty()) stmt.bindText(8, placement.openhome_id); else stmt.bindNull(8);
    stmt.bindInt64(9, placement.updated_at_unix);
    stmt.stepDone();
}

std::optional<PokemonPlacementRecord> OpenHomeLinkRepository::findPlacementForPokemon(const std::string& pkrid) const {
    auto stmt = connection_.prepare(R"sql(
SELECT pkrid, placement_kind, profile_id, game_id, save_path, box_index, slot_index, openhome_id, updated_at
FROM pokemon_placements
WHERE pkrid = ?
LIMIT 1
)sql");
    stmt.bindText(1, pkrid);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    PokemonPlacementRecord out;
    out.pkrid = stmt.columnText(0);
    out.kind = placementKindFromString(stmt.columnText(1));
    out.profile_id = optionalText(stmt, 2).value_or("");
    out.game_id = optionalU16(stmt, 3);
    out.save_path = optionalText(stmt, 4).value_or("");
    out.box_index = stmt.columnIsNull(5) ? -1 : stmt.columnInt(5);
    out.slot_index = stmt.columnIsNull(6) ? -1 : stmt.columnInt(6);
    out.openhome_id = optionalText(stmt, 7).value_or("");
    out.updated_at_unix = stmt.columnInt64(8);
    return out;
}

} // namespace pr::resort
