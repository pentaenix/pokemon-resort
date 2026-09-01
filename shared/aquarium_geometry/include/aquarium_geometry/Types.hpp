#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pr::aquarium::geometry {

inline constexpr std::uint32_t kKernelAbiVersion = 6;
inline constexpr std::uint32_t kDesignSchemaVersion = 4;
inline constexpr std::int32_t kWorldUnitsPerCell = 16;
inline constexpr std::int32_t kVerticalStepWorldUnits = 8;
inline constexpr std::int32_t kRadiusStepWorldUnits = 4;
inline constexpr std::int32_t kPlacementOffsetWorldUnits = 8;
inline constexpr std::int32_t kGlassThicknessMilliWorldUnits = 880;
inline constexpr std::int32_t kMaxFootprintCells = 4096;

enum class FootprintShape : std::uint8_t {
    Rectangle,
    L,
    U,
};

enum class TunnelRoute : std::uint8_t {
    Straight,
    OneElbow,
};

enum class MeshMaterial : std::uint8_t {
    Structure,
    Sand,
    WaterVolume,
    WaterSurface,
    Glass,
};

enum class DiagnosticSeverity : std::uint8_t {
    Warning,
    Error,
};

struct GridCell {
    std::int32_t column = 0;
    std::int32_t row = 0;
};

struct CornerRadiusDesign {
    // Local boundary-grid vertex, relative to the footprint origin.
    GridCell vertex;
    std::int32_t radius_steps = 0;
};

struct CellPoint {
    std::int32_t column = 0;
    std::int32_t row = 0;
};

struct TunnelDesign {
    std::string id;
    TunnelRoute route = TunnelRoute::Straight;
    std::vector<CellPoint> centreline_cells;
};

struct FootprintDesign {
    FootprintShape shape = FootprintShape::Rectangle;
    GridCell origin_cell;
    std::int32_t width_cells = 3;
    std::int32_t depth_cells = 3;
    std::int32_t rotation_quarter_turns = 0;
    std::int32_t notch_width_cells = 0;
    std::int32_t notch_depth_cells = 0;
    // Canonical, unrotated cells removed from the outer bounds. Resort's
    // subtract tool only permits cuts connected to the exterior, so the
    // resulting footprint remains one simple water polygon.
    std::vector<GridCell> subtracted_cells;
};

struct TankDesign {
    std::string id;
    FootprintDesign footprint;
    std::int32_t height_steps = 8;
    // Zero is the original standard tank profile. Positive values extend the
    // basin below the room floor in the same half-cell increments as height.
    std::int32_t depth_steps = 0;
    std::int32_t corner_radius_steps = 0;
    std::vector<CornerRadiusDesign> corner_radii;
    std::vector<TunnelDesign> tunnels;
};

struct AquariumBuildRequest {
    std::uint32_t kernel_abi_version = kKernelAbiVersion;
    std::uint32_t design_schema_version = kDesignSchemaVersion;
    TankDesign tank;
};

struct Vec2 {
    float x = 0.0F;
    float y = 0.0F;
};

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Vertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

struct SemanticMesh {
    MeshMaterial material = MeshMaterial::Structure;
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct SemanticMeshSet {
    std::vector<SemanticMesh> meshes;
};

struct CollisionCellSet {
    std::vector<GridCell> blocked_cells;
};

struct Polygon2 {
    std::vector<Vec2> outer;
    std::vector<std::vector<Vec2>> holes;
};

struct NavigationLayer {
    float floor_y = 0.0F;
    float ceiling_y = 0.0F;
    Polygon2 area;
};

struct NavigationVolumeSet {
    std::vector<NavigationLayer> layers;
    std::vector<Vec3> suggested_spawns;
};

struct ValidationDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string path;
    std::string message;
};

struct ValidationReport {
    std::vector<ValidationDiagnostic> diagnostics;

    bool valid() const;
};

struct GeometryStatistics {
    std::uint32_t mesh_count = 0;
    std::uint32_t vertex_count = 0;
    std::uint32_t index_count = 0;
    std::uint32_t triangle_count = 0;
    std::uint32_t collision_cell_count = 0;
    std::uint32_t navigation_layer_count = 0;
    // Derived usable water capacity. This is deliberately not authored in the
    // design document; stock policies can trust the kernel to rebuild it.
    std::uint64_t water_volume_litres = 0;
};

struct AquariumBuildResult {
    ValidationReport validation;
    SemanticMeshSet meshes;
    CollisionCellSet collision;
    NavigationVolumeSet navigation;
    GeometryStatistics statistics;
    std::string content_hash;
};

} // namespace pr::aquarium::geometry
