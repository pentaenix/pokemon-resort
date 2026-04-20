#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <optional>
#include <string>

namespace pr::resort {

class PokemonRepository {
public:
    explicit PokemonRepository(SqliteConnection& connection);

    void insert(const ResortPokemon& pokemon);
    void updateAfterMerge(const ResortPokemon& pokemon);
    bool exists(const std::string& pkrid) const;
    std::optional<ResortPokemon> findById(const std::string& pkrid) const;
    std::optional<ResortPokemon> findByHomeTracker(const std::string& home_tracker) const;
    std::optional<ResortPokemon> findByPidEcTidSidOt(
        std::uint32_t pid,
        std::uint32_t encryption_constant,
        std::optional<std::uint16_t> tid16,
        std::optional<std::uint16_t> sid16,
        const std::string& ot_name) const;
    std::optional<ResortPokemon> findByPidTidSidOt(
        std::uint32_t pid,
        std::optional<std::uint16_t> tid16,
        std::optional<std::uint16_t> sid16,
        const std::string& ot_name) const;

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
