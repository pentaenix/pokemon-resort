#include "AquariumGeometryJsonAdapter.hpp"

#include "aquarium_geometry/Kernel.hpp"
#include "core/config/Json.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include <string>

namespace pr::aquarium::geometry::wasm {

namespace construction = pr::gameplay::world3d::aquarium::construction;

namespace {

JsonValue number(double value) {
    return JsonValue(value);
}

JsonValue serializeDiagnostic(const ValidationDiagnostic& diagnostic) {
    return JsonValue(JsonValue::Object{
        {"code", JsonValue(diagnostic.code)},
        {"message", JsonValue(diagnostic.message)},
        {"path", JsonValue(diagnostic.path)},
        {"severity", JsonValue(std::string(
             diagnostic.severity == DiagnosticSeverity::Error ? "error" : "warning"))},
    });
}

JsonValue serializeMesh(const SemanticMesh& mesh) {
    JsonValue::Array vertices;
    vertices.reserve(mesh.vertices.size() * 8U);
    for (const Vertex& vertex : mesh.vertices) {
        vertices.emplace_back(static_cast<double>(vertex.position.x));
        vertices.emplace_back(static_cast<double>(vertex.position.y));
        vertices.emplace_back(static_cast<double>(vertex.position.z));
        vertices.emplace_back(static_cast<double>(vertex.normal.x));
        vertices.emplace_back(static_cast<double>(vertex.normal.y));
        vertices.emplace_back(static_cast<double>(vertex.normal.z));
        vertices.emplace_back(static_cast<double>(vertex.uv.x));
        vertices.emplace_back(static_cast<double>(vertex.uv.y));
    }
    JsonValue::Array indices;
    indices.reserve(mesh.indices.size());
    for (std::uint32_t index : mesh.indices) indices.emplace_back(static_cast<double>(index));
    return JsonValue(JsonValue::Object{
        {"indices", JsonValue(std::move(indices))},
        {"material", JsonValue(std::string(meshMaterialName(mesh.material)))},
        {"vertexStrideFloats", number(8.0)},
        {"vertices", JsonValue(std::move(vertices))},
    });
}

JsonValue invalidResult(
    const std::vector<std::string>& diagnostics,
    const std::string& compatibility) {
    JsonValue::Array entries;
    for (const std::string& diagnostic : diagnostics) entries.emplace_back(diagnostic);
    return JsonValue(JsonValue::Object{
        {"compatibility", JsonValue(compatibility)},
        {"designSchemaVersion", number(kDesignSchemaVersion)},
        {"diagnostics", JsonValue(std::move(entries))},
        {"kernelAbiVersion", number(kKernelAbiVersion)},
        {"valid", JsonValue(false)},
    });
}

JsonValue serializeBuild(const AquariumBuildResult& result) {
    JsonValue::Array diagnostics;
    for (const ValidationDiagnostic& diagnostic : result.validation.diagnostics) {
        diagnostics.push_back(serializeDiagnostic(diagnostic));
    }
    JsonValue::Array meshes;
    for (const SemanticMesh& mesh : result.meshes.meshes) meshes.push_back(serializeMesh(mesh));
    JsonValue::Array collision;
    for (const GridCell& cell : result.collision.blocked_cells) {
        collision.emplace_back(JsonValue::Object{
            {"column", number(cell.column)},
            {"row", number(cell.row)},
        });
    }
    JsonValue::Array dry_corridor;
    for (const GridCell& cell : result.collision.dry_corridor_cells) {
        dry_corridor.emplace_back(JsonValue::Object{
            {"column", number(cell.column)},
            {"row", number(cell.row)},
        });
    }
    JsonValue::Array layers;
    for (const NavigationLayer& layer : result.navigation.layers) {
        JsonValue::Array outer;
        for (const Vec2& point : layer.area.outer) {
            outer.emplace_back(JsonValue::Array{number(point.x), number(point.y)});
        }
        JsonValue::Array holes;
        for (const std::vector<Vec2>& hole : layer.area.holes) {
            JsonValue::Array points;
            for (const Vec2& point : hole) points.emplace_back(JsonValue::Array{number(point.x), number(point.y)});
            holes.emplace_back(std::move(points));
        }
        layers.emplace_back(JsonValue::Object{
            {"ceilingY", number(layer.ceiling_y)},
            {"floorY", number(layer.floor_y)},
            {"holes", JsonValue(std::move(holes))},
            {"outer", JsonValue(std::move(outer))},
        });
    }
    JsonValue::Array spawns;
    for (const Vec3& spawn : result.navigation.suggested_spawns) {
        spawns.emplace_back(JsonValue::Array{number(spawn.x), number(spawn.y), number(spawn.z)});
    }
    JsonValue::Array dry_volumes;
    for (const NavigationDryVolume& volume : result.navigation.dry_volumes) {
        JsonValue::Array outer;
        for (const Vec2& point : volume.area.outer) {
            outer.emplace_back(JsonValue::Array{number(point.x), number(point.y)});
        }
        dry_volumes.emplace_back(JsonValue::Object{
            {"ceilingY", number(volume.ceiling_y)},
            {"floorY", number(volume.floor_y)},
            {"outer", JsonValue(std::move(outer))},
            {"tunnelId", JsonValue(volume.tunnel_id)},
        });
    }
    const GeometryStatistics& stats = result.statistics;
    return JsonValue(JsonValue::Object{
        {"collision", JsonValue(JsonValue::Object{
             {"blockedCells", JsonValue(std::move(collision))},
             {"dryCorridorCells", JsonValue(std::move(dry_corridor))},
         })},
        {"compatibility", JsonValue(std::string("compatible"))},
        {"contentHash", JsonValue(result.content_hash)},
        {"designSchemaVersion", number(kDesignSchemaVersion)},
        {"diagnostics", JsonValue(std::move(diagnostics))},
        {"kernelAbiVersion", number(kKernelAbiVersion)},
        {"meshes", JsonValue(std::move(meshes))},
        {"navigation", JsonValue(JsonValue::Object{
             {"dryVolumes", JsonValue(std::move(dry_volumes))},
             {"layers", JsonValue(std::move(layers))},
             {"suggestedSpawns", JsonValue(std::move(spawns))},
         })},
        {"statistics", JsonValue(JsonValue::Object{
             {"collisionCellCount", number(stats.collision_cell_count)},
             {"indexCount", number(stats.index_count)},
             {"meshCount", number(stats.mesh_count)},
             {"navigationLayerCount", number(stats.navigation_layer_count)},
             {"triangleCount", number(stats.triangle_count)},
             {"vertexCount", number(stats.vertex_count)},
             {"waterVolumeLitres", number(static_cast<double>(stats.water_volume_litres))},
         })},
        {"valid", JsonValue(result.validation.valid())},
    });
}

} // namespace

std::string buildAquariumDocumentJson(const std::string& design_json) {
    const construction::AquariumDesignLoadResult loaded =
        construction::parseAquariumDesign(design_json);
    if (loaded.status == construction::AquariumDesignLoadStatus::NewerVersion) {
        return serializeJsonValue(invalidResult(loaded.diagnostics, "newer-version"));
    }
    if (loaded.status != construction::AquariumDesignLoadStatus::Loaded || !loaded.document) {
        return serializeJsonValue(invalidResult(loaded.diagnostics, "invalid"));
    }
    if (loaded.document->tanks.size() != 1) {
        return serializeJsonValue(invalidResult(
            {"milestone_0_adapter_requires_exactly_one_tank"}, "invalid"));
    }
    AquariumBuildRequest request;
    request.tank = loaded.document->tanks.front();
    return serializeJsonValue(serializeBuild(buildAquarium(request)));
}

} // namespace pr::aquarium::geometry::wasm
