#include "resort/integration/BridgeImportAdapter.hpp"

#include "core/config/Json.hpp"
#include "core/domain/PcSlotSpecies.hpp"

#include <array>
#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>

namespace pr::resort {

namespace {

const pr::JsonValue* child(const pr::JsonValue& parent, const std::string& key) {
    return parent.isObject() ? parent.get(key) : nullptr;
}

std::string requireString(const pr::JsonValue& object, const std::string& key) {
    const pr::JsonValue* value = child(object, key);
    if (!value || !value->isString() || value->asString().empty()) {
        throw std::runtime_error("Missing required string field: " + key);
    }
    return value->asString();
}

int requireInt(const pr::JsonValue& object, const std::string& key) {
    const pr::JsonValue* value = child(object, key);
    if (!value || !value->isNumber()) {
        throw std::runtime_error("Missing required number field: " + key);
    }
    return static_cast<int>(value->asNumber());
}

std::string optionalString(const pr::JsonValue& object, const std::string& key, const std::string& fallback = {}) {
    const pr::JsonValue* value = child(object, key);
    return value && value->isString() ? value->asString() : fallback;
}

int optionalInt(const pr::JsonValue& object, const std::string& key, int fallback = 0) {
    const pr::JsonValue* value = child(object, key);
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

bool optionalBool(const pr::JsonValue& object, const std::string& key, bool fallback = false) {
    const pr::JsonValue* value = child(object, key);
    return value && value->isBool() ? value->asBool() : fallback;
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char raw : value) {
        const char ch = static_cast<char>(raw);
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (raw >= 0x20) {
                    out.push_back(ch);
                }
                break;
        }
    }
    return out;
}

void appendJsonStringField(std::string& json, const char* key, const std::string& value) {
    if (value.empty()) {
        return;
    }
    json += ",\"";
    json += key;
    json += "\":\"";
    json += escapeJson(value);
    json += "\"";
}

void appendJsonIntField(std::string& json, const char* key, int value) {
    json += ",\"";
    json += key;
    json += "\":";
    json += std::to_string(value);
}

void appendJsonBoolField(std::string& json, const char* key, bool value) {
    json += ",\"";
    json += key;
    json += "\":";
    json += value ? "true" : "false";
}

std::optional<unsigned short> optionalU16(const pr::JsonValue& object, const std::string& key) {
    const pr::JsonValue* value = child(object, key);
    if (!value || !value->isNumber()) {
        return std::nullopt;
    }
    return static_cast<unsigned short>(value->asNumber());
}

std::optional<unsigned int> optionalU32(const pr::JsonValue& object, const std::string& key) {
    const pr::JsonValue* value = child(object, key);
    if (!value || !value->isNumber()) {
        return std::nullopt;
    }
    return static_cast<unsigned int>(value->asNumber());
}

int base64Value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    if (ch == '=') return -2;
    if (std::isspace(static_cast<unsigned char>(ch))) return -3;
    return -1;
}

std::vector<unsigned char> decodeBase64(const std::string& text) {
    std::vector<unsigned char> out;
    std::array<int, 4> block{};
    int count = 0;
    for (const char ch : text) {
        const int value = base64Value(ch);
        if (value == -3) {
            continue;
        }
        if (value == -1) {
            throw std::runtime_error("raw_payload_base64 contains an invalid character");
        }
        block[static_cast<std::size_t>(count++)] = value;
        if (count != 4) {
            continue;
        }
        if (block[0] < 0 || block[1] < 0) {
            throw std::runtime_error("raw_payload_base64 has invalid padding");
        }
        out.push_back(static_cast<unsigned char>((block[0] << 2) | (block[1] >> 4)));
        if (block[2] != -2) {
            out.push_back(static_cast<unsigned char>(((block[1] & 0x0f) << 4) | (block[2] >> 2)));
        }
        if (block[3] != -2) {
            if (block[2] == -2) {
                throw std::runtime_error("raw_payload_base64 has invalid padding");
            }
            out.push_back(static_cast<unsigned char>(((block[2] & 0x03) << 6) | block[3]));
        }
        count = 0;
    }
    if (count != 0) {
        throw std::runtime_error("raw_payload_base64 has truncated data");
    }
    return out;
}

