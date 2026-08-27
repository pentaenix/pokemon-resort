#include "mapmaker/app/EditorController.hpp"

#include "gameplay/world3d/scripts/OverworldScript.hpp"
#include "mapmaker/assets/BgfxThumbnailCache.hpp"
#include "mapmaker/document/MapMetadataEditing.hpp"
#include "mapmaker/interaction/WorldPicker.hpp"

#include <algorithm>
#include <cmath>

namespace pr::mapmaker {
namespace {

std::size_t configuredLayer(const OwmapDocument& document) {
    const JsonValue* tile_layers = document.metadata().get("tileLayers");
    const JsonValue* active = tile_layers && tile_layers->isObject()
        ? tile_layers->get("activeLayer") : nullptr;
    return active && active->isNumber()
        ? static_cast<std::size_t>(std::max(0.0, active->asNumber())) : 0U;
}

std::string configuredLayerId(const OwmapDocument& document, std::size_t layer_index) {
    const auto layers = projectTileLayers(document);
    return layer_index < layers.size() ? layers[layer_index].id : std::string{};
}

float terrainHeight(const OwmapDocument& document, int x, int y) {
    if (x < 0 || y < 0 || x >= document.width() || y >= document.height()) return 0.0f;
    return static_cast<float>(document.heightAt(
        static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y))) *
        std::max(1.0f, document.tileSize());
}

} // namespace

EditorController::EditorController(
    std::filesystem::path resort_root,
    ProjectWorkspace& workspace,
    ExactWorldPreview& preview,
    RtpksEditorCatalog& tile_catalog,
    std::vector<ModelAsset> model_catalog,
    BgfxThumbnailCache& thumbnails,
    AutosaveRecovery& recovery,
    StructuredLogger& logger)
    : resort_root_(std::move(resort_root)), workspace_(&workspace), preview_(&preview),
      tile_catalog_(&tile_catalog), model_catalog_(std::move(model_catalog)),
      thumbnails_(&thumbnails), recovery_(&recovery), logger_(&logger) {
    std::vector<gameplay::world3d::scripts::ScriptValidationIssue> script_issues;
    const auto scripts = gameplay::world3d::scripts::loadScriptCatalog(
        resort_root_.string(), &script_issues);
    for (const auto& script : scripts.scripts) {
        if (script.kind == gameplay::world3d::scripts::ScriptKind::Door && script.valid) {
            door_script_choices_.push_back({script.id, script.id});
        }
    }
    for (const auto& issue : script_issues) {
        logger_->log(LogLevel::Warning, "scripts", issue.script_id + ": " + issue.message);
    }
    rebuildAssetViews();
    if (const OpenMapSource* source = workspace_->activeSource()) {
        active_layer_index_ = configuredLayer(source->document);
    }
    refreshDiagnostics();
}

void EditorController::setViewportTexture(const ExactWorldPreview::ViewportTexture& texture) {
    viewport_texture_ = texture;
}

std::optional<std::pair<int, int>> EditorController::pickCell(
    const ViewportGesture& gesture) const {
    if (gesture.top_down) {
        const OpenMapSource* source = workspace_->activeSource();
        if (!source) return std::nullopt;
        const int width = source->document.width();
        const int height = source->document.height();
        const bool inside = gesture.tile_x >= 0 && gesture.tile_y >= 0 &&
            gesture.tile_x < width && gesture.tile_y < height;
        const bool horizontal_halo = (gesture.tile_x == -1 || gesture.tile_x == width) &&
            gesture.tile_y >= 0 && gesture.tile_y < height;
        const bool vertical_halo = (gesture.tile_y == -1 || gesture.tile_y == height) &&
            gesture.tile_x >= 0 && gesture.tile_x < width;
        if (!inside && !horizontal_halo && !vertical_halo) return std::nullopt;
        return std::pair{gesture.tile_x, gesture.tile_y};
    }
    const auto* scene = preview_->scene();
    const auto* camera = preview_->camera();
    if (!scene || !camera || gesture.width <= 0.0f || gesture.height <= 0.0f ||
        viewport_texture_.width <= 0 || viewport_texture_.height <= 0) return std::nullopt;
    const float x = gesture.local_x * static_cast<float>(viewport_texture_.width) / gesture.width;
    const float y = gesture.local_y * static_cast<float>(viewport_texture_.height) / gesture.height;
    const auto pick = pickTerrainFromScreen(
        *scene, *camera, x, y, viewport_texture_.width, viewport_texture_.height);
    return pick ? std::optional(std::pair{pick->tile_x, pick->tile_y}) : std::nullopt;
}

