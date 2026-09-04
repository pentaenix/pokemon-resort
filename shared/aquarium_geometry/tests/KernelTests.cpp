#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
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

bool nearlySamePoint(Vec2 left, Vec2 right) {
    return std::fabs(left.x - right.x) <= 0.0001F &&
        std::fabs(left.y - right.y) <= 0.0001F;
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

void requireValidMeshSet(const AquariumBuildResult& result, const std::string& fixture);

const SemanticMesh& meshWithMaterial(
    const AquariumBuildResult& result, MeshMaterial material) {
    const auto found = std::find_if(
        result.meshes.meshes.begin(), result.meshes.meshes.end(),
        [&](const SemanticMesh& mesh) { return mesh.material == material; });
    if (found == result.meshes.meshes.end()) {
        throw std::runtime_error("missing semantic mesh: " +
            std::string(meshMaterialName(material)));
    }
    return *found;
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
    request.tank.tunnels.push_back({"tunnel_1", TunnelRoute::Straight, {}});
    const ValidationReport report = validateAquarium(request);
    require(!report.valid(), "invalid request was accepted");
    require(report.diagnostics.size() == 2, "unexpected diagnostic count");
    require(report.diagnostics[0].code == "footprint_too_narrow", "diagnostic ordering changed");
    require(std::any_of(report.diagnostics.begin(), report.diagnostics.end(),
            [](const auto& diagnostic) {
                return diagnostic.code == "tunnel_route_too_short";
            }), "tunnel diagnostic missing");
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

void testVersionBoundaryRejectsStaleDerivedData() {
    AquariumBuildRequest stale_kernel = rectangleRequest();
    stale_kernel.kernel_abi_version = kKernelAbiVersion - 1;
    const ValidationReport stale_report = validateAquarium(stale_kernel);
    require(!stale_report.valid() && std::any_of(
            stale_report.diagnostics.begin(), stale_report.diagnostics.end(),
            [](const auto& diagnostic) {
                return diagnostic.code == "unsupported_kernel_abi";
            }),
        "stale kernel ABI was accepted as reusable derived geometry");

    AquariumBuildRequest newer_schema = rectangleRequest();
    newer_schema.design_schema_version = kDesignSchemaVersion + 1;
    const ValidationReport newer_report = validateAquarium(newer_schema);
    require(!newer_report.valid() && std::any_of(
            newer_report.diagnostics.begin(), newer_report.diagnostics.end(),
            [](const auto& diagnostic) {
                return diagnostic.code == "unsupported_design_schema";
            }),
        "newer design schema was accepted by an older geometry kernel");
}

void testRectangleGolden() {
    const AquariumBuildResult result = buildAquarium(rectangleRequest());
    require(result.validation.valid(), "golden rectangle failed validation");
    require(result.meshes.meshes.size() == 5, "semantic material partition changed");
    require(result.statistics.mesh_count == 5, "mesh count changed");
    require(result.statistics.vertex_count == 188, "vertex count changed");
    require(result.statistics.index_count == 282, "index count changed");
    require(result.statistics.triangle_count == 94, "triangle count changed");
    require(result.statistics.collision_cell_count == 24, "full-footprint collision changed");
    require(std::any_of(result.collision.blocked_cells.begin(),
                result.collision.blocked_cells.end(), [](GridCell cell) {
                    return cell.column == 9 && cell.row == 7;
                }),
        "interior occupied cell is not blocked");
    require(result.statistics.navigation_layer_count == 1, "navigation layer count changed");
    require(result.navigation.suggested_spawns.size() == 1, "spawn count changed");
    require(result.content_hash == "fnv1a64:a7c4b0150a3c90e3", "content hash changed: " + result.content_hash);
    require(result.statistics.water_volume_litres == 80735,
        "standard tank derived water volume changed");

    const SemanticMesh& structure = result.meshes.meshes.front();
    require(std::any_of(structure.vertices.begin(), structure.vertices.end(), [](const Vertex& vertex) {
                return std::abs(vertex.position.y - 1.2F) < 0.0001F && vertex.normal.y > 0.9F;
            }),
        "solid lower plinth top is missing");
    require(std::any_of(structure.vertices.begin(), structure.vertices.end(), [](const Vertex& vertex) {
                return std::abs(vertex.position.x) > 49.5F && vertex.position.y <= 1.2F;
            }),
        "lower plinth overhang is missing");

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

void testBelowFloorDepthAndVolume() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.height_steps = 4;
    request.tank.depth_steps = 6;
    const AquariumBuildResult result = buildAquarium(request);
    requireValidMeshSet(result, "below-floor rectangle");
    requireNear(result.navigation.layers.front().floor_y, -44.984F,
        "below-floor sand datum changed");
    require(result.navigation.layers.front().ceiling_y > 0.0F,
        "below-floor water surface did not remain above the room floor");
    require(result.statistics.water_volume_litres == 102575,
        "below-floor derived water volume changed");
    const SemanticMesh& glass = result.meshes.meshes.back();
    require(std::any_of(glass.vertices.begin(), glass.vertices.end(), [](const Vertex& vertex) {
                return std::abs(vertex.position.y) < 0.0001F;
            }), "below-floor glass does not begin at the room floor");
    request.tank.depth_steps = 13;
    const ValidationReport invalid = validateAquarium(request);
    require(!invalid.valid() && std::any_of(invalid.diagnostics.begin(), invalid.diagnostics.end(),
            [](const auto& diagnostic) { return diagnostic.code == "depth_out_of_range"; }),
        "out-of-range below-floor depth was accepted");
}

void testSubtractedFootprintsStaySimpleAndConnected() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.footprint.width_cells = 9;
    request.tank.footprint.depth_cells = 6;
    request.tank.footprint.subtracted_cells = {
        {3, 0}, {4, 0}, {5, 0},
        {3, 1}, {4, 1}, {5, 1},
        {3, 2}, {4, 2}, {5, 2},
    };
    const AquariumBuildResult result = buildAquarium(request);
    requireValidMeshSet(result, "subtracted footprint");
    require(footprintCells(request.tank.footprint).size() == 45U &&
            result.navigation.layers.front().area.holes.empty(),
        "exterior-connected subtraction did not produce one simple water polygon");

    request.tank.footprint.width_cells = 5;
    request.tank.footprint.depth_cells = 5;
    request.tank.footprint.subtracted_cells = {{2, 0}, {2, 1}};
    require(!footprintHasMinimumThreeCellSections(request.tank.footprint),
        "shared editing rule accepted subtraction that left two-cell arms");
    require(validateAquarium(request).valid(),
        "new editing-width rule made an existing narrow save unloadable");

    request.tank.footprint.subtracted_cells = {{2, 2}};
    ValidationReport validation = validateAquarium(request);
    require(!validation.valid() && std::any_of(validation.diagnostics.begin(),
            validation.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == "enclosed_subtracted_footprint";
            }), "interior subtraction was not rejected as an enclosed hole");

    request.tank.footprint.subtracted_cells = {{2, 0}, {2, 1}, {2, 2}, {2, 3}, {2, 4}};
    validation = validateAquarium(request);
    require(!validation.valid() && std::any_of(validation.diagnostics.begin(),
            validation.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == "disconnected_subtracted_footprint";
            }), "subtraction that splits the tank was accepted");
}

