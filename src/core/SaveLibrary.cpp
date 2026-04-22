#include "core/SaveLibrary.hpp"
#include "core/SaveBridgeClient.hpp"
#include "core/Json.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <unordered_set>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace pr {

namespace {

/// Must match `bridge_probe_schema` in `tools/pkhex_bridge/BridgeConsole.cs`.
constexpr int kBridgeProbeSchemaRequired = 2;

struct CachedSaveRecord {
    std::string path;
    std::string filename;
    std::uintmax_t size = 0;
    std::string file_hash;
    SaveProbeStatus probe_status = SaveProbeStatus::NotProbed;
    std::string raw_bridge_output;
    std::optional<TransferSaveSummary> transfer_summary;
};

bool isIgnoredExtension(const fs::path& path) {
    const std::string extension = path.extension().string();
    static const std::vector<std::string> ignored{
        ".gba",
        ".gbc",
        ".gb",
        ".nds",
        ".3ds",
        ".cia",
        ".xci",
        ".nsp",
        ".iso",
        ".zip",
        ".7z",
        ".rar"
    };

    return std::find(ignored.begin(), ignored.end(), extension) != ignored.end();
}

bool isLikelySaveCandidate(const fs::path& path) {
    const std::string filename = path.filename().string();
    if (filename == "main") {
        return true;
    }

    const std::string extension = path.extension().string();
    static const std::vector<std::string> allowed{
        ".sav",
        ".dsv",
        ".dat",
        ".gci",
        ".bin",
        ".raw"
    };

    return std::find(allowed.begin(), allowed.end(), extension) != allowed.end();
}

std::string trimTrailingWhitespace(std::string value) {
    while (!value.empty() &&
           (value.back() == '\n' || value.back() == '\r' || value.back() == ' ' || value.back() == '\t')) {
        value.pop_back();
    }
    return value;
}

std::string formatFileTime(const fs::file_time_type& value) {
    using namespace std::chrono;

    const auto system_now = system_clock::now();
    const auto file_now = fs::file_time_type::clock::now();
    const auto adjusted = time_point_cast<system_clock::duration>(value - file_now + system_now);
    const std::time_t time = system_clock::to_time_t(adjusted);

    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    std::ostringstream out;
    out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

const char* probeStatusLabel(SaveProbeStatus status) {
    switch (status) {
        case SaveProbeStatus::NotProbed:
            return "not_probed";
        case SaveProbeStatus::ValidSave:
            return "valid_save";
        case SaveProbeStatus::InvalidSave:
            return "invalid_save";
        case SaveProbeStatus::BridgeError:
            return "bridge_error";
    }
    return "unknown";
}

SaveProbeStatus probeStatusFromString(const std::string& value) {
    if (value == "valid_save") return SaveProbeStatus::ValidSave;
    if (value == "invalid_save") return SaveProbeStatus::InvalidSave;
    if (value == "bridge_error") return SaveProbeStatus::BridgeError;
    return SaveProbeStatus::NotProbed;
}

const JsonValue* child(const JsonValue& parent, const std::string& key) {
    return parent.get(key);
}

std::string asStringOrEmpty(const JsonValue* value) {
    return value && value->isString() ? value->asString() : "";
}

int asIntOrDefault(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

int asIntOrZero(const JsonValue* value) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : 0;
}

bool asBoolOrDefault(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

std::vector<std::string> parseStringArray(const JsonValue* value) {
    std::vector<std::string> result;
    if (!value || !value->isArray()) {
        return result;
    }

    for (const JsonValue& item : value->asArray()) {
        if (item.isString()) {
            result.push_back(item.asString());
        }
    }
    return result;
}

std::string jsonEscapeDebug(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char uc : s) {
        const char c = static_cast<char>(uc);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (uc < 0x20) {
                    // Control chars: emit \u00XX
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(uc >> 4) & 0xF]);
                    out.push_back(hex[uc & 0xF]);
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    return out;
}

void appendJsonDebug(std::string& out, const JsonValue& v, int indent, int depth) {
    if (depth > 32) {
        out += "\"<max_depth>\"";
        return;
    }
    if (v.isNull()) {
        out += "null";
        return;
    }
    if (v.isBool()) {
        out += (v.asBool() ? "true" : "false");
        return;
    }
    if (v.isNumber()) {
        // Preserve integers cleanly where possible.
        const double d = v.asNumber();
        const long long as_i = static_cast<long long>(d);
        if (static_cast<double>(as_i) == d) {
            out += std::to_string(as_i);
        } else {
            std::ostringstream ss;
            ss << d;
            out += ss.str();
        }
        return;
    }
    if (v.isString()) {
        out += "\"";
        out += jsonEscapeDebug(v.asString());
        out += "\"";
        return;
    }
    if (v.isArray()) {
        out += "[";
        const auto& arr = v.asArray();
        if (!arr.empty()) {
            out += "\n";
            for (std::size_t i = 0; i < arr.size(); ++i) {
                out.append(static_cast<std::size_t>(indent + 2), ' ');
                appendJsonDebug(out, arr[i], indent + 2, depth + 1);
                if (i + 1 < arr.size()) out += ",";
                out += "\n";
            }
            out.append(static_cast<std::size_t>(indent), ' ');
        }
        out += "]";
        return;
    }
    if (v.isObject()) {
        out += "{";
        const auto& obj = v.asObject();
        if (!obj.empty()) {
            out += "\n";
            std::size_t i = 0;
            for (const auto& [k, vv] : obj) {
                out.append(static_cast<std::size_t>(indent + 2), ' ');
                out += "\"";
                out += jsonEscapeDebug(k);
                out += "\": ";
                appendJsonDebug(out, vv, indent + 2, depth + 1);
                if (i + 1 < obj.size()) out += ",";
                out += "\n";
                ++i;
            }
            out.append(static_cast<std::size_t>(indent), ' ');
        }
        out += "}";
        return;
    }
    out += "\"<unknown>\"";
}

std::string toJsonDebugString(const JsonValue& v) {
    std::string out;
    out.reserve(4096);
    appendJsonDebug(out, v, 0, 0);
    return out;
}

std::string speciesSlugFromPokemonObject(const JsonValue& pokemon) {
    if (!pokemon.isObject()) {
        return {};
    }
    std::string s = asStringOrEmpty(child(pokemon, "SpeciesSlug"));
    if (s.empty()) {
        s = asStringOrEmpty(child(pokemon, "speciesSlug"));
    }
    return s;
}

std::string formKeyFromPokemonObject(const JsonValue& pokemon) {
    if (!pokemon.isObject()) {
        return {};
    }
    std::string key = asStringOrEmpty(child(pokemon, "FormKey"));
    if (key.empty()) {
        key = asStringOrEmpty(child(pokemon, "formKey"));
    }
    if (key.empty()) {
        key = asStringOrEmpty(child(pokemon, "form_key"));
    }
    return key;
}

int genderFromPokemonObject(const JsonValue& pokemon) {
    if (!pokemon.isObject()) {
        return -1;
    }
    const JsonValue* g = child(pokemon, "Gender");
    if (!g) {
        g = child(pokemon, "gender");
    }
    if (g && g->isNumber()) {
        return static_cast<int>(g->asNumber());
    }
    return -1;
}

int speciesIdFromPokemonObject(const JsonValue& pokemon) {
    if (!pokemon.isObject()) {
        return -1;
    }
    const JsonValue* sid = child(pokemon, "SpeciesId");
    if (!sid) {
        sid = child(pokemon, "speciesId");
    }
    if (sid && sid->isNumber()) {
        return static_cast<int>(sid->asNumber());
    }
    return -1;
}

int boxIndexFromBoxObject(const JsonValue& box_el) {
    const JsonValue* idx_val = child(box_el, "Index");
    if (!idx_val) {
        idx_val = child(box_el, "index");
    }
    if (idx_val && idx_val->isNumber()) {
        return static_cast<int>(idx_val->asNumber());
    }
    return -1;
}

const JsonValue* childAny(const JsonValue& parent, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        if (const JsonValue* value = child(parent, key)) {
            return value;
        }
    }
    return nullptr;
}

void fillMoveSummaryFromObject(PcSlotMoveSummary& out, const JsonValue& move_obj) {
    if (!move_obj.isObject()) {
        return;
    }
    out.slot_index = asIntOrDefault(childAny(move_obj, {"Slot", "slot", "slot_index"}), out.slot_index);
    out.move_id = asIntOrDefault(childAny(move_obj, {"MoveId", "moveId", "move_id"}), out.move_id);
    out.move_name = asStringOrEmpty(childAny(move_obj, {"Name", "name", "move_name"}));
    out.current_pp = asIntOrDefault(childAny(move_obj, {"CurrentPp", "currentPp", "current_pp"}), out.current_pp);
    out.pp_ups = asIntOrDefault(childAny(move_obj, {"PpUps", "ppUps", "pp_ups"}), out.pp_ups);
}

void fillSlotFromPokemonObject(PcSlotSpecies& out, const JsonValue& pokemon) {
    if (!pokemon.isObject()) {
        return;
    }

    auto inferredFormKey = [](int species_id, int form) -> std::string {
        // Only used when the bridge does not provide FormKey. Keep this small and targeted.
        // Shellos/Gastrodon: 0 = West (default "$"), 1 = East ("east")
        if (species_id == 422 || species_id == 423) {
            return form == 1 ? "east" : "$";
        }
        // Burmy/Wormadam: 0 = Plant (default "$"), 1 = Sandy, 2 = Trash
        if (species_id == 412 || species_id == 413) {
            if (form == 1) return "sandy";
            if (form == 2) return "trash";
            return "$";
        }
        // Deerling/Sawsbuck: 0 = Spring (default "$"), 1 = Summer, 2 = Autumn, 3 = Winter
        if (species_id == 585 || species_id == 586) {
            if (form == 1) return "summer";
            if (form == 2) return "autumn";
            if (form == 3) return "winter";
            return "$";
        }
        // Basculin: 0 = Red-Striped (default "$"), 1 = Blue-Striped, 2 = White-Striped (later gens)
        if (species_id == 550) {
            if (form == 1) return "blue-striped";
            if (form == 2) return "white-striped";
            return "$";
        }
        return {};
    };

    out.present = true;
    out.format = asStringOrEmpty(childAny(pokemon, {"Format", "format"}));
    out.slug = speciesSlugFromPokemonObject(pokemon);
    out.species_name = asStringOrEmpty(childAny(pokemon, {"SpeciesName", "speciesName", "species_name"}));
    out.species_id = speciesIdFromPokemonObject(pokemon);
    out.nickname = asStringOrEmpty(childAny(pokemon, {"Nickname", "nickname"}));
    out.form = asIntOrDefault(childAny(pokemon, {"Form", "form"}), out.form);
    out.form_key = formKeyFromPokemonObject(pokemon);
    if (out.form_key.empty() && out.species_id >= 0 && out.form >= 0) {
        out.form_key = inferredFormKey(out.species_id, out.form);
    }
    out.gender = genderFromPokemonObject(pokemon);
    out.level = asIntOrDefault(childAny(pokemon, {"Level", "level"}), out.level);
    out.is_egg = asBoolOrDefault(childAny(pokemon, {"IsEgg", "isEgg", "is_egg"}), out.is_egg);
    out.is_shiny = asBoolOrDefault(childAny(pokemon, {"IsShiny", "isShiny", "is_shiny"}), out.is_shiny);
    out.ot_name = asStringOrEmpty(childAny(pokemon, {"OtName", "otName", "ot_name"}));
    out.tid16 = asIntOrDefault(childAny(pokemon, {"Tid16", "tid16"}), out.tid16);
    out.sid16 = asIntOrDefault(childAny(pokemon, {"Sid16", "sid16"}), out.sid16);
    out.held_item_id = asIntOrDefault(childAny(pokemon, {"HeldItemId", "heldItemId", "held_item_id"}), out.held_item_id);
    out.held_item_name = asStringOrEmpty(childAny(pokemon, {"HeldItemName", "heldItemName", "held_item_name"}));
    out.nature = asStringOrEmpty(childAny(pokemon, {"Nature", "nature"}));
    out.ability_id = asIntOrDefault(childAny(pokemon, {"AbilityId", "abilityId", "ability_id"}), out.ability_id);
    out.checksum_valid =
        asBoolOrDefault(childAny(pokemon, {"ChecksumValid", "checksumValid", "checksum_valid"}), out.checksum_valid);

    if (const JsonValue* location = childAny(pokemon, {"Location", "location"}); location && location->isObject()) {
        out.area = asStringOrEmpty(childAny(*location, {"Area", "area"}));
        out.box_index = asIntOrDefault(childAny(*location, {"Box", "box"}), out.box_index);
        out.slot_index = asIntOrDefault(childAny(*location, {"Slot", "slot"}), out.slot_index);
        out.global_index = asIntOrDefault(childAny(*location, {"GlobalIndex", "globalIndex", "global_index"}),
                                          out.global_index);
    }

    // Debug: dump the raw bridge Pokemon object for multi-form species.
    if (out.species_id == 201 || // unown
        out.species_id == 412 || out.species_id == 413 || // burmy/wormadam
        out.species_id == 422 || out.species_id == 423 || // shellos/gastrodon
        out.species_id == 585 || out.species_id == 586) { // deerling/sawsbuck
        static std::unordered_set<std::string> warned;
        const std::string key =
            std::to_string(out.species_id) + "|" + out.slug + "|" + std::to_string(out.box_index) + "|" +
            std::to_string(out.slot_index) + "|" + std::to_string(out.global_index) + "|" + out.form_key;
        if (warned.insert(key).second) {
            std::cerr << "[SaveLibrary] bridge_pokemon_debug species_id=" << out.species_id
                      << " slug=" << out.slug
                      << " area=" << out.area
                      << " box=" << out.box_index
                      << " slot=" << out.slot_index
                      << " global=" << out.global_index
                      << " parsed_form=" << out.form
                      << " parsed_form_key=\"" << out.form_key << "\""
                      << " parsed_gender=" << out.gender
                      << " parsed_level=" << out.level
                      << "\n"
                      << toJsonDebugString(pokemon) << "\n";
        }
    }

    out.moves = {};
    out.move_count = 0;
    if (const JsonValue* moves = childAny(pokemon, {"Moves", "moves"}); moves && moves->isArray()) {
        const auto& move_array = moves->asArray();
        const std::size_t limit = std::min<std::size_t>(out.moves.size(), move_array.size());
        for (std::size_t i = 0; i < limit; ++i) {
            fillMoveSummaryFromObject(out.moves[i], move_array[i]);
            ++out.move_count;
        }
    }
}

PcSlotSpecies speciesSlotFromSlotObject(const JsonValue& slot_obj, int fallback_box_index) {
    PcSlotSpecies out;
    if (!slot_obj.isObject()) {
        return out;
    }

    out.slot_index = asIntOrDefault(childAny(slot_obj, {"Slot", "slot"}), out.slot_index);
    out.global_index = asIntOrDefault(childAny(slot_obj, {"GlobalIndex", "globalIndex", "global_index"}), out.global_index);
    out.locked = asBoolOrDefault(childAny(slot_obj, {"Locked", "locked"}), out.locked);
    out.overwrite_protected = asBoolOrDefault(
        childAny(slot_obj, {"OverwriteProtected", "overwriteProtected", "overwrite_protected"}),
        out.overwrite_protected);
    out.box_index = fallback_box_index;
    out.area = fallback_box_index >= 0 ? "box" : "";

    const JsonValue* pokemon = childAny(slot_obj, {"Pokemon", "pokemon"});
    if (!pokemon || pokemon->isNull() || !pokemon->isObject()) {
        return out;
    }

    fillSlotFromPokemonObject(out, *pokemon);
    if (out.box_index < 0) {
        out.box_index = fallback_box_index;
    }
    if (out.area.empty() && out.box_index >= 0) {
        out.area = "box";
    }
    return out;
}

std::vector<PcSlotSpecies> collectSlotsFromBoxObject(const JsonValue& box_el) {
    std::vector<PcSlotSpecies> slots;
    const JsonValue* slots_val = child(box_el, "Slots");
    if (!slots_val) {
        slots_val = child(box_el, "slots");
    }
    if (!slots_val || !slots_val->isArray()) {
        return slots;
    }
    const int fallback_box_index = boxIndexFromBoxObject(box_el);
    for (const JsonValue& slot_el : slots_val->asArray()) {
        slots.push_back(speciesSlotFromSlotObject(slot_el, fallback_box_index));
    }
    return slots;
}

std::vector<PcSlotSpecies> parseBoxOneSlotsFromBoxesArray(const JsonValue& root) {
    const JsonValue* boxes_val = child(root, "boxes");
    if (!boxes_val || !boxes_val->isArray()) {
        return {};
    }

    const JsonValue::Array& boxes = boxes_val->asArray();
    for (const JsonValue& box_el : boxes) {
        if (!box_el.isObject()) {
            continue;
        }
        if (boxIndexFromBoxObject(box_el) != 0) {
            continue;
        }
        std::vector<PcSlotSpecies> slots = collectSlotsFromBoxObject(box_el);
        if (!slots.empty()) {
            return slots;
        }
    }

    // Some serializers omit Index or use 1-based values; use the first box if it has slots.
    for (const JsonValue& box_el : boxes) {
        if (!box_el.isObject()) {
            continue;
        }
        std::vector<PcSlotSpecies> slots = collectSlotsFromBoxObject(box_el);
        if (!slots.empty()) {
            return slots;
        }
    }
    return {};
}

std::vector<PcSlotSpecies> extractBoxOneSlotsFromAllPokemon(const JsonValue& root) {
    const JsonValue* arr = child(root, "all_pokemon");
    if (!arr) {
        arr = child(root, "allPokemon");
    }
    if (!arr || !arr->isArray()) {
        return {};
    }

    int max_slot = -1;
    std::vector<std::pair<int, PcSlotSpecies>> found;
    found.reserve(arr->asArray().size());

    for (const JsonValue& item : arr->asArray()) {
        if (!item.isObject()) {
            continue;
        }
        const JsonValue* loc = child(item, "Location");
        if (!loc) {
            loc = child(item, "location");
        }
        if (!loc || !loc->isObject()) {
            continue;
        }
        const JsonValue* box_v = child(*loc, "Box");
        if (!box_v) {
            box_v = child(*loc, "box");
        }
        if (!box_v || !box_v->isNumber()) {
            continue;
        }
        const int box = static_cast<int>(box_v->asNumber());
        if (box != 0) {
            continue;
        }
        const JsonValue* slot_v = child(*loc, "Slot");
        if (!slot_v) {
            slot_v = child(*loc, "slot");
        }
        if (!slot_v || !slot_v->isNumber()) {
            continue;
        }
        const int slot = static_cast<int>(slot_v->asNumber());
        if (slot < 0) {
            continue;
        }
        PcSlotSpecies ps;
        fillSlotFromPokemonObject(ps, item);
        ps.area = "box";
        ps.box_index = box;
        ps.slot_index = slot;
        found.emplace_back(slot, std::move(ps));
        max_slot = std::max(max_slot, slot);
    }

    if (found.empty() || max_slot < 0) {
        return {};
    }
    std::sort(found.begin(), found.end(), [](const std::pair<int, PcSlotSpecies>& a,
                                             const std::pair<int, PcSlotSpecies>& b) {
        return a.first < b.first;
    });
    std::vector<PcSlotSpecies> out(static_cast<std::size_t>(max_slot) + 1);
    for (const auto& entry : found) {
        const int slot = entry.first;
        if (slot >= 0 && static_cast<std::size_t>(slot) < out.size()) {
            out[static_cast<std::size_t>(slot)] = entry.second;
        }
    }
    return out;
}

std::vector<PcSlotSpecies> extractPartySlotsFromAllPokemon(const JsonValue& root) {
    const JsonValue* arr = child(root, "all_pokemon");
    if (!arr) {
        arr = child(root, "allPokemon");
    }
    if (!arr || !arr->isArray()) {
        return {};
    }

    std::vector<std::pair<int, PcSlotSpecies>> found;
    found.reserve(arr->asArray().size());
    for (const JsonValue& item : arr->asArray()) {
        if (!item.isObject()) {
            continue;
        }
        const JsonValue* loc = childAny(item, {"Location", "location"});
        if (!loc || !loc->isObject()) {
            continue;
        }
        const std::string area = asStringOrEmpty(childAny(*loc, {"Area", "area"}));
        if (area != "party") {
            continue;
        }
        const int slot = asIntOrDefault(childAny(*loc, {"Slot", "slot"}), -1);
        if (slot < 0) {
            continue;
        }
        PcSlotSpecies party_slot;
        fillSlotFromPokemonObject(party_slot, item);
        party_slot.area = "party";
        party_slot.box_index = -1;
        party_slot.slot_index = slot;
        party_slot.global_index =
            asIntOrDefault(childAny(*loc, {"GlobalIndex", "globalIndex", "global_index"}), slot);
        found.emplace_back(slot, std::move(party_slot));
    }

    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });

    std::vector<PcSlotSpecies> out;
    out.reserve(found.size());
    for (auto& entry : found) {
        out.push_back(std::move(entry.second));
    }
    return out;
}