SelectionItem EditorController::selectionAt(int tile_x, int tile_y) const {
    SelectionItem result{SelectionKind::TerrainCell, workspace_->activeMapId(), {},
        tile_x, tile_y, static_cast<int>(active_layer_index_)};
    const OpenMapSource* source = workspace_->activeSource();
    if (!source) return result;
    const auto validation = projectValidation(source->document, source->key);
    for (const DoorProjection& door : validation.doors) {
        if (door.tile_x == tile_x && door.tile_y == tile_y) {
            return {SelectionKind::DoorTrigger, workspace_->activeMapId(), door.id,
                tile_x, tile_y, static_cast<int>(active_layer_index_)};
        }
    }
    for (const AnchorProjection& anchor : validation.anchors) {
        if (anchor.tile_x == tile_x && anchor.tile_y == tile_y) {
            return {SelectionKind::Anchor, workspace_->activeMapId(), anchor.id,
                tile_x, tile_y, static_cast<int>(active_layer_index_)};
        }
    }
    const float tile_size = std::max(1.0f, source->document.tileSize());
    const auto models = projectModels(source->document);
    for (auto placed = models.rbegin(); placed != models.rend(); ++placed) {
        const int center_x = static_cast<int>(std::floor(placed->x / tile_size));
        const int center_y = static_cast<int>(std::floor(placed->z / tile_size));
        const ModelAsset* asset = modelAsset(placed->id);
        std::filesystem::path placed_path = placed->glb;
        if (!asset && !placed_path.empty()) {
            if (placed_path.is_relative()) placed_path = resort_root_ / placed_path;
            const auto found = std::find_if(model_catalog_.begin(), model_catalog_.end(),
                [&](const ModelAsset& candidate) {
                    return candidate.glb_path.lexically_normal() == placed_path.lexically_normal();
                });
            if (found != model_catalog_.end()) asset = &*found;
        }
        const int width = asset ? asset->footprint_width : 1;
        const int depth = asset ? asset->footprint_depth : 1;
        const int min_x = center_x - (width - 1) / 2;
        const int min_y = center_y - (depth - 1) / 2;
        if (tile_x >= min_x && tile_x < min_x + width &&
            tile_y >= min_y && tile_y < min_y + depth) {
            return {SelectionKind::Model, workspace_->activeMapId(),
                placed->id, center_x, center_y,
                static_cast<int>(active_layer_index_), placed->metadata_index};
        }
    }
    return result;
}

