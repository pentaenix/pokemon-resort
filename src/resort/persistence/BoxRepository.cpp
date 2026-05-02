#include "resort/persistence/BoxRepository.hpp"

#include "core/config/Json.hpp"

#include <optional>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace pr::resort {

namespace {

struct SlotWarmMetadata {
    std::string source_game_key;
    std::string species_slug;
    std::string species_name;
    std::string form_key;
    std::string held_item_name;
    std::string nature;
    std::string ability_name;
    std::string primary_type;
    std::string secondary_type;
    std::string tera_type;
    std::string mark_icon;
    std::string pokerus_status;
    bool is_alpha = false;
    bool is_gigantamax = false;
    std::uint32_t markings = 0;
};

std::string stringField(const pr::JsonValue& object, const char* key) {
    if (const pr::JsonValue* value = object.isObject() ? object.get(key) : nullptr) {
        if (value->isString()) {
            return value->asString();
        }
    }
    return {};
}

bool boolField(const pr::JsonValue& object, const char* key) {
    if (const pr::JsonValue* value = object.isObject() ? object.get(key) : nullptr) {
        if (value->isBool()) {
            return value->asBool();
        }
    }
    return false;
}

std::uint32_t u32Field(const pr::JsonValue& object, const char* key) {
    if (const pr::JsonValue* value = object.isObject() ? object.get(key) : nullptr) {
        if (value->isNumber() && value->asNumber() >= 0.0) {
            return static_cast<std::uint32_t>(value->asNumber());
        }
    }
    return 0;
}

SlotWarmMetadata slotWarmMetadataFromJson(const std::string& warm_json) {
    SlotWarmMetadata out;
    if (warm_json.empty()) {
        return out;
    }
    try {
        const pr::JsonValue root = pr::parseJsonText(warm_json);
        out.source_game_key = stringField(root, "source_game_key");
        if (const pr::JsonValue* context = root.isObject() ? root.get("source_context") : nullptr) {
            if (out.source_game_key.empty()) {
                out.source_game_key = stringField(*context, "game_key");
            }
        }
        out.species_slug = stringField(root, "species_slug");
        out.species_name = stringField(root, "species_name");
        out.form_key = stringField(root, "form_key");
        out.held_item_name = stringField(root, "held_item_name");
        out.nature = stringField(root, "nature");
        out.ability_name = stringField(root, "ability_name");
        out.primary_type = stringField(root, "primary_type");
        out.secondary_type = stringField(root, "secondary_type");
        out.tera_type = stringField(root, "tera_type");
        out.mark_icon = stringField(root, "mark_icon");
        out.pokerus_status = stringField(root, "pokerus_status");
        out.is_alpha = boolField(root, "is_alpha");
        out.is_gigantamax = boolField(root, "is_gigantamax");
        out.markings = u32Field(root, "markings");
    } catch (...) {
    }
    return out;
}

} // namespace

BoxRepository::BoxRepository(SqliteConnection& connection)
    : connection_(connection) {}

void BoxRepository::ensureDefaultBoxes(const std::string& profile_id, int box_count, int slots_per_box) {
    auto box_stmt = connection_.prepare(
        "INSERT OR IGNORE INTO boxes (profile_id, box_id, name, wallpaper_id, sort_key) VALUES (?, ?, ?, NULL, ?)");
    auto slot_stmt = connection_.prepare(
        "INSERT OR IGNORE INTO box_slots (profile_id, box_id, slot_index, pkrid) VALUES (?, ?, ?, NULL)");
    for (int box = 0; box < box_count; ++box) {
        std::ostringstream name;
        name << "Box " << (box + 1);
        box_stmt.bindText(1, profile_id);
        box_stmt.bindInt(2, box);
        box_stmt.bindText(3, name.str());
        box_stmt.bindInt(4, box);
        box_stmt.stepDone();
        box_stmt.reset();

        for (int slot = 0; slot < slots_per_box; ++slot) {
            slot_stmt.bindText(1, profile_id);
            slot_stmt.bindInt(2, box);
            slot_stmt.bindInt(3, slot);
            slot_stmt.stepDone();
            slot_stmt.reset();
        }
    }
}

std::vector<std::pair<int, std::string>> BoxRepository::listBoxes(const std::string& profile_id) const {
    auto stmt = connection_.prepare(
        "SELECT box_id, name FROM boxes WHERE profile_id = ? ORDER BY sort_key ASC, box_id ASC");
    stmt.bindText(1, profile_id);
    std::vector<std::pair<int, std::string>> out;
    while (stmt.stepRow()) {
        out.emplace_back(stmt.columnInt(0), stmt.columnText(1));
    }
    return out;
}

