#include "aquarium_geometry/Kernel.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

using namespace pr::aquarium::geometry;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void requireNear(float actual, float expected, const std::string& message) {
    if (std::fabs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message + ": expected " + std::to_string(expected) +
                                 ", got " + std::to_string(actual));
    }
}

Vec3 subtract(const Vec3& left, const Vec3& right) {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 cross(const Vec3& left, const Vec3& right) {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

float dot(const Vec3& left, const Vec3& right) {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

AquariumBuildRequest rectangleRequest() {
    AquariumBuildRequest request;
    request.tank.id = "tank_golden_rectangle_even";
    request.tank.footprint.origin_cell = {7, 6};
    request.tank.footprint.width_cells = 6;
    request.tank.footprint.depth_cells = 4;
    request.tank.height_steps = 8;
    return request;
}

void testCanonicalTransforms() {
    requireNear(cellCentreWorld(0), 8.0F, "cell zero centre");
    requireNear(cellCentreWorld(7), 120.0F, "cell seven centre");
    requireNear(footprintCentreWorld(7, 3), 136.0F, "odd footprint centre");
    requireNear(footprintCentreWorld(7, 4), 144.0F, "even footprint centre");
}

void testValidationIsStable() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.footprint.width_cells = 2;
    request.tank.corner_radius_steps = 1;
    request.tank.tunnels.push_back({"tunnel_1", TunnelRoute::Straight, {}});
    const ValidationReport report = validateAquarium(request);
    require(!report.valid(), "invalid request was accepted");
    require(report.diagnostics.size() == 3, "unexpected diagnostic count");
    require(report.diagnostics[0].code == "footprint_too_narrow", "diagnostic ordering changed");
    require(report.diagnostics[1].code == "roundness_not_implemented", "roundness diagnostic missing");
    require(report.diagnostics[2].code == "tunnels_not_implemented", "tunnel diagnostic missing");
}

void testValidationRejectsUnsafeDimensions() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.footprint.width_cells = kMaxFootprintCells + 1;
    ValidationReport report = validateAquarium(request);
    require(!report.valid(), "oversized footprint was accepted");
    require(report.diagnostics.back().code == "footprint_too_large", "oversized diagnostic changed");

    request = rectangleRequest();
    request.tank.footprint.width_cells = 3;
    request.tank.footprint.depth_cells = 4;
    request.tank.footprint.rotation_quarter_turns = 1;
    request.tank.footprint.origin_cell.column = std::numeric_limits<std::int32_t>::max() - 3;
    report = validateAquarium(request);
    require(!report.valid(), "rotated coordinate overflow was accepted");
    require(report.diagnostics.back().code == "footprint_coordinate_overflow",
            "coordinate overflow diagnostic changed");
}

void testRectangleGolden() {
    const AquariumBuildResult result = buildAquarium(rectangleRequest());
    require(result.validation.valid(), "golden rectangle failed validation");
    require(result.meshes.meshes.size() == 4, "semantic material partition changed");
    require(result.statistics.mesh_count == 4, "mesh count changed");
    require(result.statistics.vertex_count == 296, "vertex count changed");
    require(result.statistics.index_count == 444, "index count changed");
    require(result.statistics.triangle_count == 148, "triangle count changed");
    require(result.statistics.collision_cell_count == 16, "collision perimeter changed");
    require(result.statistics.navigation_layer_count == 1, "navigation layer count changed");
    require(result.navigation.suggested_spawns.size() == 1, "spawn count changed");
    require(result.content_hash == "fnv1a64:5f18ef72142032cb", "content hash changed: " + result.content_hash);

    for (const SemanticMesh& mesh : result.meshes.meshes) {
        for (const Vertex& vertex : mesh.vertices) {
            require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
                        std::isfinite(vertex.position.z),
                    "non-finite vertex");
        }
        require(mesh.indices.size() % 3 == 0, "mesh contains incomplete triangle");
        for (std::uint32_t index : mesh.indices) {
            require(index < mesh.vertices.size(), "mesh index out of range");
        }
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
            const Vertex& a = mesh.vertices[mesh.indices[index]];
            const Vertex& b = mesh.vertices[mesh.indices[index + 1]];
            const Vertex& c = mesh.vertices[mesh.indices[index + 2]];
            const Vec3 face = cross(subtract(b.position, a.position), subtract(c.position, a.position));
            require(dot(face, a.normal) > 0.0F, "triangle winding opposes its declared normal");
        }
    }
}


void testQuarterTurnSwapsRectangleAxes() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.footprint.rotation_quarter_turns = 1;
    const AquariumBuildResult result = buildAquarium(request);
    require(result.validation.valid(), "rotated rectangle failed validation");
    require(result.collision.blocked_cells.size() == 16, "rotated collision perimeter changed");
    require(result.collision.blocked_cells.back().column == 10, "rotated width did not use original depth");
    require(result.collision.blocked_cells.back().row == 11, "rotated depth did not use original width");
}

} // namespace

int main() {
    try {
        testCanonicalTransforms();
        testValidationIsStable();
        testValidationRejectsUnsafeDimensions();
        testRectangleGolden();
        testQuarterTurnSwapsRectangleAxes();
        std::cout << "aquarium_geometry_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_geometry_tests: " << error.what() << '\n';
        return 1;
    }
}
