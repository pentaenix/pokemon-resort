#include "mapmaker/validation/ProjectValidator.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace pr::mapmaker {
namespace {

using Diagnostics = std::vector<ValidationDiagnostic>;

void add(
    Diagnostics& out,
    DiagnosticSeverity severity,
    std::string code,
    std::string message,
    std::string map_id = {},
    std::string object_id = {}) {
    out.push_back({severity, std::move(code), std::move(message),
        std::move(map_id), std::move(object_id)});
}

bool isCardinal(std::string_view direction) {
    return direction == "north" || direction == "south" ||
        direction == "east" || direction == "west";
}

bool isInsideOrCardinalHalo(int width, int height, int x, int y) {
    if (x >= 0 && x < width && y >= 0 && y < height) return true;
    const bool vertical_halo = (y == -1 || y == height) && x >= 0 && x < width;
    const bool horizontal_halo = (x == -1 || x == width) && y >= 0 && y < height;
    return vertical_halo || horizontal_halo;
}

bool escapesProjectDirectory(const std::string& file) {
    std::string portable = file;
    std::replace(portable.begin(), portable.end(), '\\', '/');
    const std::filesystem::path path(portable);
    if (path.is_absolute()) return true;
    const std::string normalized = path.lexically_normal().generic_string();
    return normalized == ".." || normalized.rfind("../", 0) == 0;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

} // namespace

std::vector<ValidationDiagnostic> validateMapProject(
    const MapProjectDocument& project,
    const std::vector<MapValidationProjection>& loaded_maps) {
    Diagnostics diagnostics;
    std::unordered_map<std::string, const MapValidationProjection*> maps_by_source;
    std::unordered_map<std::string, const MapValidationProjection*> maps_by_scene;

    for (const MapValidationProjection& map : loaded_maps) {
        const std::string key = normalizedMapSourceKey(map.source_file);
        if (!key.empty() && !maps_by_source.emplace(key, &map).second) {
            add(diagnostics, DiagnosticSeverity::Warning, "map.source_loaded_twice",
                "More than one loaded map projection represents " + map.source_file + ".",
                map.scene_id);
        }
        if (!map.scene_id.empty() && !maps_by_scene.emplace(map.scene_id, &map).second) {
            add(diagnostics, DiagnosticSeverity::Error, "map.scene_id_duplicate",
                "Loaded maps contain duplicate scene id '" + map.scene_id + "'.", map.scene_id);
        }
    }

    if (project.id().empty()) {
        add(diagnostics, DiagnosticSeverity::Error, "project.id_missing", "Project id is required.");
    }
    if (project.version() != 1) {
        add(diagnostics, DiagnosticSeverity::Error, "project.version_unsupported",
            "Project version " + std::to_string(project.version()) + " is not supported.");
    }
    if (project.maps().empty()) {
        add(diagnostics, DiagnosticSeverity::Error, "project.maps_empty",
            "Project must contain at least one map.");
    }

    std::unordered_set<std::string> map_ids;
    std::set<std::pair<int, int>> linked_positions;
    for (const MapProjectEntry& entry : project.maps()) {
        if (entry.id.empty()) {
            add(diagnostics, DiagnosticSeverity::Error, "map.id_missing",
                "Every project map requires an id.");
        } else if (!map_ids.insert(entry.id).second) {
            add(diagnostics, DiagnosticSeverity::Error, "map.id_duplicate",
                "Map id '" + entry.id + "' is used more than once.", entry.id);
        }
        if (entry.file.empty()) {
            add(diagnostics, DiagnosticSeverity::Error, "map.file_missing",
                "Map entry has no OWMAP file.", entry.id);
        } else {
            if (lowercase(std::filesystem::path(entry.file).extension().string()) != ".owmap") {
                add(diagnostics, DiagnosticSeverity::Warning, "map.file_extension",
                    "Map file should use the .owmap extension.", entry.id);
            }
            if (escapesProjectDirectory(entry.file)) {
                add(diagnostics, DiagnosticSeverity::Error, "map.file_escapes_root",
                    "Map file must remain relative to the project map directory.", entry.id);
            }
        }
        if (entry.linked && !linked_positions.emplace(entry.grid_x, entry.grid_y).second) {
            add(diagnostics, DiagnosticSeverity::Error, "map.grid_overlap",
                "Two linked map entries occupy grid " + std::to_string(entry.grid_x) + "," +
                    std::to_string(entry.grid_y) + ".", entry.id);
        }
    }

    for (const MapProjectEntry& entry : project.maps()) {
        if (entry.source_map_id.empty()) continue;
        const MapProjectEntry* source = project.findMap(entry.source_map_id);
        if (!source) {
            add(diagnostics, DiagnosticSeverity::Error, "map.source_missing",
                "Reused map source '" + entry.source_map_id + "' does not exist.", entry.id);
        } else if (source->id == entry.id) {
            add(diagnostics, DiagnosticSeverity::Error, "map.source_self",
                "A reused map entry cannot name itself as its source.", entry.id);
        } else if (normalizedMapSourceKey(source->file) != normalizedMapSourceKey(entry.file)) {
            add(diagnostics, DiagnosticSeverity::Error, "map.source_file_mismatch",
                "Reused map entry and source must reference the same OWMAP file.", entry.id);
        }
    }

    std::unordered_set<std::string> package_ids;
    for (const TilePackageEntry& package : project.tilePackages()) {
        if (package.id.empty()) {
            add(diagnostics, DiagnosticSeverity::Error, "tile_package.id_missing",
                "Every tile package requires an id.");
        } else if (!package_ids.insert(package.id).second) {
            add(diagnostics, DiagnosticSeverity::Error, "tile_package.id_duplicate",
                "Tile package id '" + package.id + "' is used more than once.");
        }
        if (package.file.empty()) {
            add(diagnostics, DiagnosticSeverity::Error, "tile_package.file_missing",
                "Tile package '" + package.id + "' has no file.");
        }
    }
    if (!project.defaultTilePackageId().empty() &&
        !project.findTilePackage(project.defaultTilePackageId())) {
        add(diagnostics, DiagnosticSeverity::Error, "tile_package.default_missing",
            "Default tile package '" + project.defaultTilePackageId() + "' does not exist.");
    }

    for (const MapSourceGroup& source : project.sourceGroups()) {
        if (!maps_by_source.contains(source.key)) {
            add(diagnostics, DiagnosticSeverity::Warning, "map.source_not_loaded",
                "Map source '" + source.file + "' is not loaded, so its links cannot be verified.",
                source.entry_ids.empty() ? std::string{} : source.entry_ids.front());
        }
    }

    const auto resolveMap = [&](std::string_view id) -> const MapValidationProjection* {
        if (const MapProjectEntry* entry = project.findMap(id)) {
            const auto found = maps_by_source.find(normalizedMapSourceKey(entry->file));
            return found == maps_by_source.end() ? nullptr : found->second;
        }
        const auto found = maps_by_scene.find(std::string(id));
        return found == maps_by_scene.end() ? nullptr : found->second;
    };

    std::unordered_map<const MapValidationProjection*, std::unordered_set<std::string>> anchor_ids;
    for (const MapValidationProjection& map : loaded_maps) {
        const std::string context = map.scene_id.empty() ? map.source_file : map.scene_id;
        if (map.width <= 0 || map.height <= 0) {
            add(diagnostics, DiagnosticSeverity::Error, "map.dimensions_invalid",
                "Map dimensions must be positive.", context);
        }
        auto& ids = anchor_ids[&map];
        for (const AnchorProjection& anchor : map.anchors) {
            if (anchor.id.empty()) {
                add(diagnostics, DiagnosticSeverity::Error, "anchor.id_missing",
                    "Every anchor requires an id.", context);
            } else if (!ids.insert(anchor.id).second) {
                add(diagnostics, DiagnosticSeverity::Error, "anchor.id_duplicate",
                    "Anchor id '" + anchor.id + "' is used more than once.", context, anchor.id);
            }
            if (!isInsideOrCardinalHalo(map.width, map.height, anchor.tile_x, anchor.tile_y)) {
                add(diagnostics, DiagnosticSeverity::Error, "anchor.tile_out_of_bounds",
                    "Anchor must be inside the map or its one-tile cardinal halo.", context, anchor.id);
            }
            if (!isCardinal(anchor.facing)) {
                add(diagnostics, DiagnosticSeverity::Error, "anchor.facing_invalid",
                    "Anchor facing must be north, south, east, or west.", context, anchor.id);
            }
        }
    }

    for (const MapValidationProjection& map : loaded_maps) {
        const std::string context = map.scene_id.empty() ? map.source_file : map.scene_id;
        std::unordered_set<std::string> link_ids;
        for (const LinkProjection& link : map.links) {
            if (link.id.empty()) {
                add(diagnostics, DiagnosticSeverity::Error, "link.id_missing",
                    "Every link requires an id.", context);
            } else if (!link_ids.insert(link.id).second) {
                add(diagnostics, DiagnosticSeverity::Error, "link.id_duplicate",
                    "Link id '" + link.id + "' is used more than once.", context, link.id);
            }
            if (link.destination_map_id.empty()) {
                add(diagnostics, DiagnosticSeverity::Error, "link.destination_map_missing",
                    "Link must name a destination map.", context, link.id);
                continue;
            }
            const MapValidationProjection* destination = resolveMap(link.destination_map_id);
            if (!destination) {
                const bool known_but_unloaded = project.findMap(link.destination_map_id) != nullptr;
                add(diagnostics,
                    known_but_unloaded ? DiagnosticSeverity::Warning : DiagnosticSeverity::Error,
                    known_but_unloaded ? "link.destination_not_loaded" : "link.destination_map_unknown",
                    known_but_unloaded
                        ? "Destination map is not loaded; its anchor could not be verified."
                        : "Destination map '" + link.destination_map_id + "' does not exist.",
                    context, link.id);
            } else if (link.destination_anchor_id.empty() ||
                !anchor_ids[destination].contains(link.destination_anchor_id)) {
                add(diagnostics, DiagnosticSeverity::Error, "link.destination_anchor_unknown",
                    "Destination anchor '" + link.destination_anchor_id + "' does not exist.",
                    context, link.id);
            }
        }

        std::unordered_set<std::string> door_ids;
        std::unordered_set<std::string> used_links;
        std::set<std::string> occupied_approaches;
        for (const DoorProjection& door : map.doors) {
            if (door.id.empty()) {
                add(diagnostics, DiagnosticSeverity::Error, "door.id_missing",
                    "Every door trigger requires an id.", context);
            } else if (!door_ids.insert(door.id).second) {
                add(diagnostics, DiagnosticSeverity::Error, "door.id_duplicate",
                    "Door id '" + door.id + "' is used more than once.", context, door.id);
            }
            if (!isInsideOrCardinalHalo(map.width, map.height, door.tile_x, door.tile_y)) {
                add(diagnostics, DiagnosticSeverity::Error, "door.tile_out_of_bounds",
                    "Door trigger must be inside the map or its one-tile cardinal halo.",
                    context, door.id);
            }
            if (door.link_id.empty() || !link_ids.contains(door.link_id)) {
                add(diagnostics, DiagnosticSeverity::Error, "door.link_unknown",
                    "Door references an unknown local link '" + door.link_id + "'.", context, door.id);
            } else {
                used_links.insert(door.link_id);
            }
            if (door.script_id.empty()) {
                add(diagnostics, DiagnosticSeverity::Warning, "door.script_missing",
                    "Door has no explicit script and will rely on the runtime default.", context, door.id);
            }
            if (door.allowed_directions.empty()) {
                add(diagnostics, DiagnosticSeverity::Warning, "door.directions_missing",
                    "Door has no allowed direction and will default to north.", context, door.id);
            }
            for (const std::string& direction : door.allowed_directions) {
                if (!isCardinal(direction)) {
                    add(diagnostics, DiagnosticSeverity::Error, "door.direction_invalid",
                        "Door direction '" + direction + "' is not cardinal.", context, door.id);
                    continue;
                }
                const std::string occupancy = std::to_string(door.tile_x) + ":" +
                    std::to_string(door.tile_y) + ":" + direction;
                if (!occupied_approaches.insert(occupancy).second) {
                    add(diagnostics, DiagnosticSeverity::Warning, "door.approach_overlap",
                        "More than one door handles the same tile and approach direction.",
                        context, door.id);
                }
            }
            if (door.visual) {
                const std::string visual_map_id = door.visual->map_id.empty()
                    ? context
                    : door.visual->map_id;
                const MapValidationProjection* visual_map = door.visual->map_id.empty()
                    ? &map
                    : resolveMap(visual_map_id);
                if (!visual_map) {
                    add(diagnostics, DiagnosticSeverity::Error, "door.visual_map_unknown",
                        "Door visual map '" + visual_map_id + "' does not exist.", context, door.id);
                } else if (!isInsideOrCardinalHalo(
                    visual_map->width, visual_map->height,
                    door.visual->tile_x, door.visual->tile_y)) {
                    add(diagnostics, DiagnosticSeverity::Error, "door.visual_tile_out_of_bounds",
                        "Door visual tile is outside its map and cardinal halo.", context, door.id);
                }
                if (door.visual->layer_id.empty()) {
                    add(diagnostics, DiagnosticSeverity::Warning, "door.visual_layer_missing",
                        "Door visual does not name a tile layer.", context, door.id);
                }
            }
        }

        for (const std::string& link_id : link_ids) {
            if (!used_links.contains(link_id)) {
                add(diagnostics, DiagnosticSeverity::Info, "link.unused",
                    "Link '" + link_id + "' is not referenced by a door trigger.", context, link_id);
            }
        }
    }

    return diagnostics;
}

bool hasValidationErrors(const std::vector<ValidationDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const ValidationDiagnostic& item) {
        return item.severity == DiagnosticSeverity::Error;
    });
}

} // namespace pr::mapmaker
