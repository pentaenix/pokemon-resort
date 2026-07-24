#include "gameplay/attend/PokemonModelCatalog.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace pr::gameplay::attend {

namespace {

std::string normalizeId(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c == ' ' || c == '-') return '_';
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int decimalAt(const std::string& value, std::size_t offset, std::size_t count) {
    if (offset + count > value.size()) return 0;
    int number = 0;
    for (std::size_t i = offset; i < offset + count; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (!std::isdigit(c)) return 0;
        number = number * 10 + (value[i] - '0');
    }
    return number;
}

int formatRank(const std::string& path) {
    return std::filesystem::path(path).extension() == ".glbz" ? 0 : 1;
}

} // namespace

PokemonModelCatalogEntry pokemonModelCatalogEntry(const std::filesystem::path& path) {
    PokemonModelCatalogEntry entry;
    entry.path = path.string();

    std::string stem = path.stem().string();
    const std::string normalized = normalizeId(stem);
    if (normalized.size() > 10 && normalized.rfind("pm", 0) == 0 && normalized[6] == '_') {
        entry.dex_number = decimalAt(normalized, 2, 4);
        entry.form_number = decimalAt(normalized, 7, 2);
        const std::size_t species_name_separator = normalized.find('_', 7);
        if (species_name_separator != std::string::npos && species_name_separator + 1 < stem.size()) {
            stem = stem.substr(species_name_separator + 1);
        }
    }
    entry.id = normalizeId(std::move(stem));
    return entry;
}

std::vector<PokemonModelCatalogEntry> discoverPokemonModels(const std::filesystem::path& directory) {
    std::vector<PokemonModelCatalogEntry> entries;
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) return entries;

    for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator(directory, ec)) {
        if (ec || !file.is_regular_file()) continue;
        const std::filesystem::path extension = file.path().extension();
        if (extension != ".glb" && extension != ".glbz") continue;
        entries.push_back(pokemonModelCatalogEntry(file.path()));
    }

    std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
        const int lhs_dex = lhs.dex_number > 0 ? lhs.dex_number : std::numeric_limits<int>::max();
        const int rhs_dex = rhs.dex_number > 0 ? rhs.dex_number : std::numeric_limits<int>::max();
        if (lhs_dex != rhs_dex) return lhs_dex < rhs_dex;
        if (lhs.form_number != rhs.form_number) return lhs.form_number < rhs.form_number;
        if (lhs.id != rhs.id) return lhs.id < rhs.id;
        const int lhs_format = formatRank(lhs.path);
        const int rhs_format = formatRank(rhs.path);
        if (lhs_format != rhs_format) return lhs_format < rhs_format;
        return lhs.path < rhs.path;
    });
    entries.erase(
        std::unique(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.id == rhs.id;
        }),
        entries.end());
    return entries;
}

int wrappedPokemonModelCatalogIndex(int current_index, int offset, int entry_count) {
    if (entry_count <= 0) return -1;
    const long long count = entry_count;
    const long long next = static_cast<long long>(current_index) + static_cast<long long>(offset);
    return static_cast<int>((next % count + count) % count);
}

} // namespace pr::gameplay::attend
