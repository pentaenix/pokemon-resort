#include "GeometryBuilder.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace pr::aquarium::geometry::detail {
namespace {

constexpr float kGlassThickness =
    static_cast<float>(kGlassThicknessMilliWorldUnits) / 1000.0F;
constexpr float kBaseTop = 1.2F;
constexpr float kBottomRimTop = 2.4F;
constexpr float kBaseOverhang = 1.6F;
constexpr float kFrameOverhang = 0.88F;
constexpr float kFrameWidth = 1.6F;
constexpr float kGlassBottom = 1.608F;
constexpr float kSandSurfaceY = 3.016F;
constexpr float kTopRimHeight = 1.52F;
constexpr float kGlassTopInset = 0.5168F;
constexpr float kWaterCeilingInset = 0.88F;
constexpr float kWaterLevel = 0.91F;
constexpr float kWaterBottom = 2.504F;
constexpr float kFloorRimTop = 1.2F;

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

void addQuadWithNormals(
    SemanticMesh& mesh, const std::array<Vec3, 4>& positions,
    const std::array<Vec3, 4>& normals) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    constexpr std::array<Vec2, 4> uvs{{{0.0F, 0.0F}, {1.0F, 0.0F}, {1.0F, 1.0F}, {0.0F, 1.0F}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        mesh.vertices.push_back({positions[index], normals[index], uvs[index]});
    }
    mesh.indices.insert(mesh.indices.end(),
        {base, base + 1, base + 2, base, base + 2, base + 3});
}

std::vector<Vec2> perimeterOutwardNormals(const std::vector<Vec2>& boundary) {
    std::vector<Vec2> segment_normals;
    segment_normals.reserve(boundary.size());
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const Vec2 start = boundary[index];
        const Vec2 end = boundary[(index + 1) % boundary.size()];
        const float dx = end.x - start.x;
        const float dz = end.y - start.y;
        const float length = std::max(0.0001F, std::hypot(dx, dz));
        segment_normals.push_back({dz / length, -dx / length});
    }
    std::vector<Vec2> result;
    result.reserve(boundary.size());
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const Vec2 previous = segment_normals[(index + boundary.size() - 1) % boundary.size()];
        const Vec2 next = segment_normals[index];
        const float dot = previous.x * next.x + previous.y * next.y;
        if (dot < 0.6F) {
            result.push_back(next);
        } else {
            const float length = std::max(0.0001F,
                std::hypot(previous.x + next.x, previous.y + next.y));
            result.push_back({(previous.x + next.x) / length, (previous.y + next.y) / length});
        }
    }
    return result;
}

std::vector<Vec2> offsetBoundary(
    const std::vector<Vec2>& boundary,
    float distance) {
    if (boundary.size() < 3 || std::abs(distance) <= 0.0001F) return boundary;
    const auto segment_normals = [&]() {
        std::vector<Vec2> normals;
        normals.reserve(boundary.size());
        for (std::size_t index = 0; index < boundary.size(); ++index) {
            const Vec2 start = boundary[index];
            const Vec2 end = boundary[(index + 1U) % boundary.size()];
            const float dx = end.x - start.x;
            const float dz = end.y - start.y;
            const float length = std::max(0.0001F, std::hypot(dx, dz));
            normals.push_back({dz / length, -dx / length});
        }
        return normals;
    }();
    std::vector<Vec2> result;
    result.reserve(boundary.size());
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const Vec2 previous = segment_normals[
            (index + boundary.size() - 1U) % boundary.size()];
        const Vec2 next = segment_normals[index];
        const float sum_x = previous.x + next.x;
        const float sum_z = previous.y + next.y;
        const float sum_length = std::hypot(sum_x, sum_z);
        if (sum_length <= 0.0001F) {
            result.push_back({
                boundary[index].x + next.x * distance,
                boundary[index].y + next.y * distance});
            continue;
        }
        const Vec2 bisector{sum_x / sum_length, sum_z / sum_length};
        const float projection = std::max(0.2F,
            std::abs(bisector.x * next.x + bisector.y * next.y));
        result.push_back({
            boundary[index].x + bisector.x * distance / projection,
            boundary[index].y + bisector.y * distance / projection});
    }
    return result;
}

