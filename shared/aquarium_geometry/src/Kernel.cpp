#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace pr::aquarium::geometry {

namespace {

constexpr float kGlassThickness =
    static_cast<float>(kGlassThicknessMilliWorldUnits) / 1000.0F;
constexpr float kFrameHeight = 1.0F;
constexpr float kSandSurfaceY = 0.5F;

void addError(ValidationReport& report, std::string code, std::string path, std::string message) {
    report.diagnostics.push_back({
        DiagnosticSeverity::Error,
        std::move(code),
        std::move(path),
        std::move(message),
    });
}

void addQuad(
    SemanticMesh& mesh,
    const std::array<Vec3, 4>& positions,
    const Vec3& normal) {
    const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr std::array<Vec2, 4> uvs{{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}};
    for (std::size_t i = 0; i < positions.size(); ++i) {
        mesh.vertices.push_back({positions[i], normal, uvs[i]});
    }
    mesh.indices.insert(mesh.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void addBox(SemanticMesh& mesh, const Vec3& minimum, const Vec3& maximum) {
    addQuad(mesh, {{{minimum.x, minimum.y, maximum.z}, {maximum.x, minimum.y, maximum.z},
                    {maximum.x, maximum.y, maximum.z}, {minimum.x, maximum.y, maximum.z}}},
            {0.0F, 0.0F, 1.0F});
    addQuad(mesh, {{{maximum.x, minimum.y, minimum.z}, {minimum.x, minimum.y, minimum.z},
                    {minimum.x, maximum.y, minimum.z}, {maximum.x, maximum.y, minimum.z}}},
            {0.0F, 0.0F, -1.0F});
    addQuad(mesh, {{{maximum.x, minimum.y, maximum.z}, {maximum.x, minimum.y, minimum.z},
                    {maximum.x, maximum.y, minimum.z}, {maximum.x, maximum.y, maximum.z}}},
            {1.0F, 0.0F, 0.0F});
    addQuad(mesh, {{{minimum.x, minimum.y, minimum.z}, {minimum.x, minimum.y, maximum.z},
                    {minimum.x, maximum.y, maximum.z}, {minimum.x, maximum.y, minimum.z}}},
            {-1.0F, 0.0F, 0.0F});
    addQuad(mesh, {{{minimum.x, maximum.y, maximum.z}, {maximum.x, maximum.y, maximum.z},
                    {maximum.x, maximum.y, minimum.z}, {minimum.x, maximum.y, minimum.z}}},
            {0.0F, 1.0F, 0.0F});
    addQuad(mesh, {{{minimum.x, minimum.y, minimum.z}, {maximum.x, minimum.y, minimum.z},
                    {maximum.x, minimum.y, maximum.z}, {minimum.x, minimum.y, maximum.z}}},
            {0.0F, -1.0F, 0.0F});
}

SemanticMesh& addMesh(SemanticMeshSet& set, MeshMaterial material) {
    set.meshes.push_back({});
    set.meshes.back().material = material;
    return set.meshes.back();
}

void addPerimeterBoxes(
    SemanticMesh& mesh,
    float half_width,
    float half_depth,
    float bottom,
    float top,
    float thickness) {
    addBox(mesh, {-half_width, bottom, -half_depth}, {half_width, top, -half_depth + thickness});
    addBox(mesh, {-half_width, bottom, half_depth - thickness}, {half_width, top, half_depth});
    addBox(mesh,
           {-half_width, bottom, -half_depth + thickness},
           {-half_width + thickness, top, half_depth - thickness});
    addBox(mesh,
           {half_width - thickness, bottom, -half_depth + thickness},
           {half_width, top, half_depth - thickness});
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
    hasher.addUnsigned(result.navigation.suggested_spawns.size());
    for (const Vec3& spawn : result.navigation.suggested_spawns) {
        hasher.addFloat(spawn.x);
        hasher.addFloat(spawn.y);
        hasher.addFloat(spawn.z);
    }
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
    if (request.tank.footprint.width_cells > kMaxFootprintCells ||
        request.tank.footprint.depth_cells > kMaxFootprintCells) {
        addError(report, "footprint_too_large", "/tank/footprint", "Footprint dimensions exceed the kernel safety limit");
    }
    const bool swaps_axes = (request.tank.footprint.rotation_quarter_turns == 1 ||
                             request.tank.footprint.rotation_quarter_turns == 3);
    const std::int32_t occupied_width = swaps_axes
        ? request.tank.footprint.depth_cells
        : request.tank.footprint.width_cells;
    const std::int32_t occupied_depth = swaps_axes
        ? request.tank.footprint.width_cells
        : request.tank.footprint.depth_cells;
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
    if (request.tank.footprint.rotation_quarter_turns < 0 ||
        request.tank.footprint.rotation_quarter_turns > 3) {
        addError(report, "rotation_out_of_range", "/tank/footprint/rotationQuarterTurns", "Rotation must be a quarter turn from zero through three");
    }
    if (request.tank.footprint.shape != FootprintShape::Rectangle) {
        addError(report, "shape_not_implemented", "/tank/footprint/shape", "This kernel milestone supports rectangle footprints only");
    }
    if (request.tank.corner_radius_steps != 0) {
        addError(report, "roundness_not_implemented", "/tank/cornerRadiusSteps", "This kernel milestone supports square corners only");
    }
    if (!request.tank.tunnels.empty()) {
        addError(report, "tunnels_not_implemented", "/tank/tunnels", "This kernel milestone does not generate tunnels");
    }
    return report;
}

AquariumBuildResult buildAquarium(const AquariumBuildRequest& request) {
    AquariumBuildResult result;
    result.validation = validateAquarium(request);
    if (!result.validation.valid()) {
        return result;
    }

    const bool swaps_axes = (request.tank.footprint.rotation_quarter_turns % 2) != 0;
    const std::int32_t width_cells = swaps_axes
        ? request.tank.footprint.depth_cells
        : request.tank.footprint.width_cells;
    const std::int32_t depth_cells = swaps_axes
        ? request.tank.footprint.width_cells
        : request.tank.footprint.depth_cells;
    const float half_width = static_cast<float>(width_cells) * static_cast<float>(kWorldUnitsPerCell) * 0.5F;
    const float half_depth = static_cast<float>(depth_cells) * static_cast<float>(kWorldUnitsPerCell) * 0.5F;
    const float height = static_cast<float>(request.tank.height_steps * kVerticalStepWorldUnits);
    const float inner_min_x = -half_width + kGlassThickness;
    const float inner_max_x = half_width - kGlassThickness;
    const float inner_min_z = -half_depth + kGlassThickness;
    const float inner_max_z = half_depth - kGlassThickness;

    SemanticMesh& structure = addMesh(result.meshes, MeshMaterial::Structure);
    addPerimeterBoxes(structure, half_width, half_depth, 0.0F, kFrameHeight, kGlassThickness);
    addPerimeterBoxes(structure, half_width, half_depth, height - kFrameHeight, height, kGlassThickness);

    SemanticMesh& sand = addMesh(result.meshes, MeshMaterial::Sand);
    addQuad(sand,
            {{{inner_min_x, kSandSurfaceY, inner_max_z}, {inner_max_x, kSandSurfaceY, inner_max_z},
              {inner_max_x, kSandSurfaceY, inner_min_z}, {inner_min_x, kSandSurfaceY, inner_min_z}}},
            {0.0F, 1.0F, 0.0F});

    SemanticMesh& water = addMesh(result.meshes, MeshMaterial::Water);
    const float water_y = height - kGlassThickness;
    addQuad(water,
            {{{inner_min_x, water_y, inner_max_z}, {inner_max_x, water_y, inner_max_z},
              {inner_max_x, water_y, inner_min_z}, {inner_min_x, water_y, inner_min_z}}},
            {0.0F, 1.0F, 0.0F});

    SemanticMesh& glass = addMesh(result.meshes, MeshMaterial::Glass);
    addPerimeterBoxes(glass, half_width, half_depth, kFrameHeight, height - kFrameHeight, kGlassThickness);

    const auto& footprint = request.tank.footprint;
    for (std::int32_t row_offset = 0; row_offset < depth_cells; ++row_offset) {
        for (std::int32_t column_offset = 0; column_offset < width_cells; ++column_offset) {
            const bool perimeter = row_offset == 0 || column_offset == 0 ||
                                   row_offset == depth_cells - 1 ||
                                   column_offset == width_cells - 1;
            if (perimeter) {
                result.collision.blocked_cells.push_back({
                    footprint.origin_cell.column + column_offset,
                    footprint.origin_cell.row + row_offset,
                });
            }
        }
    }

    NavigationLayer layer;
    layer.floor_y = kSandSurfaceY;
    layer.ceiling_y = water_y;
    layer.area.outer = {
        {inner_min_x, inner_min_z},
        {inner_max_x, inner_min_z},
        {inner_max_x, inner_max_z},
        {inner_min_x, inner_max_z},
    };
    result.navigation.layers.push_back(std::move(layer));
    result.navigation.suggested_spawns.push_back({0.0F, (kSandSurfaceY + water_y) * 0.5F, 0.0F});

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
    case MeshMaterial::Water: return "water";
    case MeshMaterial::Glass: return "glass";
    }
    return "unknown";
}

} // namespace pr::aquarium::geometry
