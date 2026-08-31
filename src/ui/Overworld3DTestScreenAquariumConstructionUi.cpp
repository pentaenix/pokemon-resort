#include "ui/Overworld3DTestScreen.hpp"

#include "gameplay/world3d/rendering/PixelScale.hpp"

#include <algorithm>
#include <iostream>

namespace pr {

std::optional<pr::aquarium::geometry::GridCell>
Overworld3DTestScreen::aquariumConstructionCellAt(int logical_x, int logical_y) const {
    if (!aquarium_construction_.active()) return std::nullopt;
    const int logical_w = std::max(1, app_config_.window.virtual_width);
    const int logical_h = std::max(1, app_config_.window.virtual_height);
    namespace aqc = gameplay::world3d::aquarium::construction;
    const bool property_draft = aquarium_construction_.draftOperation() ==
        aqc::ConstructionDraftOperation::Properties;
    const auto hud = aqc::aquariumConstructionHudLayout(
        logical_w, logical_h, aquarium_construction_.state(), property_draft);
    if (!hud.safe_world.contains(logical_x, logical_y)) return std::nullopt;
    const SDL_Rect viewport = visibleWorldViewportRect(logical_w, logical_h);
    const SDL_Point point{logical_x, logical_y};
    if (!SDL_PointInRect(&point, &viewport)) return std::nullopt;
    const int projection_w = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseWidth(scene_) : logical_w;
    const int projection_h = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseHeight(scene_) : logical_h;
    const int projected_x = (logical_x - viewport.x) * projection_w / std::max(1, viewport.w);
    const int projected_y = (logical_y - viewport.y) * projection_h / std::max(1, viewport.h);
    return gameplay::world3d::aquarium::construction::hitTestAquariumConstructionCell(
        aquariumConstructionVisual(), camera_, projected_x, projected_y,
        projection_w, projection_h);
}

std::optional<gameplay::world3d::aquarium::construction::ConstructionGizmoHit>
Overworld3DTestScreen::aquariumConstructionGizmoAt(int logical_x, int logical_y) const {
    if (!aquarium_construction_.active()) return std::nullopt;
    const int logical_w = std::max(1, app_config_.window.virtual_width);
    const int logical_h = std::max(1, app_config_.window.virtual_height);
    namespace aqc = gameplay::world3d::aquarium::construction;
    const bool property_draft = aquarium_construction_.draftOperation() ==
        aqc::ConstructionDraftOperation::Properties;
    const auto hud = aqc::aquariumConstructionHudLayout(
        logical_w, logical_h, aquarium_construction_.state(), property_draft);
    if (!hud.safe_world.contains(logical_x, logical_y)) return std::nullopt;
    const SDL_Rect viewport = visibleWorldViewportRect(logical_w, logical_h);
    const SDL_Point point{logical_x, logical_y};
    if (!SDL_PointInRect(&point, &viewport)) return std::nullopt;
    const int projection_w = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseWidth(scene_) : logical_w;
    const int projection_h = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseHeight(scene_) : logical_h;
    const int projected_x = (logical_x - viewport.x) * projection_w / std::max(1, viewport.w);
    const int projected_y = (logical_y - viewport.y) * projection_h / std::max(1, viewport.h);
    return gameplay::world3d::aquarium::construction::hitTestAquariumConstructionGizmo(
        aquariumConstructionVisual(), camera_, projected_x, projected_y,
        projection_w, projection_h);
}

bool Overworld3DTestScreen::handleAquariumConstructionPointerPressed(
    int logical_x, int logical_y) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    if (aquarium_pointer_operation_active_) {
        return finishAquariumConstructionPointerOperation(logical_x, logical_y);
    }
    const auto hud_hit = aquariumConstructionHudHitAt(logical_x, logical_y);
    if (hud_hit.action != aqc::ConstructionHudAction::None) {
        aquarium_construction_focused_action_ = hud_hit.action;
        if (hud_hit.value) {
            setAquariumConstructionPropertyValue(hud_hit.action, *hud_hit.value);
        } else if (hud_hit.action == aqc::ConstructionHudAction::Shape ||
                   hud_hit.action == aqc::ConstructionHudAction::Height ||
                   hud_hit.action == aqc::ConstructionHudAction::Roundness ||
                   hud_hit.action == aqc::ConstructionHudAction::Rotate ||
                   hud_hit.action == aqc::ConstructionHudAction::NotchWidth ||
                   hud_hit.action == aqc::ConstructionHudAction::NotchDepth) {
            syncAquariumConstructionFocus();
        } else {
            activateAquariumConstructionAction(hud_hit.action);
        }
        return true;
    }
    if (aquariumConstructionUiAt(logical_x, logical_y)) return true;
    if (const auto gizmo = aquariumConstructionGizmoAt(logical_x, logical_y)) {
        bool began = false;
        switch (gizmo->kind) {
        case aqc::ConstructionGizmoKind::Move:
            began = aquarium_construction_.beginMoveSelected();
            break;
        case aqc::ConstructionGizmoKind::Resize:
            began = aquarium_construction_.beginResizeSelected(gizmo->resize_handle);
            break;
        case aqc::ConstructionGizmoKind::Height:
            began = aquarium_construction_.beginPropertySelected();
            break;
        case aqc::ConstructionGizmoKind::CornerRadius:
            began = gizmo->corner_vertex.has_value() &&
                aquarium_construction_.beginPropertySelected();
            break;
        }
        if (began) {
            aquarium_pointer_operation_active_ = true;
            aquarium_pointer_gizmo_ = gizmo->kind;
            aquarium_pointer_corner_vertex_ = gizmo->corner_vertex;
            aquarium_pointer_gizmo_y_ = logical_y;
            syncAquariumConstructionFocus();
        }
        return true;
    }
    if (const auto cell = aquariumConstructionCellAt(logical_x, logical_y)) {
        aquarium_construction_.pointAt(*cell);
        const bool subtract = aquarium_subtract_mode_ || aquarium_pointer_subtract_;
        bool began = false;
        if (aquarium_construction_.state() == aqc::ConstructionState::Browse) {
            if (aquarium_construction_.selectAtCursor()) {
                if (subtract) began = aquarium_construction_.beginPaintSelected(true);
            } else if (!subtract) {
                began = aquarium_construction_.beginRectangle();
            }
        } else if (aquarium_construction_.state() == aqc::ConstructionState::Selected) {
            const auto* selected = aquarium_construction_.selectedTank();
            const auto selected_cells = selected
                ? aqc::tankFootprintCells(*selected)
                : std::vector<pr::aquarium::geometry::GridCell>{};
            const bool on_selected = std::any_of(
                selected_cells.begin(), selected_cells.end(), [&](auto occupied) {
                    return occupied.column == cell->column && occupied.row == cell->row;
                });
            if (on_selected && subtract) {
                began = aquarium_construction_.beginPaintSelected(true);
            } else if (!on_selected &&
                       aquarium_construction_.beginPaintSelected(subtract)) {
                // The first cell is applied when the paint gesture begins.
                began = true;
            } else {
                aquarium_construction_.selectAtCursor();
            }
        } else if (aquarium_construction_.state() == aqc::ConstructionState::SubtractFootprint) {
            if (!aquarium_construction_.toggleSubtractedCell()) {
                requestAquariumConstructionErrorFeedback();
            }
        }
        aquarium_pointer_operation_active_ = began;
        if (!began) aquarium_pointer_subtract_ = false;
        syncAquariumConstructionFocus();
    }
    return true;
}