std::string boxNameFromBoxObject(const JsonValue& box_el, int fallback_index) {
    const JsonValue* name_val = child(box_el, "Name");
    if (!name_val) {
        name_val = child(box_el, "name");
    }
    if (name_val && name_val->isString()) {
        const std::string n = name_val->asString();
        if (!n.empty()) {
            return n;
        }
    }
    if (fallback_index >= 0) {
        return "BOX " + std::to_string(fallback_index + 1);
    }
    return "BOX";
}

std::vector<TransferSaveSummary::PcBox> extractPcBoxes(const JsonValue& root) {
    const JsonValue* boxes_val = child(root, "boxes");
    if (!boxes_val || !boxes_val->isArray()) {
        return {};
    }
    const JsonValue::Array& boxes = boxes_val->asArray();
    struct IndexedBox {
        int index = -1;
        TransferSaveSummary::PcBox box;
    };
    std::vector<IndexedBox> parsed;
    parsed.reserve(boxes.size());
    int seq_index = 0;
    for (const JsonValue& box_el : boxes) {
        if (!box_el.isObject()) {
            continue;
        }
        const int idx = boxIndexFromBoxObject(box_el);
        std::vector<PcSlotSpecies> slots = collectSlotsFromBoxObject(box_el);
        if (slots.empty()) {
            continue;
        }
        // Normalize to 30 slots (pad/truncate).
        if (slots.size() < 30) {
            slots.resize(30);
        } else if (slots.size() > 30) {
            slots.resize(30);
        }
        IndexedBox ib;
        ib.index = idx;
        ib.box.name = boxNameFromBoxObject(box_el, idx >= 0 ? idx : seq_index);
        ib.box.slots = std::move(slots);
        parsed.push_back(std::move(ib));
        ++seq_index;
    }
    if (parsed.empty()) {
        return {};
    }
    // Sort by explicit index when present; otherwise preserve relative order.
    std::stable_sort(parsed.begin(), parsed.end(), [](const IndexedBox& a, const IndexedBox& b) {
        const bool ai = a.index >= 0;
        const bool bi = b.index >= 0;
        if (ai != bi) {
            return ai; // indexed boxes first
        }
        if (ai && bi) {
            return a.index < b.index;
        }
        return false;
    });
    std::vector<TransferSaveSummary::PcBox> out;
    out.reserve(parsed.size());
    for (auto& ib : parsed) {
        out.push_back(std::move(ib.box));
    }
    return out;
}