void addPerimeterSides(
    SemanticMesh& mesh, const std::vector<Vec2>& boundary,
    float bottom, float top) {
    const auto normals = perimeterOutwardNormals(boundary);
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const std::size_t next = (index + 1) % boundary.size();
        const Vec2 start = boundary[index];
        const Vec2 end = boundary[next];
        const Vec3 start_normal{normals[index].x, 0.0F, normals[index].y};
        const float dx = end.x - start.x;
        const float dz = end.y - start.y;
        const float length = std::max(0.0001F, std::hypot(dx, dz));
        const Vec2 segment_normal{dz / length, -dx / length};
        const Vec2 following = [&]() {
            const Vec2 following_end = boundary[(next + 1) % boundary.size()];
            const float following_dx = following_end.x - end.x;
            const float following_dz = following_end.y - end.y;
            const float following_length = std::max(0.0001F,
                std::hypot(following_dx, following_dz));
            return Vec2{following_dz / following_length, -following_dx / following_length};
        }();
        const bool hard_end = segment_normal.x * following.x +
            segment_normal.y * following.y < 0.6F;
        const Vec3 end_normal = hard_end
            ? Vec3{segment_normal.x, 0.0F, segment_normal.y}
            : Vec3{normals[next].x, 0.0F, normals[next].y};
        addQuadWithNormals(mesh,
            {{{end.x, bottom, end.y}, {start.x, bottom, start.y},
              {start.x, top, start.y}, {end.x, top, end.y}}},
            {{end_normal, start_normal, start_normal, end_normal}});
    }
}

void addWallSegment(
    SemanticMesh& mesh,
    Vec2 start,
    Vec2 end,
    float bottom,
    float top,
    bool cap_ends = false,
    float thickness = kGlassThickness) {
    const float dx = end.x - start.x;
    const float dz = end.y - start.y;
    const float length = std::hypot(dx, dz);
    if (length <= 0.0001F || top <= bottom) return;
    const Vec2 direction{dx / length, dz / length};
    const Vec2 inward{-direction.y, direction.x};
    const Vec2 inner_start{
        start.x + inward.x * thickness,
        start.y + inward.y * thickness,
    };
    const Vec2 inner_end{
        end.x + inward.x * thickness,
        end.y + inward.y * thickness,
    };
    const Vec3 outward{-inward.x, 0.0F, -inward.y};
    const Vec3 inward_normal{inward.x, 0.0F, inward.y};
    addQuad(mesh, {{{end.x, bottom, end.y}, {start.x, bottom, start.y},
                    {start.x, top, start.y}, {end.x, top, end.y}}}, outward);
    addQuad(mesh, {{{inner_start.x, bottom, inner_start.y}, {inner_end.x, bottom, inner_end.y},
                    {inner_end.x, top, inner_end.y}, {inner_start.x, top, inner_start.y}}},
        inward_normal);
    if (cap_ends) {
        addQuad(mesh, {{{start.x, bottom, start.y}, {inner_start.x, bottom, inner_start.y},
                        {inner_start.x, top, inner_start.y}, {start.x, top, start.y}}},
            {-direction.x, 0.0F, -direction.y});
        addQuad(mesh, {{{inner_end.x, bottom, inner_end.y}, {end.x, bottom, end.y},
                        {end.x, top, end.y}, {inner_end.x, top, inner_end.y}}},
            {direction.x, 0.0F, direction.y});
    }
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

void addSolidPlinth(
    SemanticMesh& mesh,
    const std::vector<Vec2>& boundary,
    float bottom,
    float top) {
    addPerimeterSides(mesh, boundary, bottom, top);
    addPolygonSurface(mesh, boundary, top);
}

} // namespace

