#include "mapmaker/app/EditorController.hpp"

#include "gameplay/world3d/data/GlbModelLoader.hpp"
#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <cmath>

namespace pr::mapmaker {

const ModelTopDownProjection& EditorController::modelTopDownProjection(
    const std::filesystem::path& glb_path) {
    const std::string key = glb_path.lexically_normal().string();
    const auto cached = model_top_down_projections_.find(key);
    if (cached != model_top_down_projections_.end()) return cached->second;

    std::string error;
    const auto mesh = gameplay::world3d::data::loadGlbModel(key, &error);
    std::vector<ModelTopDownPoint> points;
    if (mesh.valid) {
        points.reserve(mesh.triangles.size() * 3U);
        for (const auto& triangle : mesh.triangles) {
            points.push_back({triangle.a.x, triangle.a.z});
            points.push_back({triangle.b.x, triangle.b.z});
            points.push_back({triangle.c.x, triangle.c.z});
        }
    } else {
        log(LogLevel::Warning, "top_down_projection",
            "Could not project " + key + (error.empty() ? std::string{} : ": " + error));
    }
    return model_top_down_projections_.emplace(
        key, buildModelTopDownProjection(points)).first->second;
}

void EditorController::rebuildTopDownCache() {
    const OpenMapSource* source = workspace_->activeSource();
    if (!source) {
        composed_tiles_.clear();
        active_layer_tiles_.clear();
        automatic_collision_.clear();
        cached_layers_.clear();
        top_down_markers_.clear();
        top_down_cache_dirty_ = false;
        return;
    }
    const int width = source->document.width();
    const int height = source->document.height();
    if (top_down_cache_dirty_) {
        composed_tiles_.assign(static_cast<std::size_t>(width * height), -1);
        active_layer_tiles_.assign(static_cast<std::size_t>(width * height), -1);
        automatic_collision_.assign(static_cast<std::size_t>(width * height), 0U);
        cached_layers_ = projectTileLayers(source->document);
        if (!cached_layers_.empty()) {
            active_layer_index_ = std::min(active_layer_index_, cached_layers_.size() - 1U);
            const TileLayerProjection& active_layer = cached_layers_[active_layer_index_];
            for (int y = 0; y < height && y < static_cast<int>(active_layer.cells.size()); ++y) {
                const auto& row = active_layer.cells[static_cast<std::size_t>(y)];
                for (int x = 0; x < width && x < static_cast<int>(row.size()); ++x) {
                    active_layer_tiles_[static_cast<std::size_t>(y * width + x)] =
                        row[static_cast<std::size_t>(x)];
                }
            }
        }
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
        const InteriorRoomProjection room = projectInteriorRoom(source->document);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                automatic_collision_[static_cast<std::size_t>(y * width + x)] =
                    interiorBoundaryCellBlocked(room, width, height, x, y) ? 1U : 0U;
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
        TopDownMarkerView marker{TopDownMarkerKind::Model, model.id, x, y,
            selected && selected->kind == SelectionKind::Model &&
                selected->metadata_index == model.metadata_index};
        marker.center_tile_x = model.x / tile_size;
        marker.center_tile_y = model.z / tile_size;

        std::filesystem::path glb_path = model.glb;
        const ModelAsset* asset = modelAsset(model.id);
        if (glb_path.empty() && asset) glb_path = asset->glb_path;
        if (!glb_path.empty() && glb_path.is_relative()) glb_path = resort_root_ / glb_path;
        ModelTopDownProjection source_projection;
        if (!glb_path.empty()) source_projection = modelTopDownProjection(glb_path);
        if (!source_projection.valid && asset) {
            const float half_width = asset->footprint_width * tile_size * 0.5f;
            const float half_depth = asset->footprint_depth * tile_size * 0.5f;
            const std::array<ModelTopDownPoint, 4> footprint{{
                {-half_width, -half_depth}, {half_width, -half_depth},
                {half_width, half_depth}, {-half_width, half_depth}}};
            source_projection = buildModelTopDownProjection(footprint);
        }
        const auto placed = placeModelTopDownProjection(
            source_projection, model.yaw_deg, model.scale, tile_size);
        marker.projected_width_tiles = placed.width_tiles;
        marker.projected_depth_tiles = placed.depth_tiles;
        marker.model_outline_tiles.reserve(placed.outline_tiles.size());
        for (const auto& point : placed.outline_tiles) {
            marker.model_outline_tiles.push_back({point.x, point.z});
        }
        top_down_markers_.push_back(std::move(marker));
    }
}

} // namespace pr::mapmaker