std::vector<PcSlotSpecies> parseBoxOneSlotsArrayField(const JsonValue* value) {
    std::vector<PcSlotSpecies> result;
    if (!value || !value->isArray()) {
        return result;
    }
    for (const JsonValue& item : value->asArray()) {
        if (item.isString()) {
            PcSlotSpecies slot;
            slot.present = !item.asString().empty();
            slot.slug = item.asString();
            result.push_back(std::move(slot));
            continue;
        }
        if (item.isObject()) {
            PcSlotSpecies s;
            const JsonValue* present_value = child(item, "present");
            s.area = asStringOrEmpty(child(item, "area"));
            s.box_index = asIntOrDefault(child(item, "box_index"), s.box_index);
            s.slot_index = asIntOrDefault(child(item, "slot_index"), s.slot_index);
            s.global_index = asIntOrDefault(child(item, "global_index"), s.global_index);
            s.locked = asBoolOrDefault(child(item, "locked"), s.locked);
            s.overwrite_protected = asBoolOrDefault(child(item, "overwrite_protected"), s.overwrite_protected);
            s.format = asStringOrEmpty(child(item, "format"));
            s.slug = asStringOrEmpty(child(item, "slug"));
            if (s.slug.empty()) {
                s.slug = asStringOrEmpty(child(item, "species_slug"));
            }
            if (s.slug.empty()) {
                s.slug = asStringOrEmpty(child(item, "speciesSlug"));
            }
            s.species_name = asStringOrEmpty(child(item, "species_name"));
            s.species_id = asIntOrDefault(childAny(item, {"species_id", "speciesId"}), s.species_id);
            s.nickname = asStringOrEmpty(child(item, "nickname"));
            s.form = asIntOrDefault(child(item, "form"), s.form);
            s.form_key = asStringOrEmpty(childAny(item, {"form_key", "formKey"}));
            s.gender = asIntOrDefault(childAny(item, {"gender", "Gender"}), s.gender);
            s.level = asIntOrDefault(child(item, "level"), s.level);
            s.is_egg = asBoolOrDefault(child(item, "is_egg"), s.is_egg);
            s.is_shiny = asBoolOrDefault(child(item, "is_shiny"), s.is_shiny);
            s.ot_name = asStringOrEmpty(child(item, "ot_name"));
            s.tid16 = asIntOrDefault(child(item, "tid16"), s.tid16);
            s.sid16 = asIntOrDefault(child(item, "sid16"), s.sid16);
            s.held_item_id = asIntOrDefault(child(item, "held_item_id"), s.held_item_id);
            s.held_item_name = asStringOrEmpty(child(item, "held_item_name"));
            s.nature = asStringOrEmpty(child(item, "nature"));
            s.ability_id = asIntOrDefault(child(item, "ability_id"), s.ability_id);
            s.checksum_valid = asBoolOrDefault(child(item, "checksum_valid"), s.checksum_valid);
            s.present = asBoolOrDefault(
                present_value,
                !s.slug.empty() || s.species_id >= 0 || !s.nickname.empty() || !s.species_name.empty());
            s.move_count = 0;
            if (const JsonValue* moves = child(item, "moves"); moves && moves->isArray()) {
                const auto& move_array = moves->asArray();
                const std::size_t limit = std::min<std::size_t>(s.moves.size(), move_array.size());
                for (std::size_t move_index = 0; move_index < limit; ++move_index) {
                    fillMoveSummaryFromObject(s.moves[move_index], move_array[move_index]);
                    ++s.move_count;
                }
            }
            result.push_back(std::move(s));
            continue;
        }
        result.push_back({});
    }
    return result;
}