void EditorController::handle(const EditorUiEvents& events) {
    if (events.inspect_mode) {
        active_tool_ = EditorTool::Select;
        active_asset_id_.clear();
        paint_ = {};
        drag_ = {};
    }
    if (events.activate_asset_kind) {
        active_asset_kind_ = *events.activate_asset_kind;
        active_asset_id_.clear();
        active_category_.clear();
    }
    if (events.activate_asset_id) {
        active_asset_id_ = active_asset_id_ == *events.activate_asset_id
            ? std::string{} : *events.activate_asset_id;
        if (!active_asset_id_.empty()) {
            active_tool_ = active_asset_kind_ == AssetKind::Model ? EditorTool::Objects
                : active_asset_kind_ == AssetKind::Door ? EditorTool::Doors
                : active_asset_kind_ == AssetKind::SmartSet ? EditorTool::SmartObjects
                : EditorTool::Paint;
        }
        else active_tool_ = EditorTool::Select;
    }
    if (events.activate_tool) {
        active_tool_ = *events.activate_tool;
        paint_ = {};
        drag_ = {};
        switch (active_tool_) {
            case EditorTool::Paint: active_asset_kind_ = AssetKind::Tile; break;
            case EditorTool::Objects: active_asset_kind_ = AssetKind::Model; break;
            case EditorTool::Doors: active_asset_kind_ = AssetKind::Door; break;
            case EditorTool::SmartObjects: active_asset_kind_ = AssetKind::SmartSet; break;
            default: break;
        }
        if (!active_asset_id_.empty() && !activeAsset(assets())) active_asset_id_.clear();
    }
    if (events.height_brush_value) height_brush_value_ = *events.height_brush_value;
    if (events.collision_brush_value) collision_brush_value_ = *events.collision_brush_value;
    if (events.view_mode && *events.view_mode != view_mode_) {
        view_mode_ = *events.view_mode;
        if (view_mode_ == EditorViewMode::GamePreview) {
            active_tool_ = EditorTool::Select;
            active_asset_id_.clear();
            preview_->setAnimationsEnabled(true);
            const OpenMapSource* source = workspace_->activeSource();
            if (!preview_->ready() || !source || preview_source_key_ != source->key) {
                requestPreviewReload();
                status_ = "Loading exact game preview";
            } else {
                status_ = "Play test ready";
            }
        } else if (view_mode_ == EditorViewMode::World) {
            active_tool_ = EditorTool::Select;
            active_asset_id_.clear();
            status_ = "World workspace";
        } else {
            status_ = "Top-down authoring view";
        }
    }
    if (events.refresh_preview) requestPreviewReload();
    if (events.restart_animation) preview_->setAnimationTimeSeconds(0.0);
    if (events.animation_time_seconds) {
        preview_->setAnimationTimeSeconds(*events.animation_time_seconds);
        preview_->setAnimationsEnabled(false);
    }
    if (events.activate_category) active_category_ = *events.activate_category;
    if (events.activate_layer_index) {
        active_layer_index_ = *events.activate_layer_index;
        top_down_cache_dirty_ = true;
    }
    if (events.select_world_map_id && workspace_->activateMap(*events.select_world_map_id)) {
        world_cache_dirty_ = true;
        top_down_cache_dirty_ = true;
        selection_.clear();
        if (const OpenMapSource* selected = workspace_->activeSource()) {
            active_layer_index_ = configuredLayer(selected->document);
        }
        status_ = "Selected " + *events.select_world_map_id;
    }
    if (events.activate_map_id && workspace_->activateMap(*events.activate_map_id)) {
        view_mode_ = EditorViewMode::TopDown;
        selection_.clear();
        paint_ = {};
        drag_ = {};
        hovered_cell_.reset();
        active_layer_index_ = configuredLayer(workspace_->activeSource()->document);
        requestPreviewReload();
        status_ = "Switched to " + *events.activate_map_id;
    }
    handleWorldEvents(events);
    OpenMapSource* source = workspace_->activeSource();
    if (events.resize_map && source) {
        const int width = std::clamp(events.resize_map->width, 1, 256);
        const int height = std::clamp(events.resize_map->height, 1, 256);
        executeMutation(
            "Resize map to " + std::to_string(width) + " x " + std::to_string(height),
            [width, height](OwmapDocument& document) {
                (void)resizeMap(document, static_cast<std::uint16_t>(width),
                    static_cast<std::uint16_t>(height));
            });
        selection_.clear();
        paint_ = {};
        drag_ = {};
        hovered_cell_.reset();
    }
    if (events.undo) {
        const bool changed = view_mode_ == EditorViewMode::World
            ? workspace_->undoProject()
            : (source && source->commands.undo());
        if (changed) {
            world_cache_dirty_ = true;
            refreshDiagnostics();
            status_ = "Undo";
        }
    }
    if (events.redo) {
        const bool changed = view_mode_ == EditorViewMode::World
            ? workspace_->redoProject()
            : (source && source->commands.redo());
        if (changed) {
            world_cache_dirty_ = true;
            refreshDiagnostics();
            status_ = "Redo";
        }
    }
    if (events.save && source) {
        try {
            std::vector<std::string> saved_keys;
            for (const OpenMapSource* open_source : workspace_->sources()) {
                if (open_source->dirty()) saved_keys.push_back(open_source->key);
            }
            workspace_->saveAll();
            for (const std::string& key : saved_keys) {
                std::string error;
                if (!recovery_->remove(key, &error) && !error.empty()) {
                    log(LogLevel::Warning, "recovery", key + ": " + error);
                }
            }
            status_ = saved_keys.empty() ? "No changes to save" :
                "Saved " + std::to_string(saved_keys.size()) + " map source(s)";
            log(LogLevel::Info, "save", status_);
        } catch (const std::exception& exception) {
            status_ = exception.what();
            log(LogLevel::Error, "save", status_);
        }
    }
    if (events.delete_selection) deleteSelection();
    if (events.duplicate_selection) duplicateSelection();
    if (events.focus_selection) focusSelection();
    if (events.toggle_animations) preview_->setAnimationsEnabled(!preview_->animationsEnabled());
    preview_->setMovementInput(events.play_move_x, events.play_move_y);
    if (events.play_reset) preview_->resetPlayer();
    if (events.play_focus_player) preview_->focusPlayer();
    if (events.play_start_from_selection) {
        if (const auto selected = selection_.primary()) {
            if (!preview_->startPlayerAtTile(selected->tile_x, selected->tile_y)) {
                status_ = "Selected cell is not a valid player position";
            }
        }
    }
    if (events.toggle_grid) grid_overlay_ = !grid_overlay_;
    if (events.toggle_collision) collision_overlay_ = !collision_overlay_;
    if (events.validate) {
        refreshDiagnostics();
        status_ = diagnostics_.empty() ? "Validation passed" :
            "Validation found " + std::to_string(diagnostics_.size()) + " diagnostic(s)";
    }
    if (events.reveal_log) {
        status_ = "Log: " + logger_->currentPath().string();
        const std::string url = "file://" + logger_->currentPath().parent_path().string();
        if (SDL_OpenURL(url.c_str()) != 0) log(LogLevel::Warning, "ui", SDL_GetError());
    }
    handleLayerEvents(events);
    handleTerrainInspectorEvents(events);
    handleTravelEvents(events);
    const auto primary = selection_.primary();
    if (source && primary && primary->kind == SelectionKind::DoorTrigger &&
        (events.door_destination_map_id || events.door_destination_anchor_id ||
         events.door_direction || events.door_script_id)) {
        const auto projected = projectValidation(source->document, source->key);
        const auto found = std::find_if(projected.doors.begin(), projected.doors.end(),
            [&](const DoorProjection& door) { return door.id == primary->object_id; });
        if (found != projected.doors.end()) {
            const DoorProjection door = *found;
            const auto link = std::find_if(projected.links.begin(), projected.links.end(),
                [&](const LinkProjection& value) { return value.id == door.link_id; });
            const std::string current_map = link == projected.links.end()
                ? std::string{} : link->destination_map_id;
            if (events.door_destination_map_id) {
                std::string default_anchor;
                if (const OpenMapSource* destination = workspace_->sourceForMap(
                    *events.door_destination_map_id)) {
                    const auto target = projectValidation(destination->document, destination->key);
                    default_anchor = proposedDestinationAnchorId(target.anchors);
                }
                executeMutation("Link door", [=](OwmapDocument& document) {
                    (void)addOrUpdateLink(document, door.link_id,
                        *events.door_destination_map_id, default_anchor);
                });
            }
            if (events.door_destination_anchor_id) {
                executeMutation("Set door anchor", [=](OwmapDocument& document) {
                    (void)addOrUpdateLink(document, door.link_id, current_map,
                        *events.door_destination_anchor_id);
                });
            }
            if (events.door_direction) {
                executeMutation("Set door direction", [=](OwmapDocument& document) {
                    (void)setDoorAllowedDirection(document, door.id, *events.door_direction);
                });
            }
            if (events.door_script_id) {
                executeMutation("Set door script", [=](OwmapDocument& document) {
                    (void)setDoorScript(document, door.id, *events.door_script_id);
                });
            }
        }
    }
    if (view_mode_ != EditorViewMode::World) handleViewport(events.viewport);
}