bool looksLikeSha256(const std::string& value) {
    if (value.size() != 64) {
        return false;
    }
    for (const char ch : value) {
        if (!std::isxdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

void parseMoves(const pr::JsonValue& hot, PokemonHot& out) {
    const pr::JsonValue* moves = child(hot, "moves");
    if (!moves || !moves->isArray()) {
        return;
    }
    int index = 0;
    for (const pr::JsonValue& item : moves->asArray()) {
        if (index >= 4 || !item.isObject()) {
            break;
        }
        out.move_ids[static_cast<std::size_t>(index)] = optionalU16(item, "move_id");
        if (const auto pp = optionalU16(item, "pp")) {
            out.move_pp[static_cast<std::size_t>(index)] = static_cast<unsigned char>(*pp);
        }
        if (const auto pp_ups = optionalU16(item, "pp_ups")) {
            out.move_pp_ups[static_cast<std::size_t>(index)] = static_cast<unsigned char>(*pp_ups);
        }
        ++index;
    }
}

ImportedPokemon parseOnePokemon(const pr::JsonValue& item) {
    if (!item.isObject()) {
        throw std::runtime_error("pokemon entries must be objects");
    }
    ImportedPokemon imported;
    imported.source_game = static_cast<unsigned short>(requireInt(item, "source_game"));
    imported.format_name = requireString(item, "format_name");
    imported.raw_bytes = decodeBase64(requireString(item, "raw_payload_base64"));
    imported.raw_hash_sha256 = requireString(item, "raw_hash_sha256");
    if (imported.raw_bytes.empty()) {
        throw std::runtime_error("raw_payload_base64 decoded to an empty payload");
    }
    if (!looksLikeSha256(imported.raw_hash_sha256)) {
        throw std::runtime_error("raw_hash_sha256 must be a 64-character hex SHA-256 string");
    }

    const pr::JsonValue* hot = child(item, "hot");
    if (!hot || !hot->isObject()) {
        throw std::runtime_error("Missing required object field: hot");
    }

    PokemonHot& h = imported.hot;
    h.species_id = static_cast<unsigned short>(requireInt(*hot, "species_id"));
    h.form_id = static_cast<unsigned short>(optionalInt(*hot, "form_id"));
    h.nickname = optionalString(*hot, "nickname");
    h.is_nicknamed = optionalBool(*hot, "is_nicknamed");
    h.level = static_cast<unsigned char>(requireInt(*hot, "level"));
    h.exp = static_cast<unsigned int>(optionalInt(*hot, "exp"));
    h.gender = static_cast<unsigned char>(optionalInt(*hot, "gender"));
    h.shiny = optionalBool(*hot, "shiny");
    h.ability_id = optionalU16(*hot, "ability_id");
    if (const auto slot = optionalU16(*hot, "ability_slot")) {
        h.ability_slot = static_cast<unsigned char>(*slot);
    }
    h.held_item_id = optionalU16(*hot, "held_item_id");
    parseMoves(*hot, h);
    h.hp_current = static_cast<unsigned short>(optionalInt(*hot, "hp_current"));
    h.hp_max = static_cast<unsigned short>(optionalInt(*hot, "hp_max"));
    h.status_flags = static_cast<unsigned int>(optionalInt(*hot, "status_flags"));
    h.ot_name = requireString(*hot, "ot_name");
    h.tid16 = optionalU16(*hot, "tid16");
    h.sid16 = optionalU16(*hot, "sid16");
    h.tid32 = optionalU32(*hot, "tid32");
    h.origin_game = static_cast<unsigned short>(optionalInt(*hot, "origin_game", imported.source_game));
    if (const auto language = optionalU16(*hot, "language")) {
        h.language = static_cast<unsigned char>(*language);
    }
    h.met_location_id = optionalU16(*hot, "met_location_id");
    if (const auto met_level = optionalU16(*hot, "met_level")) {
        h.met_level = static_cast<unsigned char>(*met_level);
    }
    h.ball_id = optionalU16(*hot, "ball_id");
    h.pid = optionalU32(*hot, "pid");
    h.encryption_constant = optionalU32(*hot, "encryption_constant");
    if (const std::string tracker = optionalString(*hot, "home_tracker"); !tracker.empty()) {
        h.home_tracker = tracker;
    }
    h.dv16 = optionalU16(*hot, "dv16");
    h.lineage_root_species = static_cast<unsigned short>(optionalInt(*hot, "lineage_root_species", h.species_id));
    h.identity_strength = static_cast<unsigned char>(optionalInt(*hot, "identity_strength"));

    imported.warm_json = optionalString(item, "warm_json", "{\"schema_version\":1}");
    imported.suspended_json = optionalString(item, "suspended_json", "{\"schema_version\":1}");
    imported.identity.pid = h.pid;
    imported.identity.encryption_constant = h.encryption_constant;
    imported.identity.home_tracker = h.home_tracker;
    imported.identity.dv16 = h.dv16;
    imported.identity.tid16 = h.tid16;
    imported.identity.sid16 = h.sid16;
    imported.identity.ot_name = h.ot_name;
    imported.identity.lineage_root_species = h.lineage_root_species;
    return imported;
}

std::optional<ImportedPokemon> tryBuildImportedFromGamePcSlot(const pr::PcSlotSpecies& slot, std::uint16_t source_game) {
    if (source_game == 0) {
        return std::nullopt;
    }
    if (slot.species_id <= 0) {
        return std::nullopt;
    }
    if (slot.format.empty() || slot.bridge_box_payload_base64.empty() || !looksLikeSha256(slot.bridge_box_payload_hash_sha256)) {
        return std::nullopt;
    }
    std::vector<unsigned char> raw;
    try {
        raw = decodeBase64(slot.bridge_box_payload_base64);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (raw.empty()) {
        return std::nullopt;
    }

    ImportedPokemon imported;
    imported.source_game = source_game;
    imported.format_name = slot.format;
    imported.raw_bytes = std::move(raw);
    imported.raw_hash_sha256 = slot.bridge_box_payload_hash_sha256;
    imported.warm_json = "{\"schema_version\":1";
    appendJsonStringField(imported.warm_json, "source_game_key", slot.source_game_key);
    if (slot.source_game_id >= 0) {
        appendJsonIntField(imported.warm_json, "source_game_id", slot.source_game_id);
    }
    appendJsonStringField(imported.warm_json, "species_slug", slot.slug);
    appendJsonStringField(imported.warm_json, "species_name", slot.species_name);
    appendJsonStringField(imported.warm_json, "form_key", slot.form_key);
    appendJsonStringField(imported.warm_json, "held_item_name", slot.held_item_name);
    appendJsonStringField(imported.warm_json, "nature", slot.nature);
    appendJsonStringField(imported.warm_json, "ability_name", slot.ability_name);
    appendJsonStringField(imported.warm_json, "primary_type", slot.primary_type);
    appendJsonStringField(imported.warm_json, "secondary_type", slot.secondary_type);
    appendJsonStringField(imported.warm_json, "tera_type", slot.tera_type);
    appendJsonStringField(imported.warm_json, "mark_icon", slot.mark_icon);
    appendJsonStringField(imported.warm_json, "pokerus_status", slot.pokerus_status);
    if (slot.is_alpha) {
        appendJsonBoolField(imported.warm_json, "is_alpha", true);
    }
    if (slot.is_gigantamax) {
        appendJsonBoolField(imported.warm_json, "is_gigantamax", true);
    }
    if (slot.markings != 0) {
        appendJsonIntField(imported.warm_json, "markings", slot.markings);
    }
    if (slot.move_count > 0) {
        imported.warm_json += ",\"moves\":[";
        bool first_move = true;
        for (int i = 0; i < slot.move_count && i < static_cast<int>(slot.moves.size()); ++i) {
            const auto& move = slot.moves[static_cast<std::size_t>(i)];
            if (move.move_id <= 0 && move.move_name.empty()) {
                continue;
            }
            if (!first_move) {
                imported.warm_json += ",";
            }
            first_move = false;
            imported.warm_json += "{\"slot_index\":" + std::to_string(move.slot_index);
            if (move.move_id > 0) {
                appendJsonIntField(imported.warm_json, "move_id", move.move_id);
            }
            appendJsonStringField(imported.warm_json, "move_name", move.move_name);
            if (move.current_pp >= 0) {
                appendJsonIntField(imported.warm_json, "current_pp", move.current_pp);
            }
            if (move.pp_ups >= 0) {
                appendJsonIntField(imported.warm_json, "pp_ups", move.pp_ups);
            }
            imported.warm_json += "}";
        }
        imported.warm_json += "]";
    }
    if (!slot.source_game_key.empty() || slot.source_game_id >= 0 || !slot.source_save_trainer_name.empty() ||
        !slot.source_save_play_time.empty() || !slot.source_save_badges.empty()) {
        imported.warm_json += ",\"source_context\":{\"schema_version\":1";
        if (!slot.source_game_key.empty()) {
            appendJsonStringField(imported.warm_json, "game_key", slot.source_game_key);
        }
        if (slot.source_game_id >= 0) {
            appendJsonIntField(imported.warm_json, "game_id", slot.source_game_id);
        }
        appendJsonStringField(imported.warm_json, "trainer_name", slot.source_save_trainer_name);
        appendJsonStringField(imported.warm_json, "play_time", slot.source_save_play_time);
        appendJsonStringField(imported.warm_json, "badges", slot.source_save_badges);
        imported.warm_json += "}";
    }
    imported.warm_json += "}";
    imported.suspended_json = "{\"schema_version\":1}";

    PokemonHot& h = imported.hot;
    h.species_id = static_cast<unsigned short>(slot.species_id);
    h.form_id = slot.form >= 0 ? static_cast<unsigned short>(slot.form) : 0;
    h.nickname = slot.nickname;
    h.is_nicknamed = !slot.nickname.empty();
    h.level = slot.level >= 0 ? static_cast<unsigned char>(slot.level) : 1;
    h.exp = slot.exp >= 0 ? static_cast<unsigned int>(slot.exp) : 0;
    h.gender = slot.gender >= 0 ? static_cast<unsigned char>(slot.gender) : 0;
    h.shiny = slot.is_shiny;
    if (slot.ability_id >= 0) {
        h.ability_id = static_cast<unsigned short>(slot.ability_id);
    }
    if (slot.ability_slot >= 0) {
        h.ability_slot = static_cast<unsigned char>(std::min(slot.ability_slot, 255));
    }
    if (slot.held_item_id >= 0) {
        h.held_item_id = static_cast<unsigned short>(slot.held_item_id);
    }
    for (std::size_t i = 0; i < slot.moves.size(); ++i) {
        const auto& m = slot.moves[i];
        if (m.move_id > 0) {
            h.move_ids[i] = static_cast<unsigned short>(m.move_id);
            if (m.current_pp >= 0) {
                h.move_pp[i] = static_cast<unsigned char>(std::min(m.current_pp, 255));
            }
            if (m.pp_ups >= 0) {
                h.move_pp_ups[i] = static_cast<unsigned char>(std::min(m.pp_ups, 255));
            }
        }
    }
    h.hp_current = slot.hp_current >= 0 ? static_cast<unsigned short>(slot.hp_current) : 0;
    h.hp_max = slot.hp_max >= 0 ? static_cast<unsigned short>(slot.hp_max) : 0;
    h.status_flags = slot.status_flags >= 0 ? static_cast<unsigned int>(slot.status_flags) : 0;
    h.ot_name = slot.ot_name.empty() ? std::string("?") : slot.ot_name;
    if (slot.tid16 >= 0) {
        h.tid16 = static_cast<unsigned short>(slot.tid16);
    }
    if (slot.sid16 >= 0) {
        h.sid16 = static_cast<unsigned short>(slot.sid16);
    }
    if (slot.tid32.has_value()) {
        h.tid32 = *slot.tid32;
    }
    h.origin_game = slot.origin_game_id >= 0 ? static_cast<unsigned short>(slot.origin_game_id) : source_game;
    if (slot.language >= 0) {
        h.language = static_cast<unsigned char>(std::min(slot.language, 255));
    }
    if (slot.met_location_id >= 0) {
        h.met_location_id = static_cast<unsigned short>(slot.met_location_id);
    }
    if (slot.met_level >= 0) {
        h.met_level = static_cast<unsigned char>(std::min(slot.met_level, 255));
    }
    if (slot.met_date_unix.has_value()) {
        h.met_date_unix = *slot.met_date_unix;
    }
    if (slot.ball_id >= 0) {
        h.ball_id = static_cast<unsigned short>(slot.ball_id);
    }
    if (slot.pid.has_value()) {
        h.pid = *slot.pid;
    }
    if (slot.encryption_constant.has_value()) {
        h.encryption_constant = *slot.encryption_constant;
    }
    if (!slot.home_tracker.empty()) {
        h.home_tracker = slot.home_tracker;
    }
    h.dv16 = slot.dv16;
    if (slot.lineage_root_species > 0) {
        h.lineage_root_species = static_cast<unsigned short>(slot.lineage_root_species);
    } else {
        h.lineage_root_species = h.species_id;
    }

    imported.identity.tid16 = h.tid16;
    imported.identity.sid16 = h.sid16;
    imported.identity.ot_name = h.ot_name;
    imported.identity.lineage_root_species = h.lineage_root_species;
    imported.identity.pid = h.pid;
    imported.identity.encryption_constant = h.encryption_constant;
    imported.identity.home_tracker = h.home_tracker;
    imported.identity.dv16 = h.dv16;
    return imported;
}

} // namespace

BridgeImportParseResult parseBridgeImportPayload(const std::string& json_text) {
    BridgeImportParseResult result;
    try {
        const pr::JsonValue root = pr::parseJsonText(json_text);
        if (!root.isObject()) {
            result.error = "Bridge import payload root must be an object";
            return result;
        }
        const int schema = requireInt(root, "bridge_import_schema");
        if (schema != 1) {
            result.error = "Unsupported bridge_import_schema";
            return result;
        }
        const pr::JsonValue* arr = child(root, "pokemon");
        if (!arr || !arr->isArray()) {
            result.error = "Import payload must contain a pokemon array";
            return result;
        }
        for (const pr::JsonValue& item : arr->asArray()) {
            result.pokemon.push_back(parseOnePokemon(item));
        }
        result.success = true;
        return result;
    } catch (const std::exception& ex) {
        result.error = ex.what();
        result.pokemon.clear();
        return result;
    }
}

std::optional<ImportedPokemon> importedPokemonFromGamePcSlot(const pr::PcSlotSpecies& slot, std::uint16_t source_game) {
    return tryBuildImportedFromGamePcSlot(slot, source_game);
}

} // namespace pr::resort