std::vector<PcSlotSpecies> parsePartySlotsArrayField(const JsonValue* value) {
    std::vector<PcSlotSpecies> result;
    if (!value || !value->isArray()) {
        return result;
    }
    const auto& items = value->asArray();
    result.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        const JsonValue& item = items[i];
        if (item.isString()) {
            PcSlotSpecies slot;
            slot.present = !item.asString().empty();
            slot.area = "party";
            slot.slot_index = static_cast<int>(i);
            slot.global_index = static_cast<int>(i);
            slot.slug = item.asString();
            result.push_back(std::move(slot));
            continue;
        }
        if (item.isObject()) {
            PcSlotSpecies slot;
            const JsonValue* pokemon = childAny(item, {"Pokemon", "pokemon"});
            if (pokemon && pokemon->isObject()) {
                fillSlotFromPokemonObject(slot, *pokemon);
            } else {
                fillSlotFromPokemonObject(slot, item);
            }
            slot.area = "party";
            if (slot.slot_index < 0) {
                slot.slot_index = static_cast<int>(i);
            }
            if (slot.global_index < 0) {
                slot.global_index = slot.slot_index;
            }
            result.push_back(std::move(slot));
        }
    }
    return result;
}

std::vector<PcSlotSpecies> extractBoxOneSlots(const JsonValue& root) {
    std::vector<PcSlotSpecies> slots = parseBoxOneSlotsFromBoxesArray(root);
    if (!slots.empty()) {
        return slots;
    }
    const JsonValue* box1 = child(root, "box_1");
    if (box1 && box1->isArray()) {
        std::vector<PcSlotSpecies> from_box1 = parseBoxOneSlotsArrayField(box1);
        if (!from_box1.empty()) {
            return from_box1;
        }
    }
    return extractBoxOneSlotsFromAllPokemon(root);
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::optional<TransferSaveSummary> parseTransferSummary(const std::string& json_text, std::string* error_message) {
    try {
        const JsonValue root = parseJsonText(json_text);
        if (!root.isObject()) {
            if (error_message) {
                *error_message = "Bridge JSON root is not an object";
            }
            return std::nullopt;
        }

        TransferSaveSummary summary;
        summary.bridge_probe_schema = asIntOrZero(child(root, "bridge_probe_schema"));
        summary.game_id = asStringOrEmpty(child(root, "game_id"));
        summary.player_name = asStringOrEmpty(child(root, "player_name"));
        summary.party = parseStringArray(child(root, "party"));
        summary.party_slots = extractPartySlotsFromAllPokemon(root);
        if (summary.party_slots.empty()) {
            summary.party_slots = parsePartySlotsArrayField(child(root, "party"));
        }
        summary.play_time = asStringOrEmpty(child(root, "play_time"));
        summary.pokedex_count = asIntOrZero(child(root, "pokedex_count"));
        summary.badges = asIntOrZero(child(root, "badges"));
        summary.status = asStringOrEmpty(child(root, "status"));
        summary.error = asStringOrEmpty(child(root, "error"));
        summary.box_1_slots = extractBoxOneSlots(root);
        summary.pc_boxes = extractPcBoxes(root);
        return summary;
    } catch (const std::exception& e) {
        if (error_message) {
            *error_message = e.what();
        }
        return std::nullopt;
    }
}

std::string joinParty(const std::vector<std::string>& party) {
    std::ostringstream out;
    for (std::size_t i = 0; i < party.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << party[i];
    }
    return out.str();
}

std::string serializePcSlotArray(const std::vector<PcSlotSpecies>& slots, const std::string& child_padding) {
    std::ostringstream out;
    out << "[\n";
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (i > 0) {
            out << ",\n";
        }
        const PcSlotSpecies& s = slots[i];
        out << child_padding << "  {\n"
            << child_padding << "    \"present\": " << (s.present ? "true" : "false") << ",\n"
            << child_padding << "    \"area\": \"" << escapeJson(s.area) << "\",\n"
            << child_padding << "    \"box_index\": " << s.box_index << ",\n"
            << child_padding << "    \"slot_index\": " << s.slot_index << ",\n"
            << child_padding << "    \"global_index\": " << s.global_index << ",\n"
            << child_padding << "    \"locked\": " << (s.locked ? "true" : "false") << ",\n"
            << child_padding << "    \"overwrite_protected\": " << (s.overwrite_protected ? "true" : "false") << ",\n"
            << child_padding << "    \"format\": \"" << escapeJson(s.format) << "\",\n"
            << child_padding << "    \"slug\": \"" << escapeJson(s.slug) << "\",\n"
            << child_padding << "    \"species_name\": \"" << escapeJson(s.species_name) << "\",\n"
            << child_padding << "    \"species_id\": " << s.species_id << ",\n"
            << child_padding << "    \"nickname\": \"" << escapeJson(s.nickname) << "\",\n"
            << child_padding << "    \"form\": " << s.form << ",\n"
            << child_padding << "    \"form_key\": \"" << escapeJson(s.form_key) << "\",\n"
            << child_padding << "    \"gender\": " << s.gender << ",\n"
            << child_padding << "    \"level\": " << s.level << ",\n"
            << child_padding << "    \"is_egg\": " << (s.is_egg ? "true" : "false") << ",\n"
            << child_padding << "    \"is_shiny\": " << (s.is_shiny ? "true" : "false") << ",\n"
            << child_padding << "    \"ot_name\": \"" << escapeJson(s.ot_name) << "\",\n"
            << child_padding << "    \"tid16\": " << s.tid16 << ",\n"
            << child_padding << "    \"sid16\": " << s.sid16 << ",\n"
            << child_padding << "    \"held_item_id\": " << s.held_item_id << ",\n"
            << child_padding << "    \"held_item_name\": \"" << escapeJson(s.held_item_name) << "\",\n"
            << child_padding << "    \"nature\": \"" << escapeJson(s.nature) << "\",\n"
            << child_padding << "    \"ability_id\": " << s.ability_id << ",\n"
            << child_padding << "    \"checksum_valid\": " << (s.checksum_valid ? "true" : "false") << ",\n"
            << child_padding << "    \"moves\": [";
        for (int move_index = 0; move_index < s.move_count; ++move_index) {
            if (move_index > 0) {
                out << ", ";
            }
            const PcSlotMoveSummary& move = s.moves[static_cast<std::size_t>(move_index)];
            out << "{ \"slot_index\": " << move.slot_index
                << ", \"move_id\": " << move.move_id
                << ", \"move_name\": \"" << escapeJson(move.move_name) << "\""
                << ", \"current_pp\": " << move.current_pp
                << ", \"pp_ups\": " << move.pp_ups << " }";
        }
        out << "]\n"
            << child_padding << "  }";
    }
    out << "\n"
        << child_padding << "]";
    return out.str();
}