void requireValidMeshSet(const AquariumBuildResult& result, const std::string& fixture) {
    require(result.validation.valid(), fixture + " failed validation");
    for (const SemanticMesh& mesh : result.meshes.meshes) {
        for (const Vertex& vertex : mesh.vertices) {
            require(std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) &&
                        std::isfinite(vertex.position.z),
                    fixture + " contains a non-finite vertex");
        }
        require(mesh.indices.size() % 3 == 0, fixture + " contains an incomplete triangle");
        for (std::uint32_t index : mesh.indices) {
            require(index < mesh.vertices.size(), fixture + " contains an out-of-range index");
        }
        for (std::size_t index = 0; index < mesh.indices.size(); index += 3) {
            const Vertex& a = mesh.vertices[mesh.indices[index]];
            const Vertex& b = mesh.vertices[mesh.indices[index + 1]];
            const Vertex& c = mesh.vertices[mesh.indices[index + 2]];
            const Vec3 face = cross(subtract(b.position, a.position), subtract(c.position, a.position));
            require(dot(face, a.normal) > 0.0F,
                fixture + " triangle winding opposes its declared normal");
        }
    }
}


void testQuarterTurnSwapsRectangleAxes() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.footprint.rotation_quarter_turns = 1;
    const AquariumBuildResult result = buildAquarium(request);
    require(result.validation.valid(), "rotated rectangle failed validation");
    require(result.collision.blocked_cells.size() == 24, "rotated full-footprint collision changed");
    require(result.collision.blocked_cells.back().column == 10, "rotated width did not use original depth");
    require(result.collision.blocked_cells.back().row == 11, "rotated depth did not use original width");
}

