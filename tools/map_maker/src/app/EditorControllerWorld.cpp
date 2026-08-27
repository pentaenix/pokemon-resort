#include "mapmaker/app/EditorController.hpp"

#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>

namespace pr::mapmaker {
namespace {

std::string mapType(const OpenMapSource* source) {
    if (!source) return "unknown";
    const JsonValue* value = source->document.metadata().get("type");
    return value && value->isString() ? value->asString() : "exterior";
}

bool destinationAnchorExists(
    const ProjectWorkspace& workspace,
    const std::string& map_id,
    const std::string& anchor_id) {
    if (map_id.empty()) return false;
    const OpenMapSource* source = workspace.sourceForMap(map_id);
    if (!source) return false;
    const auto projection = projectValidation(source->document, source->key);
    return canResolveDoorArrival(projection, anchor_id);
}

bool hasReturnConnection(
    const ProjectWorkspace& workspace,
    const std::string& source_map_id,
    const std::string& destination_map_id) {
    const OpenMapSource* destination = workspace.sourceForMap(destination_map_id);
    if (!destination) return false;
    const auto projection = projectValidation(destination->document, destination->key);
    for (const DoorProjection& door : projection.doors) {
        const auto link = std::find_if(projection.links.begin(), projection.links.end(),
            [&](const LinkProjection& candidate) { return candidate.id == door.link_id; });
        if (link != projection.links.end() && link->destination_map_id == source_map_id) return true;
    }
    return false;
}

} // namespace

void EditorController::populateWorldUiModel(EditorUiModel& model) {
    if (world_cache_dirty_) {
        world_maps_cache_.clear();
        world_connections_cache_.clear();
        world_maps_cache_.reserve(workspace_->project().maps().size());
        for (const MapProjectEntry& entry : workspace_->project().maps()) {
            const OpenMapSource* source = workspace_->sourceForMap(entry.id);
            const auto projection = source
                ? projectValidation(source->document, source->key)
                : MapValidationProjection{};
            int errors = 0;
            for (const ValidationDiagnostic& diagnostic : diagnostics_) {
                if ((diagnostic.map_id == entry.id ||
                    (source && diagnostic.map_id == source->key)) &&
                    diagnostic.severity == DiagnosticSeverity::Error) ++errors;
            }
            world_maps_cache_.push_back({
                entry.id,
                entry.name,
                mapType(source),
                entry.grid_x,
                entry.grid_y,
                source ? source->document.width() : 0,
                source ? source->document.height() : 0,
                static_cast<int>(projection.doors.size()),
                errors,
                entry.id == workspace_->activeMapId(),
                source && source->dirty(),
                workspace_->project().isReusedMap(entry.id),
                entry.linked});

            for (const DoorProjection& door : projection.doors) {
                const auto link = std::find_if(projection.links.begin(), projection.links.end(),
                    [&](const LinkProjection& candidate) { return candidate.id == door.link_id; });
                WorldConnectionView connection;
                connection.source_map_id = entry.id;
                connection.source_door_id = door.id;
                if (link != projection.links.end()) {
                    connection.destination_map_id = link->destination_map_id;
                    connection.destination_anchor_id = link->destination_anchor_id;
                }
                connection.broken = !workspace_->project().findMap(connection.destination_map_id) ||
                    !destinationAnchorExists(*workspace_, connection.destination_map_id,
                        connection.destination_anchor_id);
                connection.reciprocal = !connection.destination_map_id.empty() &&
                    hasReturnConnection(*workspace_, entry.id, connection.destination_map_id);
                world_connections_cache_.push_back(std::move(connection));
            }
        }
        world_cache_dirty_ = false;
    }
    model.world_maps = world_maps_cache_;
    model.world_connections = world_connections_cache_;
    for (WorldMapNodeView& node : model.world_maps) {
        node.active = node.id == workspace_->activeMapId();
        const OpenMapSource* source = workspace_->sourceForMap(node.id);
        node.dirty = source && source->dirty();
    }
}

void EditorController::handleWorldEvents(const EditorUiEvents& events) {
    if (events.move_map) {
        const MapMoveRequest move = *events.move_map;
        if (workspace_->moveMap(move.map_id, move.grid_x, move.grid_y)) {
            world_cache_dirty_ = true;
            status_ = "Moved " + move.map_id + " to " +
                std::to_string(move.grid_x) + ", " + std::to_string(move.grid_y);
        }
    }
    if (events.create_map) {
        const NewMapRequest request = *events.create_map;
        const MapProjectEntry* source = workspace_->project().findMap(request.source_map_id);
        NewMapSpec spec;
        spec.id = request.id;
        spec.name = request.name;
        spec.type = request.type;
        spec.width = request.width;
        spec.height = request.height;
        spec.linked = request.linked;
        if (source) {
            spec.grid_x = source->grid_x + request.direction_x;
            spec.grid_y = source->grid_y + request.direction_y;
        } else {
            const auto [grid_x, grid_y] = workspace_->suggestedStandaloneMapPosition();
            spec.grid_x = grid_x;
            spec.grid_y = grid_y;
        }
        std::string error;
        const bool created = request.reuse_source_map_id.empty()
            ? workspace_->createMap(spec, &error)
            : workspace_->createMapInstance(spec.id, spec.name,
                request.reuse_source_map_id, spec.grid_x, spec.grid_y, &error);
        if (created) {
            world_cache_dirty_ = true;
            selection_.clear();
            active_layer_index_ = 0;
            top_down_cache_dirty_ = true;
            view_mode_ = EditorViewMode::TopDown;
            status_ = "Created " + spec.name;
        } else {
            status_ = error;
            log(LogLevel::Error, "world", error);
        }
    }
}

} // namespace pr::mapmaker
