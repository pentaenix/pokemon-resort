#include "aquarium_geometry/Kernel.hpp"

#include "GeometryBuilder.hpp"
#include "Tunnel.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <string_view>

namespace pr::aquarium::geometry {

namespace {

void addError(ValidationReport& report, std::string code, std::string path, std::string message) {
    report.diagnostics.push_back({
        DiagnosticSeverity::Error,
        std::move(code),
        std::move(path),
        std::move(message),
    });
}

void addWarning(ValidationReport& report, std::string code, std::string path, std::string message) {
    report.diagnostics.push_back({
        DiagnosticSeverity::Warning,
        std::move(code),
        std::move(path),
        std::move(message),
    });
}

using CellKey = std::pair<std::int32_t, std::int32_t>;

bool connectedCells(const std::set<CellKey>& cells) {
    if (cells.empty()) return false;
    std::set<CellKey> visited;
    std::queue<CellKey> pending;
    pending.push(*cells.begin());
    visited.insert(*cells.begin());
    constexpr CellKey neighbours[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    while (!pending.empty()) {
        const CellKey current = pending.front();
        pending.pop();
        for (const CellKey delta : neighbours) {
            const CellKey next{current.first + delta.first, current.second + delta.second};
            if (cells.count(next) && visited.insert(next).second) pending.push(next);
        }
    }
    return visited.size() == cells.size();
}

bool allCutComponentsReachExterior(const FootprintDesign& footprint) {
    std::set<CellKey> remaining;
    for (const GridCell cell : footprint.subtracted_cells) {
        remaining.emplace(cell.column, cell.row);
    }
    constexpr CellKey neighbours[]{{0, -1}, {1, 0}, {0, 1}, {-1, 0}};
    while (!remaining.empty()) {
        std::queue<CellKey> pending;
        pending.push(*remaining.begin());
        remaining.erase(remaining.begin());
        bool reaches_exterior = false;
        while (!pending.empty()) {
            const CellKey current = pending.front();
            pending.pop();
            reaches_exterior = reaches_exterior || current.first == 0 || current.second == 0 ||
                current.first == footprint.width_cells - 1 ||
                current.second == footprint.depth_cells - 1;
            for (const CellKey delta : neighbours) {
                const CellKey next{current.first + delta.first, current.second + delta.second};
                const auto found = remaining.find(next);
                if (found != remaining.end()) {
                    pending.push(next);
                    remaining.erase(found);
                }
            }
        }
        if (!reaches_exterior) return false;
    }
    return true;
}

class StableHasher {
public:
    void addBytes(const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            value_ ^= static_cast<std::uint64_t>(bytes[i]);
            value_ *= 1099511628211ULL;
        }
    }

    void addString(std::string_view value) {
        addUnsigned(static_cast<std::uint64_t>(value.size()));
        addBytes(value.data(), value.size());
    }

    void addUnsigned(std::uint64_t value) {
        for (int shift = 0; shift < 64; shift += 8) {
            const unsigned char byte = static_cast<unsigned char>((value >> shift) & 0xFFU);
            addBytes(&byte, 1);
        }
    }

    void addSigned(std::int64_t value) {
        addUnsigned(static_cast<std::uint64_t>(value));
    }

    void addFloat(float value) {
        const auto quantized = static_cast<std::int64_t>(std::llround(static_cast<double>(value) * 1000000.0));
        addSigned(quantized);
    }

    std::string finish() const {
        std::ostringstream output;
        output << "fnv1a64:" << std::hex << std::setfill('0') << std::setw(16) << value_;
        return output.str();
    }

private:
    std::uint64_t value_ = 14695981039346656037ULL;
};

void hashResult(StableHasher& hasher, const AquariumBuildResult& result) {
    hasher.addUnsigned(kKernelAbiVersion);
    hasher.addUnsigned(kDesignSchemaVersion);
    hasher.addUnsigned(result.meshes.meshes.size());
    for (const SemanticMesh& mesh : result.meshes.meshes) {
        hasher.addUnsigned(static_cast<std::uint8_t>(mesh.material));
        hasher.addUnsigned(mesh.vertices.size());
        for (const Vertex& vertex : mesh.vertices) {
            hasher.addFloat(vertex.position.x);
            hasher.addFloat(vertex.position.y);
            hasher.addFloat(vertex.position.z);
            hasher.addFloat(vertex.normal.x);
            hasher.addFloat(vertex.normal.y);
            hasher.addFloat(vertex.normal.z);
            hasher.addFloat(vertex.uv.x);
            hasher.addFloat(vertex.uv.y);
        }
        hasher.addUnsigned(mesh.indices.size());
        for (const std::uint32_t index : mesh.indices) {
            hasher.addUnsigned(index);
        }
    }
    hasher.addUnsigned(result.collision.blocked_cells.size());
    for (const GridCell& cell : result.collision.blocked_cells) {
        hasher.addSigned(cell.column);
        hasher.addSigned(cell.row);
    }
    hasher.addUnsigned(result.collision.dry_corridor_cells.size());
    for (const GridCell& cell : result.collision.dry_corridor_cells) {
        hasher.addSigned(cell.column);
        hasher.addSigned(cell.row);
    }
    hasher.addUnsigned(result.navigation.layers.size());
    for (const NavigationLayer& layer : result.navigation.layers) {
        hasher.addFloat(layer.floor_y);
        hasher.addFloat(layer.ceiling_y);
        hasher.addUnsigned(layer.area.outer.size());
        for (const Vec2& point : layer.area.outer) {
            hasher.addFloat(point.x);
            hasher.addFloat(point.y);
        }
        hasher.addUnsigned(layer.area.holes.size());
        for (const std::vector<Vec2>& hole : layer.area.holes) {
            hasher.addUnsigned(hole.size());
            for (const Vec2& point : hole) {
                hasher.addFloat(point.x);
                hasher.addFloat(point.y);
            }
        }
    }
    hasher.addUnsigned(result.navigation.dry_volumes.size());
    for (const NavigationDryVolume& volume : result.navigation.dry_volumes) {
        hasher.addString(volume.tunnel_id);
        hasher.addFloat(volume.floor_y);
        hasher.addFloat(volume.ceiling_y);
        hasher.addUnsigned(volume.area.outer.size());
        for (const Vec2& point : volume.area.outer) {
            hasher.addFloat(point.x);
            hasher.addFloat(point.y);
        }
    }
    hasher.addUnsigned(result.navigation.suggested_spawns.size());
    for (const Vec3& spawn : result.navigation.suggested_spawns) {
        hasher.addFloat(spawn.x);
        hasher.addFloat(spawn.y);
        hasher.addFloat(spawn.z);
    }
    hasher.addUnsigned(result.statistics.water_volume_litres);
}

void populateStatistics(AquariumBuildResult& result) {
    result.statistics.mesh_count = static_cast<std::uint32_t>(result.meshes.meshes.size());
    for (const SemanticMesh& mesh : result.meshes.meshes) {
        result.statistics.vertex_count += static_cast<std::uint32_t>(mesh.vertices.size());
        result.statistics.index_count += static_cast<std::uint32_t>(mesh.indices.size());
    }
    result.statistics.triangle_count = result.statistics.index_count / 3U;
    result.statistics.collision_cell_count =
        static_cast<std::uint32_t>(result.collision.blocked_cells.size());
    result.statistics.navigation_layer_count =
        static_cast<std::uint32_t>(result.navigation.layers.size());
}

} // namespace

bool ValidationReport::valid() const {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const ValidationDiagnostic& diagnostic) {
        return diagnostic.severity == DiagnosticSeverity::Error;
    });
}