void testShapeOccupancyRotationAndValidation() {
    FootprintDesign l_shape;
    l_shape.shape = FootprintShape::L;
    l_shape.origin_cell = {7, 6};
    l_shape.width_cells = 5;
    l_shape.depth_cells = 5;
    l_shape.notch_width_cells = 3;
    l_shape.notch_depth_cells = 3;
    require(footprintCells(l_shape).size() == 16, "L footprint notch occupancy changed");
    l_shape.rotation_quarter_turns = 1;
    const auto rotated_l = footprintCells(l_shape);
    require(rotated_l.size() == 16 && rotated_l.front().column == 7 &&
            rotated_l.front().row == 6 && rotated_l.back().column == 11 &&
            rotated_l.back().row == 10,
        "rotated L footprint escaped its canonical occupied bounds");

    AquariumBuildRequest l_request;
    l_request.tank.id = "tank_l";
    l_request.tank.footprint = l_shape;
    l_request.tank.corner_radius_steps = 2;
    const AquariumBuildResult l_result = buildAquarium(l_request);
    require(l_result.validation.valid() && l_result.navigation.layers.size() == 1 &&
            l_result.navigation.layers.front().area.outer.size() > 6,
        "rounded L footprint did not produce one connected navigation polygon");
    requireValidMeshSet(l_result, "rounded L footprint");

    AquariumBuildRequest u_request;
    u_request.tank.id = "tank_u";
    u_request.tank.footprint.shape = FootprintShape::U;
    u_request.tank.footprint.origin_cell = {3, 4};
    u_request.tank.footprint.width_cells = 7;
    u_request.tank.footprint.depth_cells = 5;
    u_request.tank.footprint.notch_width_cells = 3;
    u_request.tank.footprint.notch_depth_cells = 3;
    u_request.tank.height_steps = 12;
    const AquariumBuildResult u_result = buildAquarium(u_request);
    require(u_result.validation.valid() && footprintCells(u_request.tank.footprint).size() == 26,
        "valid U footprint was rejected or occupied its opening");
    requireValidMeshSet(u_result, "U footprint");
    requireNear(u_result.navigation.layers.front().ceiling_y, 85.4474F,
        "maximum height did not reach the expected discrete water ceiling");

    u_request.tank.footprint.notch_width_cells = 4;
    require(!validateAquarium(u_request).valid(), "U footprint accepted an arm thinner than two cells");
}