bool Overworld3DTestScreen::finishAquariumConstructionPointerOperation(
    int logical_x, int logical_y) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    if (!aquarium_pointer_gizmo_ ||
        (*aquarium_pointer_gizmo_ != aqc::ConstructionGizmoKind::Height &&
         *aquarium_pointer_gizmo_ != aqc::ConstructionGizmoKind::CornerRadius)) {
        if (const auto cell = aquariumConstructionCellAt(logical_x, logical_y)) {
            aquarium_construction_.pointAt(*cell);
        }
    }
    const auto state = aquarium_construction_.state();
    bool ready = state == aqc::ConstructionState::DraftReview;
    if (!ready && (state == aqc::ConstructionState::ResizeFootprint ||
                   state == aqc::ConstructionState::MoveTank ||
                   state == aqc::ConstructionState::ResizeTank ||
                   state == aqc::ConstructionState::PaintFootprint ||
                   state == aqc::ConstructionState::SubtractFootprint)) {
        ready = aquarium_construction_.reviewDraft();
    }
    if (ready && commitAquariumConstruction()) {
        resetAquariumConstructionPointerOperation();
    } else {
        requestAquariumConstructionErrorFeedback();
    }
    syncAquariumConstructionFocus();
    return true;
}

void Overworld3DTestScreen::resetAquariumConstructionPointerOperation() {
    aquarium_pointer_operation_active_ = false;
    aquarium_pointer_subtract_ = false;
    aquarium_pointer_gizmo_.reset();
    aquarium_pointer_corner_vertex_.reset();
}

