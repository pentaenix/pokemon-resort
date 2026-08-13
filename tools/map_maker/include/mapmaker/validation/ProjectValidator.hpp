#pragma once

#include "mapmaker/project/MapProjectDocument.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::mapmaker {

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error,
};

struct ValidationDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    std::string map_id;
    std::string object_id;
};

struct AnchorProjection {
    std::string id;
    int tile_x = 0;
    int tile_y = 0;
    std::string facing = "south";
};

struct LinkProjection {
    std::string id;
    std::string destination_map_id;
    std::string destination_anchor_id;
};

struct DoorVisualProjection {
    std::string map_id;
    std::string layer_id;
    int tile_x = 0;
    int tile_y = 0;
};

struct DoorProjection {
    std::string id;
    int tile_x = 0;
    int tile_y = 0;
    std::vector<std::string> allowed_directions;
    std::string link_id;
    std::string script_id;
    std::optional<DoorVisualProjection> visual;
};

// A dependency-free projection from an OWMAP document. source_file identifies
// the shared file, so multiple project instances validate against one payload.
struct MapValidationProjection {
    std::string source_file;
    std::string scene_id;
    int width = 0;
    int height = 0;
    std::vector<AnchorProjection> anchors;
    std::vector<LinkProjection> links;
    std::vector<DoorProjection> doors;
};

std::vector<ValidationDiagnostic> validateMapProject(
    const MapProjectDocument& project,
    const std::vector<MapValidationProjection>& loaded_maps);
bool hasValidationErrors(const std::vector<ValidationDiagnostic>& diagnostics);

} // namespace pr::mapmaker