void populateAquariumGeometry(
    const AquariumBuildRequest& request,
    AquariumBuildResult& result) {
    const auto& footprint = request.tank.footprint;
    const std::vector<Vec2> boundary = footprintBoundaryLocalWorld(
        footprint, request.tank.corner_radius_steps, request.tank.corner_radii);
    const std::vector<Vec2> base_boundary = offsetBoundary(boundary, kBaseOverhang);
    const std::vector<Vec2> frame_boundary = offsetBoundary(boundary, kFrameOverhang);
    const float height = static_cast<float>(request.tank.height_steps * kVerticalStepWorldUnits);
    const float below_floor_depth =
        static_cast<float>(request.tank.depth_steps * kVerticalStepWorldUnits);
    const bool below_floor = request.tank.depth_steps > 0;
    const float profile_bottom = -below_floor_depth;
    const float base_top = profile_bottom + kBaseTop;
    const float bottom_rim_top = profile_bottom + kBottomRimTop;
    const float sand_surface_y = profile_bottom + kSandSurfaceY;
    const float water_bottom = profile_bottom + kWaterBottom;
    const float top_rim_bottom = height - kTopRimHeight;
    const float glass_top = height - kGlassTopInset;
    const float water_ceiling = top_rim_bottom - kWaterCeilingInset;
    const float water_y = sand_surface_y +
        (water_ceiling - sand_surface_y) * kWaterLevel;

    result.meshes.meshes.reserve(result.meshes.meshes.size() + 5U);
    SemanticMesh& structure = addMesh(result.meshes, MeshMaterial::Structure);
    SemanticMesh& sand = addMesh(result.meshes, MeshMaterial::Sand);
    SemanticMesh& water_volume = addMesh(result.meshes, MeshMaterial::WaterVolume);
    SemanticMesh& water_surface = addMesh(result.meshes, MeshMaterial::WaterSurface);
    SemanticMesh& glass = addMesh(result.meshes, MeshMaterial::Glass);
    addSolidPlinth(structure, base_boundary, profile_bottom, base_top);
    for (std::size_t index = 0; index < frame_boundary.size(); ++index) {
        const Vec2 start = frame_boundary[index];
        const Vec2 end = frame_boundary[(index + 1) % frame_boundary.size()];
        addWallSegment(structure, start, end, base_top, bottom_rim_top, false, kFrameWidth);
        if (below_floor) {
            addWallSegment(structure, start, end, bottom_rim_top, 0.0F, false,
                kGlassThickness);
            addWallSegment(structure, start, end, 0.0F, kFloorRimTop, false,
                kFrameWidth);
        }
        addWallSegment(structure, start, end, top_rim_bottom, height, false, kFrameWidth);
    }
    addPerimeterSides(glass, boundary, below_floor ? 0.0F : kGlassBottom, glass_top);
    addPerimeterSides(water_volume, boundary, water_bottom, water_y - 0.002F);
    addPolygonSurface(sand, boundary, sand_surface_y);
    addPolygonSurface(water_surface, boundary, water_y);

    const std::vector<GridCell> occupied = footprintCells(footprint);
    // The complete above-floor tank footprint is solid to overworld actors.
    // Blocking only the perimeter allowed actors to enter interior cells when
    // rounding or follower movement crossed more than one grid boundary.
    result.collision.blocked_cells = occupied;

    NavigationLayer layer;
    layer.floor_y = sand_surface_y;
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
            (sand_surface_y + water_y) * 0.5F,
            static_cast<float>(spawn_cell->row - footprint.origin_cell.row) * kWorldUnitsPerCell +
                static_cast<float>(kWorldUnitsPerCell) * 0.5F - half_depth,
        });
    }
    double twice_area = 0.0;
    for (std::size_t index = 0; index < boundary.size(); ++index) {
        const Vec2 a = boundary[index];
        const Vec2 b = boundary[(index + 1U) % boundary.size()];
        twice_area += static_cast<double>(a.x) * static_cast<double>(b.y) -
            static_cast<double>(b.x) * static_cast<double>(a.y);
    }
    const double area_world_units = std::abs(twice_area) * 0.5;
    // Match Aquarium Maker's capacity contract: the water band begins at the
    // rendered water-volume bottom, slightly below the flat sand surface.
    const double depth_world_units = std::max(0.0F, water_y - water_bottom);
    result.statistics.water_volume_litres = static_cast<std::uint64_t>(std::llround(
        area_world_units * depth_world_units * 1000.0 / 4096.0));
    canonicalizeResultFloats(result);
}

} // namespace pr::aquarium::geometry::detail