void BoxRepository::swapBoxContents(const std::string& profile_id, int box_a, int box_b) {
    if (box_a == box_b) {
        return;
    }
    auto read_pkrid = [&](int box_id, int slot_index) -> std::optional<std::string> {
        auto stmt = connection_.prepare(
            "SELECT pkrid FROM box_slots WHERE profile_id = ? AND box_id = ? AND slot_index = ? LIMIT 1");
        stmt.bindText(1, profile_id);
        stmt.bindInt(2, box_id);
        stmt.bindInt(3, slot_index);
        if (!stmt.stepRow()) {
            return std::nullopt;
        }
        if (stmt.columnIsNull(0)) {
            return std::nullopt;
        }
        return stmt.columnText(0);
    };
    auto write_pkrid = [&](int box_id, int slot_index, const std::optional<std::string>& pkrid) {
        auto stmt = connection_.prepare(
            "UPDATE box_slots SET pkrid = ? WHERE profile_id = ? AND box_id = ? AND slot_index = ?");
        if (pkrid.has_value()) {
            stmt.bindText(1, *pkrid);
        } else {
            stmt.bindNull(1);
        }
        stmt.bindText(2, profile_id);
        stmt.bindInt(3, box_id);
        stmt.bindInt(4, slot_index);
        stmt.stepDone();
    };

    for (int slot = 0; slot < 30; ++slot) {
        std::optional<std::string> pa = read_pkrid(box_a, slot);
        std::optional<std::string> pb = read_pkrid(box_b, slot);
        write_pkrid(box_a, slot, pb);
        write_pkrid(box_b, slot, pa);
    }
}

void BoxRepository::renameBox(const std::string& profile_id, int box_id, const std::string& name) {
    auto stmt = connection_.prepare("UPDATE boxes SET name = ? WHERE profile_id = ? AND box_id = ?");
    stmt.bindText(1, name);
    stmt.bindText(2, profile_id);
    stmt.bindInt(3, box_id);
    stmt.stepDone();
}

void BoxRepository::swapSlotContents(const BoxLocation& a, const BoxLocation& b) {
    if (a.profile_id != b.profile_id) {
        throw std::runtime_error("swapSlotContents requires same profile_id");
    }
    if (a.box_id == b.box_id && a.slot_index == b.slot_index) {
        return;
    }
    auto read_one = [&](const BoxLocation& loc) -> std::optional<std::string> {
        auto stmt = connection_.prepare(
            "SELECT pkrid FROM box_slots WHERE profile_id = ? AND box_id = ? AND slot_index = ? LIMIT 1");
        stmt.bindText(1, loc.profile_id);
        stmt.bindInt(2, loc.box_id);
        stmt.bindInt(3, loc.slot_index);
        if (!stmt.stepRow()) {
            return std::nullopt;
        }
        if (stmt.columnIsNull(0)) {
            return std::nullopt;
        }
        return stmt.columnText(0);
    };
    auto write_one = [&](const BoxLocation& loc, const std::optional<std::string>& pkrid) {
        auto stmt = connection_.prepare(
            "UPDATE box_slots SET pkrid = ? WHERE profile_id = ? AND box_id = ? AND slot_index = ?");
        if (pkrid.has_value()) {
            stmt.bindText(1, *pkrid);
        } else {
            stmt.bindNull(1);
        }
        stmt.bindText(2, loc.profile_id);
        stmt.bindInt(3, loc.box_id);
        stmt.bindInt(4, loc.slot_index);
        stmt.stepDone();
    };

    std::optional<std::string> pa = read_one(a);
    std::optional<std::string> pb = read_one(b);
    write_one(a, pb);
    write_one(b, pa);
}

void BoxRepository::placePokemon(
    const BoxLocation& location,
    const std::string& pkrid,
    BoxPlacementPolicy policy) {
    auto occupant_stmt = connection_.prepare(
        "SELECT pkrid FROM box_slots WHERE profile_id = ? AND box_id = ? AND slot_index = ? LIMIT 1");
    occupant_stmt.bindText(1, location.profile_id);
    occupant_stmt.bindInt(2, location.box_id);
    occupant_stmt.bindInt(3, location.slot_index);
    if (!occupant_stmt.stepRow()) {
        throw std::runtime_error("Could not place Pokemon: target box slot does not exist");
    }
    const bool occupied = !occupant_stmt.columnIsNull(0);
    const std::string occupant = occupied ? occupant_stmt.columnText(0) : std::string();
    if (occupied && occupant != pkrid && policy == BoxPlacementPolicy::RejectIfOccupied) {
        throw std::runtime_error("Could not place Pokemon: target box slot is occupied");
    }

    removePokemon(location.profile_id, pkrid);
    auto stmt = connection_.prepare(
        "UPDATE box_slots SET pkrid = ? WHERE profile_id = ? AND box_id = ? AND slot_index = ?");
    stmt.bindText(1, pkrid);
    stmt.bindText(2, location.profile_id);
    stmt.bindInt(3, location.box_id);
    stmt.bindInt(4, location.slot_index);
    stmt.stepDone();
    if (connection_.changes() != 1) {
        throw std::runtime_error("Could not place Pokemon: target box slot does not exist");
    }
}

void BoxRepository::removePokemon(const std::string& profile_id, const std::string& pkrid) {
    auto stmt = connection_.prepare("UPDATE box_slots SET pkrid = NULL WHERE profile_id = ? AND pkrid = ?");
    stmt.bindText(1, profile_id);
    stmt.bindText(2, pkrid);
    stmt.stepDone();
}

