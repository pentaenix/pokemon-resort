#pragma once

#include "aquarium_geometry/Types.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

struct ConstructionCellSurface {
    pr::aquarium::geometry::GridCell cell;
    float floor_y = 0.0f;
    bool blocked = false;
};

enum class ConstructionHudAction {
    None,
    Place,
    Select,
    Move,
    Resize,
    Shape,
    Height,
    Roundness,
    Rotate,
    NotchWidth,
    NotchDepth,
    Review,
    Build,
    Adjust,
    Delete,
    Undo,
    Redo,
    Cancel,
    Done,
    Exit,
};

struct AquariumConstructionVisual {
    bool visible = false;
    float tile_world_units = 16.0f;
    std::vector<ConstructionCellSurface> cells;
    std::vector<pr::aquarium::geometry::GridCell> locked_cells;
    std::vector<pr::aquarium::geometry::GridCell> draft_cells;
    std::vector<pr::aquarium::geometry::GridCell> selected_cells;
    std::vector<pr::aquarium::geometry::GridCell> original_cells;
    pr::aquarium::geometry::GridCell cursor{};
    std::optional<pr::aquarium::geometry::GridCell> anchor;
    std::optional<pr::aquarium::geometry::TankDesign> selected_tank;
    std::optional<pr::aquarium::geometry::TankDesign> preview_tank;
    std::optional<AquariumResizeHandle> active_resize_handle;
    ConstructionState state = ConstructionState::Dormant;
    bool draft_valid = false;
    bool undo_available = false;
    bool redo_available = false;
    bool property_draft = false;
    ConstructionHudAction focused_action = ConstructionHudAction::None;
    std::string navigation_hint;
    std::string status_hint;
};

struct ConstructionVisualVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint32_t abgr = 0xffffffffU;
};

struct ConstructionVisualMesh {
    std::vector<ConstructionVisualVertex> vertices;
    std::vector<std::uint16_t> indices;
};

struct ConstructionHudRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(int point_x, int point_y) const;
};

struct ConstructionHudLayout {
    ConstructionHudRect place;
    ConstructionHudRect select;
    ConstructionHudRect move;
    ConstructionHudRect resize;
    ConstructionHudRect shape;
    ConstructionHudRect height;
    ConstructionHudRect roundness;
    ConstructionHudRect rotate;
    ConstructionHudRect notch_width;
    ConstructionHudRect notch_depth;
    ConstructionHudRect review;
    ConstructionHudRect build;
    ConstructionHudRect adjust;
    ConstructionHudRect remove;
    ConstructionHudRect undo;
    ConstructionHudRect redo;
    ConstructionHudRect cancel;
    ConstructionHudRect done;
    ConstructionHudRect exit;
};

enum class ConstructionGizmoKind {
    Move,
    Resize,
    Height,
    Rotation,
    Roundness,
    NotchWidth,
    NotchDepth,
};

struct ConstructionGizmoHit {
    ConstructionGizmoKind kind = ConstructionGizmoKind::Move;
    AquariumResizeHandle resize_handle = AquariumResizeHandle::SouthEast;
};

ConstructionVisualMesh buildAquariumConstructionWorldMesh(
    const AquariumConstructionVisual& visual);

std::optional<pr::aquarium::geometry::GridCell> hitTestAquariumConstructionCell(
    const AquariumConstructionVisual& visual,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int screen_x,
    int screen_y,
    int viewport_width,
    int viewport_height);

std::optional<ConstructionGizmoHit> hitTestAquariumConstructionGizmo(
    const AquariumConstructionVisual& visual,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int screen_x,
    int screen_y,
    int viewport_width,
    int viewport_height);

ConstructionHudLayout aquariumConstructionHudLayout(
    int viewport_width,
    int viewport_height,
    ConstructionState state,
    bool property_draft = false);

ConstructionHudAction hitTestAquariumConstructionHud(
    const ConstructionHudLayout& layout,
    int screen_x,
    int screen_y,
    ConstructionState state,
    bool property_draft = false);

std::vector<ConstructionHudAction> aquariumConstructionHudActions(
    ConstructionState state, bool property_draft = false);
ConstructionHudAction defaultAquariumConstructionHudAction(
    ConstructionState state, bool property_draft = false);

std::vector<pr::aquarium::geometry::GridCell> aquariumConstructionContextLockedCells(
    const std::vector<pr::aquarium::geometry::GridCell>& allowed_cells,
    const std::vector<pr::aquarium::geometry::GridCell>& authored_obstacles,
    int padding_cells = 2);

std::string aquariumConstructionHintForValidation(std::string_view validation_message);

} // namespace pr::gameplay::world3d::aquarium::construction
