#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::resort {

/// Matches transfer UI default (`game_transfer.json` `resort_pc_box_count`) for first-time profile bootstrap.
inline constexpr int kDefaultResortPcBoxCount = 60;

class BoxRepository {
public:
    explicit BoxRepository(SqliteConnection& connection);

    void ensureDefaultBoxes(
        const std::string& profile_id,
        int box_count = kDefaultResortPcBoxCount,
        int slots_per_box = 30);

    /// Ordered by `sort_key` / `box_id` (creation order).
    std::vector<std::pair<int, std::string>> listBoxes(const std::string& profile_id) const;

    /// Pairwise swap of `pkrid` occupancy for each slot index (used when the UI swaps two Resort boxes).
    void swapBoxContents(const std::string& profile_id, int box_a, int box_b);

    /// Swap two slot occupants (used when swapping two Pokémon within Resort storage).
    void swapSlotContents(const BoxLocation& a, const BoxLocation& b);

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
