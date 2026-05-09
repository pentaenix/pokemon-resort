#pragma once

#include "resort/openhome/OpenHomePokemonPayload.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pr::resort::openhome {

struct OpenHomeBox {
    std::string id;
    std::optional<std::string> name;
    int index = 0;
    std::map<int, OpenHomeId> identifiers_by_slot;
};

struct OpenHomeBank {
    std::string id;
    std::optional<std::string> name;
    int index = 0;
    int current_box = 0;
    std::vector<OpenHomeBox> boxes;
};

struct OpenHomeBankData {
    std::vector<OpenHomeBank> banks;
    int current_bank = 0;
};

struct OpenHomeStoredPokemon {
    OpenHomeId openhome_id;
    OpenHomePokemonPayload payload;
};

class OpenHomeStorageBridge {
public:
    explicit OpenHomeStorageBridge(std::filesystem::path storage_root);

    const std::filesystem::path& storageRoot() const;
    std::filesystem::path monsDirectory() const;
    std::filesystem::path banksPath() const;

    std::vector<OpenHomeStoredPokemon> loadOhpkmStore() const;
    void upsertOhpkm(const OpenHomePokemonPayload& payload) const;
    bool removeOhpkm(const OpenHomeId& openhome_id) const;

    OpenHomeBankData loadHomeBanks() const;
    void writeHomeBanks(const OpenHomeBankData& data) const;
    void placeInHomeBox(const OpenHomeId& openhome_id, int bank, int box, int slot) const;

private:
    std::filesystem::path storage_root_;
};

OpenHomeBankData defaultOpenHomeBankData();

} // namespace pr::resort::openhome
