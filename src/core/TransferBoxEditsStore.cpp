#include "core/TransferBoxEditsStore.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace pr {

namespace {

const JsonValue* child(const JsonValue& parent, const std::string& key) { return parent.get(key); }

int asIntOrDefault(const JsonValue* value, int fallback) {
    return (value && value->isNumber()) ? static_cast<int>(value->asNumber()) : fallback;
}

bool asBoolOrDefault(const JsonValue* value, bool fallback) {
    return (value && value->isBool()) ? value->asBool() : fallback;
}

std::string asStringOrEmpty(const JsonValue* value) {
    return (value && value->isString()) ? value->asString() : std::string{};
}

std::string escapeJsonString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char raw : s) {
        const char c = static_cast<char>(raw);
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u"
                        << std::hex << std::uppercase
                        << ((raw >> 4) & 0xF) << (raw & 0xF);
                    // Keep it simple: control chars are rare here; fall back to space.
                    out += " ";
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

std::string quoted(const std::string& s) { return std::string{"\""} + escapeJsonString(s) + "\""; }

void serializeMoveArray(std::ostringstream& out, const PcSlotSpecies& slot) {
    out << "[";
    const int n = std::clamp(slot.move_count, 0, 4);
    for (int i = 0; i < n; ++i) {
        const auto& m = slot.moves[static_cast<std::size_t>(i)];
        if (i) out << ",";
        out << "{"
            << "\"move_id\":" << m.move_id << ","
            << "\"move_name\":" << quoted(m.move_name) << ","
            << "\"current_pp\":" << m.current_pp << ","
            << "\"pp_ups\":" << m.pp_ups
            << "}";
    }
    out << "]";
}

void serializeSlot(std::ostringstream& out, const PcSlotSpecies& slot) {
    out << "{"
        << "\"present\":" << (slot.present ? "true" : "false") << ","
        << "\"slug\":" << quoted(slot.slug) << ","
        << "\"species_name\":" << quoted(slot.species_name) << ","
        << "\"species_id\":" << slot.species_id << ","
        << "\"nickname\":" << quoted(slot.nickname) << ","
        << "\"form\":" << slot.form << ","
        << "\"form_key\":" << quoted(slot.form_key) << ","
        << "\"gender\":" << slot.gender << ","
        << "\"level\":" << slot.level << ","
        << "\"is_egg\":" << (slot.is_egg ? "true" : "false") << ","
        << "\"is_shiny\":" << (slot.is_shiny ? "true" : "false") << ","
        << "\"ball_id\":" << slot.ball_id << ","
        << "\"held_item_id\":" << slot.held_item_id << ","
        << "\"held_item_name\":" << quoted(slot.held_item_name) << ","
        << "\"nature\":" << quoted(slot.nature) << ","
        << "\"ability_id\":" << slot.ability_id << ","
        << "\"ability_name\":" << quoted(slot.ability_name) << ","
        << "\"primary_type\":" << quoted(slot.primary_type) << ","
        << "\"secondary_type\":" << quoted(slot.secondary_type) << ","
        << "\"markings\":" << slot.markings << ","
        << "\"bridge_box_payload_base64\":" << quoted(slot.bridge_box_payload_base64) << ","
        << "\"bridge_box_payload_hash_sha256\":" << quoted(slot.bridge_box_payload_hash_sha256) << ","
        << "\"moves\":";
    serializeMoveArray(out, slot);
    out << "}";
}

PcSlotSpecies parseSlot(const JsonValue& obj) {
    if (!obj.isObject()) {
        throw std::runtime_error("overlay slot must be an object");
    }
    PcSlotSpecies s;
    s.present = asBoolOrDefault(child(obj, "present"), false);
    s.slug = asStringOrEmpty(child(obj, "slug"));
    s.species_name = asStringOrEmpty(child(obj, "species_name"));
    s.species_id = asIntOrDefault(child(obj, "species_id"), -1);
    s.nickname = asStringOrEmpty(child(obj, "nickname"));
    s.form = asIntOrDefault(child(obj, "form"), -1);
    s.form_key = asStringOrEmpty(child(obj, "form_key"));
    s.gender = asIntOrDefault(child(obj, "gender"), -1);
    s.level = asIntOrDefault(child(obj, "level"), -1);
    s.is_egg = asBoolOrDefault(child(obj, "is_egg"), false);
    s.is_shiny = asBoolOrDefault(child(obj, "is_shiny"), false);
    s.ball_id = asIntOrDefault(child(obj, "ball_id"), -1);
    s.held_item_id = asIntOrDefault(child(obj, "held_item_id"), -1);
    s.held_item_name = asStringOrEmpty(child(obj, "held_item_name"));
    s.nature = asStringOrEmpty(child(obj, "nature"));
    s.ability_id = asIntOrDefault(child(obj, "ability_id"), -1);
    s.ability_name = asStringOrEmpty(child(obj, "ability_name"));
    s.primary_type = asStringOrEmpty(child(obj, "primary_type"));
    s.secondary_type = asStringOrEmpty(child(obj, "secondary_type"));
    s.markings = asIntOrDefault(child(obj, "markings"), 0);
    s.bridge_box_payload_base64 = asStringOrEmpty(child(obj, "bridge_box_payload_base64"));
    s.bridge_box_payload_hash_sha256 = asStringOrEmpty(child(obj, "bridge_box_payload_hash_sha256"));
    if (const JsonValue* moves = child(obj, "moves"); moves && moves->isArray()) {
        const auto& arr = moves->asArray();
        const int n = std::min<int>(4, static_cast<int>(arr.size()));
        s.move_count = n;
        for (int i = 0; i < n; ++i) {
            const JsonValue& mv = arr[static_cast<std::size_t>(i)];
            if (!mv.isObject()) continue;
            s.moves[static_cast<std::size_t>(i)].move_id = asIntOrDefault(child(mv, "move_id"), -1);
            s.moves[static_cast<std::size_t>(i)].move_name = asStringOrEmpty(child(mv, "move_name"));
            s.moves[static_cast<std::size_t>(i)].current_pp = asIntOrDefault(child(mv, "current_pp"), -1);
            s.moves[static_cast<std::size_t>(i)].pp_ups = asIntOrDefault(child(mv, "pp_ups"), -1);
        }
    }
    return s;
}

fs::path overlayPath(const std::string& save_directory) {
    return fs::path(save_directory) / "transfer_box_edits.json";
}

std::string serializeOverlayFile(const TransferBoxEditsOverlay& overlay) {
    std::ostringstream out;
    out << "{\n"
        << "  \"version\": " << std::max(1, overlay.version) << ",\n"
        << "  \"source_path\": " << quoted(overlay.source_path) << ",\n"
        << "  \"game_key\": " << quoted(overlay.game_key) << ",\n"
        << "  \"pc_boxes\": [\n";
    for (std::size_t bi = 0; bi < overlay.pc_boxes.size(); ++bi) {
        const auto& box = overlay.pc_boxes[bi];
        out << "    {\"name\": " << quoted(box.name) << ", \"slots\": [";
        for (std::size_t si = 0; si < box.slots.size(); ++si) {
            if (si) out << ",";
            serializeSlot(out, box.slots[si]);
        }
        out << "]}";
        if (bi + 1 < overlay.pc_boxes.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n"
        << "}\n";
    return out.str();
}

bool fileExists(const fs::path& path) {
    std::error_code error;
    return fs::exists(path, error);
}

} // namespace

std::optional<TransferBoxEditsOverlay> loadTransferBoxEditsOverlay(
    const std::string& save_directory,
    const std::string& source_path,
    const std::string& game_key,
    std::string* error_message) {
    if (error_message) error_message->clear();
    try {
        const fs::path path = overlayPath(save_directory);
        if (!fileExists(path)) {
            return std::nullopt;
        }
        const JsonValue root = parseJsonFile(path.string());
        if (!root.isObject()) {
            throw std::runtime_error("overlay root must be an object");
        }
        const std::string src = asStringOrEmpty(child(root, "source_path"));
        const std::string gk = asStringOrEmpty(child(root, "game_key"));
        if (src != source_path || gk != game_key) {
            return std::nullopt;
        }
        TransferBoxEditsOverlay overlay;
        overlay.version = std::max(1, asIntOrDefault(child(root, "version"), 1));
        if (overlay.version < 2) {
            return std::nullopt;
        }
        overlay.source_path = src;
        overlay.game_key = gk;
        const JsonValue* boxes = child(root, "pc_boxes");
        if (!boxes || !boxes->isArray()) {
            throw std::runtime_error("overlay pc_boxes must be an array");
        }
        for (const JsonValue& boxv : boxes->asArray()) {
            if (!boxv.isObject()) continue;
            TransferSaveSelection::PcBox box;
            box.name = asStringOrEmpty(child(boxv, "name"));
            if (const JsonValue* slots = child(boxv, "slots"); slots && slots->isArray()) {
                for (const JsonValue& slotv : slots->asArray()) {
                    box.slots.push_back(parseSlot(slotv));
                }
            }
            overlay.pc_boxes.push_back(std::move(box));
        }
        return overlay;
    } catch (const std::exception& ex) {
        if (error_message) *error_message = ex.what();
        return std::nullopt;
    }
}

bool saveTransferBoxEditsOverlayAtomic(
    const std::string& save_directory,
    const TransferBoxEditsOverlay& overlay,
    std::string* error_message) {
    if (error_message) error_message->clear();
    try {
        const fs::path dir(save_directory);
        if (!dir.empty()) {
            fs::create_directories(dir);
        }
        const fs::path path = overlayPath(save_directory);
        const fs::path temp = path.string() + ".tmp";
        {
            std::ofstream output(temp, std::ios::trunc);
            if (!output) {
                throw std::runtime_error("Could not open overlay temp file for writing");
            }
            output << serializeOverlayFile(overlay);
            output.flush();
            if (!output) {
                throw std::runtime_error("Failed while writing overlay");
            }
        }
        std::error_code remove_error;
        fs::remove(path, remove_error);
        std::error_code rename_error;
        fs::rename(temp, path, rename_error);
        if (rename_error) {
            std::error_code cleanup_error;
            fs::remove(temp, cleanup_error);
            throw std::runtime_error("Could not replace overlay file: " + rename_error.message());
        }
        return true;
    } catch (const std::exception& ex) {
        if (error_message) *error_message = ex.what();
        return false;
    }
}

} // namespace pr