gameplay::world3d::aquarium::construction::ConstructionHudHit
Overworld3DTestScreen::aquariumConstructionHudHitAt(int logical_x, int logical_y) const {
    namespace aqc = gameplay::world3d::aquarium::construction;
    const int width = std::max(1, app_config_.window.virtual_width);
    const int height = std::max(1, app_config_.window.virtual_height);
    const bool property_draft = aquarium_construction_.draftOperation() ==
        aqc::ConstructionDraftOperation::Properties;
    const auto visual = aquariumConstructionVisual();
    return aqc::hitTestAquariumConstructionHud(
        aqc::aquariumConstructionHudLayout(width, height, visual.state, property_draft),
        visual, logical_x, logical_y);
}

bool Overworld3DTestScreen::aquariumConstructionUiAt(int logical_x, int logical_y) const {
    namespace aqc = gameplay::world3d::aquarium::construction;
    const int width = std::max(1, app_config_.window.virtual_width);
    const int height = std::max(1, app_config_.window.virtual_height);
    const bool property_draft = aquarium_construction_.draftOperation() ==
        aqc::ConstructionDraftOperation::Properties;
    return aqc::aquariumConstructionHudContainsUi(
        aqc::aquariumConstructionHudLayout(
            width, height, aquarium_construction_.state(), property_draft),
        logical_x, logical_y);
}

bool Overworld3DTestScreen::activateAquariumConstructionAction(
    gameplay::world3d::aquarium::construction::ConstructionHudAction action) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    bool handled = true;
    switch (action) {
        case aqc::ConstructionHudAction::Place:
            aquarium_subtract_mode_ = false;
            handled = true;
            break;
        case aqc::ConstructionHudAction::Select:
            handled = aquarium_construction_.selectAtCursor();
            break;
        case aqc::ConstructionHudAction::Move:
            handled = aquarium_construction_.beginMoveSelected();
            break;
        case aqc::ConstructionHudAction::Resize:
            handled = aquarium_construction_.beginResizeSelected(
                aquarium_construction_.preferredResizeHandle());
            break;
        case aqc::ConstructionHudAction::Subtract:
            aquarium_subtract_mode_ = true;
            handled = true;
            break;
        case aqc::ConstructionHudAction::Shape:
        case aqc::ConstructionHudAction::Height:
        case aqc::ConstructionHudAction::Roundness:
        case aqc::ConstructionHudAction::Rotate:
        case aqc::ConstructionHudAction::NotchWidth:
        case aqc::ConstructionHudAction::NotchDepth:
            aquarium_construction_focused_action_ = action;
            handled = true;
            break;
        case aqc::ConstructionHudAction::Review:
            handled = aquarium_construction_.reviewDraft();
            break;
        case aqc::ConstructionHudAction::Build:
            if (aquarium_construction_.state() != aqc::ConstructionState::DraftReview) {
                aquarium_construction_.reviewDraft();
            }
            handled = commitAquariumConstruction();
            break;
        case aqc::ConstructionHudAction::Adjust:
            handled = aquarium_construction_.adjustDraft();
            break;
        case aqc::ConstructionHudAction::Delete:
            if (aquarium_construction_.state() != aqc::ConstructionState::DeleteConfirm) {
                aquarium_construction_.requestDeleteSelected();
            }
            handled = commitAquariumConstruction();
            break;
        case aqc::ConstructionHudAction::Undo:
            handled = aquarium_construction_.canUndo() && undoAquariumConstruction();
            break;
        case aqc::ConstructionHudAction::Redo:
            handled = aquarium_construction_.canRedo() && redoAquariumConstruction();
            break;
        case aqc::ConstructionHudAction::Cancel:
            onBackPressed();
            break;
        case aqc::ConstructionHudAction::Done:
            aquarium_construction_.clearSelection();
            break;
        case aqc::ConstructionHudAction::Exit:
            exitAquariumConstruction();
            break;
        case aqc::ConstructionHudAction::None:
            handled = false;
            break;
    }
    if (!handled) requestAquariumConstructionErrorFeedback();
    syncAquariumConstructionFocus();
    return handled;
}

