#include "mapmaker/app/EditorController.hpp"

#include "gameplay/world3d/terrain/TerrainSurface.hpp"
#include "mapmaker/assets/BgfxThumbnailCache.hpp"
#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>

namespace pr::mapmaker {
namespace {

std::string severityName(DiagnosticSeverity severity) {
    switch (severity) {
        case DiagnosticSeverity::Info: return "info";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Error: return "error";
    }
    return "error";
}

std::string decimalPair(float first, float second, const char* suffix) {
    char text[96]{};
    std::snprintf(text, sizeof(text), "%.2f x %.2f %s", first, second, suffix);
    return text;
}

std::string decimalValue(float value, const char* suffix) {
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.2f%s", value, suffix);
    return text;
}

bool tileOverlay(
    const gameplay::world3d::SceneConfig& scene,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int x,
    int y,
    int viewport_width,
    int viewport_height,
    std::uint32_t color,
    float thickness,
    ViewportOverlayQuad& result) {
    if (x < 0 || y < 0 || x >= scene.grid.width || y >= scene.grid.height) return false;
    float heights[4]{};
    gameplay::world3d::terrain::fillTileCornerHeights(scene, x, y, heights);
    const float size = scene.grid.tile_size;
    const std::array<gameplay::world3d::camera::Vec3, 4> corners{{
        {x * size, heights[0], y * size},
        {(x + 1) * size, heights[1], y * size},
        {(x + 1) * size, heights[2], (y + 1) * size},
        {x * size, heights[3], (y + 1) * size}}};
    for (std::size_t index = 0; index < corners.size(); ++index) {
        float depth = 0.0f;
        if (!camera.worldToScreen(corners[index], viewport_width, viewport_height,
            result.xy[index * 2], result.xy[index * 2 + 1], depth) || depth <= 0.0f) return false;
    }
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (std::size_t index = 0; index < corners.size(); ++index) {
        min_x = std::min(min_x, result.xy[index * 2]);
        min_y = std::min(min_y, result.xy[index * 2 + 1]);
        max_x = std::max(max_x, result.xy[index * 2]);
        max_y = std::max(max_y, result.xy[index * 2 + 1]);
    }
    if (max_x < 0.0f || max_y < 0.0f || min_x > viewport_width || min_y > viewport_height) {
        return false;
    }
    result.color = color;
    result.thickness = thickness;
    return true;
}

} // namespace

void EditorController::rebuildAssetViews() {
    asset_views_.clear();
    tile_categories_.clear();
    asset_views_.reserve(tile_catalog_->tiles().size() + model_catalog_.size() +
        tile_catalog_->smartSets().size());
    for (const TileAsset& tile : tile_catalog_->tiles()) {
        const std::string category = tile.interior_role.empty()
            ? tile.tab_id : "interior_" + tile.interior_role;
        asset_views_.push_back({tile.key, tile.name, category,
            tile.door ? AssetKind::Door : AssetKind::Tile, tile.resort_tile_id,
            tile.width, tile.height, tile.door});
    }
    asset_views_.push_back({"invisible_door_trigger", "Invisible door trigger", "triggers",
        AssetKind::Door, -1, 1, 1, true});
    for (const ModelAsset& model : model_catalog_) {
        asset_views_.push_back({model.id, model.display_name, "models", AssetKind::Model, -1,
            model.footprint_width, model.footprint_depth, false});
    }
    for (const SmartTileSet& smart : tile_catalog_->smartSets()) {
        asset_views_.push_back({smart.id, smart.name, "smart", AssetKind::SmartSet, -1,
            std::max(1, smart.width), std::max(1, smart.height), false});
    }
    for (const TileAssetTab& tab : tile_catalog_->tabs()) tile_categories_.push_back(tab.id);
    for (const TileAsset& tile : tile_catalog_->tiles()) {
        if (tile.interior_role.empty()) continue;
        const std::string category = "interior_" + tile.interior_role;
        if (std::find(tile_categories_.begin(), tile_categories_.end(), category) ==
            tile_categories_.end()) tile_categories_.push_back(category);
    }
    tile_categories_.push_back("triggers");
}

const AssetView* EditorController::activeAsset(const std::vector<AssetView>& list) const {
    const auto found = std::find_if(list.begin(), list.end(), [&](const AssetView& asset) {
        return asset.id == active_asset_id_ && asset.kind == active_asset_kind_;
    });
    return found == list.end() ? nullptr : &*found;
}

const ModelAsset* EditorController::modelAsset(const std::string& id) const {
    const auto found = std::find_if(model_catalog_.begin(), model_catalog_.end(),
        [&](const ModelAsset& asset) { return asset.id == id; });
    return found == model_catalog_.end() ? nullptr : &*found;
}

std::optional<std::size_t> EditorController::placedModelIndex(const SelectionItem& item) const {
    const OpenMapSource* source = workspace_->activeSource();
    if (!source) return std::nullopt;
    const auto models = projectModels(source->document);
    if (item.metadata_index != std::numeric_limits<std::size_t>::max()) {
        const auto exact = std::find_if(models.begin(), models.end(), [&](const auto& model) {
            return model.metadata_index == item.metadata_index;
        });
        if (exact != models.end()) return exact->metadata_index;
    }
    const auto found = std::find_if(models.begin(), models.end(),
        [&](const ModelPlacementProjection& model) { return model.id == item.object_id; });
    return found == models.end() ? std::nullopt
        : std::optional<std::size_t>(found->metadata_index);
}

EditorUiModel EditorController::buildUiModel(const FrameMetrics& metrics) {
    EditorUiModel model;
    rebuildTopDownCache();
    model.project_name = workspace_->project().name();
    model.assets = asset_views_;
    model.tile_categories = tile_categories_;
    const OpenMapSource* active_source = workspace_->activeSource();
    for (const MapProjectEntry& entry : workspace_->project().maps()) {
        const OpenMapSource* source = workspace_->sourceForMap(entry.id);
        model.maps.push_back({entry.id, entry.name, entry.id == workspace_->activeMapId(),
            source && source->dirty(), workspace_->project().isReusedMap(entry.id)});
    }
    populateWorldUiModel(model);
    if (active_source) {
        if (!cached_layers_.empty()) {
            active_layer_index_ = std::min(active_layer_index_, cached_layers_.size() - 1U);
        }
        for (std::size_t index = 0; index < cached_layers_.size(); ++index) {
            model.layers.push_back({index, cached_layers_[index].id, cached_layers_[index].name,
                index == active_layer_index_, cached_layers_[index].visible});
        }
        model.can_undo = view_mode_ == EditorViewMode::World
            ? workspace_->canUndoProject() : active_source->commands.canUndo();
        model.can_redo = view_mode_ == EditorViewMode::World
            ? workspace_->canRedoProject() : active_source->commands.canRedo();

        const auto projected = projectValidation(active_source->document, active_source->key);
        const auto primary = selection_.primary();
        for (const DoorProjection& door : projected.doors) {
            const auto link = std::find_if(projected.links.begin(), projected.links.end(),
                [&](const LinkProjection& value) { return value.id == door.link_id; });
            std::string summary = "Unlinked";
            if (link != projected.links.end() && !link->destination_map_id.empty()) {
                summary = "to " + link->destination_map_id;
                if (!link->destination_anchor_id.empty()) summary += " / " + link->destination_anchor_id;
            }
            model.placed_doors.push_back({door.id, std::move(summary), door.tile_x, door.tile_y,
                primary && primary->kind == SelectionKind::DoorTrigger &&
                    primary->object_id == door.id});
        }
        for (const AnchorProjection& anchor : projected.anchors) {
            std::string summary = "faces " + anchor.facing;
            if (anchor.id == "entry") summary += " • proposed door destination";
            model.placed_anchors.push_back({anchor.id, std::move(summary),
                anchor.tile_x, anchor.tile_y,
                primary && primary->kind == SelectionKind::Anchor &&
                    primary->object_id == anchor.id});
        }
    }
    model.active_asset_kind = active_asset_kind_;
    model.active_asset_id = active_asset_id_;
    model.active_category = active_category_;
    model.active_tool_text = active_asset_id_.empty() ? "Select" : "Place " + active_asset_id_;
    if (active_tool_ == EditorTool::Height) {
        model.active_tool_text = "Paint height " + std::to_string(height_brush_value_);
    } else if (active_tool_ == EditorTool::Collision) {
        model.active_tool_text = collision_brush_value_ ? "Paint blocked" : "Paint walkable";
    }
    model.active_tool = active_tool_;
    model.view_mode = view_mode_;
    model.height_brush_value = height_brush_value_;
    model.collision_brush_value = collision_brush_value_;
    model.dirty = workspace_->dirty();
    model.animations_enabled = preview_->animationsEnabled();
    model.animation_time_seconds = preview_->animationTimeSeconds();
    model.preview_stale = preview_reload_pending_;
    model.grid_overlay = grid_overlay_;
    model.collision_overlay = collision_overlay_;
    model.fps = metrics.fps();
    model.frame_ms_p95 = metrics.frameMillisecondsP95();
    model.input_latency_ms = metrics.inputLatencyMilliseconds();
    model.scene_rebuild_count = preview_->sceneRebuildCount();
    model.viewport_texture = viewport_texture_.valid() ? viewport_texture_.texture_handle_idx : UINT16_MAX;
    model.viewport_texture_width = viewport_texture_.width;
    model.viewport_texture_height = viewport_texture_.height;
    model.viewport_origin_bottom_left = viewport_texture_.origin_bottom_left;
    model.status_text = status_;
    model.tile_thumbnail = [this](int tile_id) { return thumbnails_->textureForTile(tile_id); };
    if (active_source) {
        model.top_down.map_id = workspace_->activeMapId();
        model.top_down.width = active_source->document.width();
        model.top_down.height = active_source->document.height();
        model.top_down.composed_tiles = composed_tiles_;
        model.top_down.active_layer_tiles = active_layer_tiles_;
        model.top_down.heights = active_source->document.heights();
        model.top_down.specials = active_source->document.specials();
        model.top_down.collision = active_source->document.collision();
        model.top_down.automatic_collision = automatic_collision_;
        model.top_down.markers = top_down_markers_;
        const InteriorRoomProjection interior = projectInteriorRoom(active_source->document);
        model.top_down.default_interior_room = interior.default_room;
        model.top_down.default_wall_height_tiles = interior.wall_height_tiles;
        model.top_down.default_room_inset_tiles = interior.walkable_inset_tiles;
        model.top_down.default_wall_offset_tiles = interior.wall_face_offset_tiles;
        model.top_down.default_entry_extension_depth_tiles =
            interior.entry_extension_depth_tiles;
        model.top_down.default_room_black_top_cap = interior.black_top_cap;
        for (const InteriorOpeningProjection& opening : interior.openings) {
            model.top_down.interior_openings.push_back(
                {opening.edge, opening.from, opening.to});
        }
        model.top_down.focus_tile_x = top_down_focus_x_;
        model.top_down.focus_tile_y = top_down_focus_y_;
        model.top_down.focus_serial = top_down_focus_serial_;
    }
    for (const ValidationDiagnostic& diagnostic : diagnostics_) {
        model.validation.push_back({severityName(diagnostic.severity), diagnostic.code,
            diagnostic.message});
    }
    if (const auto primary = selection_.primary()) {
        model.selection.id = primary->object_id;
        model.selection.tile_x = primary->tile_x;
        model.selection.tile_y = primary->tile_y;
        model.selection.title = primary->object_id.empty() ? "Terrain cell" : primary->object_id;
        model.selection.kind = primary->kind == SelectionKind::Model ? InspectorSelectionKind::Model
            : primary->kind == SelectionKind::DoorTrigger ? InspectorSelectionKind::Door
            : primary->kind == SelectionKind::Anchor ? InspectorSelectionKind::Anchor
            : InspectorSelectionKind::TerrainCell;
        model.selection.fields.push_back({"Map", primary->map_id, false});
        model.selection.fields.push_back({"Layer", std::to_string(primary->layer), false});
        if (primary->kind == SelectionKind::Anchor && active_source) {
            const auto anchors = projectValidation(
                active_source->document, active_source->key).anchors;
            const auto anchor = std::find_if(anchors.begin(), anchors.end(),
                [&](const AnchorProjection& value) {
                    return value.id == primary->object_id;
                });
            if (anchor != anchors.end()) {
                model.selection.anchor_facing = anchor->facing;
                model.selection.fields.push_back({"Role",
                    anchor->id == "entry" ? "Default center entry" : "Arrival node", false});
            }
        }
        if (primary->kind == SelectionKind::TerrainCell && active_source &&
            primary->tile_x >= 0 && primary->tile_y >= 0 &&
            primary->tile_x < active_source->document.width() &&
            primary->tile_y < active_source->document.height()) {
            const auto x = static_cast<std::uint16_t>(primary->tile_x);
            const auto y = static_cast<std::uint16_t>(primary->tile_y);
            model.selection.terrain_editable = true;
            model.selection.height = active_source->document.heightAt(x, y);
            model.selection.special = active_source->document.specialAt(x, y);
            model.selection.collision = active_source->document.collisionAt(x, y) != 0U;
            const std::size_t cell_index =
                static_cast<std::size_t>(primary->tile_y) * active_source->document.width() + x;
            model.selection.automatic_collision =
                cell_index < automatic_collision_.size() && automatic_collision_[cell_index] != 0U;
            if (model.selection.special == 14) {
                const auto spawn_tiles = projectSpawnTiles(active_source->document);
                const auto spawn = std::find_if(spawn_tiles.begin(), spawn_tiles.end(),
                    [&](const SpawnTileProjection& value) {
                        return value.tile_x == primary->tile_x && value.tile_y == primary->tile_y;
                    });
                model.selection.spawn_tile_use = spawn == spawn_tiles.end()
                    ? "pokemon_random_from_boxes" : spawn->allows;
            }
            int tile_id = -1;
            if (active_layer_index_ < cached_layers_.size() &&
                y < cached_layers_[active_layer_index_].cells.size() &&
                x < cached_layers_[active_layer_index_].cells[y].size()) {
                tile_id = cached_layers_[active_layer_index_].cells[y][x];
            }
            model.selection.fields.push_back({"Tile",
                tile_id < 0 ? std::string("Empty") : std::to_string(tile_id), false});
            if (model.selection.automatic_collision) {
                model.selection.fields.push_back({"Runtime collision",
                    "Blocked by default room wall", true});
            }
        }
        if (primary->kind == SelectionKind::Model && active_source) {
            const auto marker = std::find_if(top_down_markers_.begin(), top_down_markers_.end(),
                [](const TopDownMarkerView& value) {
                    return value.kind == TopDownMarkerKind::Model && value.selected;
                });
            if (marker != top_down_markers_.end()) {
                model.selection.fields.push_back({"Top-down size", decimalPair(
                    marker->projected_width_tiles, marker->projected_depth_tiles, "tiles"), false});
            }
            const auto placements = projectModels(active_source->document);
            const auto placement = std::find_if(placements.begin(), placements.end(),
                [&](const ModelPlacementProjection& value) {
                    return value.metadata_index == primary->metadata_index;
                });
            if (placement != placements.end()) {
                model.selection.fields.push_back({"Rotation", decimalValue(placement->yaw_deg, " deg"), false});
                model.selection.fields.push_back({"Scale", decimalValue(placement->scale, "x"), false});
            }
        }
        if (primary->kind == SelectionKind::DoorTrigger && active_source) {
            const MapValidationProjection projected = projectValidation(
                active_source->document, active_source->key);
            const auto door = std::find_if(projected.doors.begin(), projected.doors.end(),
                [&](const DoorProjection& value) { return value.id == primary->object_id; });
            if (door != projected.doors.end()) {
                model.door_editor.active = true;
                model.door_editor.door_id = door->id;
                model.door_editor.direction = door->allowed_directions.empty()
                    ? std::string{} : door->allowed_directions.front();
                model.door_editor.script_id = door->script_id;
                const auto link = std::find_if(projected.links.begin(), projected.links.end(),
                    [&](const LinkProjection& value) { return value.id == door->link_id; });
                if (link != projected.links.end()) {
                    model.door_editor.destination_map_id = link->destination_map_id;
                    model.door_editor.destination_anchor_id = link->destination_anchor_id;
                }
                model.selection.fields.push_back({"Link", door->link_id, door->link_id.empty()});
                model.selection.fields.push_back({"Script", door->script_id, door->script_id.empty()});
                for (const MapProjectEntry& entry : workspace_->project().maps()) {
                    model.door_editor.map_choices.push_back({entry.id, entry.name});
                }
                if (!model.door_editor.destination_map_id.empty()) {
                    if (const OpenMapSource* destination = workspace_->sourceForMap(
                        model.door_editor.destination_map_id)) {
                        const auto destination_projection = projectValidation(
                            destination->document, destination->key);
                        model.door_editor.automatic_arrival =
                            hasAutomaticDoorArrival(destination_projection);
                        if (model.door_editor.automatic_arrival) {
                            model.door_editor.automatic_arrival_label =
                                destination_projection.anchors.size() == 1U
                                ? "Automatic — only arrival anchor"
                                : "Automatic — only doorway";
                            const bool explicit_anchor_exists = std::any_of(
                                destination_projection.anchors.begin(),
                                destination_projection.anchors.end(),
                                [&](const AnchorProjection& anchor) {
                                    return anchor.id == model.door_editor.destination_anchor_id;
                                });
                            if (!explicit_anchor_exists) {
                                model.door_editor.destination_anchor_id.clear();
                            }
                        }
                        for (const AnchorProjection& anchor : destination_projection.anchors) {
                            model.door_editor.anchor_choices.push_back({anchor.id,
                                anchor.id == "entry"
                                    ? "entry (default center)" : anchor.id});
                        }
                    }
                }
                model.door_editor.script_choices = door_script_choices_;
            }
        }
    }
    const auto* scene = preview_->scene();
    const auto* camera = preview_->camera();
    if (scene && camera && viewport_texture_.valid()) {
        const int cell_count = scene->grid.width * scene->grid.height;
        if ((grid_overlay_ || collision_overlay_) && cell_count <= 4096) {
            for (int y = 0; y < scene->grid.height; ++y) {
                for (int x = 0; x < scene->grid.width; ++x) {
                    const bool blocked = y < static_cast<int>(scene->terrain.collision.size()) &&
                        x < static_cast<int>(scene->terrain.collision[static_cast<std::size_t>(y)].size()) &&
                        scene->terrain.collision[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0;
                    if (!grid_overlay_ && !(collision_overlay_ && blocked)) continue;
                    ViewportOverlayQuad cell;
                    const std::uint32_t color = collision_overlay_ && blocked
                        ? 0x996060ffU : 0x3342c8ffU;
                    if (tileOverlay(*scene, *camera, x, y, viewport_texture_.width,
                        viewport_texture_.height, color, 1.0f, cell)) {
                        model.viewport_overlays.push_back(cell);
                    }
                }
            }
        }
        if (hovered_cell_) {
            ViewportOverlayQuad hover;
            if (tileOverlay(*scene, *camera, hovered_cell_->first, hovered_cell_->second,
                viewport_texture_.width, viewport_texture_.height, 0xff42c8ffU, 2.0f, hover)) {
                model.viewport_overlays.push_back(hover);
            }
        }
        if (const auto primary = selection_.primary()) {
            ViewportOverlayQuad selected;
            if (tileOverlay(*scene, *camera, primary->tile_x, primary->tile_y,
                viewport_texture_.width, viewport_texture_.height, 0xffe0a83aU, 3.0f, selected)) {
                model.viewport_overlays.push_back(selected);
            }
        }
    }
    return model;
}

} // namespace pr::mapmaker
