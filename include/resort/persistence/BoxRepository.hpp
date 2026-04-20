#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::resort {

class BoxRepository {
public:
    explicit BoxRepository(SqliteConnection& connection);

    void ensureDefaultBoxes(const std::string& profile_id, int box_count = 8, int slots_per_box = 30);
    void placePokemon(
        const BoxLocation& location,
        const std::string& pkrid,
        BoxPlacementPolicy policy = BoxPlacementPolicy::RejectIfOccupied);
    void removePokemon(const std::string& profile_id, const std::string& pkrid);
    std::optional<BoxLocation> findPokemonLocation(const std::string& profile_id, const std::string& pkrid) const;
    std::vector<PokemonSlotView> getBoxSlotViews(const std::string& profile_id, int box_id) const;

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