void testCornerRadiusIsFittedDeterministically() {
    FootprintDesign footprint;
    footprint.width_cells = 3;
    footprint.depth_cells = 5;
    require(fittedCornerRadiusSteps(footprint, 99) == 6,
        "corner radius did not fit to half the shortest side");
    AquariumBuildRequest request = rectangleRequest();
    request.tank.corner_radius_steps = 99;
    const ValidationReport validation = validateAquarium(request);
    require(validation.valid() && validation.diagnostics.size() == 1 &&
            validation.diagnostics.front().severity == DiagnosticSeverity::Warning &&
            validation.diagnostics.front().code == "corner_radius_fitted",
        "fitted corner radius did not remain buildable with a warning");
    const AquariumBuildResult first = buildAquarium(request);
    const AquariumBuildResult second = buildAquarium(request);
    require(first.content_hash == second.content_hash &&
            first.statistics.vertex_count > 168,
        "rounded geometry is not deterministic or did not add curved segments");
}

void testCornerRadiiAreIndependentAndStable() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.corner_radii = {
        {{0, 0}, 1},
        {{6, 0}, 3},
        {{6, 4}, 0},
        {{0, 4}, 2},
    };
    const AquariumBuildResult result = buildAquarium(request);
    requireValidMeshSet(result, "independent corner radii");
    const auto boundary = footprintBoundaryLocalWorld(
        request.tank.footprint, request.tank.corner_radius_steps,
        request.tank.corner_radii);
    require(boundary.size() == 16U,
        "independent corner radii did not create three rounded arcs and one square corner");
    request.tank.corner_radii.push_back({{0, 0}, 2});
    const ValidationReport duplicate = validateAquarium(request);
    require(!duplicate.valid() && std::any_of(duplicate.diagnostics.begin(),
            duplicate.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == "duplicate_corner_radius";
            }), "duplicate per-corner override was accepted");
}

void testUnsafeAreaDoesNotAllocateFootprintMemory() {
    AquariumBuildRequest request = rectangleRequest();
    request.tank.footprint.width_cells = kMaxFootprintCells;
    request.tank.footprint.depth_cells = kMaxFootprintCells;
    request.tank.corner_radius_steps = 4;
    const ValidationReport report = validateAquarium(request);
    require(!report.valid() && footprintCells(request.tank.footprint).empty(),
        "unsafe footprint area was accepted or allocated");
}

