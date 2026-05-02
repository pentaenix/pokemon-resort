#include "core/domain/PokemonOrigin.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string_view>

namespace pr {
namespace {

struct GameMetadata {
    std::string_view id;
    std::string_view title;
    std::string_view code;
    std::string_view region;
};

constexpr GameMetadata kGames[] = {
    {"pokemon_red", "Pokemon Red", "Rd", "kanto"},
    {"pokemon_blue", "Pokemon Blue", "Bu", "kanto"},
    {"pokemon_yellow", "Pokemon Yellow", "Yw", "kanto"},
    {"pokemon_gn", "Pokemon Red / Blue / Yellow", "R/B/Y", "kanto"},
    {"pokemon_gold", "Pokemon Gold", "G", "johto"},
    {"pokemon_silver", "Pokemon Silver", "S", "johto"},
    {"pokemon_crystal", "Pokemon Crystal", "C", "johto"},
    {"pokemon_gs", "Pokemon Gold / Silver", "G/S", "johto"},
    {"pokemon_ruby", "Pokemon Ruby", "Ru", "hoenn"},
    {"pokemon_sapphire", "Pokemon Sapphire", "Sa", "hoenn"},
    {"pokemon_emerald", "Pokemon Emerald", "E", "hoenn"},
    {"pokemon_firered", "Pokemon FireRed", "FR", "kanto"},
    {"pokemon_leafgreen", "Pokemon LeafGreen", "LG", "kanto"},
    {"pokemon_diamond", "Pokemon Diamond", "D", "sinnoh"},
    {"pokemon_pearl", "Pokemon Pearl", "P", "sinnoh"},
    {"pokemon_platinum", "Pokemon Platinum", "Pt", "sinnoh"},
    {"pokemon_heartgold", "Pokemon Heart Gold", "HG", "johto"},
    {"pokemon_soulsilver", "Pokemon Soul Silver", "SS", "johto"},
    {"pokemon_hgss", "Pokemon Heart Gold / Soul Silver", "HG/SS", "johto"},
    {"pokemon_black", "Pokemon Black", "B", "unova"},
    {"pokemon_white", "Pokemon White", "W", "unova"},
    {"pokemon_black_2", "Pokemon Black 2", "B2", "unova"},
    {"pokemon_white_2", "Pokemon White 2", "W2", "unova"},
    {"pokemon_x", "Pokemon X", "X", "kalos"},
    {"pokemon_y", "Pokemon Y", "Y", "kalos"},
    {"pokemon_omega_ruby", "Pokemon Omega Ruby", "OR", "hoenn"},
    {"pokemon_alpha_sapphire", "Pokemon Alpha Sapphire", "AS", "hoenn"},
    {"pokemon_sun", "Pokemon Sun", "Su", "alola"},
    {"pokemon_moon", "Pokemon Moon", "Mo", "alola"},
    {"pokemon_ultra_sun", "Pokemon Ultra Sun", "US", "alola"},
    {"pokemon_ultra_moon", "Pokemon Ultra Moon", "UM", "alola"},
    {"pokemon_lets_go_pikachu", "Pokemon Let's Go Pikachu", "LGP", "alola"},
    {"pokemon_lets_go_eevee", "Pokemon Let's Go Eevee", "LGE", "alola"},
    {"pokemon_sword", "Pokemon Sword", "Sw", "galar"},
    {"pokemon_shield", "Pokemon Shield", "Sh", "galar"},
    {"pokemon_swsh", "Pokemon Sword / Shield", "Sw/Sh", "galar"},
    {"pokemon_brilliant_diamond", "Pokemon Brilliant Diamond", "BD", "sinnoh"},
    {"pokemon_shining_pearl", "Pokemon Shining Pearl", "SP", "sinnoh"},
    {"pokemon_legends_arceus", "Pokemon Legends Arceus", "LA", "hisui"},
    {"pokemon_scarlet", "Pokemon Scarlet", "Sc", "paldea"},
    {"pokemon_violet", "Pokemon Violet", "Vi", "paldea"},
    {"pokemon_sv", "Pokemon Scarlet / Violet", "Sc/Vi", "paldea"},
};

std::string asciiLower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

std::string normalizeAlnumToken(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char ch : text) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
        }
    }
    return out;
}

