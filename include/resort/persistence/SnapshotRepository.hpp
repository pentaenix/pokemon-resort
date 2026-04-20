#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/SqliteConnection.hpp"

namespace pr::resort {

class SnapshotRepository {
public:
    explicit SnapshotRepository(SqliteConnection& connection);

    void insert(const PokemonSnapshot& snapshot);

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