std::string hashFileContents(const fs::path& path, std::string* error_message) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error_message) {
            *error_message = "Could not open file for hashing";
        }
        return {};
    }

    constexpr std::uint64_t offset_basis = 14695981039346656037ull;
    constexpr std::uint64_t prime = 1099511628211ull;

    std::uint64_t hash = offset_basis;
    char buffer[8192];
    while (input) {
        input.read(buffer, sizeof(buffer));
        const std::streamsize read_count = input.gcount();
        for (std::streamsize i = 0; i < read_count; ++i) {
            hash ^= static_cast<unsigned char>(buffer[i]);
            hash *= prime;
        }
    }

    if (!input.eof() && input.fail()) {
        if (error_message) {
            *error_message = "Failed while reading file for hashing";
        }
        return {};
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string serializeTransferSummary(const TransferSaveSummary& summary, int indent) {
    const std::string padding(indent, ' ');
    const std::string child_padding(indent + 2, ' ');

    std::ostringstream out;
    out << "{\n"
        << child_padding << "\"bridge_probe_schema\": " << summary.bridge_probe_schema << ",\n"
        << child_padding << "\"game_id\": \"" << escapeJson(summary.game_id) << "\",\n"
        << child_padding << "\"player_name\": \"" << escapeJson(summary.player_name) << "\",\n"
        << child_padding << "\"party\": [";
    for (std::size_t i = 0; i < summary.party.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "\"" << escapeJson(summary.party[i]) << "\"";
    }
    out << "],\n"
        << child_padding << "\"party_slots\": " << serializePcSlotArray(summary.party_slots, child_padding) << ",\n"
        << child_padding << "\"play_time\": \"" << escapeJson(summary.play_time) << "\",\n"
        << child_padding << "\"pokedex_count\": " << summary.pokedex_count << ",\n"
        << child_padding << "\"badges\": " << summary.badges << ",\n"
        << child_padding << "\"status\": \"" << escapeJson(summary.status) << "\",\n"
        << child_padding << "\"error\": \"" << escapeJson(summary.error) << "\",\n"
        << child_padding << "\"box_1_slots\": " << serializePcSlotArray(summary.box_1_slots, child_padding) << "\n"
        << padding << "}";
    return out.str();
}

/// Ticket-menu cache only: omit PC box slots so we never imply disk cache feeds the transfer-system screen.
std::string serializeTransferSummaryForMenuCache(const TransferSaveSummary& summary, int indent) {
    TransferSaveSummary copy = summary;
    copy.box_1_slots.clear();
    return serializeTransferSummary(copy, indent);
}

std::optional<TransferSaveSummary> parseTransferSummaryFromObject(const JsonValue& object) {
    if (!object.isObject()) {
        return std::nullopt;
    }

    TransferSaveSummary summary;
    summary.bridge_probe_schema = asIntOrZero(child(object, "bridge_probe_schema"));
    summary.game_id = asStringOrEmpty(child(object, "game_id"));
    summary.player_name = asStringOrEmpty(child(object, "player_name"));
    summary.party = parseStringArray(child(object, "party"));
    // Cache stores `party_slots` as the same shape as `box_1_slots` (flat PcSlotSpecies objects), not bridge `Pokemon` objects.
    summary.party_slots = parseBoxOneSlotsArrayField(child(object, "party_slots"));
    // Ensure party area/indexes are consistent for UI consumers.
    for (std::size_t i = 0; i < summary.party_slots.size(); ++i) {
        PcSlotSpecies& s = summary.party_slots[i];
        s.area = "party";
        if (s.slot_index < 0) s.slot_index = static_cast<int>(i);
        if (s.global_index < 0) s.global_index = s.slot_index;
    }
    if (summary.party_slots.empty()) {
        summary.party_slots = parsePartySlotsArrayField(child(object, "party"));
    }
    summary.play_time = asStringOrEmpty(child(object, "play_time"));
    summary.pokedex_count = asIntOrZero(child(object, "pokedex_count"));
    summary.badges = asIntOrZero(child(object, "badges"));
    summary.status = asStringOrEmpty(child(object, "status"));
    summary.error = asStringOrEmpty(child(object, "error"));
    summary.box_1_slots = parseBoxOneSlotsArrayField(child(object, "box_1_slots"));
    return summary;
}

bool hasUsableTransferSummary(const std::optional<TransferSaveSummary>& summary) {
    // Ticket UI needs at least party data to render. If we have no party slots/strings, treat cache as stale
    // so we re-probe instead of showing empty sprites until the user clears the cache manually.
    return summary &&
           !summary->game_id.empty() &&
           !summary->player_name.empty() &&
           (!summary->party_slots.empty() || !summary->party.empty());
}

std::map<std::string, CachedSaveRecord> parseCacheRecords(const JsonValue& root) {
    std::map<std::string, CachedSaveRecord> records;
    const JsonValue* entries = child(root, "records");
    if (!entries || !entries->isArray()) {
        return records;
    }

    for (const JsonValue& item : entries->asArray()) {
        if (!item.isObject()) {
            continue;
        }

        CachedSaveRecord record;
        record.path = asStringOrEmpty(child(item, "path"));
        record.filename = asStringOrEmpty(child(item, "filename"));
        record.size = static_cast<std::uintmax_t>(asIntOrZero(child(item, "size")));
        record.file_hash = asStringOrEmpty(child(item, "file_hash"));
        record.probe_status = probeStatusFromString(asStringOrEmpty(child(item, "probe_status")));
        record.raw_bridge_output = asStringOrEmpty(child(item, "raw_bridge_output"));
        if (const JsonValue* summary = child(item, "transfer_summary")) {
            record.transfer_summary = parseTransferSummaryFromObject(*summary);
        }

        if (!record.path.empty()) {
            records.emplace(record.path, std::move(record));
        }
    }

    return records;
}

std::optional<TransferSaveSummary> probe_transfer_summary_from_bridge_stdout(const std::string& json_text) {
    std::string parse_error;
    return parseTransferSummary(json_text, &parse_error);
}

} // namespace

