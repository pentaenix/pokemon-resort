#include "gameplay/world3d/npc/ResortPokemonSpawnConfig.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <filesystem>

namespace pr::gameplay::world3d::npc {

namespace fs = std::filesystem;

namespace {

int intOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

std::string strOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

} // namespace

ResortPokemonSpawnConfig loadResortPokemonSpawnConfig(const std::string& project_root) {
    ResortPokemonSpawnConfig out;
    try {
        const JsonValue root = parseJsonFile(
            (fs::path(project_root) / "config" / "gameplay" / "world3d" / "pokemon_spawns.json").string());
        if (!root.isObject()) {
            return out;
        }
        const JsonValue* roster = root.get("resortBoxRoster");
        if (!roster || !roster->isObject()) {
            return out;
        }
        out.enabled = roster->get("enabled") ? roster->get("enabled")->asBool() : out.enabled;
        out.profile_id = strOr(roster->get("profileId"), out.profile_id);
        out.box_id = std::max(0, intOr(roster->get("boxId"), out.box_id));
        out.max_pokemon = std::max(0, intOr(roster->get("maxPokemon"), out.max_pokemon));
    } catch (...) {
        return out;
    }
    return out;
}

} // namespace pr::gameplay::world3d::npc