void EditorController::handleViewport(const ViewportGesture& gesture) {
    if (!gesture.top_down && gesture.wheel != 0.0f) preview_->zoomByWheel(gesture.wheel);
    if (!gesture.top_down && gesture.middle_down) {
        if (middle_was_down_) preview_->panScreenPixels(
            gesture.local_x - previous_middle_x_, gesture.local_y - previous_middle_y_,
            static_cast<int>(gesture.height));
        previous_middle_x_ = gesture.local_x;
        previous_middle_y_ = gesture.local_y;
    }
    middle_was_down_ = gesture.middle_down;
    if (gesture.hovered) hovered_cell_ = pickCell(gesture);
    if (!hovered_cell_) return;
    const auto [x, y] = *hovered_cell_;

    if (gesture.right_clicked) {
        active_tool_ = EditorTool::Select;
        active_asset_id_.clear();
        paint_ = {};
        drag_ = {};
        selection_.select(selectionAt(x, y));
        return;
    }
    if (gesture.double_clicked && !gesture.top_down) preview_->focusTile(x, y);
    if (gesture.left_clicked) {
        const OpenMapSource* source = workspace_->activeSource();
        const bool inside = source && x >= 0 && y >= 0 &&
            x < source->document.width() && y < source->document.height();
        const AssetView* active_asset = activeAsset(assets());
        const bool places_asset = active_tool_ == EditorTool::Paint ||
            active_tool_ == EditorTool::Objects || active_tool_ == EditorTool::Doors ||
            active_tool_ == EditorTool::SmartObjects;
        const bool door_halo = places_asset && active_asset &&
            active_asset->kind == AssetKind::Door && active_asset->tile_id < 0;
        const bool brush = places_asset ||
            active_tool_ == EditorTool::EraseLayer || active_tool_ == EditorTool::ClearCell ||
            active_tool_ == EditorTool::Height || active_tool_ == EditorTool::Collision;
        if (brush && (inside || door_halo) &&
            (!places_asset || !active_asset_id_.empty())) {
            paint_.active = true;
            paint_.cells = {{x, y}};
        } else if (inside && active_tool_ == EditorTool::Anchors) {
            executeMutation("Add anchor", [x, y](OwmapDocument& document) {
                (void)addAnchor(document, uniqueMapObjectId(document, "anchor"), x, y, "south");
            });
            selection_.select(selectionAt(x, y));
        } else if (inside && active_tool_ == EditorTool::Eyedropper) {
            if (active_layer_index_ < cached_layers_.size() &&
                y < static_cast<int>(cached_layers_[active_layer_index_].cells.size()) &&
                x < static_cast<int>(cached_layers_[active_layer_index_].cells[static_cast<std::size_t>(y)].size())) {
                const int tile = cached_layers_[active_layer_index_].cells[static_cast<std::size_t>(y)]
                    [static_cast<std::size_t>(x)];
                const auto found = std::find_if(asset_views_.begin(), asset_views_.end(),
                    [&](const AssetView& asset) {
                        return asset.kind == AssetKind::Tile && asset.tile_id == tile;
                    });
                if (found != asset_views_.end()) {
                    active_asset_kind_ = AssetKind::Tile;
                    active_asset_id_ = found->id;
                    active_category_ = found->category;
                    active_tool_ = EditorTool::Paint;
                    status_ = "Sampled " + found->name;
                }
            }
        } else if (active_tool_ == EditorTool::Select) {
            const SelectionItem item = selectionAt(x, y);
            selection_.select(item);
            if (gesture.top_down && item.kind != SelectionKind::TerrainCell) {
                drag_ = {item, item.tile_x, item.tile_y, item.tile_x, item.tile_y,
                    item.tile_x - x, item.tile_y - y, true};
            }
        }
    }
    if (gesture.left_down && paint_.active &&
        std::find(paint_.cells.begin(), paint_.cells.end(), std::pair{x, y}) == paint_.cells.end()) {
        paint_.cells.emplace_back(x, y);
    }
    if (gesture.left_down && drag_.active) {
        drag_.current_x = x + drag_.grab_offset_x;
        drag_.current_y = y + drag_.grab_offset_y;
    }
    if (gesture.left_released) {
        if (paint_.active) commitPaint();
        if (drag_.active) commitDrag();
    }
}