SaveLibrary::SaveLibrary(std::string project_root, std::string cache_directory, const char* argv0)
    : project_root_(std::move(project_root)),
      cache_directory_(std::move(cache_directory)),
      argv0_(argv0) {}

void SaveLibrary::refreshForTransferPage() {
    scanAndProbeProjectSaves();
}

void SaveLibrary::scanAndProbeProjectSaves() {
    loadCache();
    discoverFiles();
    probeDiscoveredFiles();
    saveCache();
}

const std::vector<SaveFileRecord>& SaveLibrary::records() const {
    return records_;
}

std::vector<SaveFileRecord> SaveLibrary::transferPageRecords() const {
    std::vector<SaveFileRecord> result;
    for (const SaveFileRecord& record : records_) {
        if (record.probe_status == SaveProbeStatus::ValidSave && record.transfer_summary) {
            result.push_back(record);
        }
    }
    return result;
}

const SaveFileRecord* SaveLibrary::findRecordByPath(const std::string& path) const {
    for (const SaveFileRecord& record : records_) {
        if (record.path == path) {
            return &record;
        }
    }
    return nullptr;
}

const SaveFileRecord* SaveLibrary::findRecordByGameId(const std::string& game_id) const {
    for (const SaveFileRecord& record : records_) {
        if (record.transfer_summary && record.transfer_summary->game_id == game_id) {
            return &record;
        }
    }
    return nullptr;
}

