#include "GeometryBuilder.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <vector>

namespace pr::aquarium::geometry::detail {
namespace {

constexpr float kGlassThickness =
    static_cast<float>(kGlassThicknessMilliWorldUnits) / 1000.0F;
constexpr float kFrameHeight = 1.0F;
constexpr float kSandSurfaceY = 0.5F;

float canonicalFloat(float value) {
    constexpr double kOutputStepsPerWorldUnit = 10000.0;
    return static_cast<float>(std::round(
        static_cast<double>(value) * kOutputStepsPerWorldUnit) /
        kOutputStepsPerWorldUnit);
}

void canonicalizeResultFloats(AquariumBuildResult& result) {
    for (SemanticMesh& mesh : result.meshes.meshes) {
        for (Vertex& vertex : mesh.vertices) {
            vertex.position.x = canonicalFloat(vertex.position.x);
            vertex.position.y = canonicalFloat(vertex.position.y);
            vertex.position.z = canonicalFloat(vertex.position.z);
            vertex.normal.x = canonicalFloat(vertex.normal.x);
            vertex.normal.y = canonicalFloat(vertex.normal.y);
            vertex.normal.z = canonicalFloat(vertex.normal.z);
            vertex.uv.x = canonicalFloat(vertex.uv.x);
            vertex.uv.y = canonicalFloat(vertex.uv.y);
        }
    }
    for (NavigationLayer& layer : result.navigation.layers) {
        layer.floor_y = canonicalFloat(layer.floor_y);
        layer.ceiling_y = canonicalFloat(layer.ceiling_y);
        for (Vec2& point : layer.area.outer) {
            point.x = canonicalFloat(point.x);
            point.y = canonicalFloat(point.y);
        }
        for (auto& hole : layer.area.holes) {
            for (Vec2& point : hole) {
                point.x = canonicalFloat(point.x);
                point.y = canonicalFloat(point.y);
            }
        }
    }
    for (Vec3& spawn : result.navigation.suggested_spawns) {
        spawn.x = canonicalFloat(spawn.x);
        spawn.y = canonicalFloat(spawn.y);
        spawn.z = canonicalFloat(spawn.z);
    }
}

SemanticMesh& addMesh(SemanticMeshSet& set, MeshMaterial material) {
    set.meshes.push_back({});
    set.meshes.back().material = material;
    return set.meshes.back();
}

void addQuad(
    SemanticMesh& mesh,
    const std::array<Vec3, 4>& positions,
    Vec3 normal) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr std::array<Vec2, 4> uvs{{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        mesh.vertices.push_back({positions[index], normal, uvs[index]});
    }
    mesh.indices.insert(mesh.indices.end(),
        {base, base + 1, base + 2, base, base + 2, base + 3});
}

void addWallSegment(
    SemanticMesh& mesh,
    Vec2 start,
    Vec2 end,
    float bottom,
    float top) {
    const float dx = end.x - start.x;
    const float dz = end.y - start.y;
    const float length = std::hypot(dx, dz);
    if (length <= 0.0001F || top <= bottom) return;
    const Vec2 direction{dx / length, dz / length};
    const Vec2 inward{-direction.y, direction.x};
    const Vec2 inner_start{
        start.x + inward.x * kGlassThickness,
        start.y + inward.y * kGlassThickness,
    };
    const Vec2 inner_end{
        end.x + inward.x * kGlassThickness,
        end.y + inward.y * kGlassThickness,
    };
    const Vec3 outward{-inward.x, 0.0F, -inward.y};
    const Vec3 inward_normal{inward.x, 0.0F, inward.y};
    addQuad(mesh, {{{end.x, bottom, end.y}, {start.x, bottom, start.y},
                    {start.x, top, start.y}, {end.x, top, end.y}}}, outward);
    addQuad(mesh, {{{inner_start.x, bottom, inner_start.y}, {inner_end.x, bottom, inner_end.y},
                    {inner_end.x, top, inner_end.y}, {inner_start.x, top, inner_start.y}}},
        inward_normal);
    addQuad(mesh, {{{start.x, bottom, start.y}, {inner_start.x, bottom, inner_start.y},
                    {inner_start.x, top, inner_start.y}, {start.x, top, start.y}}},
        {-direction.x, 0.0F, -direction.y});
    addQuad(mesh, {{{inner_end.x, bottom, inner_end.y}, {end.x, bottom, end.y},
                    {end.x, top, end.y}, {inner_end.x, top, inner_end.y}}},
        {direction.x, 0.0F, direction.y});
    addQuad(mesh, {{{start.x, top, start.y}, {inner_start.x, top, inner_start.y},
                    {inner_end.x, top, inner_end.y}, {end.x, top, end.y}}},
        {0.0F, 1.0F, 0.0F});
    addQuad(mesh, {{{end.x, bottom, end.y}, {inner_end.x, bottom, inner_end.y},
                    {inner_start.x, bottom, inner_start.y}, {start.x, bottom, start.y}}},
        {0.0F, -1.0F, 0.0F});
}

float polygonCross(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool pointInTriangle(Vec2 point, Vec2 a, Vec2 b, Vec2 c) {
    const float a_cross = polygonCross(a, b, point);
    const float b_cross = polygonCross(b, c, point);
    const float c_cross = polygonCross(c, a, point);
    return a_cross >= -0.0001F && b_cross >= -0.0001F && c_cross >= -0.0001F;
}

std::vector<std::array<std::uint32_t, 3>> triangulate(const std::vector<Vec2>& polygon) {
    std::vector<std::array<std::uint32_t, 3>> triangles;
    if (polygon.size() < 3) return triangles;
    std::vector<std::uint32_t> remaining(polygon.size());
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        remaining[index] = static_cast<std::uint32_t>(index);
    }
    while (remaining.size() > 3) {
        bool removed = false;
        for (std::size_t index = 0; index < remaining.size(); ++index) {
            const std::uint32_t previous = remaining[(index + remaining.size() - 1) % remaining.size()];
            const std::uint32_t current = remaining[index];
            const std::uint32_t next = remaining[(index + 1) % remaining.size()];
            if (polygonCross(polygon[previous], polygon[current], polygon[next]) <= 0.0001F) continue;
            bool contains = false;
            for (const std::uint32_t candidate : remaining) {
                if (candidate == previous || candidate == current || candidate == next) continue;
                if (pointInTriangle(polygon[candidate], polygon[previous], polygon[current], polygon[next])) {
                    contains = true;
                    break;
                }
            }
            if (contains) continue;
            triangles.push_back({previous, current, next});
            remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
            removed = true;
            break;
        }
        if (!removed) return {};
    }
    if (remaining.size() == 3) {
        triangles.push_back({remaining[0], remaining[1], remaining[2]});
    }
    return triangles;
}

void addPolygonSurface(
    SemanticMesh& mesh,
    const std::vector<Vec2>& polygon,
    float y) {
    const auto triangles = triangulate(polygon);
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    float min_x = std::numeric_limits<float>::max();
    float min_z = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_z = std::numeric_limits<float>::lowest();
    for (const Vec2 point : polygon) {
        min_x = std::min(min_x, point.x);
        min_z = std::min(min_z, point.y);
        max_x = std::max(max_x, point.x);
        max_z = std::max(max_z, point.y);
    }
    const float width = std::max(0.001F, max_x - min_x);
    const float depth = std::max(0.001F, max_z - min_z);
    for (const Vec2 point : polygon) {
        mesh.vertices.push_back({
            {point.x, y, point.y},
            {0.0F, 1.0F, 0.0F},
            {(point.x - min_x) / width, (point.y - min_z) / depth},
        });
    }
    for (const auto triangle : triangles) {
        mesh.indices.insert(mesh.indices.end(), {
            base + triangle[0], base + triangle[2], base + triangle[1],
        });
    }
}

} // namespace

void populateAquariumGeometry(
    const AquariumBuildRequest& request,
    AquariumBuildResult& result) {
    const auto& footprint = request.tank.footprint;
    const std::int32_t radius_steps = fittedCornerRadiusSteps(
        footprint, request.tank.corner_radius_steps);
    const std::vector<Vec2> boundary = footprintBoundaryLocalWorld(footprint, radius_steps);
    const float height = static_cast<float>(request.tank.height_steps * kVerticalStepWorldUnits);
    const float water_y = height - kGlassThickness;

    result.meshes.meshes.reserve(result.meshes.meshes.size() + 4U);
    SemanticMesh& structure = addMesh(result.meshes, MeshMaterial::Structure);
    SemanticMesh& sand = addMesh(result.meshes, MeshMaterial::Sand);
    SemanticMesh& water = addMesh(result.meshes, MeshMaterial::Water);
    SemanticMesh& glass = addMesh(result.meshes, MeshMaterial::Glass);
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const Vec2 start = boundary[index];
        const Vec2 end = boundary[(index + 1) % boundary.size()];
        addWallSegment(structure, start, end, 0.0F, kFrameHeight);
        addWallSegment(structure, start, end, height - kFrameHeight, height);
        addWallSegment(glass, start, end, kFrameHeight, height - kFrameHeight);
    }
    addPolygonSurface(sand, boundary, kSandSurfaceY);
    addPolygonSurface(water, boundary, water_y);

