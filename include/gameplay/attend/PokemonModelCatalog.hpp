#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pr::gameplay::attend {

struct PokemonModelCatalogEntry {
    int dex_number = 0;
    int form_number = 0;
    std::string id;
    std::string path;
};

PokemonModelCatalogEntry pokemonModelCatalogEntry(const std::filesystem::path& path);
std::vector<PokemonModelCatalogEntry> discoverPokemonModels(const std::filesystem::path& directory);
int wrappedPokemonModelCatalogIndex(int current_index, int offset, int entry_count);

} // namespace pr::gameplay::attend