void EditorController::commitDrag() {
    const DragState drag = drag_;
    drag_ = {};
    if (drag.current_x == drag.start_x && drag.current_y == drag.start_y) return;
    if (drag.item.kind == SelectionKind::DoorTrigger) {
        const std::string map_id = workspace_->activeMapId();
        executeMutation("Move door", [drag, map_id](OwmapDocument& document) {
            (void)moveDoorWithVisual(
                document, drag.item.object_id, drag.current_x, drag.current_y, map_id);
        });
    } else if (drag.item.kind == SelectionKind::Anchor) {
        executeMutation("Move anchor", [drag](OwmapDocument& document) {
            (void)moveAnchor(document, drag.item.object_id, drag.current_x, drag.current_y);
        });
    } else if (drag.item.kind == SelectionKind::Model) {
        const auto index = placedModelIndex(drag.item);
        if (!index) return;
        const float tile_size = workspace_->activeSource()->document.tileSize();
        const float y = terrainHeight(
            workspace_->activeSource()->document, drag.current_x, drag.current_y);
        executeMutation("Move model", [=](OwmapDocument& document) {
            (void)moveModel(document, *index, (drag.current_x + 0.5f) * tile_size, y,
                (drag.current_y + 0.5f) * tile_size);
        });
    }
    selection_.select(selectionAt(drag.current_x, drag.current_y));
}

