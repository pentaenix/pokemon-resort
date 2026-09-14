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
    Subtract,
    Shape,
    Height,
    Roundness,
    Rotate,
    NotchWidth,
    NotchDepth,
    Review,
    Build,
    Adjust,
    Stock,
    Decorate,
    Delete,
    Undo,
    Redo,
    Cancel,
    Done,
    Exit,
    Room,
    RoomNarrower,
    RoomWider,
    RoomShallower,
    RoomDeeper,
};

struct AquariumConstructionVisual {
    struct RoomHandle {
        float x=0,y=0;
        int wall=0;
        bool add_door=false, focused=false;
    };
    bool visible = false;
    bool stocking_active = false;
    std::string decoration_focus_tank;
    std::vector<std::string> inspection_hidden_tanks;
    float tile_world_units = 16.0f;
    float placement_offset_world_units = 8.0f;
    std::vector<ConstructionCellSurface> cells;
    std::vector<pr::aquarium::geometry::GridCell> locked_cells;
    std::vector<pr::aquarium::geometry::GridCell> draft_cells;
    std::vector<pr::aquarium::geometry::GridCell> selected_cells;
    std::vector<pr::aquarium::geometry::GridCell> original_cells;
    std::vector<pr::aquarium::geometry::GridCell> cut_cells;
    std::vector<pr::aquarium::geometry::GridCell> tunnel_portal_cells;
    std::vector<pr::aquarium::geometry::GridCell> tunnel_route_cells;
    std::vector<pr::aquarium::geometry::GridCell> existing_tunnel_cells;
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
    bool subtract_mode = false;
    ConstructionHudAction focused_action = ConstructionHudAction::None;
    std::string navigation_hint;
    std::string status_hint;
    std::optional<std::array<float,4>> room_outline;
    std::vector<RoomHandle> room_handles;
    std::vector<std::array<float,4>> room_portals;
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

ConstructionVisualMesh buildAquariumConstructionDepthPreviewMesh(
    const AquariumConstructionVisual& visual);

ConstructionVisualMesh buildAquariumConstructionHeightPreviewMesh(
    const AquariumConstructionVisual& visual);

struct ConstructionHudRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(int point_x, int point_y) const;
};

struct ConstructionHudLayout {
    ConstructionHudRect safe_world;
    ConstructionHudRect tool_panel;
    ConstructionHudRect property_panel;
    ConstructionHudRect property_options;
    ConstructionHudRect status;
    ConstructionHudRect place;
    ConstructionHudRect select;
    ConstructionHudRect move;
    ConstructionHudRect resize;
    ConstructionHudRect subtract;
    ConstructionHudRect shape;
    ConstructionHudRect height;
    ConstructionHudRect roundness;
    ConstructionHudRect rotate;
    ConstructionHudRect notch_width;
    ConstructionHudRect notch_depth;
    ConstructionHudRect review;
    ConstructionHudRect build;
    ConstructionHudRect adjust;
    ConstructionHudRect stock;
    ConstructionHudRect decorate;
    ConstructionHudRect remove;
    ConstructionHudRect undo;
    ConstructionHudRect redo;
    ConstructionHudRect cancel;
    ConstructionHudRect done;
    ConstructionHudRect exit;
    ConstructionHudRect room;
    ConstructionHudRect room_narrower, room_wider, room_shallower, room_deeper;
};

struct ConstructionHudChoice {
    ConstructionHudRect rect;
    ConstructionHudAction action = ConstructionHudAction::None;
    int value = 0;
    bool selected = false;
    bool enabled = true;
};

struct ConstructionHudHit {
    ConstructionHudAction action = ConstructionHudAction::None;
    std::optional<int> value;
};

enum class ConstructionGizmoKind {
    Move,
    Resize,
    Height,
    Depth,
    CornerRadius,
    TunnelPortal,
    TunnelDelete,
};

struct ConstructionGizmoHit {
    ConstructionGizmoKind kind = ConstructionGizmoKind::Move;
    AquariumResizeHandle resize_handle = AquariumResizeHandle::SouthEast;
    std::optional<pr::aquarium::geometry::GridCell> corner_vertex;
    std::optional<pr::aquarium::geometry::GridCell> portal_cell;
    std::optional<std::string> tunnel_id;
};

bool aquariumConstructionCellInWorkingView(
    pr::aquarium::geometry::GridCell cell,
    const camera::Gen4FollowCamera& camera, int width, int height,
    float tile, float offset, float floor);

ConstructionVisualMesh buildAquariumConstructionWorldMesh(
    const AquariumConstructionVisual& visual);

std::optional<pr::aquarium::geometry::GridCell> hitTestAquariumConstructionCell(
    const AquariumConstructionVisual& visual,
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int screen_x,
    int screen_y,
    int viewport_width,
    int viewport_height);

std::optional<pr::aquarium::geometry::GridCell> hitTestAquariumConstructionTankCell(
    const AquariumConstructionVisual& visual,
    const std::vector<pr::aquarium::geometry::TankDesign>& tanks,
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

// Decoration mode reuses the construction icon renderer and its hit rectangles.
inline ConstructionHudLayout aquariumDecorationHudLayout(int width,int height) {
    auto layout=aquariumConstructionHudLayout(width,height,ConstructionState::Selected);
    auto draft=aquariumConstructionHudLayout(width,height,ConstructionState::DraftReview);
    layout.stock={};layout.decorate={};layout.room={};layout.place={};layout.subtract={};layout.exit={};layout.status={};
    layout.build=draft.build;layout.cancel=draft.cancel;
    layout.remove.x=layout.cancel.x-layout.cancel.width-10;
    return layout;
}

ConstructionHudAction hitTestAquariumConstructionHud(
    const ConstructionHudLayout& layout,
    int screen_x,
    int screen_y,
    ConstructionState state,
    bool property_draft = false);

std::vector<ConstructionHudChoice> aquariumConstructionPropertyChoices(
    const ConstructionHudLayout& layout,
    const AquariumConstructionVisual& visual);

ConstructionHudHit hitTestAquariumConstructionHud(
    const ConstructionHudLayout& layout,
    const AquariumConstructionVisual& visual,
    int screen_x,
    int screen_y);

bool aquariumConstructionHudContainsUi(
    const ConstructionHudLayout& layout,
    int screen_x,
    int screen_y);

bool aquariumConstructionPropertyPanelVisible(
    ConstructionState state,
    bool has_tank);

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
