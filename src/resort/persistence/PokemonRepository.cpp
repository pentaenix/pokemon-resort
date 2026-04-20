#include "resort/persistence/PokemonRepository.hpp"

namespace pr::resort {

namespace {

constexpr const char* kPokemonSelectColumns = R"sql(
    pkrid, origin_fingerprint, revision, created_at, updated_at,
    species_id, form_id, nickname, is_nicknamed, level, exp, gender, shiny,
    ability_id, ability_slot, held_item_id,
    move1_id, move2_id, move3_id, move4_id,
    move1_pp, move2_pp, move3_pp, move4_pp,
    move1_ppups, move2_ppups, move3_ppups, move4_ppups,
    hp_current, hp_max, status_flags,
    ot_name, tid16, sid16, tid32, origin_game, language,
    met_location_id, met_level, met_date, ball_id,
    pid, encryption_constant, home_tracker,
    lineage_root_species, identity_strength, warm_json, suspended_json
)sql";

template <typename T>
void bindOptionalInt(SqliteStatement& stmt, int index, const std::optional<T>& value) {
    if (value) {
        stmt.bindInt(index, static_cast<int>(*value));
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

std::optional<unsigned char> optionalU8(const SqliteStatement& stmt, int index) {
    if (stmt.columnIsNull(index)) {
        return std::nullopt;
    }
    return static_cast<unsigned char>(stmt.columnInt(index));
}

std::optional<unsigned int> optionalU32(const SqliteStatement& stmt, int index) {
    if (stmt.columnIsNull(index)) {
        return std::nullopt;
    }
    return static_cast<unsigned int>(stmt.columnInt64(index));
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

void bindOptionalMatch(SqliteStatement& stmt, int index, std::optional<std::uint16_t> value) {
    if (value) {
        stmt.bindInt(index, static_cast<int>(*value));
    } else {
        stmt.bindNull(index);
    }
}

ResortPokemon pokemonFromCurrentRow(const SqliteStatement& stmt) {
    ResortPokemon p;
    p.id.pkrid = stmt.columnText(0);
    p.id.origin_fingerprint = stmt.columnText(1);
    p.revision = static_cast<unsigned long long>(stmt.columnInt64(2));
    p.created_at_unix = stmt.columnInt64(3);
    p.updated_at_unix = stmt.columnInt64(4);
    PokemonHot& h = p.hot;
    h.species_id = static_cast<unsigned short>(stmt.columnInt(5));
    h.form_id = static_cast<unsigned short>(stmt.columnInt(6));
    h.nickname = stmt.columnText(7);
    h.is_nicknamed = stmt.columnInt(8) != 0;
    h.level = static_cast<unsigned char>(stmt.columnInt(9));
    h.exp = static_cast<unsigned int>(stmt.columnInt64(10));
    h.gender = static_cast<unsigned char>(stmt.columnInt(11));
    h.shiny = stmt.columnInt(12) != 0;
    h.ability_id = optionalU16(stmt, 13);
    h.ability_slot = optionalU8(stmt, 14);
    h.held_item_id = optionalU16(stmt, 15);
    for (int i = 0; i < 4; ++i) {
        h.move_ids[static_cast<std::size_t>(i)] = optionalU16(stmt, 16 + i);
        h.move_pp[static_cast<std::size_t>(i)] = optionalU8(stmt, 20 + i);
        h.move_pp_ups[static_cast<std::size_t>(i)] = optionalU8(stmt, 24 + i);
    }
    h.hp_current = static_cast<unsigned short>(stmt.columnInt(28));
    h.hp_max = static_cast<unsigned short>(stmt.columnInt(29));
    h.status_flags = static_cast<unsigned int>(stmt.columnInt64(30));
    h.ot_name = stmt.columnText(31);
    h.tid16 = optionalU16(stmt, 32);
    h.sid16 = optionalU16(stmt, 33);
    h.tid32 = optionalU32(stmt, 34);
    h.origin_game = static_cast<unsigned short>(stmt.columnInt(35));
    h.language = optionalU8(stmt, 36);
    h.met_location_id = optionalU16(stmt, 37);
    h.met_level = optionalU8(stmt, 38);
    h.met_date_unix = optionalI64(stmt, 39);
    h.ball_id = optionalU16(stmt, 40);
    h.pid = optionalU32(stmt, 41);
    h.encryption_constant = optionalU32(stmt, 42);
    h.home_tracker = optionalText(stmt, 43);
    h.lineage_root_species = static_cast<unsigned short>(stmt.columnInt(44));
    h.identity_strength = static_cast<unsigned char>(stmt.columnInt(45));
    p.warm.json = stmt.columnBlobAsString(46);
    p.cold.suspended_json = stmt.columnBlobAsString(47);
    return p;
}

} // namespace

PokemonRepository::PokemonRepository(SqliteConnection& connection)
    : connection_(connection) {}

void PokemonRepository::insert(const ResortPokemon& pokemon) {
    auto stmt = connection_.prepare(R"sql(
INSERT INTO pokemon (
    pkrid, origin_fingerprint, revision, created_at, updated_at,
    species_id, form_id, nickname, is_nicknamed, level, exp, gender, shiny,
    ability_id, ability_slot, held_item_id,
    move1_id, move2_id, move3_id, move4_id,
    move1_pp, move2_pp, move3_pp, move4_pp,
    move1_ppups, move2_ppups, move3_ppups, move4_ppups,
    hp_current, hp_max, status_flags,
    ot_name, tid16, sid16, tid32, origin_game, language,
    met_location_id, met_level, met_date, ball_id,
    pid, encryption_constant, home_tracker,
    lineage_root_species, identity_strength, warm_json, suspended_json
) VALUES (
    ?, ?, ?, ?, ?,
    ?, ?, ?, ?, ?, ?, ?, ?,
    ?, ?, ?,
    ?, ?, ?, ?,
    ?, ?, ?, ?,
    ?, ?, ?, ?,
    ?, ?, ?,
    ?, ?, ?, ?, ?, ?,
    ?, ?, ?, ?,
    ?, ?, ?,
    ?, ?, ?, ?
)
)sql");

    const PokemonHot& h = pokemon.hot;
    stmt.bindText(1, pokemon.id.pkrid);
    stmt.bindText(2, pokemon.id.origin_fingerprint);
    stmt.bindInt64(3, static_cast<long long>(pokemon.revision));
    stmt.bindInt64(4, pokemon.created_at_unix);
    stmt.bindInt64(5, pokemon.updated_at_unix);
    stmt.bindInt(6, h.species_id);
    stmt.bindInt(7, h.form_id);
    stmt.bindText(8, h.nickname);
    stmt.bindInt(9, h.is_nicknamed ? 1 : 0);
    stmt.bindInt(10, h.level);
    stmt.bindInt64(11, h.exp);
    stmt.bindInt(12, h.gender);
    stmt.bindInt(13, h.shiny ? 1 : 0);
    bindOptionalInt(stmt, 14, h.ability_id);
    bindOptionalInt(stmt, 15, h.ability_slot);
    bindOptionalInt(stmt, 16, h.held_item_id);
    for (int i = 0; i < 4; ++i) {
        bindOptionalInt(stmt, 17 + i, h.move_ids[static_cast<std::size_t>(i)]);
        bindOptionalInt(stmt, 21 + i, h.move_pp[static_cast<std::size_t>(i)]);
        bindOptionalInt(stmt, 25 + i, h.move_pp_ups[static_cast<std::size_t>(i)]);
    }
    stmt.bindInt(29, h.hp_current);
    stmt.bindInt(30, h.hp_max);
    stmt.bindInt64(31, h.status_flags);
    stmt.bindText(32, h.ot_name);
    bindOptionalInt(stmt, 33, h.tid16);
    bindOptionalInt(stmt, 34, h.sid16);
    bindOptionalInt(stmt, 35, h.tid32);
    stmt.bindInt(36, h.origin_game);
    bindOptionalInt(stmt, 37, h.language);
    bindOptionalInt(stmt, 38, h.met_location_id);
    bindOptionalInt(stmt, 39, h.met_level);
    if (h.met_date_unix) stmt.bindInt64(40, *h.met_date_unix); else stmt.bindNull(40);
    bindOptionalInt(stmt, 41, h.ball_id);
    bindOptionalInt(stmt, 42, h.pid);
    bindOptionalInt(stmt, 43, h.encryption_constant);
    if (h.home_tracker) stmt.bindText(44, *h.home_tracker); else stmt.bindNull(44);
    stmt.bindInt(45, h.lineage_root_species);
    stmt.bindInt(46, h.identity_strength);
    stmt.bindBlob(47, pokemon.warm.json.data(), static_cast<int>(pokemon.warm.json.size()));
    stmt.bindBlob(48, pokemon.cold.suspended_json.data(), static_cast<int>(pokemon.cold.suspended_json.size()));
    stmt.stepDone();
}

void PokemonRepository::updateAfterMerge(const ResortPokemon& pokemon) {
    auto stmt = connection_.prepare(R"sql(
UPDATE pokemon SET
    revision = ?,
    updated_at = ?,
    species_id = ?,
    form_id = ?,
    nickname = ?,
    is_nicknamed = ?,
    level = ?,
    exp = ?,
    gender = ?,
    shiny = ?,
    ability_id = ?,
    ability_slot = ?,
    held_item_id = ?,
    move1_id = ?,
    move2_id = ?,
    move3_id = ?,
    move4_id = ?,
    move1_pp = ?,
    move2_pp = ?,
    move3_pp = ?,
    move4_pp = ?,
    move1_ppups = ?,
    move2_ppups = ?,
    move3_ppups = ?,
    move4_ppups = ?,
    hp_current = ?,
    hp_max = ?,
    status_flags = ?,
    ot_name = ?,
    tid16 = ?,
    sid16 = ?,
    tid32 = ?,
    origin_game = ?,
    language = ?,
    met_location_id = ?,
    met_level = ?,
    met_date = ?,
    ball_id = ?,
    pid = ?,
    encryption_constant = ?,
    home_tracker = ?,
    lineage_root_species = ?,
    identity_strength = ?,
    warm_json = ?,
    suspended_json = ?
WHERE pkrid = ?
)sql");

    const PokemonHot& h = pokemon.hot;
    stmt.bindInt64(1, static_cast<long long>(pokemon.revision));
    stmt.bindInt64(2, pokemon.updated_at_unix);
    stmt.bindInt(3, h.species_id);
    stmt.bindInt(4, h.form_id);
    stmt.bindText(5, h.nickname);
    stmt.bindInt(6, h.is_nicknamed ? 1 : 0);
    stmt.bindInt(7, h.level);
    stmt.bindInt64(8, h.exp);
    stmt.bindInt(9, h.gender);
    stmt.bindInt(10, h.shiny ? 1 : 0);
    bindOptionalInt(stmt, 11, h.ability_id);
    bindOptionalInt(stmt, 12, h.ability_slot);
    bindOptionalInt(stmt, 13, h.held_item_id);
    for (int i = 0; i < 4; ++i) {
        bindOptionalInt(stmt, 14 + i, h.move_ids[static_cast<std::size_t>(i)]);
        bindOptionalInt(stmt, 18 + i, h.move_pp[static_cast<std::size_t>(i)]);
        bindOptionalInt(stmt, 22 + i, h.move_pp_ups[static_cast<std::size_t>(i)]);
    }
    stmt.bindInt(26, h.hp_current);
    stmt.bindInt(27, h.hp_max);
    stmt.bindInt64(28, h.status_flags);
    stmt.bindText(29, h.ot_name);
    bindOptionalInt(stmt, 30, h.tid16);
    bindOptionalInt(stmt, 31, h.sid16);
    bindOptionalInt(stmt, 32, h.tid32);
    stmt.bindInt(33, h.origin_game);
    bindOptionalInt(stmt, 34, h.language);
    bindOptionalInt(stmt, 35, h.met_location_id);
    bindOptionalInt(stmt, 36, h.met_level);
    if (h.met_date_unix) stmt.bindInt64(37, *h.met_date_unix); else stmt.bindNull(37);
    bindOptionalInt(stmt, 38, h.ball_id);
    bindOptionalInt(stmt, 39, h.pid);
    bindOptionalInt(stmt, 40, h.encryption_constant);
    if (h.home_tracker) stmt.bindText(41, *h.home_tracker); else stmt.bindNull(41);
    stmt.bindInt(42, h.lineage_root_species);
    stmt.bindInt(43, h.identity_strength);
    stmt.bindBlob(44, pokemon.warm.json.data(), static_cast<int>(pokemon.warm.json.size()));
    stmt.bindBlob(45, pokemon.cold.suspended_json.data(), static_cast<int>(pokemon.cold.suspended_json.size()));
    stmt.bindText(46, pokemon.id.pkrid);
    stmt.stepDone();
}

bool PokemonRepository::exists(const std::string& pkrid) const {
    auto stmt = connection_.prepare("SELECT 1 FROM pokemon WHERE pkrid = ? LIMIT 1");
    stmt.bindText(1, pkrid);
    return stmt.stepRow();
}

std::optional<ResortPokemon> PokemonRepository::findById(const std::string& pkrid) const {
    auto stmt = connection_.prepare(std::string("SELECT ") + kPokemonSelectColumns + " FROM pokemon WHERE pkrid = ?");
    stmt.bindText(1, pkrid);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return pokemonFromCurrentRow(stmt);
}

std::optional<ResortPokemon> PokemonRepository::findByHomeTracker(const std::string& home_tracker) const {
    auto stmt = connection_.prepare(std::string("SELECT ") + kPokemonSelectColumns + R"sql(
FROM pokemon
WHERE home_tracker = ?
ORDER BY updated_at DESC
LIMIT 1
)sql");
    stmt.bindText(1, home_tracker);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return pokemonFromCurrentRow(stmt);
}

std::optional<ResortPokemon> PokemonRepository::findByPidEcTidSidOt(
    std::uint32_t pid,
    std::uint32_t encryption_constant,
    std::optional<std::uint16_t> tid16,
    std::optional<std::uint16_t> sid16,
    const std::string& ot_name) const {
    auto stmt = connection_.prepare(std::string("SELECT ") + kPokemonSelectColumns + R"sql(
FROM pokemon
WHERE pid = ?
  AND encryption_constant = ?
  AND ((tid16 IS NULL AND ? IS NULL) OR tid16 = ?)
  AND ((sid16 IS NULL AND ? IS NULL) OR sid16 = ?)
  AND ot_name = ?
ORDER BY updated_at DESC
LIMIT 1
)sql");
    stmt.bindInt64(1, pid);
    stmt.bindInt64(2, encryption_constant);
    bindOptionalMatch(stmt, 3, tid16);
    bindOptionalMatch(stmt, 4, tid16);
    bindOptionalMatch(stmt, 5, sid16);
    bindOptionalMatch(stmt, 6, sid16);
    stmt.bindText(7, ot_name);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return pokemonFromCurrentRow(stmt);
}

std::optional<ResortPokemon> PokemonRepository::findByPidTidSidOt(
    std::uint32_t pid,
    std::optional<std::uint16_t> tid16,
    std::optional<std::uint16_t> sid16,
    const std::string& ot_name) const {
    auto stmt = connection_.prepare(std::string("SELECT ") + kPokemonSelectColumns + R"sql(
FROM pokemon
WHERE pid = ?
  AND ((tid16 IS NULL AND ? IS NULL) OR tid16 = ?)
  AND ((sid16 IS NULL AND ? IS NULL) OR sid16 = ?)
  AND ot_name = ?
ORDER BY updated_at DESC
LIMIT 1
)sql");
    stmt.bindInt64(1, pid);
    bindOptionalMatch(stmt, 2, tid16);
    bindOptionalMatch(stmt, 3, tid16);
    bindOptionalMatch(stmt, 4, sid16);
    bindOptionalMatch(stmt, 5, sid16);
    stmt.bindText(6, ot_name);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return pokemonFromCurrentRow(stmt);
}

} // namespace pr::resort
