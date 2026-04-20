#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/SqliteConnection.hpp"

namespace pr::resort {

class HistoryRepository {
public:
    explicit HistoryRepository(SqliteConnection& connection);

    void insert(const PokemonHistoryEvent& event);

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