void testStraightAndElbowTunnelsDeriveDrySpace() {
    AquariumBuildRequest straight = rectangleRequest();
    straight.tank.tunnels.push_back({"tunnel_straight", TunnelRoute::Straight,
        {{7, 7}, {8, 7}, {9, 7}, {10, 7}, {11, 7}, {12, 7}, {13, 7}}});
    const AquariumBuildResult straight_result = buildAquarium(straight);
    requireValidMeshSet(straight_result, "straight tunnel");
    require(straight_result.collision.blocked_cells.size() == 24U &&
            straight_result.collision.dry_corridor_cells.size() == 7U,
        "straight tunnel collision did not separate shell and dry corridor cells");
    require(straight_result.navigation.dry_volumes.size() == 1U &&
            straight_result.navigation.layers.size() >= 2U,
        "straight tunnel did not create one dry volume and layered water regions");
    require(straight_result.navigation.dry_volumes.front().area.outer.size() == 4U,
        "straight dry corridor did not remain a cell-aligned rectangle");
    require(straight_result.statistics.water_volume_litres < 80735U,
        "straight dry corridor did not reduce derived water capacity");
    const SemanticMesh& structure = straight_result.meshes.meshes.front();
    require(std::none_of(structure.vertices.begin(), structure.vertices.end(), [](const Vertex& vertex) {
                return std::abs(vertex.position.y - 0.06F) < 0.0001F;
            }), "standard tunnel covered the room floor with a structure strip");
    const SemanticMesh& glass = meshWithMaterial(straight_result, MeshMaterial::Glass);
    require(std::any_of(glass.vertices.begin(), glass.vertices.end(), [](const Vertex& vertex) {
                return vertex.position.y > static_cast<float>(kTunnelCrownWorldUnits) &&
                    std::abs(std::abs(vertex.position.x) - 48.0F) < 0.001F;
            }), "portal glass above the tunnel arch is missing");
    const SemanticMesh& portal_frame =
        meshWithMaterial(straight_result, MeshMaterial::TunnelFrame);
    require(!portal_frame.vertices.empty() &&
            std::any_of(portal_frame.vertices.begin(), portal_frame.vertices.end(),
                [](const Vertex& vertex) {
                    return vertex.position.y > static_cast<float>(kTunnelCrownWorldUnits);
                }),
        "tunnel entrances are missing their raised arch borders");

    AquariumBuildRequest elbow = rectangleRequest();
    elbow.tank.footprint.depth_cells = 6;
    elbow.tank.tunnels.push_back({"tunnel_elbow", TunnelRoute::OneElbow,
        {{7, 8}, {8, 8}, {9, 8}, {10, 8}, {10, 9}, {10, 10}, {10, 11}, {10, 12}}});
    const AquariumBuildResult elbow_result = buildAquarium(elbow);
    requireValidMeshSet(elbow_result, "one-elbow tunnel");
    require(elbow_result.collision.dry_corridor_cells.size() == 8U &&
            elbow_result.navigation.dry_volumes.size() == 1U,
        "one-elbow tunnel did not preserve its ordered discrete route: " +
            std::to_string(elbow_result.collision.dry_corridor_cells.size()));

    AquariumBuildRequest crossing_request = rectangleRequest();
    crossing_request.tank.footprint.depth_cells = 6;
    crossing_request.tank.tunnels.push_back({"tunnel_crossing_horizontal", TunnelRoute::Straight,
        {{7, 9}, {8, 9}, {9, 9}, {10, 9}, {11, 9}, {12, 9}, {13, 9}}});
    crossing_request.tank.tunnels.push_back({"tunnel_crossing_vertical", TunnelRoute::Straight,
        {{10, 6}, {10, 7}, {10, 8}, {10, 9}, {10, 10}, {10, 11}, {10, 12}}});
    const AquariumBuildResult crossing = buildAquarium(crossing_request);
    requireValidMeshSet(crossing, "connected four-way tunnel crossing");
    require(crossing.collision.dry_corridor_cells.size() == 13U,
        "crossing tunnel network did not union its shared walking cell");
    require(crossing.navigation.dry_volumes.size() == 1U,
        "connected crossing did not publish one unioned dry navigation volume");
    crossing_request.tank.depth_steps = 6;
    requireValidMeshSet(buildAquarium(crossing_request),
        "below-floor connected tunnel crossing");

    AquariumBuildRequest tee = rectangleRequest();
    tee.tank.footprint.depth_cells = 6;
    tee.tank.tunnels.push_back({"tunnel_trunk", TunnelRoute::Straight,
        {{10, 6}, {10, 7}, {10, 8}, {10, 9}, {10, 10}, {10, 11}, {10, 12}}});
    tee.tank.tunnels.push_back({"tunnel_branch", TunnelRoute::Straight,
        {{7, 9}, {8, 9}, {9, 9}, {10, 9}}});
    const AquariumBuildResult tee_result = buildAquarium(tee);
    requireValidMeshSet(tee_result, "connected three-exit tunnel junction");
    require(tee_result.collision.dry_corridor_cells.size() == 10U,
        "T-junction did not union its shared walking cell");
    require(tee_result.navigation.dry_volumes.size() == 1U,
        "T-junction did not publish one unioned dry navigation volume");

    AquariumBuildRequest crowded = rectangleRequest();
    crowded.tank.footprint.depth_cells = 6;
    crowded.tank.tunnels.push_back({"tunnel_left", TunnelRoute::Straight,
        {{9, 6}, {9, 7}, {9, 8}, {9, 9}, {9, 10}, {9, 11}, {9, 12}}});
    crowded.tank.tunnels.push_back({"tunnel_right", TunnelRoute::Straight,
        {{10, 6}, {10, 7}, {10, 8}, {10, 9}, {10, 10}, {10, 11}, {10, 12}}});
    const ValidationReport crowded_report = validateAquarium(crowded);
    require(!crowded_report.valid() && std::any_of(crowded_report.diagnostics.begin(),
            crowded_report.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == "tunnel_separation_too_small";
            }), "independent tunnels without a clear separating tile were accepted");

    crowded.tank.tunnels.back().centreline_cells =
        {{12, 6}, {12, 7}, {12, 8}, {12, 9}, {12, 10}, {12, 11}, {12, 12}};
    require(validateAquarium(crowded).valid(),
        "independent tunnels with one clear centreline tile were rejected");

    AquariumBuildRequest crowded_connection = rectangleRequest();
    crowded_connection.tank.footprint.width_cells = 8;
    crowded_connection.tank.footprint.depth_cells = 6;
    crowded_connection.tank.tunnels.push_back({"tunnel_vertical", TunnelRoute::Straight,
        {{9, 6}, {9, 7}, {9, 8}, {9, 9}, {9, 10}, {9, 11}, {9, 12}}});
    crowded_connection.tank.tunnels.push_back({"tunnel_false_branch", TunnelRoute::OneElbow,
        {{7, 8}, {8, 8}, {9, 8}, {10, 8}, {10, 9}, {10, 10}, {10, 11}, {10, 12}}});
    const ValidationReport crowded_connection_report = validateAquarium(crowded_connection);
    require(!crowded_connection_report.valid() && std::any_of(
            crowded_connection_report.diagnostics.begin(),
            crowded_connection_report.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == "tunnel_separation_too_small";
            }), "a junction incorrectly exempted parallel route sections from separation");

    straight.tank.corner_radius_steps = 1;
    const ValidationReport affected_rounding = validateAquarium(straight);
    require(!affected_rounding.valid() && std::any_of(affected_rounding.diagnostics.begin(),
            affected_rounding.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == "tunnel_portal_intersects_rounding";
            }), "rounding that cuts into a tunnel portal was accepted");

    AquariumBuildRequest safe_rounding = rectangleRequest();
    safe_rounding.tank.corner_radius_steps = 4;
    safe_rounding.tank.tunnels.push_back({"tunnel_rounded", TunnelRoute::Straight,
        {{7, 8}, {8, 8}, {9, 8}, {10, 8}, {11, 8}, {12, 8}, {13, 8}}});
    require(tunnelPortalFitsBoundary(safe_rounding.tank, {7, 8}) &&
            tunnelPortalFitsBoundary(safe_rounding.tank, {13, 8}),
        "safe rounded-wall portal was hidden from construction");
    requireValidMeshSet(buildAquarium(safe_rounding),
        "tunnel clear of rounded corners");

    AquariumBuildRequest below_floor_tunnel = rectangleRequest();
    below_floor_tunnel.tank.depth_steps = 6;
    below_floor_tunnel.tank.tunnels = safe_rounding.tank.tunnels;
    const AquariumBuildResult bridge_result = buildAquarium(below_floor_tunnel);
    requireValidMeshSet(bridge_result, "below-floor glass tunnel bridge");
    const SemanticMesh& bridge_sand = meshWithMaterial(bridge_result, MeshMaterial::Sand);
    const SemanticMesh& bridge_glass = meshWithMaterial(bridge_result, MeshMaterial::Glass);
    const SemanticMesh& bridge_frame =
        meshWithMaterial(bridge_result, MeshMaterial::TunnelFrame);
    require(std::any_of(bridge_glass.vertices.begin(), bridge_glass.vertices.end(),
            [](const Vertex& vertex) {
                return std::abs(vertex.position.y - 0.03F) < 0.0001F &&
                    vertex.normal.y > 0.9F;
            }), "below-floor tunnel did not add transparent floor panels");
    require(std::any_of(bridge_frame.vertices.begin(), bridge_frame.vertices.end(),
            [](const Vertex& vertex) {
                return std::abs(vertex.position.y - 1.36F) < 0.0001F;
            }) && std::any_of(bridge_frame.vertices.begin(), bridge_frame.vertices.end(),
            [](const Vertex& vertex) {
                return std::abs(vertex.position.y - 0.24F) < 0.0001F;
            }), "below-floor tunnel did not add raised side rails and cell separators");
    require(bridge_sand.vertices.size() == footprintBoundaryLocalWorld(
            below_floor_tunnel.tank.footprint, 0).size(),
        "below-floor tunnel cut away sand beneath its glass panels");
    require(std::any_of(bridge_result.navigation.layers.begin(),
            bridge_result.navigation.layers.end(), [](const NavigationLayer& layer) {
                return layer.floor_y < 0.0F && layer.ceiling_y < 0.0F;
            }), "below-floor glass bridge did not preserve underwater navigation beneath it");

    AquariumBuildRequest short_tank = rectangleRequest();
    short_tank.tank.height_steps = 4;
    short_tank.tank.tunnels = straight.tank.tunnels;
    short_tank.tank.corner_radius_steps = 0;
    const ValidationReport short_report = validateAquarium(short_tank);
    require(!short_report.valid() && std::any_of(short_report.diagnostics.begin(),
            short_report.diagnostics.end(), [](const auto& diagnostic) {
                return diagnostic.code == "tunnel_tank_too_short";
            }), "tunnel was accepted below the three-level minimum height");
}

