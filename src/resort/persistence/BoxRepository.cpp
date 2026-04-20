#include "resort/persistence/BoxRepository.hpp"

#include <sstream>
#include <stdexcept>

namespace pr::resort {

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
        view.hp_current = static_cast<unsigned short>(stmt.columnInt(9));
        view.hp_max = static_cast<unsigned short>(stmt.columnInt(10));
        view.status_icon = static_cast<unsigned char>(stmt.columnInt(11) & 0xff);
        views.push_back(std::move(view));
    }
    return views;
}

} // namespace pr::resort