float cellCentreWorld(std::int32_t cell_index) {
    return (static_cast<float>(cell_index) + 0.5F) * static_cast<float>(kWorldUnitsPerCell);
}

float footprintCentreWorld(std::int32_t origin_cell, std::int32_t size_cells) {
    return (static_cast<float>(origin_cell) + static_cast<float>(size_cells) * 0.5F) *
           static_cast<float>(kWorldUnitsPerCell);
}

ValidationReport validateAquarium(const AquariumBuildRequest& request) {
    ValidationReport report;
    if (request.kernel_abi_version != kKernelAbiVersion) {
        addError(report, "unsupported_kernel_abi", "/kernelAbiVersion", "Unsupported kernel ABI version");
    }
    if (request.design_schema_version != kDesignSchemaVersion) {
        addError(report, "unsupported_design_schema", "/designSchemaVersion", "Unsupported design schema version");
    }
    if (request.tank.id.empty()) {
        addError(report, "missing_tank_id", "/tank/id", "Tank ID must be stable and non-empty");
    }
    if (request.tank.footprint.width_cells < 3) {
        addError(report, "footprint_too_narrow", "/tank/footprint/widthCells", "Rectangle width must be at least three cells");
    }
    if (request.tank.footprint.depth_cells < 3) {
        addError(report, "footprint_too_shallow", "/tank/footprint/depthCells", "Rectangle depth must be at least three cells");
    }
    const std::int64_t footprint_area =
        static_cast<std::int64_t>(request.tank.footprint.width_cells) *
        static_cast<std::int64_t>(request.tank.footprint.depth_cells);
    const bool dimensions_safe = request.tank.footprint.width_cells > 0 &&
        request.tank.footprint.depth_cells > 0 &&
        request.tank.footprint.width_cells <= kMaxFootprintCells &&
        request.tank.footprint.depth_cells <= kMaxFootprintCells &&
        footprint_area <= kMaxFootprintCells;
    if (!dimensions_safe) {
        addError(report, "footprint_too_large", "/tank/footprint", "Footprint dimensions exceed the kernel safety limit");
    }
    const FootprintDesign& footprint = request.tank.footprint;
    if (!footprint.subtracted_cells.empty()) {
        if (footprint.shape != FootprintShape::Rectangle) {
            addError(report, "subtraction_requires_rectangle", "/tank/footprint/subtractedCells",
                "Subtracted cells require a rectangular base footprint");
        }
        if (footprint.rotation_quarter_turns != 0) {
            addError(report, "subtraction_requires_unrotated_footprint",
                "/tank/footprint/rotationQuarterTurns",
                "Subtracted footprints use their authored north-facing cell layout");
        }
        std::set<CellKey> cuts;
        bool cut_out_of_bounds = false;
        bool duplicate_cut = false;
        for (const GridCell cell : footprint.subtracted_cells) {
            cut_out_of_bounds = cut_out_of_bounds || cell.column < 0 || cell.row < 0 ||
                cell.column >= footprint.width_cells || cell.row >= footprint.depth_cells;
            duplicate_cut = duplicate_cut || !cuts.emplace(cell.column, cell.row).second;
        }
        if (cut_out_of_bounds) {
            addError(report, "subtracted_cell_out_of_bounds", "/tank/footprint/subtractedCells",
                "Subtracted cells must remain inside the tank's outer bounds");
        }
        if (duplicate_cut) {
            addError(report, "duplicate_subtracted_cell", "/tank/footprint/subtractedCells",
                "Each subtracted cell must appear exactly once");
        }
        if (!cut_out_of_bounds && !duplicate_cut) {
            std::set<CellKey> occupied;
            for (const GridCell cell : footprintCells(footprint)) {
                occupied.emplace(cell.column, cell.row);
            }
            if (occupied.size() < 9U || !connectedCells(occupied)) {
                addError(report, "disconnected_subtracted_footprint", "/tank/footprint/subtractedCells",
                    "Subtracting those cells would disconnect or collapse the tank");
            }
            if (!allCutComponentsReachExterior(footprint)) {
                addError(report, "enclosed_subtracted_footprint", "/tank/footprint/subtractedCells",
                    "Subtracted cells must form an opening connected to the tank exterior");
            }
        }
    }
    const std::int32_t occupied_width = occupiedWidthCells(request.tank.footprint);
    const std::int32_t occupied_depth = occupiedDepthCells(request.tank.footprint);
    const std::int64_t max_column = static_cast<std::int64_t>(request.tank.footprint.origin_cell.column) +
                                    static_cast<std::int64_t>(occupied_width);
    const std::int64_t max_row = static_cast<std::int64_t>(request.tank.footprint.origin_cell.row) +
                                 static_cast<std::int64_t>(occupied_depth);
    if (max_column > std::numeric_limits<std::int32_t>::max() ||
        max_row > std::numeric_limits<std::int32_t>::max()) {
        addError(report, "footprint_coordinate_overflow", "/tank/footprint/originCell", "Footprint exceeds the grid coordinate range");
    }
    if (request.tank.height_steps < 4 || request.tank.height_steps > 12) {
        addError(report, "height_out_of_range", "/tank/heightSteps", "Height must be between four and twelve steps");
    }
    if (request.tank.depth_steps < 0 || request.tank.depth_steps > 12) {
        addError(report, "depth_out_of_range", "/tank/depthSteps",
            "Below-floor depth must be between zero and twelve steps");
    }
    if (request.tank.footprint.rotation_quarter_turns < 0 ||
        request.tank.footprint.rotation_quarter_turns > 3) {
        addError(report, "rotation_out_of_range", "/tank/footprint/rotationQuarterTurns", "Rotation must be a quarter turn from zero through three");
    }
    if (footprint.shape != FootprintShape::Rectangle &&
        (footprint.width_cells < 5 || footprint.depth_cells < 5)) {
        addError(report, "shaped_footprint_too_small", "/tank/footprint",
            "L and U footprints require outer bounds of at least five cells");
    }
    if (footprint.shape == FootprintShape::L) {
        if (footprint.notch_width_cells < 1 || footprint.notch_depth_cells < 1 ||
            footprint.width_cells - footprint.notch_width_cells < 2 ||
            footprint.depth_cells - footprint.notch_depth_cells < 2) {
            addError(report, "invalid_l_arms", "/tank/footprint/notch",
                "L footprint arms must be at least two cells thick");
        }
    } else if (footprint.shape == FootprintShape::U) {
        const std::int32_t remaining_width =
            footprint.width_cells - footprint.notch_width_cells;
        if (footprint.notch_width_cells < 1 || footprint.notch_depth_cells < 1 ||
            footprint.depth_cells - footprint.notch_depth_cells < 2 ||
            remaining_width / 2 < 2 || remaining_width - remaining_width / 2 < 2) {
            addError(report, "invalid_u_arms", "/tank/footprint/notch",
                "U footprint arms and rear connector must be at least two cells thick");
        }
    }
    if (request.tank.corner_radius_steps < 0) {
        addError(report, "negative_corner_radius", "/tank/cornerRadiusSteps",
            "Corner radius cannot be negative");
    } else if (dimensions_safe && request.tank.corner_radius_steps >
               fittedCornerRadiusSteps(footprint, request.tank.corner_radius_steps)) {
        addWarning(report, "corner_radius_fitted", "/tank/cornerRadiusSteps",
            "Corner radius was fitted to the available footprint");
    }
    std::set<CellKey> corner_keys;
    const auto corners = footprintCorners(footprint);
    for (std::size_t index = 0; index < request.tank.corner_radii.size(); ++index) {
        const CornerRadiusDesign& radius = request.tank.corner_radii[index];
        const std::string path = "/tank/cornerRadii/" + std::to_string(index);
        if (!corner_keys.emplace(radius.vertex.column, radius.vertex.row).second) {
            addError(report, "duplicate_corner_radius", path,
                "Each footprint corner may have only one radius");
            continue;
        }
        const auto corner = std::find_if(corners.begin(), corners.end(), [&](const auto& item) {
            return item.vertex.column == radius.vertex.column &&
                item.vertex.row == radius.vertex.row && item.convex;
        });
        if (corner == corners.end()) {
            addError(report, "unknown_corner_radius", path,
                "Corner radius must reference a convex footprint corner");
        } else if (radius.radius_steps < 0) {
            addError(report, "negative_corner_radius", path,
                "Corner radius cannot be negative");
        } else if (radius.radius_steps > fittedCornerRadiusStepsAt(
                       footprint, radius.vertex, radius.radius_steps)) {
            addWarning(report, "corner_radius_fitted", path,
                "Corner radius was fitted to the available footprint");
        }
    }
    const auto tunnel_diagnostics = detail::validateAndResolveTunnels(request.tank);
    report.diagnostics.insert(report.diagnostics.end(),
        tunnel_diagnostics.begin(), tunnel_diagnostics.end());
    return report;
}

AquariumBuildResult buildAquarium(const AquariumBuildRequest& request) {
    AquariumBuildResult result;
    result.validation = validateAquarium(request);
    if (!result.validation.valid()) {
        return result;
    }

    detail::populateAquariumGeometry(request, result);

    populateStatistics(result);
    StableHasher hasher;
    hashResult(hasher, result);
    result.content_hash = hasher.finish();
    return result;
}

const char* meshMaterialName(MeshMaterial material) {
    switch (material) {
    case MeshMaterial::Structure: return "structure";
    case MeshMaterial::Sand: return "sand";
    case MeshMaterial::WaterVolume: return "water-volume";
    case MeshMaterial::WaterSurface: return "water-surface";
    case MeshMaterial::Glass: return "glass";
    }
    return "unknown";
}

} // namespace pr::aquarium::geometry
