#include "mapmaker/app/EditorController.hpp"

#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <cmath>

namespace pr::mapmaker {

void EditorController::rebuildTopDownCache() {
    const OpenMapSource* source = workspace_->activeSource();
    if (!source) {
        composed_tiles_.clear();
        cached_layers_.clear();
        top_down_markers_.clear();
        top_down_cache_dirty_ = false;
        return;
    }
    const int width = source->document.width();
    const int height = source->document.height();
    if (top_down_cache_dirty_) {
        composed_tiles_.assign(static_cast<std::size_t>(width * height), -1);
        cached_layers_ = projectTileLayers(source->document);
        for (const TileLayerProjection& layer : cached_layers_) {
            if (!layer.visible) continue;
            for (int y = 0; y < height && y < static_cast<int>(layer.cells.size()); ++y) {
                const auto& row = layer.cells[static_cast<std::size_t>(y)];
                for (int x = 0; x < width && x < static_cast<int>(row.size()); ++x) {
                    const int tile = row[static_cast<std::size_t>(x)];
                    if (tile >= 0) composed_tiles_[static_cast<std::size_t>(y * width + x)] = tile;
                }
            }
        }
        top_down_cache_dirty_ = false;
    }

    top_down_markers_.clear();
    const auto selected = selection_.primary();
    const auto projected = projectValidation(source->document, source->key);
    for (const DoorProjection& door : projected.doors) {
        top_down_markers_.push_back({TopDownMarkerKind::Door, door.id,
            door.tile_x, door.tile_y, selected &&
                selected->kind == SelectionKind::DoorTrigger && selected->object_id == door.id});
    }
    for (const AnchorProjection& anchor : projected.anchors) {
        top_down_markers_.push_back({TopDownMarkerKind::Anchor, anchor.id,
            anchor.tile_x, anchor.tile_y, selected &&
                selected->kind == SelectionKind::Anchor && selected->object_id == anchor.id});
    }
    const float tile_size = std::max(1.0f, source->document.tileSize());
    for (const ModelPlacementProjection& model : projectModels(source->document)) {
        const int x = static_cast<int>(std::floor(model.x / tile_size));
        const int y = static_cast<int>(std::floor(model.z / tile_size));
        top_down_markers_.push_back({TopDownMarkerKind::Model, model.id, x, y,
            selected && selected->kind == SelectionKind::Model &&
                selected->metadata_index == model.metadata_index});
    }
}

} // namespace pr::mapmaker
