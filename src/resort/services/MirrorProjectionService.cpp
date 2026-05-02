#include "resort/services/MirrorProjectionService.hpp"

#include "core/config/Json.hpp"
#include "core/crypto/Sha256.hpp"
#include "resort/domain/ResortTypes.hpp"
#include "resort/integration/BridgeImportAdapter.hpp"
#include "resort/persistence/PokemonRepository.hpp"
#include "resort/persistence/SnapshotRepository.hpp"

#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace pr::resort {

namespace {

constexpr char kBase64Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string encodeBase64(const std::vector<unsigned char>& bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = (i + 1) < bytes.size() ? bytes[i + 1] : 0;
        const unsigned int b2 = (i + 2) < bytes.size() ? bytes[i + 2] : 0;
        out.push_back(kBase64Alphabet[(b0 >> 2) & 0x3f]);
        out.push_back(kBase64Alphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0f)]);
        out.push_back((i + 1) < bytes.size() ? kBase64Alphabet[((b1 & 0x0f) << 2) | ((b2 >> 6) & 0x03)] : '=');
        out.push_back((i + 2) < bytes.size() ? kBase64Alphabet[b2 & 0x3f] : '=');
    }
    return out;
}

std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::optional<int> jsonIntFromObject(const pr::JsonValue& obj, const char* key) {
    if (!obj.isObject()) {
        return std::nullopt;
    }
    const pr::JsonValue* v = obj.get(key);
    if (!v || !v->isNumber()) {
        return std::nullopt;
    }
    return static_cast<int>(std::lround(v->asNumber()));
}

/// Builds `move_reconciliation` from warm JSON `resort_catalog.moves` (slot_index, move_name, move_id).
void appendMoveReconciliationJson(std::ostringstream& body, const std::string& warm_json) {
    if (warm_json.empty()) {
        return;
    }
    try {
        const pr::JsonValue root = pr::parseJsonText(warm_json);
        const pr::JsonValue* rc = root.get("resort_catalog");
        if (!rc || !rc->isObject()) {
            return;
        }
        const pr::JsonValue* moves = rc->get("moves");
        if (!moves || !moves->isArray() || moves->asArray().empty()) {
            return;
        }
        std::ostringstream slots;
        bool first = true;
        for (const pr::JsonValue& slot : moves->asArray()) {
            if (!slot.isObject()) {
                continue;
            }
            const pr::JsonValue* si = slot.get("slot_index");
            if (!si || !si->isNumber()) {
                continue;
            }
            const int idx = static_cast<int>(std::lround(si->asNumber()));
            if (idx < 0 || idx > 3) {
                continue;
            }
            if (!first) {
                slots << ",";
            }
            first = false;
            slots << "{\"slot_index\":" << idx;
            const pr::JsonValue* mn = slot.get("move_name");
            if (mn && mn->isString()) {
                slots << ",\"move_name\":\"" << jsonEscape(mn->asString()) << "\"";
            }
            const pr::JsonValue* mid = slot.get("move_id");
            if (mid && mid->isNumber()) {
                slots << ",\"move_id\":" << static_cast<int>(std::lround(mid->asNumber()));
            }
            slots << "}";
        }
        if (first) {
            return;
        }
        body << ",\"move_reconciliation\":{"
             << "\"enabled\":true,"
             << "\"moves\":[" << slots.str() << "]}";
    } catch (const std::exception&) {
        // Omit reconciliation when warm JSON is not parseable.
    }
}