const GameMetadata* findGameById(const std::string& game_id) {
    for (const GameMetadata& game : kGames) {
        if (game.id == game_id) {
            return &game;
        }
    }
    return nullptr;
}

std::string titleCaseFallback(std::string game_id) {
    for (char& c : game_id) {
        if (c == '_') {
            c = ' ';
        }
    }

    bool capitalize_next = true;
    for (char& c : game_id) {
        if (c == ' ') {
            capitalize_next = true;
            continue;
        }
        if (capitalize_next && c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
        capitalize_next = false;
    }
    return game_id;
}

bool containsToken(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

bool filenameHintsHeartGold(const std::string& filename_hint) {
    const std::string hint = asciiLower(filename_hint);
    return containsToken(hint, "heartgold") || containsToken(hint, "heart_gold") ||
           containsToken(hint, "heart gold") || containsToken(hint, "hg");
}

bool filenameHintsSoulSilver(const std::string& filename_hint) {
    const std::string hint = asciiLower(filename_hint);
    return containsToken(hint, "soulsilver") || containsToken(hint, "soul_silver") ||
           containsToken(hint, "soul silver") || containsToken(hint, "ss");
}

std::optional<int> extractRouteNumber(const std::string& text) {
    const std::string lower = asciiLower(text);
    const std::size_t route = lower.find("route");
    if (route == std::string::npos) {
        return std::nullopt;
    }
    std::size_t pos = route + 5;
    while (pos < lower.size() && !std::isdigit(static_cast<unsigned char>(lower[pos]))) {
        ++pos;
    }
    if (pos >= lower.size()) {
        return std::nullopt;
    }
    std::size_t end = pos;
    while (end < lower.size() && std::isdigit(static_cast<unsigned char>(lower[end]))) {
        ++end;
    }
    try {
        return std::stoi(lower.substr(pos, end - pos));
    } catch (...) {
        return std::nullopt;
    }
}

bool containsAnyToken(const std::string& haystack, const std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (haystack.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string inferKantoJohtoRegionFromMetLocation(const std::string& met_location_name) {
    const std::string location = asciiLower(met_location_name);
    if (location.empty()) {
        return {};
    }

    if (const std::optional<int> route = extractRouteNumber(location)) {
        if (*route >= 1 && *route <= 28) {
            return "kanto";
        }
        if (*route >= 29 && *route <= 48) {
            return "johto";
        }
    }

    if (containsAnyToken(location,
                         {"kanto", "pallet", "viridian", "pewter", "cerulean", "vermilion", "lavender",
                          "celadon", "fuchsia", "saffron", "cinnabar", "diglett", "mt. moon", "mount moon",
                          "rock tunnel", "seafoam", "power plant", "tohjo"})) {
        return "kanto";
    }

    if (containsAnyToken(location,
                         {"johto", "new bark", "cherrygrove", "violet", "azalea", "goldenrod", "ecruteak",
                          "olivine", "cianwood", "mahogany", "blackthorn", "sprout tower", "burned tower",
                          "bell tower", "whirl islands", "dragon's den", "dragon den", "union cave",
                          "slowpoke well", "national park", "pokeathlon", "pokathlon", "sinjoh"})) {
        return "johto";
    }

    return {};
}

std::string inferRegionFromMetLocationText(const std::string& met_location_name) {
    const std::string location = asciiLower(met_location_name);
    if (location.empty()) {
        return {};
    }
    if (location.find("kanto") != std::string::npos) return "kanto";
    if (location.find("johto") != std::string::npos) return "johto";
    if (location.find("unova") != std::string::npos) return "unova";
    if (location.find("hoenn") != std::string::npos) return "hoenn";
    if (location.find("sinnoh") != std::string::npos) return "sinnoh";
    if (location.find("kalos") != std::string::npos) return "kalos";
    if (location.find("alola") != std::string::npos) return "alola";
    if (location.find("galar") != std::string::npos) return "galar";
    if (location.find("hisui") != std::string::npos) return "hisui";
    if (location.find("paldea") != std::string::npos) return "paldea";
    return {};
}

} // namespace

std::string normalizePokemonGameId(const std::string& raw_game, const std::string& filename_hint) {
    const std::string raw_lower = asciiLower(raw_game);
    if (findGameById(raw_lower)) {
        return raw_lower;
    }

    const std::string token = normalizeAlnumToken(raw_game);
    if (token.empty()) {
        return {};
    }

    if (token == "pokemonred" || token == "pokemonrd" || token == "red" || token == "rd") return "pokemon_red";
    if (token == "pokemonblue" || token == "pokemonbu" || token == "blue" || token == "bu") return "pokemon_blue";
    if (token == "pokemonyellow" || token == "pokemonyw" || token == "yellow" || token == "yw") return "pokemon_yellow";
    if (token == "pokemongn" || token == "gn") return "pokemon_gn";
    if (token == "pokemongold" || token == "pokemongd" || token == "gold" || token == "gd") return "pokemon_gold";
    if (token == "pokemonsilver" || token == "pokemonsi" || token == "silver" || token == "si") return "pokemon_silver";
    if (token == "pokemoncrystal" || token == "pokemonc" || token == "crystal" || token == "c") return "pokemon_crystal";
    if (token == "pokemongs" || token == "gs") return "pokemon_gs";
    if (token == "pokemonruby" || token == "ruby" || token == "r") return "pokemon_ruby";
    if (token == "pokemonsapphire" || token == "sapphire" || token == "s") return "pokemon_sapphire";
    if (token == "pokemonemerald" || token == "emerald" || token == "e") return "pokemon_emerald";
    if (token == "pokemonfirered" || token == "firered" || token == "fr") return "pokemon_firered";
    if (token == "pokemonleafgreen" || token == "leafgreen" || token == "lg") return "pokemon_leafgreen";
    if (token == "pokemondiamond" || token == "diamond" || token == "d" || token == "dp") return "pokemon_diamond";
    if (token == "pokemonpearl" || token == "pearl" || token == "p") return "pokemon_pearl";
    if (token == "pokemonplatinum" || token == "platinum" || token == "pt") return "pokemon_platinum";
    if (token == "pokemonheartgold" || token == "heartgold" || token == "hg") return "pokemon_heartgold";
    if (token == "pokemonsoulsilver" || token == "soulsilver" || token == "ss") return "pokemon_soulsilver";
    if (token == "pokemonhgss" || token == "hgss") {
        if (filenameHintsHeartGold(filename_hint)) return "pokemon_heartgold";
        if (filenameHintsSoulSilver(filename_hint)) return "pokemon_soulsilver";
        return "pokemon_hgss";
    }
    if (token == "pokemonblack" || token == "black" || token == "b") return "pokemon_black";
    if (token == "pokemonwhite" || token == "white" || token == "w") return "pokemon_white";
    if (token == "pokemonblack2" || token == "black2" || token == "b2") return "pokemon_black_2";
    if (token == "pokemonwhite2" || token == "white2" || token == "w2") return "pokemon_white_2";
    if (token == "pokemonx" || token == "x") return "pokemon_x";
    if (token == "pokemony" || token == "y") return "pokemon_y";
    if (token == "pokemonomegaruby" || token == "omegaruby" || token == "or") return "pokemon_omega_ruby";
    if (token == "pokemonalphasapphire" || token == "alphasapphire" || token == "as") return "pokemon_alpha_sapphire";
    if (token == "pokemonsun" || token == "pokemonsn" || token == "sun" || token == "sn") return "pokemon_sun";
    if (token == "pokemonmoon" || token == "pokemonmn" || token == "moon" || token == "mn") return "pokemon_moon";
    if (token == "pokemonultrasun" || token == "pokemonus" || token == "ultrasun" || token == "us") return "pokemon_ultra_sun";
    if (token == "pokemonultramoon" || token == "pokemonum" || token == "ultramoon" || token == "um") return "pokemon_ultra_moon";
    if (token == "pokemonletsgopikachu" || token == "pokemongp" || token == "letsgopikachu" || token == "gp" || token == "lgp") return "pokemon_lets_go_pikachu";
    if (token == "pokemonletsgoeevee" || token == "pokemonge" || token == "letsgoeevee" || token == "ge" || token == "lge") return "pokemon_lets_go_eevee";
    if (token == "pokemonsword" || token == "sword" || token == "sw") return "pokemon_sword";
    if (token == "pokemonshield" || token == "shield" || token == "sh") return "pokemon_shield";
    if (token == "pokemonswsh" || token == "swsh") return "pokemon_swsh";
    if (token == "pokemonbrilliantdiamond" || token == "pokemonbd" || token == "brilliantdiamond" || token == "bd") return "pokemon_brilliant_diamond";
    if (token == "pokemonshiningpearl" || token == "pokemonsp" || token == "shiningpearl" || token == "sp") return "pokemon_shining_pearl";
    if (token == "pokemonlegendsarceus" || token == "pokemonla" || token == "legendsarceus" || token == "la") return "pokemon_legends_arceus";
    if (token == "pokemonscarlet" || token == "pokemonsl" || token == "scarlet" || token == "sl" || token == "sc") return "pokemon_scarlet";
    if (token == "pokemonviolet" || token == "pokemonvl" || token == "violet" || token == "vl" || token == "vi") return "pokemon_violet";
    if (token == "pokemonsv" || token == "sv") return "pokemon_sv";

    return {};
}

std::string pokemonGameTitle(const std::string& game_id, const std::string& filename_hint) {
    const std::string normalized = normalizePokemonGameId(game_id, filename_hint);
    const std::string id = normalized.empty() ? game_id : normalized;
    if (const GameMetadata* game = findGameById(id)) {
        return std::string(game->title);
    }
    return titleCaseFallback(id);
}

std::string pokemonGameCode(const std::string& game_id) {
    const std::string normalized = normalizePokemonGameId(game_id);
    const std::string id = normalized.empty() ? asciiLower(game_id) : normalized;
    if (const GameMetadata* game = findGameById(id)) {
        return std::string(game->code);
    }
    return {};
}

std::string pokemonGameRegionKey(const std::string& game_id) {
    const std::string normalized = normalizePokemonGameId(game_id);
    const std::string id = normalized.empty() ? asciiLower(game_id) : normalized;
    if (const GameMetadata* game = findGameById(id)) {
        return std::string(game->region);
    }
    return "unknown";
}

PokemonOriginInfo resolvePokemonOrigin(const PcSlotSpecies& slot, const std::string& source_game_key) {
    PokemonOriginInfo out;
    out.game_id = normalizePokemonGameId(slot.origin_game);
    if (out.game_id.empty()) {
        out.game_id = normalizePokemonGameId(source_game_key);
    }

    if (!out.game_id.empty()) {
        out.region_key = pokemonGameRegionKey(out.game_id);
        out.game_code = pokemonGameCode(out.game_id);
    }

    if (out.region_key == "johto") {
        const std::string kanto_or_johto = inferKantoJohtoRegionFromMetLocation(slot.met_location_name);
        if (!kanto_or_johto.empty()) {
            out.region_key = kanto_or_johto;
        }
    }

    if (out.region_key == "unknown") {
        const std::string from_location = inferRegionFromMetLocationText(slot.met_location_name);
        if (!from_location.empty()) {
            out.region_key = from_location;
        }
    }

    return out;
}

} // namespace pr
