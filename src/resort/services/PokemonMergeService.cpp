#include "resort/services/PokemonMergeService.hpp"

#include "core/config/Json.hpp"
#include "resort/domain/PokemonMergeFieldPolicy.hpp"
#include "resort/integration/Gen12DvBytes.hpp"

#include <string>

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

pr::JsonValue stripIncomingWarmMirrorStaticKeys(const pr::JsonValue& incoming) {
    if (!incoming.isObject()) {
        return incoming;
    }
    pr::JsonValue::Object obj = incoming.asObject();
    for (const auto key : pr::resort::kMirrorWarmStripIncomingKeys) {
        obj.erase(std::string(key));
    }
    auto catalog_it = obj.find("resort_catalog");
    if (catalog_it != obj.end() && catalog_it->second.isObject()) {
        pr::JsonValue::Object catalog = catalog_it->second.asObject();
        // `static_fields` describes the projected/cart format read. On mirror return, keep Resort's
        // original static catalog instead of letting Pal Park / transfer metadata replace it.
        catalog.erase("static_fields");
        catalog_it->second = pr::JsonValue(catalog);
    }
    return pr::JsonValue(obj);
}

std::string mergeWarmJsonMirrorReturn(const std::string& existing, const std::string& incoming) {
    if (isEmptyPayload(incoming)) {
        return isEmptyPayload(existing) ? std::string(kDefaultJsonPayload) : existing;
    }
    if (isEmptyPayload(existing)) {
        try {
            const pr::JsonValue inc_root = pr::parseJsonText(incoming);
            return serializeJson(stripIncomingWarmMirrorStaticKeys(inc_root));
        } catch (const std::exception&) {
            return incoming;
        }
    }

    try {
        const pr::JsonValue existing_json = pr::parseJsonText(existing);
        const pr::JsonValue stripped_incoming =
            stripIncomingWarmMirrorStaticKeys(pr::parseJsonText(incoming));
        return serializeJson(mergeJsonValue(existing_json, stripped_incoming));
    } catch (const std::exception&) {
        return existing;
    }
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
        return existing;
    }
}

std::string movesJsonSnippet(const PokemonHot& hot) {
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < hot.move_ids.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << (hot.move_ids[i] ? std::to_string(*hot.move_ids[i]) : std::string("0"));
    }
    out << "]";
    return out.str();
}

bool movesSlotsEqual(const PokemonHot& a, const PokemonHot& b) {
    for (std::size_t i = 0; i < a.move_ids.size(); ++i) {
        if (a.move_ids[i] != b.move_ids[i]) {
            return false;
        }
        if (a.move_pp[i] != b.move_pp[i]) {
            return false;
        }
        if (a.move_pp_ups[i] != b.move_pp_ups[i]) {
            return false;
        }
    }
    return true;
}

bool hotEquals(const PokemonHot& a, const PokemonHot& b) {
    return a.species_id == b.species_id && a.form_id == b.form_id && a.nickname == b.nickname &&
           a.is_nicknamed == b.is_nicknamed && a.level == b.level && a.exp == b.exp && a.gender == b.gender &&
           a.shiny == b.shiny && a.ability_id == b.ability_id && a.ability_slot == b.ability_slot &&
           a.held_item_id == b.held_item_id && movesSlotsEqual(a, b) && a.hp_current == b.hp_current &&
           a.hp_max == b.hp_max && a.status_flags == b.status_flags && a.ot_name == b.ot_name &&
           a.tid16 == b.tid16 && a.sid16 == b.sid16 && a.tid32 == b.tid32 && a.origin_game == b.origin_game &&
           a.language == b.language && a.met_location_id == b.met_location_id && a.met_level == b.met_level &&
           a.met_date_unix == b.met_date_unix && a.ball_id == b.ball_id && a.pid == b.pid &&
           a.encryption_constant == b.encryption_constant && a.home_tracker == b.home_tracker &&
           a.dv16 == b.dv16 && a.lineage_root_species == b.lineage_root_species &&
           a.identity_strength == b.identity_strength;
}

std::string mergeDiffJsonFull(
    const ResortPokemon& before,
    const ResortPokemon& after,
    const ImportedPokemon& imported) {
    std::ostringstream out;
    out << "{"
        << "\"event\":\"merged_from_import\","
        << "\"kind\":\"full\","
        << "\"source_game\":" << imported.source_game << ","
        << "\"format\":\"" << escapeJson(imported.format_name) << "\","
        << "\"species_id\":{\"before\":" << before.hot.species_id << ",\"after\":" << after.hot.species_id << "},"
        << "\"level\":{\"before\":" << static_cast<int>(before.hot.level)
        << ",\"after\":" << static_cast<int>(after.hot.level) << "},"
        << "\"exp\":{\"before\":" << before.hot.exp << ",\"after\":" << after.hot.exp << "},"
        << "\"moves_before\":" << movesJsonSnippet(before.hot) << ","
        << "\"moves_after\":" << movesJsonSnippet(after.hot)
        << "}";
    return out.str();
}