bool Overworld3DTestScreen::adjustAquariumConstructionProperty(int direction) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    std::optional<aqc::AquariumTankProperty> property;
    switch (aquarium_construction_focused_action_) {
    case aqc::ConstructionHudAction::Shape: property = aqc::AquariumTankProperty::Shape; break;
    case aqc::ConstructionHudAction::Height: property = aqc::AquariumTankProperty::Height; break;
    case aqc::ConstructionHudAction::Roundness: property = aqc::AquariumTankProperty::Roundness; break;
    case aqc::ConstructionHudAction::Rotate: property = aqc::AquariumTankProperty::Rotation; break;
    case aqc::ConstructionHudAction::NotchWidth: property = aqc::AquariumTankProperty::NotchWidth; break;
    case aqc::ConstructionHudAction::NotchDepth: property = aqc::AquariumTankProperty::NotchDepth; break;
    default: break;
    }
    if (!property || direction == 0) return false;
    const bool adjusted = aquarium_construction_.adjustTankProperty(*property, direction);
    if (adjusted) aquarium_construction_move_sfx_requested_ = true;
    else requestAquariumConstructionErrorFeedback();
    syncAquariumConstructionFocus();
    return adjusted;
}

bool Overworld3DTestScreen::setAquariumConstructionPropertyValue(
    gameplay::world3d::aquarium::construction::ConstructionHudAction action,
    int value) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    std::optional<aqc::AquariumTankProperty> property;
    switch (action) {
    case aqc::ConstructionHudAction::Shape: property = aqc::AquariumTankProperty::Shape; break;
    case aqc::ConstructionHudAction::Height: property = aqc::AquariumTankProperty::Height; break;
    case aqc::ConstructionHudAction::Roundness: property = aqc::AquariumTankProperty::Roundness; break;
    case aqc::ConstructionHudAction::Rotate: property = aqc::AquariumTankProperty::Rotation; break;
    case aqc::ConstructionHudAction::NotchWidth: property = aqc::AquariumTankProperty::NotchWidth; break;
    case aqc::ConstructionHudAction::NotchDepth: property = aqc::AquariumTankProperty::NotchDepth; break;
    default: return false;
    }
    const auto current_value = [&]() -> std::optional<int> {
        const auto preview = aquarium_construction_.previewTank();
        const auto* tank = preview ? &*preview : aquarium_construction_.selectedTank();
        if (!tank) return std::nullopt;
        switch (*property) {
        case aqc::AquariumTankProperty::Shape: return static_cast<int>(tank->footprint.shape);
        case aqc::AquariumTankProperty::Height: return tank->height_steps;
        case aqc::AquariumTankProperty::Roundness: return tank->corner_radius_steps;
        case aqc::AquariumTankProperty::Rotation: return tank->footprint.rotation_quarter_turns;
        case aqc::AquariumTankProperty::NotchWidth: return tank->footprint.notch_width_cells;
        case aqc::AquariumTankProperty::NotchDepth: return tank->footprint.notch_depth_cells;
        }
        return std::nullopt;
    };
    for (int attempt = 0; attempt < 32; ++attempt) {
        const auto current = current_value();
        if (!current) return false;
        if (*current == value) {
            aquarium_construction_move_sfx_requested_ = true;
            syncAquariumConstructionFocus();
            return true;
        }
        const bool cyclic = *property == aqc::AquariumTankProperty::Shape ||
            *property == aqc::AquariumTankProperty::Rotation;
        const int direction = cyclic ? 1 : (value > *current ? 1 : -1);
        if (!aquarium_construction_.adjustTankProperty(*property, direction)) {
            requestAquariumConstructionErrorFeedback();
            return false;
        }
    }
    requestAquariumConstructionErrorFeedback();
    return false;
}

