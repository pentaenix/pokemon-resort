#include "mapmaker/app/EditorController.hpp"

#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <utility>

namespace pr::mapmaker {

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
        (void)addModel(document, uniqueMapObjectId(document, model.id), model.glb,
            model.x + document.tileSize(), model.y, model.z, model.yaw_deg, model.scale);
    });
}

void EditorController::focusSelection() {
    if (view_mode_ == EditorViewMode::TopDown) {
        if (const auto primary = selection_.primary()) {
            top_down_focus_x_ = primary->tile_x;
            top_down_focus_y_ = primary->tile_y;
            ++top_down_focus_serial_;
            status_ = "Focused selected cell";
        }
        return;
    }
    if (const auto primary = selection_.primary()) {
        preview_->focusTile(primary->tile_x, primary->tile_y);
    } else {
        preview_->focusMap();
    }
}

void EditorController::executeMutation(
    const std::string& label, OwmapMutationCommand::Mutation mutation) {
    OpenMapSource* source = workspace_->activeSource();
    if (!source) return;
    auto command = makeOwmapMutationCommand(source->document, label, std::move(mutation),
        [this] { requestPreviewReload(); });
    if (source->commands.execute(std::move(command))) {
        status_ = label;
        world_cache_dirty_ = true;
        refreshDiagnostics();
        log(LogLevel::Info, "edit", label);
    }
}

void EditorController::refreshDiagnostics() {
    diagnostics_ = workspace_->validate();
    world_cache_dirty_ = true;
}

void EditorController::requestPreviewReload() {
    preview_reload_pending_ = true;
    top_down_cache_dirty_ = true;
}

} // namespace pr::mapmaker