/// Canonical friendship / Pokerus from Resort warm catalog (with legacy warm-json fallbacks).
void appendPreSaveReviewJson(
    std::ostringstream& body,
    const std::string& warm_json,
    const PokemonHot* hot) {
    bool any = false;
    auto open = [&]() {
        if (!any) {
            body << ",\"pre_save_review\":{\"enabled\":true";
            any = true;
        }
    };

    auto appendFriendshipBlock = [&](const pr::JsonValue& friendship_obj) {
        if (const auto v = jsonIntFromObject(friendship_obj, "original_trainer")) {
            open();
            body << ",\"friendship_ot\":" << *v;
        }
        if (const auto v = jsonIntFromObject(friendship_obj, "handling_trainer")) {
            open();
            body << ",\"friendship_handling\":" << *v;
        }
        if (const auto v = jsonIntFromObject(friendship_obj, "current")) {
            open();
            body << ",\"friendship_current\":" << *v;
        }
    };

    try {
        const pr::JsonValue root = pr::parseJsonText(warm_json);
        const pr::JsonValue* rc = root.get("resort_catalog");
        bool friendship_from_catalog = false;
        if (rc && rc->isObject()) {
            const pr::JsonValue* fr = rc->get("friendship");
            if (fr && fr->isObject()) {
                appendFriendshipBlock(*fr);
                friendship_from_catalog = true;
            }
            const pr::JsonValue* pokerus = rc->get("pokerus");
            if (pokerus && pokerus->isObject()) {
                if (const auto v = jsonIntFromObject(*pokerus, "strain_or_state")) {
                    open();
                    body << ",\"pokerus_strain\":" << *v;
                }
                if (const auto v = jsonIntFromObject(*pokerus, "days")) {
                    open();
                    body << ",\"pokerus_days\":" << *v;
                }
            }
        }
        if (!friendship_from_catalog) {
            if (const auto v = jsonIntFromObject(root, "original_trainer_friendship")) {
                open();
                body << ",\"friendship_ot\":" << *v;
            }
            if (const auto v = jsonIntFromObject(root, "handling_trainer_friendship")) {
                open();
                body << ",\"friendship_handling\":" << *v;
            }
            if (const auto v = jsonIntFromObject(root, "current_friendship")) {
                open();
                body << ",\"friendship_current\":" << *v;
            }
        }
    } catch (const std::exception&) {
    }

    if (hot) {
        open();
        body << ",\"nickname\":\"" << jsonEscape(hot->nickname) << "\""
             << ",\"is_nicknamed\":" << (hot->is_nicknamed ? "true" : "false");
    }

    if (any) {
        body << "}";
    }
}

void appendHotMutableOverlayJson(std::ostringstream& body, const PokemonHot* hot) {
    if (!hot) {
        return;
    }
    body << ",\"hot_mutable_overlay\":{"
         << "\"enabled\":true,"
         << "\"species_id\":" << hot->species_id << ","
         << "\"form_id\":" << hot->form_id << ","
         << "\"level\":" << static_cast<int>(hot->level) << ","
         << "\"exp\":" << hot->exp << ","
         << "\"nickname\":\"" << jsonEscape(hot->nickname) << "\","
         << "\"is_nicknamed\":" << (hot->is_nicknamed ? "true" : "false") << ","
         << "\"gender\":" << static_cast<int>(hot->gender) << ","
         << "\"shiny\":" << (hot->shiny ? "true" : "false") << ","
         << "\"held_item_id\":" << (hot->held_item_id ? std::to_string(*hot->held_item_id) : "null") << ","
         << "\"hp_current\":" << hot->hp_current << ","
         << "\"hp_max\":" << hot->hp_max << ","
         << "\"status_flags\":" << hot->status_flags << ","
         << "\"moves\":[";
    for (std::size_t i = 0; i < hot->move_ids.size(); ++i) {
        if (i > 0) {
            body << ",";
        }
        body << "{\"slot_index\":" << i
             << ",\"move_id\":" << (hot->move_ids[i] ? std::to_string(*hot->move_ids[i]) : "0")
             << ",\"current_pp\":" << (hot->move_pp[i] ? std::to_string(*hot->move_pp[i]) : "0")
             << ",\"pp_ups\":" << (hot->move_pp_ups[i] ? std::to_string(*hot->move_pp_ups[i]) : "0")
             << "}";
    }
    body << "]"
         << "}";
}

} // namespace

MirrorProjectionService::MirrorProjectionService(
    const PokemonRepository& pokemon,
    const SnapshotRepository& snapshots)
    : pokemon_(pokemon),
      snapshots_(snapshots) {}