fs::path SaveLibrary::savesDirectory() const {
    return fs::path(project_root_).parent_path() / "saves";
}

fs::path SaveLibrary::cacheFilePath() const {
    return fs::path(cache_directory_) / "transfer_save_cache.json";
}

void SaveLibrary::loadCache() {
    const fs::path cache_path = cacheFilePath();
    std::error_code error;
    if (!fs::exists(cache_path, error)) {
        std::cerr << "[SaveLibrary] cache_status=missing path=" << cache_path.string() << '\n';
        return;
    }

    try {
        const JsonValue root = parseJsonFile(cache_path.string());
        const auto records = parseCacheRecords(root);
        std::cerr << "[SaveLibrary] cache_status=loaded path=" << cache_path.string()
                  << " entries=" << records.size() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "[SaveLibrary] cache_status=invalid path=" << cache_path.string()
                  << " error=" << e.what() << '\n';
    }
}

void SaveLibrary::discoverFiles() {
    records_.clear();

    const fs::path directory = savesDirectory();
    std::cerr << "[SaveLibrary] scan_begin directory=" << directory.string() << '\n';

    std::error_code dir_error;
    if (!fs::exists(directory, dir_error) || !fs::is_directory(directory, dir_error)) {
        std::cerr << "[SaveLibrary] scan_skipped reason=missing_directory\n";
        return;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        if (isIgnoredExtension(entry.path())) {
            std::cerr << "[SaveLibrary] skip filename=" << entry.path().filename().string()
                      << " reason=ignored_extension\n";
            continue;
        }

        if (!isLikelySaveCandidate(entry.path())) {
            std::cerr << "[SaveLibrary] skip filename=" << entry.path().filename().string()
                      << " reason=not_likely_save\n";
            continue;
        }

        SaveFileRecord record;
        record.path = entry.path().string();
        record.filename = entry.path().filename().string();

        std::error_code metadata_error;
        record.size = entry.file_size(metadata_error);
        if (metadata_error) {
            record.size = 0;
        }

        metadata_error.clear();
        record.last_write_time = entry.last_write_time(metadata_error);
        if (metadata_error) {
            record.last_write_time = fs::file_time_type{};
        }

        records_.push_back(std::move(record));
    }

    std::sort(
        records_.begin(),
        records_.end(),
        [](const SaveFileRecord& lhs, const SaveFileRecord& rhs) {
            return lhs.filename < rhs.filename;
        });

    std::cerr << "[SaveLibrary] discovered_count=" << records_.size() << '\n';
    for (const SaveFileRecord& record : records_) {
        std::cerr << "[SaveLibrary] file"
                  << " filename=" << record.filename
                  << " size=" << record.size
                  << " last_write=\"" << formatFileTime(record.last_write_time) << "\""
                  << " path=" << record.path
                  << '\n';
    }
}

