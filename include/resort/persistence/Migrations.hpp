#pragma once

#include "resort/persistence/SqliteConnection.hpp"

namespace pr::resort {

constexpr int kCurrentResortSchemaVersion = 5;

void runResortMigrations(SqliteConnection& connection);

} // namespace pr::resort
