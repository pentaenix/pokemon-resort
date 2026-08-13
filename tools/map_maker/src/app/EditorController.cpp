#include "mapmaker/app/EditorController.hpp"

#include "gameplay/world3d/terrain/TerrainSurface.hpp"
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

float terrainHeight(const gameplay::world3d::SceneConfig& scene, int x, int y) {
    return gameplay::world3d::terrain::heightAtTileCenter(scene, x, y);
}

std::string uniqueId(const std::string& prefix, const OwmapDocument& document) {
    return prefix + "_" + std::to_string(document.metadata().asObject().size()) + "_" +
        std::to_string(document.serialize().size());
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
    }
    if (events.activate_category) active_category_ = *events.activate_category;
    if (events.activate_layer_index) active_layer_index_ = *events.activate_layer_index;
    if (events.activate_map_id && workspace_->activateMap(*events.activate_map_id)) {
        selection_.clear();
        paint_ = {};
        drag_ = {};
        hovered_cell_.reset();
        active_layer_index_ = configuredLayer(workspace_->activeSource()->document);
        requestPreviewReload();
        status_ = "Switched to " + *events.activate_map_id;
    }
    OpenMapSource* source = workspace_->activeSource();
    if (source && events.undo && source->commands.undo()) {
        refreshDiagnostics();
        status_ = "Undo";
    }
    if (source && events.redo && source->commands.redo()) {
        refreshDiagnostics();
        status_ = "Redo";
    }
    if (events.save && source) {
        try {
            workspace_->saveActive();
            std::string error;
            (void)recovery_->remove(source->key, &error);
            status_ = "Saved " + source->path.filename().string();
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
    if (events.toggle_grid) grid_overlay_ = !grid_overlay_;
    if (events.toggle_collision) collision_overlay_ = !collision_overlay_;
    if (events.validate) {
        refreshDiagnostics();
        status_ = diagnostics_.empty() ? "Validation passed" :
            "Validation found " + std::to_string(diagnostics_.size()) + " diagnostic(s)";
    }
    if (events.open_project) status_ = "Use --project <path> to open another project";
    if (events.reveal_log) {
        status_ = "Log: " + logger_->currentPath().string();
        const std::string url = "file://" + logger_->currentPath().parent_path().string();
        if (SDL_OpenURL(url.c_str()) != 0) log(LogLevel::Warning, "ui", SDL_GetError());
    }
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
                    if (target.anchors.size() == 1U) default_anchor = target.anchors.front().id;
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
    handleViewport(events.viewport);
}

void EditorController::handleViewport(const ViewportGesture& gesture) {
    if (gesture.wheel != 0.0f) preview_->zoomByWheel(gesture.wheel);
    if (gesture.middle_down) {
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
        active_asset_id_.clear();
        paint_ = {};
        drag_ = {};
        selection_.select(selectionAt(x, y));
        return;
    }
    if (gesture.double_clicked) preview_->focusTile(x, y);
    if (gesture.left_clicked) {
        if (!active_asset_id_.empty()) {
            paint_.active = true;
            paint_.cells = {{x, y}};
        } else {
            const SelectionItem item = selectionAt(x, y);
            selection_.select(item);
            if (item.kind != SelectionKind::TerrainCell) {
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

void EditorController::commitPaint() {
    if (paint_.cells.empty()) {
        paint_ = {};
        return;
    }
    const auto& list = assets();
    const AssetView* asset = activeAsset(list);
    if (!asset) {
        paint_ = {};
        return;
    }
    if (asset->kind == AssetKind::Tile || asset->kind == AssetKind::Door) {
        const auto cells = paint_.cells;
        const int tile_id = asset->tile_id;
        const std::size_t layer = active_layer_index_;
        if (asset->kind == AssetKind::Door) {
            const auto [x, y] = cells.front();
            executeMutation("Place door", [=](OwmapDocument& document) {
                const bool visible = tile_id >= 0;
                if (visible) (void)setTileLayerCell(document, layer, x, y, tile_id);
                const std::string id = uniqueId("door", document);
                const std::string link = id + "_link";
                (void)addDoorTrigger(document, id, x, y, "north", link,
                    "door_enter_default", visible ? sceneId(document) : std::string{},
                    visible ? configuredLayerId(document, layer) : std::string{},
                    visible ? std::optional{std::pair{x, y}} : std::nullopt);
                (void)addOrUpdateLink(document, link, {}, {});
            });
        } else {
            executeMutation("Paint " + asset->name, [cells, tile_id, layer](OwmapDocument& document) {
                for (const auto& [x, y] : cells) {
                    (void)setTileLayerCell(document, layer, x, y, tile_id);
                }
            });
        }
    } else if (asset->kind == AssetKind::SmartSet) {
        const auto found = std::find_if(tile_catalog_->smartSets().begin(),
            tile_catalog_->smartSets().end(), [&](const SmartTileSet& smart) {
                return smart.id == asset->id;
            });
        if (found != tile_catalog_->smartSets().end()) {
            const int origin_x = paint_.cells.front().first;
            const int origin_y = paint_.cells.front().second;
            const auto columns = found->columns;
            const std::size_t layer = active_layer_index_;
            executeMutation("Stamp " + found->name, [=](OwmapDocument& document) {
                for (std::size_t x = 0; x < columns.size(); ++x) {
                    for (std::size_t y = 0; y < columns[x].size(); ++y) {
                        if (columns[x][y] < 0) continue;
                        (void)setTileLayerCell(document, layer,
                            origin_x + static_cast<int>(x), origin_y + static_cast<int>(y),
                            columns[x][y]);
                    }
                }
            });
        }
    } else if (paint_.cells.size() == 1U) {
        placeActiveAsset(paint_.cells.front().first, paint_.cells.front().second);
    }
    paint_ = {};
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
        const auto* scene = preview_->scene();
        const float y = scene ? terrainHeight(*scene, drag.current_x, drag.current_y) : 0.0f;
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
        const float y = preview_->scene() ? terrainHeight(*preview_->scene(), tile_x, tile_y) : 0.0f;
        executeMutation("Place " + model->display_name, [=](OwmapDocument& document) {
            (void)addModel(document, uniqueId(model->id, document), relative,
                (tile_x + 0.5f) * tile_size, y, (tile_y + 0.5f) * tile_size,
                model->default_yaw_deg, model->default_scale);
        });
    } else if (asset->kind == AssetKind::Door) {
        const std::size_t layer = active_layer_index_;
        executeMutation("Add door trigger", [=](OwmapDocument& document) {
            const std::string id = uniqueId("door", document);
            const std::string link = id + "_link";
            (void)addDoorTrigger(document, id, tile_x, tile_y, "north", link,
                "door_enter_default", sceneId(document),
                configuredLayerId(document, layer), std::pair{tile_x, tile_y});
            (void)addOrUpdateLink(document, link, {}, {});
        });
    }
}

void EditorController::deleteSelection() {
    const auto primary = selection_.primary();
    if (!primary) return;
    if (primary->kind == SelectionKind::TerrainCell) {
        executeMutation("Clear cell", [cell = *primary](OwmapDocument& document) {
            (void)clearCell(document, cell.tile_x, cell.tile_y);
        });
    } else if (primary->kind == SelectionKind::DoorTrigger) {
        const std::string map_id = workspace_->activeMapId();
        executeMutation("Delete door", [id = primary->object_id, map_id](OwmapDocument& document) {
            (void)eraseDoorWithVisual(document, id, map_id);
        });
    } else if (primary->kind == SelectionKind::Anchor) {
        executeMutation("Delete anchor", [id = primary->object_id](OwmapDocument& document) {
            (void)eraseAnchor(document, id);
        });
    } else if (primary->kind == SelectionKind::Model) {
        if (const auto index = placedModelIndex(*primary)) {
            executeMutation("Delete model", [index](OwmapDocument& document) {
                (void)eraseModel(document, *index);
            });
        }
    }
    selection_.clear();
}

void EditorController::duplicateSelection() {
    const auto primary = selection_.primary();
    OpenMapSource* source = workspace_->activeSource();
    if (!primary || !source || primary->kind != SelectionKind::Model) return;
    const auto index = placedModelIndex(*primary);
    if (!index) return;
    const auto models = projectModels(source->document);
    const auto found = std::find_if(models.begin(), models.end(),
        [index](const ModelPlacementProjection& model) { return model.metadata_index == *index; });
    if (found == models.end()) return;
    const ModelPlacementProjection model = *found;
    executeMutation("Duplicate model", [model](OwmapDocument& document) {
        (void)addModel(document, uniqueId(model.id, document), model.glb,
            model.x + document.tileSize(), model.y, model.z,
            model.yaw_deg, model.scale);
    });
}

void EditorController::focusSelection() {
    if (const auto primary = selection_.primary()) preview_->focusTile(primary->tile_x, primary->tile_y);
    else preview_->focusMap();
}

void EditorController::executeMutation(
    const std::string& label, OwmapMutationCommand::Mutation mutation) {
    OpenMapSource* source = workspace_->activeSource();
    if (!source) return;
    auto command = makeOwmapMutationCommand(source->document, label, std::move(mutation),
        [this] { requestPreviewReload(); });
    if (source->commands.execute(std::move(command))) {
        status_ = label;
        refreshDiagnostics();
        log(LogLevel::Info, "edit", label);
    }
}

void EditorController::refreshDiagnostics() { diagnostics_ = workspace_->validate(); }
void EditorController::requestPreviewReload() { preview_reload_pending_ = true; }

} // namespace pr::mapmaker