void Overworld3DTestScreen::syncAquariumConstructionFocus() {
    namespace aqc = gameplay::world3d::aquarium::construction;
    const auto state = aquarium_construction_.state();
    const bool property_draft = aquarium_construction_.draftOperation() ==
        aqc::ConstructionDraftOperation::Properties;
    const auto actions = aqc::aquariumConstructionHudActions(state, property_draft);
    if (aquarium_pointer_controls_cursor_ &&
        (aquarium_construction_focused_action_ == aqc::ConstructionHudAction::None ||
         std::find(actions.begin(), actions.end(), aquarium_construction_focused_action_) !=
             actions.end())) {
        aquarium_construction_focus_state_ = state;
        return;
    }
    if (state != aquarium_construction_focus_state_ ||
        std::find(actions.begin(), actions.end(), aquarium_construction_focused_action_) ==
            actions.end()) {
        aquarium_construction_focused_action_ =
            aqc::defaultAquariumConstructionHudAction(state, property_draft);
        aquarium_construction_focus_state_ = state;
    }
}

void Overworld3DTestScreen::cycleAquariumConstructionFocus(int direction) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    syncAquariumConstructionFocus();
    const bool property_draft = aquarium_construction_.draftOperation() ==
        aqc::ConstructionDraftOperation::Properties;
    const auto actions = aqc::aquariumConstructionHudActions(
        aquarium_construction_.state(), property_draft);
    if (actions.empty() || direction == 0) return;
    auto found = std::find(actions.begin(), actions.end(), aquarium_construction_focused_action_);
    int index = found == actions.end() ? 0 : static_cast<int>(found - actions.begin());
    index = (index + (direction > 0 ? 1 : -1) + static_cast<int>(actions.size())) %
        static_cast<int>(actions.size());
    aquarium_construction_focused_action_ = actions[static_cast<std::size_t>(index)];
    aquarium_construction_move_sfx_requested_ = true;
}

void Overworld3DTestScreen::requestAquariumConstructionErrorFeedback() {
    aquarium_construction_error_sfx_requested_ = true;
#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (aquarium_construction_controller_id_ >= 0) {
        if (SDL_GameController* controller = SDL_GameControllerFromInstanceID(
                aquarium_construction_controller_id_)) {
            SDL_GameControllerRumble(controller, 0x5000, 0x2800, 110);
        }
    }
#endif
}

void Overworld3DTestScreen::exitAquariumConstruction() {
    if (!aquarium_construction_.active()) return;
    aquarium_construction_.exit();
    gameplay::world3d::aquarium::construction::resetAquariumConstructionCamera(
        aquarium_construction_camera_tracking_);
    aquarium_pointer_controls_cursor_ = false;
    aquarium_pointer_position_valid_ = false;
    if (aquarium_construction_return_cell_) {
        const auto return_cell = *aquarium_construction_return_cell_;
        if (player_.teleportToTile(
                return_cell.column, return_cell.row,
                aquarium_construction_return_facing_)) {
            animator_.setFacing(aquarium_construction_return_facing_);
            std::cerr << "[AquariumConstruction] event=player_returned cell=["
                      << return_cell.column << ',' << return_cell.row << "]\n";
        } else {
            std::cerr << "[AquariumConstruction] event=player_return_failed cell=["
                      << return_cell.column << ',' << return_cell.row << "]\n";
        }
    }
    player_.stop();
    animator_.setMoving(false);
    if (follower_controller_) {
        follower_controller_->stowUntilPlayerMoves(player_.tileX(), player_.tileY());
    }
    camera_.setTarget(player_.position());
    SDL_SetRelativeMouseMode(SDL_FALSE);
    resetAquariumConstructionPointerOperation();
    aquarium_subtract_mode_ = false;
    aquarium_left_trigger_down_ = false;
    aquarium_right_trigger_down_ = false;
    aquarium_construction_focused_action_ =
        gameplay::world3d::aquarium::construction::ConstructionHudAction::None;
    aquarium_construction_focus_state_ =
        gameplay::world3d::aquarium::construction::ConstructionState::Dormant;
    aquarium_construction_controller_id_ = -1;
    std::cerr << "[AquariumConstruction] event=exit\n";
}

} // namespace pr
