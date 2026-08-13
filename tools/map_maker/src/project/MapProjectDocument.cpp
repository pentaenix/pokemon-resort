#include "mapmaker/project/MapProjectDocument.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace pr::mapmaker {
namespace {

std::string stringOr(const JsonValue* value, std::string fallback = {}) {
    return value && value->isString() ? value->asString() : std::move(fallback);
}

int integerOr(const JsonValue* value, int fallback) {
    if (!value || !value->isNumber() || !std::isfinite(value->asNumber())) return fallback;
    return static_cast<int>(value->asNumber());
}

double numberOr(const JsonValue* value, double fallback) {
    return value && value->isNumber() && std::isfinite(value->asNumber())
        ? value->asNumber()
        : fallback;
}

bool boolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

std::string readFileExactly(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open map project: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (input.bad()) {
        throw std::runtime_error("Could not read map project: " + path.string());
    }
    return buffer.str();
}

} // namespace

MapProjectDocument MapProjectDocument::parse(
    std::string raw_json,
    std::filesystem::path source_path) {
    const JsonValue root = parseJsonText(raw_json);
    if (!root.isObject()) {
        throw std::runtime_error("Map project root must be a JSON object");
    }

    MapProjectDocument out;
    out.raw_json_ = std::move(raw_json);
    out.source_path_ = std::move(source_path);
    out.version_ = integerOr(root.get("version"), 1);
    out.id_ = stringOr(root.get("id"));
    out.name_ = stringOr(root.get("name"), out.id_);
    out.default_tile_package_id_ = stringOr(root.get("defaultTilePackageId"));

    if (const JsonValue* maps = root.get("maps"); maps && maps->isArray()) {
        out.maps_.reserve(maps->asArray().size());
        for (std::size_t index = 0; index < maps->asArray().size(); ++index) {
            const JsonValue& value = maps->asArray()[index];
            if (!value.isObject()) continue;
            MapProjectEntry entry;
            entry.id = stringOr(value.get("id"));
            entry.file = stringOr(value.get("file"));
            entry.name = stringOr(value.get("name"), entry.id.empty() ? entry.file : entry.id);
            entry.source_map_id = stringOr(value.get("sourceMapId"));
            entry.grid_x = integerOr(value.get("gridX"), 0);
            entry.grid_y = integerOr(value.get("gridY"), 0);
            entry.linked = boolOr(value.get("linked"), true);
            entry.source_index = index;
            out.maps_.push_back(std::move(entry));
        }
    }

    if (const JsonValue* packages = root.get("tilePackages"); packages && packages->isArray()) {
        out.tile_packages_.reserve(packages->asArray().size());
        for (const JsonValue& value : packages->asArray()) {
            if (!value.isObject()) continue;
            TilePackageEntry entry;
            entry.id = stringOr(value.get("id"));
            entry.file = stringOr(value.get("file"), stringOr(value.get("fileName")));
            entry.name = stringOr(value.get("name"), entry.id.empty() ? entry.file : entry.id);
            out.tile_packages_.push_back(std::move(entry));
        }
    }

    if (const JsonValue* editor = root.get("editor"); editor && editor->isObject()) {
        out.editor_.active_map_id = stringOr(editor->get("activeMapId"));
        out.editor_.view_mode = stringOr(editor->get("viewMode"), "2d");
        out.editor_.zoom = numberOr(editor->get("zoom"), 1.0);
    }
    return out;
}

MapProjectDocument MapProjectDocument::load(const std::filesystem::path& path) {
    return parse(readFileExactly(path), path);
}

const MapProjectEntry* MapProjectDocument::findMap(std::string_view map_id) const {
    const auto found = std::find_if(maps_.begin(), maps_.end(), [&](const MapProjectEntry& entry) {
        return entry.id == map_id;
    });
    return found == maps_.end() ? nullptr : &*found;
}

const TilePackageEntry* MapProjectDocument::findTilePackage(std::string_view package_id) const {
    const auto found = std::find_if(
        tile_packages_.begin(), tile_packages_.end(), [&](const TilePackageEntry& entry) {
            return entry.id == package_id;
        });
    return found == tile_packages_.end() ? nullptr : &*found;
}

std::string MapProjectDocument::sourceKeyFor(std::string_view map_id) const {
    const MapProjectEntry* entry = findMap(map_id);
    return entry ? normalizedMapSourceKey(entry->file) : std::string{};
}

std::vector<const MapProjectEntry*> MapProjectDocument::entriesSharingSource(
    std::string_view map_id) const {
    const std::string key = sourceKeyFor(map_id);
    std::vector<const MapProjectEntry*> entries;
    if (key.empty()) return entries;
    for (const MapProjectEntry& entry : maps_) {
        if (normalizedMapSourceKey(entry.file) == key) entries.push_back(&entry);
    }
    return entries;
}

std::vector<MapSourceGroup> MapProjectDocument::sourceGroups() const {
    std::vector<MapSourceGroup> groups;
    std::unordered_map<std::string, std::size_t> indices;
    for (const MapProjectEntry& entry : maps_) {
        const std::string key = normalizedMapSourceKey(entry.file);
        if (key.empty()) continue;
        const auto [iterator, inserted] = indices.emplace(key, groups.size());
        if (inserted) groups.push_back(MapSourceGroup{key, entry.file, {}});
        groups[iterator->second].entry_ids.push_back(entry.id);
    }
    return groups;
}

bool MapProjectDocument::isReusedMap(std::string_view map_id) const {
    const MapProjectEntry* entry = findMap(map_id);
    return entry && (!entry->source_map_id.empty() || entriesSharingSource(map_id).size() > 1U);
}

std::string normalizedMapSourceKey(std::string_view file) {
    std::string portable(file);
    std::replace(portable.begin(), portable.end(), '\\', '/');
    if (portable.empty()) return {};
    std::string key = std::filesystem::path(portable).lexically_normal().generic_string();
    if (key == ".") key.clear();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
#endif
    return key;
}

} // namespace pr::mapmaker
