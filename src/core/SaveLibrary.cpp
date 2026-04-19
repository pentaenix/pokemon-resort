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

int asIntOrZero(const JsonValue* value) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : 0;
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

std::string speciesSlugFromSlotObject(const JsonValue& slot_obj) {
    if (!slot_obj.isObject()) {
        return {};
    }
    const JsonValue* pokemon = child(slot_obj, "Pokemon");
    if (!pokemon) {
        pokemon = child(slot_obj, "pokemon");
    }
    if (!pokemon || pokemon->isNull()) {
        return {};
    }
    return speciesSlugFromPokemonObject(*pokemon);
}

std::vector<std::string> collectSlotsFromBoxObject(const JsonValue& box_el) {
    std::vector<std::string> slots;
    const JsonValue* slots_val = child(box_el, "Slots");
    if (!slots_val) {
        slots_val = child(box_el, "slots");
    }
    if (!slots_val || !slots_val->isArray()) {
        return slots;
    }
    for (const JsonValue& slot_el : slots_val->asArray()) {
        slots.push_back(speciesSlugFromSlotObject(slot_el));
    }
    return slots;
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

std::vector<std::string> parseBoxOneSlotsFromBoxesArray(const JsonValue& root) {
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
        std::vector<std::string> slots = collectSlotsFromBoxObject(box_el);
        if (!slots.empty()) {
            return slots;
        }
    }

    // Some serializers omit Index or use 1-based values; use the first box if it has slots.
    for (const JsonValue& box_el : boxes) {
        if (!box_el.isObject()) {
            continue;
        }
        std::vector<std::string> slots = collectSlotsFromBoxObject(box_el);
        if (!slots.empty()) {
            return slots;
        }
    }
    return {};
}

std::vector<std::string> extractBoxOneSlotsFromAllPokemon(const JsonValue& root) {
    const JsonValue* arr = child(root, "all_pokemon");
    if (!arr) {
        arr = child(root, "allPokemon");
    }
    if (!arr || !arr->isArray()) {
        return {};
    }

    int max_slot = -1;
    std::vector<std::pair<int, std::string>> found;
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
        std::string slug = asStringOrEmpty(child(item, "SpeciesSlug"));
        if (slug.empty()) {
            slug = asStringOrEmpty(child(item, "speciesSlug"));
        }
        found.emplace_back(slot, std::move(slug));
        max_slot = std::max(max_slot, slot);
    }

    if (found.empty() || max_slot < 0) {
        return {};
    }
    std::sort(found.begin(), found.end(), [](const std::pair<int, std::string>& a,
                                             const std::pair<int, std::string>& b) {
        return a.first < b.first;
    });
    std::vector<std::string> out(static_cast<std::size_t>(max_slot) + 1);
    for (const auto& entry : found) {
        const int slot = entry.first;
        if (slot >= 0 && static_cast<std::size_t>(slot) < out.size()) {
            out[static_cast<std::size_t>(slot)] = entry.second;
        }
    }
    return out;
}

std::vector<std::string> extractBoxOneSlots(const JsonValue& root) {
    std::vector<std::string> slots = parseBoxOneSlotsFromBoxesArray(root);
    if (!slots.empty()) {
        return slots;
    }
    slots = parseStringArray(child(root, "box_1"));
    if (!slots.empty()) {
        return slots;
    }
    return extractBoxOneSlotsFromAllPokemon(root);
}

std::vector<std::string> parseBoxOneSlotsArrayField(const JsonValue* value) {
    std::vector<std::string> result;
    if (!value || !value->isArray()) {
        return result;
    }
    for (const JsonValue& item : value->asArray()) {
        if (item.isString()) {
            result.push_back(item.asString());
        } else {
            result.push_back({});
        }
    }
    return result;
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
        summary.play_time = asStringOrEmpty(child(root, "play_time"));
        summary.pokedex_count = asIntOrZero(child(root, "pokedex_count"));
        summary.badges = asIntOrZero(child(root, "badges"));
        summary.status = asStringOrEmpty(child(root, "status"));
        summary.error = asStringOrEmpty(child(root, "error"));
        summary.box_1_slots = extractBoxOneSlots(root);
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
        << child_padding << "\"play_time\": \"" << escapeJson(summary.play_time) << "\",\n"
        << child_padding << "\"pokedex_count\": " << summary.pokedex_count << ",\n"
        << child_padding << "\"badges\": " << summary.badges << ",\n"
        << child_padding << "\"status\": \"" << escapeJson(summary.status) << "\",\n"
        << child_padding << "\"error\": \"" << escapeJson(summary.error) << "\",\n"
        << child_padding << "\"box_1_slots\": [";
    for (std::size_t i = 0; i < summary.box_1_slots.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << "\"" << escapeJson(summary.box_1_slots[i]) << "\"";
    }
    out << "]\n"
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
    summary.play_time = asStringOrEmpty(child(object, "play_time"));
    summary.pokedex_count = asIntOrZero(child(object, "pokedex_count"));
    summary.badges = asIntOrZero(child(object, "badges"));
    summary.status = asStringOrEmpty(child(object, "status"));
    summary.error = asStringOrEmpty(child(object, "error"));
    summary.box_1_slots = parseBoxOneSlotsArrayField(child(object, "box_1_slots"));
    return summary;
}

bool hasUsableTransferSummary(const std::optional<TransferSaveSummary>& summary) {
    return summary &&
           !summary->game_id.empty() &&
           !summary->player_name.empty();
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
                  << " transfer_save_cache_not_used box_1_slots=" << summary->box_1_slots.size()
                  << '\n';
    }
    return summary;
}

} // namespace pr