std::string mergeDiffJsonMirrorReturn(
    const ResortPokemon& before,
    const ResortPokemon& after,
    const ImportedPokemon& imported,
    bool moves_from_cart,
    bool evolved,
    bool warm_changed) {
    std::ostringstream out;
    out << "{"
        << "\"event\":\"mirror_return_sync\","
        << "\"format\":\"" << escapeJson(imported.format_name) << "\","
        << "\"level\":{\"before\":" << static_cast<int>(before.hot.level)
        << ",\"after\":" << static_cast<int>(after.hot.level) << "},"
        << "\"exp\":{\"before\":" << before.hot.exp << ",\"after\":" << after.hot.exp << "},"
        << "\"moves_from_cart\":" << (moves_from_cart ? "true" : "false") << ","
        << "\"evolved\":" << (evolved ? "true" : "false") << ","
        << "\"warm_changed\":" << (warm_changed ? "true" : "false") << ","
        << "\"moves_before\":" << movesJsonSnippet(before.hot) << ","
        << "\"moves_after\":" << movesJsonSnippet(after.hot) << ","
        << "\"met_location_id_preserved\":true,"
        << "\"static_hot_policy\":\"mirror_return_v1\""
        << "}";
    return out.str();
}

PokemonMergeResult mergeFullReplace(
    ResortPokemon& canonical,
    const ImportedPokemon& imported,
    long long updated_at_unix) {
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
    if (isGen12StorageFormat(imported.format_name)) {
        if (incoming.dv16 && *incoming.dv16 != 0) {
            next.dv16 = incoming.dv16;
        }
    } else {
        replaceIfPresent(next.dv16, incoming.dv16);
    }

    if (!incoming.ot_name.empty()) {
        next.ot_name = incoming.ot_name;
    }
    if (incoming.origin_game != 0) {
        next.origin_game = incoming.origin_game;
    } else if (next.origin_game == 0) {
        next.origin_game = imported.source_game;
    }
    if (next.lineage_root_species == 0 && incoming.lineage_root_species != 0) {
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

    const bool warm_changed = before.warm.json != canonical.warm.json;
    const bool cold_changed = before.cold.suspended_json != canonical.cold.suspended_json;
    PokemonMergeResult result;
    result.changed =
        !hotEquals(before.hot, canonical.hot) || warm_changed || cold_changed;
    if (result.changed) {
        canonical.revision += 1;
        canonical.updated_at_unix = updated_at_unix;
    }
    result.diff_json = mergeDiffJsonFull(before, canonical, imported);
    return result;
}

PokemonMergeResult mergeMirrorReturnGameplay(
    ResortPokemon& canonical,
    const ImportedPokemon& imported,
    long long updated_at_unix) {
    const ResortPokemon before = canonical;
    PokemonHot next = canonical.hot;
    const PokemonHot& ih = imported.hot;
    const PokemonHot& ch = canonical.hot;

    const bool evolved =
        (ih.species_id != ch.species_id) || (ih.form_id != ch.form_id);
    const bool leveled_up = ih.level > ch.level;

    applyMirrorReturnHotMutableOverlay(next, ch, ih, evolved, leveled_up);

    const bool moves_from_cart = evolved || leveled_up;

    canonical.hot = next;

    const std::string warm_before = canonical.warm.json;
    canonical.warm.json = mergeWarmJsonMirrorReturn(canonical.warm.json, imported.warm_json);
    const bool warm_changed = warm_before != canonical.warm.json;

    PokemonMergeResult result;
    result.changed = !hotEquals(before.hot, canonical.hot) || warm_changed;
    if (result.changed) {
        canonical.revision += 1;
        canonical.updated_at_unix = updated_at_unix;
    }
    result.diff_json =
        mergeDiffJsonMirrorReturn(before, canonical, imported, moves_from_cart, evolved, warm_changed);
    return result;
}

} // namespace

PokemonMergeResult PokemonMergeService::mergeImported(
    ResortPokemon& canonical,
    const ImportedPokemon& imported,
    long long updated_at_unix,
    ImportMergeKind kind) const {
    if (kind == ImportMergeKind::MirrorReturnGameplaySync) {
        return mergeMirrorReturnGameplay(canonical, imported, updated_at_unix);
    }
    return mergeFullReplace(canonical, imported, updated_at_unix);
}

} // namespace pr::resort
