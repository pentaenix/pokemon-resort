#include "core/BridgeImportMerge.hpp"

#include "core/Json.hpp"

#include <cctype>

namespace pr {

namespace {

const JsonValue* child(const JsonValue& parent, const std::string& key) {
    return parent.isObject() ? parent.get(key) : nullptr;
}

std::string asStringOrEmpty(const JsonValue* value) {
    return value && value->isString() ? value->asString() : std::string{};
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
        }
        return true;
    } catch (const std::exception& ex) {
        if (error_message) {
            *error_message = ex.what();
        }
        return false;
    }
}

} // namespace pr