void EditorController::placeActiveAsset(int tile_x, int tile_y) {
    const auto& list = assets();
    const AssetView* asset = activeAsset(list);
    if (!asset) return;
    if (asset->kind == AssetKind::Model) {
        const ModelAsset* model = modelAsset(asset->id);
        if (!model) return;
        const auto relative = std::filesystem::relative(model->glb_path, resort_root_).generic_string();
        const float tile_size = workspace_->activeSource()->document.tileSize();
        const float y = terrainHeight(workspace_->activeSource()->document, tile_x, tile_y);
        executeMutation("Place " + model->display_name, [=](OwmapDocument& document) {
            (void)addModel(document, uniqueMapObjectId(document, model->id), relative,
                (tile_x + 0.5f) * tile_size, y, (tile_y + 0.5f) * tile_size,
                model->default_yaw_deg, model->default_scale);
        });
    } else if (asset->kind == AssetKind::Door) {
        const std::size_t layer = active_layer_index_;
        executeMutation("Add door trigger", [=](OwmapDocument& document) {
            const std::string id = uniqueMapObjectId(document, "door");
            const std::string link = id + "_link";
            (void)addDoorTrigger(document, id, tile_x, tile_y, "north", link,
                "door_enter_default", sceneId(document),
                configuredLayerId(document, layer), std::pair{tile_x, tile_y});
            (void)addOrUpdateLink(document, link, {}, {});
        });
    }
}

} // namespace pr::mapmaker
