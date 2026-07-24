#include "gameplay/attend/AttendSceneRouter.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <random>
#include <utility>
#include <vector>

namespace pr::gameplay::attend {

namespace {

std::string normalized(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == ' ' || c == '_') c = '-';
    }
    value.erase(std::unique(value.begin(), value.end(), [](char a, char b) {
        return a == '-' && b == '-';
    }), value.end());
    return value;
}

const JsonValue* child(const JsonValue* value, const char* key) {
    return value && value->isObject() ? value->get(key) : nullptr;
}

std::string stringOr(const JsonValue* value, const std::string& fallback = {}) {
    return value && value->isString() ? value->asString() : fallback;
}

std::string routeValue(const JsonValue* value) {
    if (!value) return {};
    if (value->isString()) return value->asString();
    return stringOr(child(value, "scene"));
}

bool stringArrayContains(const JsonValue* value, const std::string& needle) {
    if (!value || !value->isArray()) return false;
    const std::string match = normalized(needle);
    for (const JsonValue& item : value->asArray()) {
        if (item.isString() && normalized(item.asString()) == match) return true;
    }
    return false;
}

std::string pickPool(const JsonValue* value, std::mt19937& rng) {
    if (!value || !value->isArray()) return {};
    struct Entry { std::string scene; double weight = 1.0; };
    std::vector<Entry> entries;
    double total = 0.0;
    for (const JsonValue& item : value->asArray()) {
        Entry entry;
        if (item.isString()) {
            entry.scene = item.asString();
        } else if (item.isObject()) {
            entry.scene = stringOr(item.get("scene"));
            if (const JsonValue* weight = item.get("weight"); weight && weight->isNumber()) {
                entry.weight = std::max(0.0, weight->asNumber());
            }
        }
        if (entry.scene.empty() || entry.weight <= 0.0) continue;
        total += entry.weight;
        entries.push_back(std::move(entry));
    }
    if (entries.empty() || total <= 0.0) return {};
    std::uniform_real_distribution<double> distribution(0.0, total);
    double selected = distribution(rng);
    for (const Entry& entry : entries) {
        selected -= entry.weight;
        if (selected <= 0.0) return entry.scene;
    }
    return entries.back().scene;
}

} // namespace

AttendSceneRouter::AttendSceneRouter(const std::string& project_root)
    : project_root_(project_root), rng_(std::random_device{}()) {
    const std::filesystem::path manifest_path =
        std::filesystem::path(project_root_) / "config" / "gameplay" / "pokemon_attend.json";
    const JsonValue manifest = parseJsonFile(manifest_path.string());
    std::string route_file;
    std::string environment_file;
    if (const JsonValue* files = manifest.get("files"); files && files->isObject()) {
        route_file = stringOr(files->get("routing"));
        environment_file = stringOr(files->get("environment"));
    }
    if (route_file.empty()) route_file = "gameplay/pokemon_attend/routing/alola_scene_routes.json";
    if (environment_file.empty()) environment_file = "gameplay/pokemon_attend/environments/alola_battle_maps.json";
    const std::filesystem::path candidate(route_file);
    routing_path_ = candidate.is_absolute()
        ? candidate.string()
        : (std::filesystem::path(project_root_) / "config" / candidate).string();
    const std::filesystem::path environment_candidate(environment_file);
    environment_path_ = environment_candidate.is_absolute()
        ? environment_candidate.string()
        : (std::filesystem::path(project_root_) / "config" / environment_candidate).string();
}

AttendSceneRouteResult AttendSceneRouter::resolve(
    const AttendLaunchContext& context,
    std::optional<std::uint32_t> deterministic_seed) {
    last_error_.clear();
    const JsonValue root = parseJsonFile(routing_path_);
    if (!root.isObject()) {
        last_error_ = "Pokemon Attend routing config is not an object: " + routing_path_;
        return {};
    }
    std::mt19937 seeded;
    std::mt19937* rng = &rng_;
    if (deterministic_seed) {
        seeded.seed(*deterministic_seed);
        rng = &seeded;
    }
    const std::string fallback = stringOr(root.get("fallbackScene"), "alola_grass_arena");
    const std::string species = normalized(context.species_slug);
    if (context.event_id && !context.event_id->empty()) {
        if (const JsonValue* routes = root.get("eventRoutes"); routes && routes->isObject()) {
            if (const std::string scene = routeValue(routes->get(normalized(*context.event_id))); !scene.empty()) {
                return {scene, "event:" + normalized(*context.event_id)};
            }
        }
    }
    if (!species.empty()) {
        const JsonValue environment = parseJsonFile(environment_path_);
        if (const JsonValue* floors = environment.get("floors"); floors && floors->isObject()) {
            for (const auto& [scene_id, floor] : floors->asObject()) {
                if (!floor.isObject() || !stringArrayContains(floor.get("appearsForPokemon"), species)) continue;
                return {scene_id, "pokemon-map:" + species};
            }
        }
        if (const JsonValue* routes = root.get("pokemonRoutes"); routes && routes->isObject()) {
            if (const std::string scene = routeValue(routes->get(species)); !scene.empty()) {
                return {scene, "pokemon:" + species};
            }
        }
        if (const JsonValue* groups = root.get("pokemonGroups"); groups && groups->isObject()) {
            for (const auto& [group_id, value] : groups->asObject()) {
                if (!value.isObject() || !stringArrayContains(value.get("species"), species)) continue;
                if (const std::string scene = routeValue(&value); !scene.empty()) {
                    return {scene, "pokemon-group:" + group_id};
                }
            }
        }
    }
    constexpr const char* forced_prefix = "attend.scene.";
    for (std::string tag : context.tile_tags) {
        std::transform(tag.begin(), tag.end(), tag.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (tag.rfind(forced_prefix, 0) == 0 &&
            tag.size() > std::char_traits<char>::length(forced_prefix)) {
            return {
                tag.substr(std::char_traits<char>::length(forced_prefix)),
                "tile-force:" + tag,
            };
        }
    }
    std::vector<std::string> tags;
    tags.reserve(context.tile_tags.size());
    for (const std::string& tag : context.tile_tags) tags.push_back(normalized(tag));
    if (const JsonValue* routes = root.get("tileTagRoutes"); routes && routes->isObject()) {
        for (const std::string& tag : tags) {
            if (const std::string scene = routeValue(routes->get(tag)); !scene.empty()) {
                return {scene, "tile-tag:" + tag};
            }
        }
    }
    std::string pool_id = normalized(context.surface);
    if (std::find(tags.begin(), tags.end(), "terrain.sea") != tags.end()) pool_id = "sea";
    else if (std::find(tags.begin(), tags.end(), "terrain.freshwater") != tags.end()) pool_id = "freshwater";
    else if (std::find(tags.begin(), tags.end(), "terrain.river") != tags.end()) pool_id = "river";
    else if (pool_id == "water") pool_id = "sea";
    if (const JsonValue* pools = root.get("surfacePools"); pools && pools->isObject()) {
        if (const std::string scene = pickPool(pools->get(pool_id), *rng); !scene.empty()) {
            return {scene, "surface-pool:" + pool_id};
        }
    }
    return {fallback, "fallback"};
}

} // namespace pr::gameplay::attend
