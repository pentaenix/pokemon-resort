#include "core/bridge/BridgeImportMerge.hpp"

#include "core/config/Json.hpp"

#include <cctype>
#include <limits>

namespace pr {

namespace {

const JsonValue* child(const JsonValue& parent, const std::string& key) {
    return parent.isObject() ? parent.get(key) : nullptr;
}

std::string asStringOrEmpty(const JsonValue* value) {
    return value && value->isString() ? value->asString() : std::string{};
}

int asIntOrDefault(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

std::optional<std::int64_t> asI64Optional(const JsonValue* value) {
    if (!value || !value->isNumber()) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(value->asNumber());
}

std::optional<std::uint32_t> asU32Optional(const JsonValue* value) {
    if (!value || !value->isNumber()) {
        return std::nullopt;
    }
    const double raw = value->asNumber();
    if (raw < 0.0 || raw > 4294967295.0) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(raw);
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

bool parseBridgeImportFirstPokemonSourceGameImpl(
    const std::string& bridge_import_stdout_json,
    std::uint16_t* out_source_game,
    std::string* error_message) {
    if (!out_source_game) {
        if (error_message) {
            *error_message = "out_source_game is null";
        }
        return false;
    }
    if (error_message) {
        error_message->clear();
    }
    try {
        const JsonValue root = parseJsonText(bridge_import_stdout_json);
        if (!root.isObject()) {
            if (error_message) {
                *error_message = "bridge import root must be an object";
            }
            return false;
        }
        const JsonValue* schema_val = child(root, "bridge_import_schema");
        if (!schema_val || !schema_val->isNumber() || static_cast<int>(schema_val->asNumber()) != 1) {
            if (error_message) {
                *error_message = "unsupported bridge_import_schema";
            }
            return false;
        }
        const JsonValue* pokemon_arr = child(root, "pokemon");
        if (!pokemon_arr || !pokemon_arr->isArray() || pokemon_arr->asArray().empty()) {
            if (error_message) {
                *error_message = "bridge import missing pokemon array";
            }
            return false;
        }
        for (const JsonValue& item : pokemon_arr->asArray()) {
            if (!item.isObject()) {
                continue;
            }
            const JsonValue* sg = child(item, "source_game");
            if (!sg || !sg->isNumber()) {
                continue;
            }
            const double raw = sg->asNumber();
            if (raw < 0.0 || raw > static_cast<double>(std::numeric_limits<std::uint16_t>::max())) {
                continue;
            }
            *out_source_game = static_cast<std::uint16_t>(raw);
            return true;
        }
        if (error_message) {
            *error_message = "bridge import pokemon entries missing source_game";
        }
        return false;
    } catch (const std::exception& ex) {
        if (error_message) {
            *error_message = ex.what();
        }
        return false;
    }
}

} // namespace

bool mergeBridgeImportIntoGamePcBoxes(
    const std::string& bridge_import_stdout_json,
    std::vector<TransferSaveSelection::PcBox>& pc_boxes,
    std::string* error_message) {
    if (error_message) {
        error_message->clear();
    }
    try {
        const JsonValue root = parseJsonText(bridge_import_stdout_json);
        if (!root.isObject()) {
            if (error_message) {
                *error_message = "bridge import root must be an object";
            }
            return false;
        }
        const JsonValue* schema_val = child(root, "bridge_import_schema");
        if (!schema_val || !schema_val->isNumber() || static_cast<int>(schema_val->asNumber()) != 1) {
            if (error_message) {
                *error_message = "unsupported bridge_import_schema";
            }
            return false;
        }
        const JsonValue* ok = child(root, "success");
        if (ok && ok->isBool() && !ok->asBool()) {
            if (error_message) {
                *error_message = "bridge import success=false";
            }
            return false;
        }
        const JsonValue* pokemon_arr = child(root, "pokemon");
        if (!pokemon_arr || !pokemon_arr->isArray()) {
            if (error_message) {
                *error_message = "bridge import missing pokemon array";
            }
            return false;
        }

        for (const JsonValue& item : pokemon_arr->asArray()) {
            if (!item.isObject()) {
                continue;
            }
            const std::string b64 = asStringOrEmpty(child(item, "raw_payload_base64"));
            const std::string hash = asStringOrEmpty(child(item, "raw_hash_sha256"));
            if (b64.empty() || hash.empty() || !looksLikeSha256(hash)) {
                continue;
            }
            const JsonValue* hot = child(item, "hot");
            const JsonValue* loc = child(item, "source_location");
            if (!loc || !loc->isObject()) {
                continue;
            }
            const std::string area = asStringOrEmpty(child(*loc, "area"));
            if (area != "box") {
                continue;
            }
            const JsonValue* box_v = child(*loc, "box");
            const JsonValue* slot_v = child(*loc, "slot");
            if (!box_v || !box_v->isNumber() || !slot_v || !slot_v->isNumber()) {
                continue;
            }
            const int box_index = static_cast<int>(box_v->asNumber());
            const int slot_index = static_cast<int>(slot_v->asNumber());
            if (box_index < 0 || slot_index < 0 ||
                static_cast<std::size_t>(box_index) >= pc_boxes.size() ||
                static_cast<std::size_t>(slot_index) >= pc_boxes[static_cast<std::size_t>(box_index)].slots.size()) {
                continue;
            }
            PcSlotSpecies& slot = pc_boxes[static_cast<std::size_t>(box_index)].slots[static_cast<std::size_t>(slot_index)];
            slot.bridge_box_payload_base64 = b64;
            slot.bridge_box_payload_hash_sha256 = hash;
            const std::string format_name = asStringOrEmpty(child(item, "format_name"));
            if (!format_name.empty()) {
                slot.format = format_name;
            }
            slot.source_game_id = asIntOrDefault(child(item, "source_game"), slot.source_game_id);
            if (hot && hot->isObject()) {
                slot.species_id = asIntOrDefault(child(*hot, "species_id"), slot.species_id);
                slot.form = asIntOrDefault(child(*hot, "form_id"), slot.form);
                const std::string nickname = asStringOrEmpty(child(*hot, "nickname"));
                if (!nickname.empty()) {
                    slot.nickname = nickname;
                }
                slot.level = asIntOrDefault(child(*hot, "level"), slot.level);
                slot.exp = asIntOrDefault(child(*hot, "exp"), slot.exp);
                slot.gender = asIntOrDefault(child(*hot, "gender"), slot.gender);
                slot.is_shiny = child(*hot, "shiny") && child(*hot, "shiny")->isBool()
                    ? child(*hot, "shiny")->asBool()
                    : slot.is_shiny;
                slot.ability_id = asIntOrDefault(child(*hot, "ability_id"), slot.ability_id);
                slot.ability_slot = asIntOrDefault(child(*hot, "ability_slot"), slot.ability_slot);
                slot.held_item_id = asIntOrDefault(child(*hot, "held_item_id"), slot.held_item_id);
                slot.hp_current = asIntOrDefault(child(*hot, "hp_current"), slot.hp_current);
                slot.hp_max = asIntOrDefault(child(*hot, "hp_max"), slot.hp_max);
                slot.status_flags = asIntOrDefault(child(*hot, "status_flags"), slot.status_flags);
                const std::string ot_name = asStringOrEmpty(child(*hot, "ot_name"));
                if (!ot_name.empty()) {
                    slot.ot_name = ot_name;
                }
                slot.tid16 = asIntOrDefault(child(*hot, "tid16"), slot.tid16);
                slot.sid16 = asIntOrDefault(child(*hot, "sid16"), slot.sid16);
                slot.tid32 = asU32Optional(child(*hot, "tid32"));
                slot.origin_game_id = asIntOrDefault(child(*hot, "origin_game"), slot.origin_game_id);
                slot.language = asIntOrDefault(child(*hot, "language"), slot.language);
                slot.met_location_id = asIntOrDefault(child(*hot, "met_location_id"), slot.met_location_id);
                slot.met_level = asIntOrDefault(child(*hot, "met_level"), slot.met_level);
                slot.met_date_unix = asI64Optional(child(*hot, "met_date_unix"));
                slot.ball_id = asIntOrDefault(child(*hot, "ball_id"), slot.ball_id);
                slot.pid = asU32Optional(child(*hot, "pid"));
                slot.encryption_constant = asU32Optional(child(*hot, "encryption_constant"));
                const std::string tracker = asStringOrEmpty(child(*hot, "home_tracker"));
                if (!tracker.empty()) {
                    slot.home_tracker = tracker;
                }
                const int dv16 = asIntOrDefault(child(*hot, "dv16"), -1);
                if (dv16 >= 0 && dv16 <= 0xffff) {
                    slot.dv16 = static_cast<std::uint16_t>(dv16);
                }
                slot.lineage_root_species = asIntOrDefault(child(*hot, "lineage_root_species"), slot.lineage_root_species);
            }
        }
        return true;
    } catch (const std::exception& ex) {
        if (error_message) {
            *error_message = ex.what();
        }
        return false;
    }
}

bool parseBridgeImportFirstPokemonSourceGame(
    const std::string& bridge_import_stdout_json,
    std::uint16_t* out_source_game,
    std::string* error_message) {
    return parseBridgeImportFirstPokemonSourceGameImpl(bridge_import_stdout_json, out_source_game, error_message);
}

bool parseBridgeImportFirstPokemonFormatNameImpl(
    const std::string& bridge_import_stdout_json,
    std::string* out_format_name,
    std::string* error_message) {
    if (!out_format_name) {
        if (error_message) {
            *error_message = "out_format_name is null";
        }
        return false;
    }
    if (error_message) {
        error_message->clear();
    }
    try {
        const JsonValue root = parseJsonText(bridge_import_stdout_json);
        if (!root.isObject()) {
            if (error_message) {
                *error_message = "bridge import root must be an object";
            }
            return false;
        }
        const JsonValue* schema_val = child(root, "bridge_import_schema");
        if (!schema_val || !schema_val->isNumber() || static_cast<int>(schema_val->asNumber()) != 1) {
            if (error_message) {
                *error_message = "unsupported bridge_import_schema";
            }
            return false;
        }
        const JsonValue* pokemon_arr = child(root, "pokemon");
        if (!pokemon_arr || !pokemon_arr->isArray() || pokemon_arr->asArray().empty()) {
            if (error_message) {
                *error_message = "bridge import missing pokemon array";
            }
            return false;
        }
        for (const JsonValue& item : pokemon_arr->asArray()) {
            if (!item.isObject()) {
                continue;
            }
            const std::string fmt = asStringOrEmpty(child(item, "format_name"));
            if (!fmt.empty()) {
                *out_format_name = fmt;
                return true;
            }
        }
        if (error_message) {
            *error_message = "bridge import pokemon entries missing format_name";
        }
        return false;
    } catch (const std::exception& ex) {
        if (error_message) {
            *error_message = ex.what();
        }
        return false;
    }
}

bool parseBridgeImportFirstPokemonFormatName(
    const std::string& bridge_import_stdout_json,
    std::string* out_format_name,
    std::string* error_message) {
    return parseBridgeImportFirstPokemonFormatNameImpl(bridge_import_stdout_json, out_format_name, error_message);
}

} // namespace pr
