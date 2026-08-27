#pragma once

#include "core/config/Json.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pr::mapmaker {

struct MapProjectEntry {
    std::string id;
    std::string name;
    std::string file;
    std::string source_map_id;
    int grid_x = 0;
    int grid_y = 0;
    bool linked = true;
    std::size_t source_index = 0;
};

struct TilePackageEntry {
    std::string id;
    std::string name;
    std::string file;
};

struct ProjectEditorProjection {
    std::string active_map_id;
    std::string view_mode = "2d";
    double zoom = 1.0;
};

struct MapSourceGroup {
    std::string key;
    std::string file;
    std::vector<std::string> entry_ids;
};

// A lossless project-file envelope plus the small projection needed by the
// editor shell. Unknown JSON remains byte-for-byte intact in rawJson().
class MapProjectDocument {
public:
    static MapProjectDocument parse(
        std::string raw_json,
        std::filesystem::path source_path = {});
    static MapProjectDocument load(const std::filesystem::path& path);

    int version() const { return version_; }
    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& defaultTilePackageId() const { return default_tile_package_id_; }
    const std::vector<MapProjectEntry>& maps() const { return maps_; }
    const std::vector<TilePackageEntry>& tilePackages() const { return tile_packages_; }
    const ProjectEditorProjection& editor() const { return editor_; }
    const std::string& rawJson() const { return raw_json_; }
    const std::filesystem::path& sourcePath() const { return source_path_; }

    const MapProjectEntry* findMap(std::string_view map_id) const;
    const TilePackageEntry* findTilePackage(std::string_view package_id) const;
    std::string sourceKeyFor(std::string_view map_id) const;
    std::vector<const MapProjectEntry*> entriesSharingSource(std::string_view map_id) const;
    std::vector<MapSourceGroup> sourceGroups() const;
    bool isReusedMap(std::string_view map_id) const;

    // World-workspace mutations preserve unknown project fields in the parsed
    // JSON envelope. They update both the authored JSON and its projections.
    bool moveMap(std::string_view map_id, int grid_x, int grid_y);
    bool addMap(MapProjectEntry entry);
    std::string serialize() const;
    void saveAtomic(const std::filesystem::path& path = {});

private:
    void rebuildProjection();

    JsonValue root_;
    std::string raw_json_;
    std::filesystem::path source_path_;
    int version_ = 1;
    std::string id_;
    std::string name_;
    std::string default_tile_package_id_;
    std::vector<MapProjectEntry> maps_;
    std::vector<TilePackageEntry> tile_packages_;
    ProjectEditorProjection editor_;
};

// Normalizes separators and dot segments without consulting the filesystem.
// This makes repeated project entries reliably share one loaded map document.
std::string normalizedMapSourceKey(std::string_view file);

} // namespace pr::mapmaker