MirrorBridgeProjectOutcome MirrorProjectionService::projectLatestSnapshotToTarget(
    const MirrorBridgeProjectInput& input,
    const std::string& project_root,
    const char* argv0,
    const std::filesystem::path& request_json_path) const {
    MirrorBridgeProjectOutcome out;
    if (input.pkrid.empty() || input.target_format_name.empty()) {
        out.error = "MirrorBridgeProjectInput requires pkrid and target_format_name";
        return out;
    }
    if (!pokemon_.exists(input.pkrid)) {
        out.error = "Pokemon not found: " + input.pkrid;
        return out;
    }
    std::optional<PokemonSnapshot> source =
        snapshots_.findLatestRawForPokemon(input.pkrid, input.target_game, input.target_format_name);
    if (!source) {
        source = snapshots_.findLatestRawForPokemon(input.pkrid, std::nullopt, input.target_format_name);
    }
    if (!source) {
        source = snapshots_.findLatestRawForPokemon(input.pkrid, std::nullopt, std::string{});
    }
    if (!source || source->raw_bytes.empty() || source->raw_hash_sha256.empty()) {
        out.error = "No import-grade raw snapshot to project for pkrid=" + input.pkrid;
        return out;
    }

    const std::string b64 = encodeBase64(source->raw_bytes);
    const std::string source_fmt = source->format_name.empty() ? "PKM" : source->format_name;

    const std::optional<ResortPokemon> canonical = pokemon_.findById(input.pkrid);
    const std::string warm_json = canonical ? canonical->warm.json : "";
    const PokemonHot* hot_ptr = canonical ? &canonical->hot : nullptr;

    std::ostringstream body;
    body << "{"
         << "\"bridge_project_schema\":1,"
         << "\"source_format_name\":\"" << jsonEscape(source_fmt) << "\","
         << "\"source_raw_payload_base64\":\"" << jsonEscape(b64) << "\","
         << "\"source_raw_hash_sha256\":\"" << jsonEscape(source->raw_hash_sha256) << "\","
         << "\"target_game\":" << static_cast<int>(input.target_game) << ","
         << "\"target_format_name\":\"" << jsonEscape(input.target_format_name) << "\","
         << "\"projection_policy\":{"
         << "\"allow_lossy_projection\":" << (input.allow_lossy_projection ? "true" : "false")
         << "},"
         << "\"source_snapshot_id\":\"" << jsonEscape(source->snapshot_id) << "\"";
    appendMoveReconciliationJson(body, warm_json);
    appendPreSaveReviewJson(body, warm_json, hot_ptr);
    appendHotMutableOverlayJson(body, hot_ptr);
    body << "}";

    {
        std::ofstream f(request_json_path, std::ios::trunc);
        if (!f) {
            out.error = "Could not open bridge project request file for writing";
            return out;
        }
        f << body.str();
        if (!f.good()) {
            out.error = "Failed writing bridge project request file";
            return out;
        }
    }

    out.bridge = pr::projectPokemonWithBridge(project_root, argv0, request_json_path.string());
    if (!out.bridge.success && out.bridge.error_message.empty()) {
        out.error = "bridge project failed";
    } else if (!out.bridge.success) {
        out.error = out.bridge.error_message;
    }
    return out;
}

MirrorProjectDecodedResult MirrorProjectionService::projectLatestSnapshotToTargetDecoded(
    const MirrorBridgeProjectInput& input,
    const std::string& project_root,
    const char* argv0,
    const std::filesystem::path& request_json_path) const {
    MirrorProjectDecodedResult out;
    const MirrorBridgeProjectOutcome bridge_out =
        projectLatestSnapshotToTarget(input, project_root, argv0, request_json_path);
    if (!bridge_out.bridge.success) {
        out.error = bridge_out.error.empty() ? bridge_out.bridge.error_message : bridge_out.error;
        return out;
    }
    const pr::SaveBridgeProjectResult& br = bridge_out.bridge;
    if (br.target_raw_payload_base64.empty() || br.target_raw_hash_sha256.empty()) {
        out.error = "bridge project response missing target payload or hash";
        return out;
    }
    std::vector<unsigned char> bytes;
    try {
        bytes = decodeBase64Payload(br.target_raw_payload_base64);
    } catch (const std::exception& ex) {
        out.error = std::string("decode projected payload: ") + ex.what();
        return out;
    }
    if (pr::sha256HexLowercase(bytes) != br.target_raw_hash_sha256) {
        out.error = "projected payload SHA-256 mismatch";
        return out;
    }
    out.success = true;
    out.raw_bytes = std::move(bytes);
    out.raw_hash_sha256 = br.target_raw_hash_sha256;
    out.target_format_name =
        br.target_format_name.empty() ? input.target_format_name : br.target_format_name;
    return out;
}

} // namespace pr::resort
