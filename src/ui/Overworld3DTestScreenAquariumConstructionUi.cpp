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

gameplay::world3d::aquarium::construction::ConstructionHudAction
Overworld3DTestScreen::aquariumConstructionHudActionAt(int logical_x, int logical_y) const {
    namespace aqc = gameplay::world3d::aquarium::construction;
    const int width = std::max(1, app_config_.window.virtual_width);
    const int height = std::max(1, app_config_.window.virtual_height);
    return aqc::hitTestAquariumConstructionHud(
        aqc::aquariumConstructionHudLayout(width, height, aquarium_construction_.state()),
        logical_x, logical_y, aquarium_construction_.state());
}

bool Overworld3DTestScreen::activateAquariumConstructionAction(
    gameplay::world3d::aquarium::construction::ConstructionHudAction action) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    bool handled = true;
    switch (action) {
        case aqc::ConstructionHudAction::Place:
            aquarium_construction_.clearSelection();
            handled = aquarium_construction_.beginRectangle();
            break;
        case aqc::ConstructionHudAction::Select:
            handled = aquarium_construction_.selectAtCursor();
            break;
        case aqc::ConstructionHudAction::Move:
            handled = aquarium_construction_.beginMoveSelected();
            break;
        case aqc::ConstructionHudAction::Resize:
            handled = aquarium_construction_.beginResizeSelected(
                aqc::AquariumResizeHandle::SouthEast);
            break;
        case aqc::ConstructionHudAction::Review:
            handled = aquarium_construction_.reviewDraft();
            break;
        case aqc::ConstructionHudAction::Build:
            handled = commitAquariumConstruction();
            break;
        case aqc::ConstructionHudAction::Adjust:
            handled = aquarium_construction_.adjustDraft();
            break;
        case aqc::ConstructionHudAction::Delete:
            handled = aquarium_construction_.state() == aqc::ConstructionState::DeleteConfirm
                ? commitAquariumConstruction()
                : aquarium_construction_.requestDeleteSelected();
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

void Overworld3DTestScreen::syncAquariumConstructionFocus() {
    namespace aqc = gameplay::world3d::aquarium::construction;
    const auto state = aquarium_construction_.state();
    const auto actions = aqc::aquariumConstructionHudActions(state);
    if (state != aquarium_construction_focus_state_ ||
        std::find(actions.begin(), actions.end(), aquarium_construction_focused_action_) ==
            actions.end()) {
        aquarium_construction_focused_action_ = aqc::defaultAquariumConstructionHudAction(state);
        aquarium_construction_focus_state_ = state;
    }
}

void Overworld3DTestScreen::cycleAquariumConstructionFocus(int direction) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    syncAquariumConstructionFocus();
    const auto actions = aqc::aquariumConstructionHudActions(aquarium_construction_.state());
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
    player_.stop();
    animator_.setMoving(false);
    camera_.setTarget(player_.position());
    SDL_SetRelativeMouseMode(SDL_FALSE);
    aquarium_pointer_down_ = false;
    aquarium_pointer_dragged_ = false;
    aquarium_pointer_second_click_ = false;
    aquarium_construction_focused_action_ =
        gameplay::world3d::aquarium::construction::ConstructionHudAction::None;
    aquarium_construction_focus_state_ =
        gameplay::world3d::aquarium::construction::ConstructionState::Dormant;
    aquarium_construction_controller_id_ = -1;
    std::cerr << "[AquariumConstruction] event=exit\n";
}

} // namespace pr