void testRoundedTunnelKeepsEverySandRegion() {
    AquariumBuildRequest request;
    request.tank.id = "tank_rounded_tunnel_sand_regression";
    request.tank.footprint.origin_cell = {13, 8};
    request.tank.footprint.width_cells = 7;
    request.tank.footprint.depth_cells = 5;
    request.tank.height_steps = 6;
    request.tank.corner_radii = {
        {{0, 5}, 8},
        {{7, 5}, 6},
    };
    request.tank.tunnels.push_back({"tunnel_south_curve", TunnelRoute::Straight,
        {{16, 13}, {16, 12}, {16, 11}, {16, 10}, {16, 9}, {16, 8}}});

    const AquariumBuildResult result = buildAquarium(request);
    requireValidMeshSet(result, "rounded tunnel beside sand regions");
    require(result.navigation.layers.size() == 3U,
        "rounded tunnel did not split its lower water into two regions");

    std::size_t expected_sand_indices = 0U;
    for (std::size_t layer_index = 0; layer_index + 1U < result.navigation.layers.size();
         ++layer_index) {
        const auto& polygon = result.navigation.layers[layer_index].area.outer;
        require(polygon.size() >= 3U, "rounded tunnel produced an empty water region");
        for (std::size_t point_index = 0; point_index < polygon.size(); ++point_index) {
            require(!nearlySamePoint(polygon[point_index],
                        polygon[(point_index + 1U) % polygon.size()]),
                "rounded tunnel left a duplicate polygon point");
        }
        expected_sand_indices += (polygon.size() - 2U) * 3U;
    }
    const SemanticMesh& sand = meshWithMaterial(result, MeshMaterial::Sand);
    require(sand.indices.size() == expected_sand_indices,
        "rounded tunnel failed to triangulate every visible sand region");
}

} // namespace

int main() {
    try {
        testCanonicalTransforms();
        testValidationIsStable();
        testValidationRejectsUnsafeDimensions();
        testVersionBoundaryRejectsStaleDerivedData();
        testRectangleGolden();
        testBelowFloorDepthAndVolume();
        testQuarterTurnSwapsRectangleAxes();
        testShapeOccupancyRotationAndValidation();
        testSubtractedFootprintsStaySimpleAndConnected();
        testCornerRadiusIsFittedDeterministically();
        testCornerRadiiAreIndependentAndStable();
        testUnsafeAreaDoesNotAllocateFootprintMemory();
        testStraightAndElbowTunnelsDeriveDrySpace();
        testRoundedTunnelKeepsEverySandRegion();
        std::cout << "aquarium_geometry_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_geometry_tests: " << error.what() << '\n';
        return 1;
    }
}
