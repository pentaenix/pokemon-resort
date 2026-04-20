#include "resort/services/PokemonMergeService.hpp"

#include "core/Json.hpp"

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <set>

namespace pr::resort {

namespace {

constexpr const char* kDefaultJsonPayload = "{\"schema_version\":1}";

template <typename T>
void replaceIfPresent(std::optional<T>& target, const std::optional<T>& incoming) {
    if (incoming) {
        target = incoming;
    }
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string serializeJson(const pr::JsonValue& value);

std::string serializeObject(const pr::JsonValue::Object& object) {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto& [key, item] : object) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "\"" << escapeJson(key) << "\":" << serializeJson(item);
    }
    out << "}";
    return out.str();
}

std::string serializeArray(const pr::JsonValue::Array& array) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < array.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << serializeJson(array[i]);
    }
    out << "]";
    return out.str();
}

std::string serializeJson(const pr::JsonValue& value) {
    if (value.isNull()) {
        return "null";
    }
    if (value.isBool()) {
        return value.asBool() ? "true" : "false";
    }
    if (value.isNumber()) {
        std::ostringstream out;
        out << std::setprecision(15) << value.asNumber();
        return out.str();
    }
    if (value.isString()) {
        return "\"" + escapeJson(value.asString()) + "\"";
    }
    if (value.isArray()) {
        return serializeArray(value.asArray());
    }
    return serializeObject(value.asObject());
}

bool isEmptyPayload(const std::string& json) {
    return json.empty() || json == "{}" || json == kDefaultJsonPayload;
}

pr::JsonValue mergeJsonValue(const pr::JsonValue& existing, const pr::JsonValue& incoming) {
    if (existing.isObject() && incoming.isObject()) {
        pr::JsonValue::Object merged = existing.asObject();
        for (const auto& [key, incoming_value] : incoming.asObject()) {
            auto it = merged.find(key);
            if (it == merged.end()) {
                merged.emplace(key, incoming_value);
            } else {
                it->second = mergeJsonValue(it->second, incoming_value);
            }
        }
        return pr::JsonValue(merged);
    }

    if (existing.isArray() && incoming.isArray()) {
        pr::JsonValue::Array merged = existing.asArray();
        std::set<std::string> seen;
        for (const auto& item : merged) {
            seen.insert(serializeJson(item));
        }
        for (const auto& item : incoming.asArray()) {
            if (seen.insert(serializeJson(item)).second) {
                merged.push_back(item);
            }
        }
        return pr::JsonValue(merged);
    }

    if (incoming.isNull()) {
        return existing;
    }

    return incoming;
}

std::string mergeJsonPayload(const std::string& existing, const std::string& incoming) {
    if (isEmptyPayload(incoming)) {
        return isEmptyPayload(existing) ? std::string(kDefaultJsonPayload) : existing;
    }
    if (isEmptyPayload(existing)) {
        return incoming;
    }

    try {
        const pr::JsonValue existing_json = pr::parseJsonText(existing);
        const pr::JsonValue incoming_json = pr::parseJsonText(incoming);
        return serializeJson(mergeJsonValue(existing_json, incoming_json));
    } catch (const std::exception&) {
        // Invalid incoming warm/cold JSON must not erase existing preserved data.
        return existing;
    }
}

std::string mergeDiffJson(
    const ResortPokemon& before,
    const ResortPokemon& after,
    const ImportedPokemon& imported) {
    std::ostringstream out;
    out << "{"
        << "\"event\":\"merged_from_import\","
        << "\"source_game\":" << imported.source_game << ","
        << "\"format\":\"" << escapeJson(imported.format_name) << "\","
        << "\"before\":{\"level\":" << static_cast<int>(before.hot.level)
        << ",\"exp\":" << before.hot.exp
        << ",\"species_id\":" << before.hot.species_id << "},"
        << "\"after\":{\"level\":" << static_cast<int>(after.hot.level)
        << ",\"exp\":" << after.hot.exp
        << ",\"species_id\":" << after.hot.species_id << "},"
        << "\"warm_merged\":" << (before.warm.json != after.warm.json ? "true" : "false") << ","
        << "\"cold_merged\":" << (before.cold.suspended_json != after.cold.suspended_json ? "true" : "false")
        << "}";
    return out.str();
}

} // namespace

PokemonMergeResult PokemonMergeService::mergeImported(
    ResortPokemon& canonical,
    const ImportedPokemon& imported,
    long long updated_at_unix) const {
    const ResortPokemon before = canonical;
    PokemonHot next = canonical.hot;
    const PokemonHot& incoming = imported.hot;

    next.species_id = incoming.species_id;
    next.form_id = incoming.form_id;
    next.nickname = incoming.nickname;
    next.is_nicknamed = incoming.is_nicknamed;
    next.level = incoming.level;
    next.exp = incoming.exp;
    next.gender = incoming.gender;
    next.shiny = incoming.shiny;
    next.move_ids = incoming.move_ids;
    next.move_pp = incoming.move_pp;
    next.move_pp_ups = incoming.move_pp_ups;
    next.hp_current = incoming.hp_current;
    next.hp_max = incoming.hp_max;
    next.status_flags = incoming.status_flags;

    replaceIfPresent(next.ability_id, incoming.ability_id);
    replaceIfPresent(next.ability_slot, incoming.ability_slot);
    replaceIfPresent(next.held_item_id, incoming.held_item_id);
    replaceIfPresent(next.tid16, incoming.tid16);
    replaceIfPresent(next.sid16, incoming.sid16);
    replaceIfPresent(next.tid32, incoming.tid32);
    replaceIfPresent(next.language, incoming.language);
    replaceIfPresent(next.met_location_id, incoming.met_location_id);
    replaceIfPresent(next.met_level, incoming.met_level);
    replaceIfPresent(next.met_date_unix, incoming.met_date_unix);
    replaceIfPresent(next.ball_id, incoming.ball_id);
    replaceIfPresent(next.pid, incoming.pid);
    replaceIfPresent(next.encryption_constant, incoming.encryption_constant);
    replaceIfPresent(next.home_tracker, incoming.home_tracker);

    if (!incoming.ot_name.empty()) {
        next.ot_name = incoming.ot_name;
    }
    if (incoming.origin_game != 0) {
        next.origin_game = incoming.origin_game;
    } else if (next.origin_game == 0) {
        next.origin_game = imported.source_game;
    }
    if (incoming.lineage_root_species != 0) {
        next.lineage_root_species = incoming.lineage_root_species;
    } else if (next.lineage_root_species == 0) {
        next.lineage_root_species = incoming.species_id;
    }
    if (incoming.identity_strength > next.identity_strength) {
        next.identity_strength = incoming.identity_strength;
    }

    canonical.hot = next;
    canonical.warm.json = mergeJsonPayload(canonical.warm.json, imported.warm_json);
    canonical.cold.suspended_json = mergeJsonPayload(canonical.cold.suspended_json, imported.suspended_json);
    canonical.revision += 1;
    canonical.updated_at_unix = updated_at_unix;

    PokemonMergeResult result;
    result.changed = true;
    result.diff_json = mergeDiffJson(before, canonical, imported);
    return result;
}

} // namespace pr::resort