std::optional<BoxLocation> BoxRepository::findFirstEmptySlot(const std::string& profile_id) const {
    auto stmt = connection_.prepare(R"sql(
SELECT s.box_id, s.slot_index
FROM box_slots s
JOIN boxes b ON b.profile_id = s.profile_id AND b.box_id = s.box_id
WHERE s.profile_id = ? AND s.pkrid IS NULL
ORDER BY b.sort_key ASC, s.box_id ASC, s.slot_index ASC
LIMIT 1
)sql");
    stmt.bindText(1, profile_id);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return BoxLocation{profile_id, stmt.columnInt(0), stmt.columnInt(1)};
}

std::optional<BoxLocation> BoxRepository::findPokemonLocation(
    const std::string& profile_id,
    const std::string& pkrid) const {
    auto stmt = connection_.prepare(
        "SELECT box_id, slot_index FROM box_slots WHERE profile_id = ? AND pkrid = ? LIMIT 1");
    stmt.bindText(1, profile_id);
    stmt.bindText(2, pkrid);
    if (!stmt.stepRow()) {
        return std::nullopt;
    }
    return BoxLocation{profile_id, stmt.columnInt(0), stmt.columnInt(1)};
}

std::vector<PokemonSlotView> BoxRepository::getBoxSlotViews(const std::string& profile_id, int box_id) const {
    auto stmt = connection_.prepare(R"sql(
SELECT
    s.slot_index,
    p.pkrid,
    p.species_id,
    p.form_id,
    COALESCE(NULLIF(p.nickname, ''), p.pkrid) AS display_name,
    p.level,
    p.shiny,
    p.gender,
    p.held_item_id,
    p.ability_id,
    p.ability_slot,
    p.ot_name,
    p.origin_game,
    (
        SELECT ps.game_id
        FROM pokemon_snapshots ps
        WHERE ps.pkrid = p.pkrid AND ps.kind = 0 AND ps.game_id IS NOT NULL
        ORDER BY ps.captured_at DESC
        LIMIT 1
    ) AS source_game,
    p.warm_json,
    p.ball_id,
    p.hp_current,
    p.hp_max,
    p.status_flags
FROM box_slots s
LEFT JOIN pokemon p ON p.pkrid = s.pkrid
WHERE s.profile_id = ? AND s.box_id = ?
ORDER BY s.slot_index
)sql");
    stmt.bindText(1, profile_id);
    stmt.bindInt(2, box_id);

    std::vector<PokemonSlotView> views;
    while (stmt.stepRow()) {
        if (stmt.columnIsNull(1)) {
            continue;
        }
        PokemonSlotView view;
        view.slot_index = stmt.columnInt(0);
        view.pkrid = stmt.columnText(1);
        view.species_id = static_cast<unsigned short>(stmt.columnInt(2));
        view.form_id = static_cast<unsigned short>(stmt.columnInt(3));
        view.display_name = stmt.columnText(4);
        view.level = static_cast<unsigned char>(stmt.columnInt(5));
        view.shiny = stmt.columnInt(6) != 0;
        view.gender = static_cast<unsigned char>(stmt.columnInt(7));
        if (!stmt.columnIsNull(8)) {
            view.held_item_id = static_cast<unsigned short>(stmt.columnInt(8));
        }
        if (!stmt.columnIsNull(9)) {
            view.ability_id = static_cast<unsigned short>(stmt.columnInt(9));
        }
        if (!stmt.columnIsNull(10)) {
            view.ability_slot = static_cast<unsigned char>(stmt.columnInt(10));
        }
        view.ot_name = stmt.columnText(11);
        view.origin_game = static_cast<unsigned short>(stmt.columnInt(12));
        if (!stmt.columnIsNull(13)) {
            view.source_game = static_cast<unsigned short>(stmt.columnInt(13));
        }
        const SlotWarmMetadata warm = slotWarmMetadataFromJson(stmt.columnBlobAsString(14));
        view.source_game_key = warm.source_game_key;
        view.species_slug = warm.species_slug;
        view.species_name = warm.species_name;
        view.form_key = warm.form_key;
        view.held_item_name = warm.held_item_name;
        view.nature = warm.nature;
        view.ability_name = warm.ability_name;
        view.primary_type = warm.primary_type;
        view.secondary_type = warm.secondary_type;
        view.tera_type = warm.tera_type;
        view.mark_icon = warm.mark_icon;
        view.pokerus_status = warm.pokerus_status;
        view.is_alpha = warm.is_alpha;
        view.is_gigantamax = warm.is_gigantamax;
        view.markings = warm.markings;
        if (!stmt.columnIsNull(15)) {
            view.ball_id = static_cast<unsigned short>(stmt.columnInt(15));
        }
        view.hp_current = static_cast<unsigned short>(stmt.columnInt(16));
        view.hp_max = static_cast<unsigned short>(stmt.columnInt(17));
        view.status_icon = static_cast<unsigned char>(stmt.columnInt(18) & 0xff);
        views.push_back(std::move(view));
    }
    return views;
}

} // namespace pr::resort