void SaveLibrary::probeDiscoveredFiles() {
    std::map<std::string, CachedSaveRecord> cache_records;
    try {
        const fs::path cache_path = cacheFilePath();
        std::error_code error;
        if (fs::exists(cache_path, error)) {
            cache_records = parseCacheRecords(parseJsonFile(cache_path.string()));
        }
    } catch (const std::exception& e) {
        std::cerr << "[SaveLibrary] cache_status=read_failed error=" << e.what() << '\n';
    }

    for (SaveFileRecord& record : records_) {
        try {
            std::string hash_error;
            record.file_hash = hashFileContents(record.path, &hash_error);
            if (record.file_hash.empty()) {
                record.probe_status = SaveProbeStatus::BridgeError;
                std::cerr << "[SaveLibrary] hash_status=failed filename=" << record.filename
                          << " error=" << hash_error << '\n';
                continue;
            }

            std::cerr << "[SaveLibrary] hash_status=ok filename=" << record.filename
                      << " hash=" << record.file_hash << '\n';

            const auto cache_it = cache_records.find(record.path);
            bool should_probe = true;
            if (cache_it != cache_records.end() &&
                cache_it->second.file_hash == record.file_hash) {
                record.used_cache = true;
                record.probe_status = cache_it->second.probe_status;
                record.raw_bridge_output = cache_it->second.raw_bridge_output;
                record.transfer_summary = cache_it->second.transfer_summary;
                record.bridge_result.bridge_path = "cache";
                record.bridge_result.command = "cache_hit";
                std::cerr << "[SaveLibrary] cache_result=hit filename=" << record.filename
                          << " probe_status=" << probeStatusLabel(record.probe_status) << '\n';
                should_probe =
                    record.probe_status == SaveProbeStatus::ValidSave &&
                    !hasUsableTransferSummary(record.transfer_summary);
                if (should_probe) {
                    std::cerr << "[SaveLibrary] cache_result=stale filename=" << record.filename
                              << " reason=missing_required_transfer_fields\n";
                    record.used_cache = false;
                    record.transfer_summary.reset();
                    record.raw_bridge_output.clear();
                }
            }

            if (should_probe) {
                std::cerr << "[SaveLibrary] cache_result=miss filename=" << record.filename << '\n';
                record.bridge_result = probeSaveWithBridge(project_root_, argv0_, record.path);
                record.raw_bridge_output = trimTrailingWhitespace(record.bridge_result.stdout_text);

                if (!record.bridge_result.launched || !record.bridge_result.error_message.empty()) {
                    record.probe_status = SaveProbeStatus::BridgeError;
                } else if (record.bridge_result.success) {
                    record.probe_status = SaveProbeStatus::ValidSave;
                } else {
                    record.probe_status = SaveProbeStatus::InvalidSave;
                }

                if (record.probe_status == SaveProbeStatus::ValidSave) {
                    std::string parse_error;
                    record.transfer_summary = parseTransferSummary(record.raw_bridge_output, &parse_error);
                    if (!record.transfer_summary) {
                        record.probe_status = SaveProbeStatus::BridgeError;
                        if (!parse_error.empty()) {
                            std::cerr << "[SaveLibrary] summary_parse_error filename=" << record.filename
                                      << " error=" << parse_error << '\n';
                        }
                    } else if (!hasUsableTransferSummary(record.transfer_summary)) {
                        record.probe_status = SaveProbeStatus::BridgeError;
                        std::cerr << "[SaveLibrary] summary_parse_error filename=" << record.filename
                                  << " error=missing_required_transfer_fields\n";
                        record.transfer_summary.reset();
                    }
                }
            }
        } catch (const std::exception& e) {
            record.probe_status = SaveProbeStatus::BridgeError;
            record.transfer_summary.reset();
            record.raw_bridge_output.clear();
            record.bridge_result.error_message = e.what();
            std::cerr << "[SaveLibrary] record_error filename=" << record.filename
                      << " error=" << e.what() << '\n';
        }

        std::cerr << "[SaveLibrary] probe"
                  << " filename=" << record.filename
                  << " status=" << probeStatusLabel(record.probe_status)
                  << " cache=" << (record.used_cache ? "hit" : "miss")
                  << " launched=" << (record.bridge_result.launched ? "true" : "false")
                  << " exit_code=" << record.bridge_result.exit_code
                  << " bridge_path=" << record.bridge_result.bridge_path
                  << '\n';
        std::cerr << "[SaveLibrary] probe_command=" << record.bridge_result.command << '\n';
        std::cerr << "[SaveLibrary] probe_stdout=" << trimTrailingWhitespace(record.bridge_result.stdout_text) << '\n';
        std::cerr << "[SaveLibrary] probe_stderr=" << trimTrailingWhitespace(record.bridge_result.stderr_text) << '\n';
        if (!record.bridge_result.error_message.empty()) {
            std::cerr << "[SaveLibrary] probe_error=" << record.bridge_result.error_message << '\n';
        }
        if (record.transfer_summary) {
            const TransferSaveSummary& summary = *record.transfer_summary;
            std::cerr << "[SaveLibrary] summary"
                      << " filename=" << record.filename
                      << " game_id=" << summary.game_id
                      << " player_name=" << summary.player_name
                      << " play_time=" << summary.play_time
                      << " pokedex_count=" << summary.pokedex_count
                      << " badges=" << summary.badges
                      << " party=[" << joinParty(summary.party) << "]"
                      << " status=" << summary.status;
            if (!summary.error.empty()) {
                std::cerr << " error=" << summary.error;
            }
            std::cerr << '\n';
        }
    }
}

void SaveLibrary::saveCache() const {
    const fs::path cache_path = cacheFilePath();
    try {
        const fs::path directory = cache_path.parent_path();
        if (!directory.empty()) {
            fs::create_directories(directory);
        }

        std::ostringstream out;
        out << "{\n"
            << "  \"version\": 2,\n"
            << "  \"records\": [\n";

        for (std::size_t i = 0; i < records_.size(); ++i) {
            const SaveFileRecord& record = records_[i];
            out << "    {\n"
                << "      \"path\": \"" << escapeJson(record.path) << "\",\n"
                << "      \"filename\": \"" << escapeJson(record.filename) << "\",\n"
                << "      \"size\": " << record.size << ",\n"
                << "      \"file_hash\": \"" << escapeJson(record.file_hash) << "\",\n"
                << "      \"probe_status\": \"" << probeStatusLabel(record.probe_status) << "\",\n"
                << "      \"transfer_summary\": ";
            if (record.transfer_summary) {
                out << serializeTransferSummaryForMenuCache(*record.transfer_summary, 6) << '\n';
            } else {
                out << "null\n";
            }
            out << "    }";
            if (i + 1 < records_.size()) {
                out << ",";
            }
            out << '\n';
        }

        out << "  ]\n"
            << "}\n";

        const fs::path temp_path = cache_path.string() + ".tmp";
        {
            std::ofstream output(temp_path, std::ios::trunc);
            if (!output) {
                throw std::runtime_error("Could not open cache file for writing");
            }
            output << out.str();
            output.flush();
            if (!output) {
                throw std::runtime_error("Failed while writing cache file");
            }
        }

        std::error_code remove_error;
        fs::remove(cache_path, remove_error);

        std::error_code rename_error;
        fs::rename(temp_path, cache_path, rename_error);
        if (rename_error) {
            fs::remove(temp_path, remove_error);
            throw std::runtime_error("Could not replace cache file: " + rename_error.message());
        }

        std::cerr << "[SaveLibrary] cache_status=saved path=" << cache_path.string()
                  << " entries=" << records_.size() << '\n';
    } catch (const std::exception& e) {
        std::cerr << "[SaveLibrary] cache_status=save_failed path=" << cache_path.string()
                  << " error=" << e.what() << '\n';
    }
}

std::optional<TransferSaveSummary> probeTransferSummaryFresh(
    const std::string& project_root,
    const char* argv0,
    const std::string& save_path) {
    SaveBridgeProbeResult bridge = probeSaveWithBridge(project_root, argv0, save_path);
    if (!bridge.launched || !bridge.error_message.empty()) {
        std::cerr << "[SaveLibrary] fresh_probe bridge_launch_failed path=" << save_path << '\n';
        return std::nullopt;
    }
    if (!bridge.success) {
        std::cerr << "[SaveLibrary] fresh_probe unsupported_or_invalid path=" << save_path << '\n';
        return std::nullopt;
    }
    std::optional<TransferSaveSummary> summary =
        probe_transfer_summary_from_bridge_stdout(trimTrailingWhitespace(bridge.stdout_text));
    if (summary) {
        if (summary->bridge_probe_schema < kBridgeProbeSchemaRequired) {
            std::cerr << "[SaveLibrary] fresh_probe reject path=" << save_path
                      << " reason=outdated_bridge_binary bridge_probe_schema=" << summary->bridge_probe_schema
                      << " need>=" << kBridgeProbeSchemaRequired
                      << " (rebuild tools/pkhex_bridge and prefer bin/Release over stale publish/)\n";
            return std::nullopt;
        }
        std::cerr << "[SaveLibrary] fresh_probe ok path=" << save_path
                  << " bridge_path=" << bridge.bridge_path
                  << " command=" << bridge.command
                  << " transfer_save_cache_not_used box_1_slots=" << summary->box_1_slots.size()
                  << '\n';
    }
    return summary;
}

} // namespace pr
