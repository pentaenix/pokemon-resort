#include "mapmaker/app/EditorController.hpp"

#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <string>

namespace pr::mapmaker {
namespace {

std::string uniqueAnchorId(const MapValidationProjection& projection, int x, int y) {
    const std::string base = "anchor_" + std::to_string(x) + "_" + std::to_string(y);
    auto available = [&](const std::string& id) {
        return std::none_of(projection.anchors.begin(), projection.anchors.end(),
            [&](const AnchorProjection& anchor) { return anchor.id == id; });
    };
    if (available(base)) return base;
    for (int suffix = 2; ; ++suffix) {
        const std::string candidate = base + "_" + std::to_string(suffix);
        if (available(candidate)) return candidate;
    }
}

} // namespace

void EditorController::handleTravelEvents(const EditorUiEvents& events) {
    OpenMapSource* source = workspace_->activeSource();
    if (!source) return;

    auto projection = projectValidation(source->document, source->key);
    if (events.select_door_id) {
        const auto found = std::find_if(projection.doors.begin(), projection.doors.end(),
            [&](const DoorProjection& door) { return door.id == *events.select_door_id; });
        if (found != projection.doors.end()) {
            active_asset_id_.clear();
            selection_.select({SelectionKind::DoorTrigger, workspace_->activeMapId(), found->id,
                found->tile_x, found->tile_y, static_cast<int>(active_layer_index_)});
        }
    }
    if (events.select_anchor_id) {
        const auto found = std::find_if(projection.anchors.begin(), projection.anchors.end(),
            [&](const AnchorProjection& anchor) { return anchor.id == *events.select_anchor_id; });
        if (found != projection.anchors.end()) {
            active_asset_id_.clear();
            selection_.select({SelectionKind::Anchor, workspace_->activeMapId(), found->id,
                found->tile_x, found->tile_y, static_cast<int>(active_layer_index_)});
        }
    }

    const auto primary = selection_.primary();
    if (events.add_anchor_at_selection && primary &&
        primary->kind == SelectionKind::TerrainCell) {
        const std::string id = uniqueAnchorId(projection, primary->tile_x, primary->tile_y);
        const int x = primary->tile_x;
        const int y = primary->tile_y;
        executeMutation("Add entry anchor", [=](OwmapDocument& document) {
            (void)addAnchor(document, id, x, y, "south");
        });
        projection = projectValidation(source->document, source->key);
        if (std::any_of(projection.anchors.begin(), projection.anchors.end(),
            [&](const AnchorProjection& anchor) { return anchor.id == id; })) {
            selection_.select({SelectionKind::Anchor, workspace_->activeMapId(), id, x, y,
                static_cast<int>(active_layer_index_)});
        }
    }

    if (!events.move_selection_tile) return;
    const auto selected = selection_.primary();
    if (!selected) return;
    const auto [x, y] = *events.move_selection_tile;
    if (selected->tile_x == x && selected->tile_y == y) return;
    if (selected->kind == SelectionKind::DoorTrigger) {
        const std::string map_id = workspace_->activeMapId();
        executeMutation("Move door", [id = selected->object_id, x, y, map_id](
            OwmapDocument& document) {
            (void)moveDoorWithVisual(document, id, x, y, map_id);
        });
    } else if (selected->kind == SelectionKind::Anchor) {
        executeMutation("Move anchor", [id = selected->object_id, x, y](
            OwmapDocument& document) {
            (void)moveAnchor(document, id, x, y);
        });
    } else {
        return;
    }

    projection = projectValidation(source->document, source->key);
    if (selected->kind == SelectionKind::DoorTrigger) {
        const auto found = std::find_if(projection.doors.begin(), projection.doors.end(),
            [&](const DoorProjection& door) { return door.id == selected->object_id; });
        if (found != projection.doors.end()) {
            SelectionItem updated = *selected;
            updated.tile_x = found->tile_x;
            updated.tile_y = found->tile_y;
            selection_.select(std::move(updated));
        }
    } else {
        const auto found = std::find_if(projection.anchors.begin(), projection.anchors.end(),
            [&](const AnchorProjection& anchor) { return anchor.id == selected->object_id; });
        if (found != projection.anchors.end()) {
            SelectionItem updated = *selected;
            updated.tile_x = found->tile_x;
            updated.tile_y = found->tile_y;
            selection_.select(std::move(updated));
        }
    }
}

} // namespace pr::mapmaker
