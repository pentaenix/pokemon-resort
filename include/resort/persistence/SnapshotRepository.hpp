#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <optional>
#include <string>

namespace pr::resort {

class SnapshotRepository {
public:
    explicit SnapshotRepository(SqliteConnection& connection);

    void insert(const PokemonSnapshot& snapshot);
    std::optional<PokemonSnapshot> findLatestRawForPokemon(
        const std::string& pkrid,
        std::optional<std::uint16_t> game_id = std::nullopt,
        const std::string& format_name = {}) const;

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
