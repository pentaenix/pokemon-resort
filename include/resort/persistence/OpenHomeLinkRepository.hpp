#pragma once

#include "resort/openhome/OpenHomePokemonPayload.hpp"
#include "resort/persistence/SqliteConnection.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pr::resort {

enum class PokemonPlacementKind {
    ResortBox,
    InGameSave,
    HomeBank
};

struct PokemonPlacementRecord {
    std::string pkrid;
    PokemonPlacementKind kind = PokemonPlacementKind::ResortBox;
    std::string profile_id;
    std::optional<std::uint16_t> game_id;
    std::string save_path;
    int box_index = -1;
    int slot_index = -1;
    std::string openhome_id;
    std::int64_t updated_at_unix = 0;
};

class OpenHomeLinkRepository {
public:
    explicit OpenHomeLinkRepository(SqliteConnection& connection);

    void upsertPayloadForPokemon(
        const std::string& pkrid,
        const openhome::OpenHomePokemonPayload& payload,
        std::int64_t updated_at_unix);

    std::optional<std::string> findOpenHomeIdForPokemon(const std::string& pkrid) const;
    /// Batched lookup for Tier-1 marker hints (one query per phase; ignores empty `pkrids` entries).
    std::unordered_map<std::string, std::string> findOpenHomeIdsForPokemon(const std::vector<std::string>& pkrids) const;

    /// Keys compatible with `pr::openHomeProfileMatchKeyFromSlot` for Resort rows with a valid OpenHome id.
    void loadPidEcOtOpenHomeProfileMatchKeys(std::unordered_set<std::string>& out) const;

    std::optional<std::string> findPokemonForOpenHomeId(const std::string& openhome_id) const;

    void recordPlacement(const PokemonPlacementRecord& placement);
    std::optional<PokemonPlacementRecord> findPlacementForPokemon(const std::string& pkrid) const;

private:
    SqliteConnection& connection_;
};

} // namespace pr::resort