    const std::vector<GridCell> occupied = footprintCells(footprint);
    std::set<std::pair<std::int32_t, std::int32_t>> occupied_set;
    for (const GridCell cell : occupied) occupied_set.emplace(cell.column, cell.row);
    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 4> neighbours{{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0},
    }};
    for (const GridCell cell : occupied) {
        const bool perimeter = std::any_of(neighbours.begin(), neighbours.end(), [&](auto delta) {
            return !occupied_set.count({cell.column + delta.first, cell.row + delta.second});
        });
        if (perimeter) result.collision.blocked_cells.push_back(cell);
    }

    NavigationLayer layer;
    layer.floor_y = kSandSurfaceY;
    layer.ceiling_y = water_y;
    layer.area.outer = boundary;
    result.navigation.layers.push_back(std::move(layer));

    const float center_column = static_cast<float>(occupiedWidthCells(footprint) - 1) * 0.5F;
    const float center_row = static_cast<float>(occupiedDepthCells(footprint) - 1) * 0.5F;
    const GridCell* spawn_cell = nullptr;
    float nearest = std::numeric_limits<float>::max();
    for (const GridCell& cell : occupied) {
        const float local_column = static_cast<float>(cell.column - footprint.origin_cell.column);
        const float local_row = static_cast<float>(cell.row - footprint.origin_cell.row);
        const float distance = std::abs(local_column - center_column) + std::abs(local_row - center_row);
        if (distance < nearest) {
            nearest = distance;
            spawn_cell = &cell;
        }
    }
    if (spawn_cell) {
        const float half_width = static_cast<float>(occupiedWidthCells(footprint) * kWorldUnitsPerCell) * 0.5F;
        const float half_depth = static_cast<float>(occupiedDepthCells(footprint) * kWorldUnitsPerCell) * 0.5F;
        result.navigation.suggested_spawns.push_back({
            static_cast<float>(spawn_cell->column - footprint.origin_cell.column) * kWorldUnitsPerCell +
                static_cast<float>(kWorldUnitsPerCell) * 0.5F - half_width,
            (kSandSurfaceY + water_y) * 0.5F,
            static_cast<float>(spawn_cell->row - footprint.origin_cell.row) * kWorldUnitsPerCell +
                static_cast<float>(kWorldUnitsPerCell) * 0.5F - half_depth,
        });
    }
    canonicalizeResultFloats(result);
}

} // namespace pr::aquarium::geometry::detail
