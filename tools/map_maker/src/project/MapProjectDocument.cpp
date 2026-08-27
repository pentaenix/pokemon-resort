#include "mapmaker/project/MapProjectDocument.hpp"

#include "core/config/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
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
    JsonValue root = parseJsonText(raw_json);
    if (!root.isObject()) {
        throw std::runtime_error("Map project root must be a JSON object");
    }

    MapProjectDocument out;
    out.raw_json_ = std::move(raw_json);
    out.source_path_ = std::move(source_path);
    out.root_ = std::move(root);
    out.rebuildProjection();
    return out;
}

void MapProjectDocument::rebuildProjection() {
    version_ = integerOr(root_.get("version"), 1);
    id_ = stringOr(root_.get("id"));
    name_ = stringOr(root_.get("name"), id_);
    default_tile_package_id_ = stringOr(root_.get("defaultTilePackageId"));
    maps_.clear();
    tile_packages_.clear();
    editor_ = {};

    if (const JsonValue* maps = root_.get("maps"); maps && maps->isArray()) {
        maps_.reserve(maps->asArray().size());
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
            maps_.push_back(std::move(entry));
        }
    }

    if (const JsonValue* packages = root_.get("tilePackages"); packages && packages->isArray()) {
        tile_packages_.reserve(packages->asArray().size());
        for (const JsonValue& value : packages->asArray()) {
            if (!value.isObject()) continue;
            TilePackageEntry entry;
            entry.id = stringOr(value.get("id"));
            entry.file = stringOr(value.get("file"), stringOr(value.get("fileName")));
            entry.name = stringOr(value.get("name"), entry.id.empty() ? entry.file : entry.id);
            tile_packages_.push_back(std::move(entry));
        }
    }
    if (const JsonValue* editor = root_.get("editor"); editor && editor->isObject()) {
        editor_.active_map_id = stringOr(editor->get("activeMapId"));
        editor_.view_mode = stringOr(editor->get("viewMode"), "2d");
        editor_.zoom = numberOr(editor->get("zoom"), 1.0);
    }
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

bool MapProjectDocument::moveMap(std::string_view map_id, int grid_x, int grid_y) {
    const MapProjectEntry* projected = findMap(map_id);
    JsonValue* maps = root_.get("maps");
    if (!projected || !maps || !maps->isArray() ||
        projected->source_index >= maps->asArray().size()) return false;
    JsonValue& value = maps->asArray()[projected->source_index];
    if (!value.isObject()) return false;
    if (projected->grid_x == grid_x && projected->grid_y == grid_y) return false;
    value["gridX"] = JsonValue(static_cast<double>(grid_x));
    value["gridY"] = JsonValue(static_cast<double>(grid_y));
    rebuildProjection();
    raw_json_ = serialize();
    return true;
}

bool MapProjectDocument::addMap(MapProjectEntry entry) {
    if (entry.id.empty() || entry.file.empty() || findMap(entry.id)) return false;
    JsonValue* maps = root_.get("maps");
    if (!maps) {
        root_["maps"] = JsonValue(JsonValue::Array{});
        maps = root_.get("maps");
    }
    if (!maps || !maps->isArray()) return false;
    JsonValue::Object value;
    value.emplace("id", JsonValue(entry.id));
    value.emplace("name", JsonValue(entry.name.empty() ? entry.id : entry.name));
    value.emplace("file", JsonValue(entry.file));
    value.emplace("gridX", JsonValue(static_cast<double>(entry.grid_x)));
    value.emplace("gridY", JsonValue(static_cast<double>(entry.grid_y)));
    value.emplace("linked", JsonValue(entry.linked));
    if (!entry.source_map_id.empty()) {
        value.emplace("sourceMapId", JsonValue(entry.source_map_id));
    }
    maps->asArray().emplace_back(std::move(value));
    rebuildProjection();
    raw_json_ = serialize();
    return true;
}

std::string MapProjectDocument::serialize() const {
    return serializeJsonValue(root_, JsonStyle::Pretty, 2) + "\n";
}

void MapProjectDocument::saveAtomic(const std::filesystem::path& requested_path) {
    const std::filesystem::path path = requested_path.empty() ? source_path_ : requested_path;
    if (path.empty()) throw std::runtime_error("Map project has no save path");
    const std::filesystem::path temporary(path.string() + ".tmp");
    const std::filesystem::path backup(path.string() + ".bak");
    const std::string encoded = serialize();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("Could not create project temporary file");
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.flush();
        if (!output) throw std::runtime_error("Could not write complete project temporary file");
    }
    (void)MapProjectDocument::load(temporary);
    std::error_code error;
    if (std::filesystem::exists(path)) {
        std::filesystem::copy_file(
            path, backup, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            std::filesystem::remove(temporary);
            throw std::runtime_error("Could not back up map project: " + error.message());
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("Could not replace map project: " + error.message());
    }
    raw_json_ = encoded;
    source_path_ = path;
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
