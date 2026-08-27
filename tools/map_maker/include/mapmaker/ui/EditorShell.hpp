#pragma once

#include "mapmaker/interaction/PlayInputCapture.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace pr::mapmaker {

enum class AssetKind { Tile, Door, Model, SmartSet };
enum class InspectorSelectionKind { None, TerrainCell, Tile, Model, Door, Anchor, Trigger };
enum class EditorTool {
    Select, Hand, Paint, EraseLayer, ClearCell, Height, Collision,
    Objects, Doors, Anchors, Regions, SmartObjects, Eyedropper, Chunks,
};
enum class EditorViewMode { World, TopDown, GamePreview };
enum class TopDownMarkerKind { Model, Door, Anchor };

struct WorldMapNodeView {
    std::string id;
    std::string name;
    std::string type;
    int grid_x = 0;
    int grid_y = 0;
    int width = 0;
    int height = 0;
    int door_count = 0;
    int error_count = 0;
    bool active = false;
    bool dirty = false;
    bool shared_source = false;
    bool linked = true;
};

struct WorldConnectionView {
    std::string source_map_id;
    std::string source_door_id;
    std::string destination_map_id;
    std::string destination_anchor_id;
    bool broken = false;
    bool reciprocal = false;
};

struct MapTabView {
    std::string id;
    std::string name;
    bool active = false;
    bool dirty = false;
    bool shared_source = false;
};

struct AssetView {
    std::string id;
    std::string name;
    std::string category;
    AssetKind kind = AssetKind::Tile;
    int tile_id = -1;
    int footprint_width = 1;
    int footprint_height = 1;
    bool door = false;
};

struct LayerView {
    std::size_t index = 0;
    std::string id;
    std::string name;
    bool active = false;
    bool visible = true;
};

struct InspectorField {
    std::string label;
    std::string value;
    bool warning = false;
};

struct SelectionView {
    InspectorSelectionKind kind = InspectorSelectionKind::None;
    std::string id;
    std::string title;
    int tile_x = -1;
    int tile_y = -1;
    int height = 0;
    int special = 0;
    std::string spawn_tile_use;
    std::string anchor_facing;
    bool collision = false;
    bool automatic_collision = false;
    bool terrain_editable = false;
    std::vector<InspectorField> fields;
};

struct TopDownMarkerView {
    TopDownMarkerKind kind = TopDownMarkerKind::Model;
    std::string id;
    int tile_x = 0;
    int tile_y = 0;
    bool selected = false;
    float center_tile_x = 0.5f;
    float center_tile_y = 0.5f;
    float projected_width_tiles = 0.0f;
    float projected_depth_tiles = 0.0f;
    std::vector<std::array<float, 2>> model_outline_tiles;
};

struct TopDownOpeningView {
    std::string edge;
    int from = 0;
    int to = 0;
};

struct TopDownMapView {
    std::string map_id;
    int width = 0;
    int height = 0;
    std::span<const int> composed_tiles;
    std::span<const int> active_layer_tiles;
    std::span<const std::uint8_t> heights;
    std::span<const std::uint8_t> specials;
    std::span<const std::uint8_t> collision;
    std::span<const std::uint8_t> automatic_collision;
    std::span<const TopDownMarkerView> markers;
    bool default_interior_room = false;
    float default_wall_height_tiles = 4.0f;
    int default_room_inset_tiles = 1;
    float default_wall_offset_tiles = 0.5f;
    float default_entry_extension_depth_tiles = 0.0f;
    bool default_room_black_top_cap = true;
    std::vector<TopDownOpeningView> interior_openings;
    int focus_tile_x = 0;
    int focus_tile_y = 0;
    std::uint64_t focus_serial = 0;
};

struct ChoiceView {
    std::string id;
    std::string name;
};

struct DoorEditorView {
    bool active = false;
    std::string door_id;
    std::string destination_map_id;
    std::string destination_anchor_id;
    std::string direction;
    std::string script_id;
    bool automatic_arrival = false;
    std::string automatic_arrival_label;
    std::vector<ChoiceView> map_choices;
    std::vector<ChoiceView> anchor_choices;
    std::vector<ChoiceView> script_choices;
};

struct TravelObjectView {
    std::string id;
    std::string summary;
    int tile_x = 0;
    int tile_y = 0;
    bool selected = false;
};

struct ValidationView {
    std::string severity;
    std::string code;
    std::string message;
};

struct ViewportOverlayQuad {
    // Coordinates are local to the displayed viewport image.
    std::array<float, 8> xy{};
    std::uint32_t color = 0xff42c8ffU;
    float thickness = 2.0f;
};

struct EditorUiModel {
    std::string project_name;
    std::vector<MapTabView> maps;
    std::span<const AssetView> assets;
    std::span<const std::string> tile_categories;
    std::vector<LayerView> layers;
    SelectionView selection;
    DoorEditorView door_editor;
    std::vector<TravelObjectView> placed_doors;
    std::vector<TravelObjectView> placed_anchors;
    std::vector<ValidationView> validation;
    TopDownMapView top_down;
    std::vector<WorldMapNodeView> world_maps;
    std::vector<WorldConnectionView> world_connections;

    std::uint16_t viewport_texture = UINT16_MAX;
    int viewport_texture_width = 0;
    int viewport_texture_height = 0;
    bool viewport_origin_bottom_left = false;
    std::vector<ViewportOverlayQuad> viewport_overlays;

    std::string status_text;
    std::string active_tool_text = "Move / inspect";
    std::string active_asset_id;
    std::string active_category;
    AssetKind active_asset_kind = AssetKind::Tile;
    bool can_undo = false;
    bool can_redo = false;
    bool dirty = false;
    bool animations_enabled = false;
    double animation_time_seconds = 0.0;
    bool preview_stale = false;
    bool grid_overlay = true;
    bool collision_overlay = false;
    double fps = 0.0;
    double frame_ms_p95 = 0.0;
    double input_latency_ms = 0.0;
    int scene_rebuild_count = 0;
    int height_brush_value = 0;
    bool collision_brush_value = true;
    EditorTool active_tool = EditorTool::Select;
    EditorViewMode view_mode = EditorViewMode::World;

    // Called only for visible tile cards, enabling lazy thumbnail decode/upload.
    std::function<std::uint16_t(int)> tile_thumbnail;
};

struct MapMoveRequest {
    std::string map_id;
    int grid_x = 0;
    int grid_y = 0;
};

struct MapResizeRequest {
    int width = 1;
    int height = 1;
};

struct NewMapRequest {
    std::string source_map_id;
    std::string id;
    std::string name;
    std::string type = "exterior";
    int direction_x = 0;
    int direction_y = 0;
    int width = 32;
    int height = 32;
    std::string reuse_source_map_id;
    bool linked = true;
};

struct ViewportGesture {
    bool hovered = false;
    bool left_clicked = false;
    bool left_down = false;
    bool left_released = false;
    bool right_clicked = false;
    bool middle_down = false;
    bool double_clicked = false;
    float local_x = 0.0f;
    float local_y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float wheel = 0.0f;
    bool top_down = false;
    int tile_x = -1;
    int tile_y = -1;
};

struct EditorUiEvents {
    std::optional<std::string> activate_map_id;
    std::optional<std::string> select_world_map_id;
    std::optional<std::string> activate_asset_id;
    std::optional<AssetKind> activate_asset_kind;
    std::optional<std::string> activate_category;
    std::optional<std::size_t> activate_layer_index;
    std::optional<EditorTool> activate_tool;
    std::optional<EditorViewMode> view_mode;
    std::optional<int> height_brush_value;
    std::optional<bool> collision_brush_value;
    std::optional<int> set_cell_height;
    std::optional<int> set_cell_special;
    std::optional<std::string> set_spawn_tile_use;
    std::optional<bool> set_cell_collision;
    std::optional<double> animation_time_seconds;
    std::optional<std::pair<std::size_t, std::string>> rename_layer;
    std::optional<std::pair<std::size_t, bool>> set_layer_visible;
    std::optional<std::string> door_destination_map_id;
    std::optional<std::string> door_destination_anchor_id;
    std::optional<std::string> door_direction;
    std::optional<std::string> door_script_id;
    std::optional<std::string> anchor_facing;
    std::optional<std::string> select_door_id;
    std::optional<std::string> select_anchor_id;
    std::optional<std::pair<int, int>> move_selection_tile;
    std::optional<MapMoveRequest> move_map;
    std::optional<MapResizeRequest> resize_map;
    std::optional<NewMapRequest> create_map;
    bool save = false;
    bool undo = false;
    bool redo = false;
    bool delete_selection = false;
    bool duplicate_selection = false;
    bool focus_selection = false;
    bool toggle_animations = false;
    bool toggle_grid = false;
    bool toggle_collision = false;
    bool validate = false;
    bool reveal_log = false;
    bool inspect_mode = false;
    bool add_layer = false;
    bool delete_layer = false;
    bool move_layer_up = false;
    bool move_layer_down = false;
    bool refresh_preview = false;
    bool restart_animation = false;
    bool add_anchor_at_selection = false;
    bool add_south_entry_anchors = false;
    bool play_reset = false;
    bool play_start_from_selection = false;
    bool play_focus_player = false;
    int play_move_x = 0;
    int play_move_y = 0;
    ViewportGesture viewport;
};

class EditorShell {
public:
    EditorUiEvents draw(EditorUiModel& model);

private:
    float right_panel_width_ = 390.0f;
    float inspector_split_ = 0.58f;
    char search_[128]{};
    bool diagnostics_open_ = false;
    bool validation_open_ = false;
    float top_down_cell_size_ = 32.0f;
    float top_down_pan_x_ = 0.0f;
    float top_down_pan_y_ = 0.0f;
    bool top_down_pan_initialized_ = false;
    std::string top_down_map_id_;
    std::uint64_t top_down_focus_serial_ = 0;
    std::string resize_map_id_;
    int resize_width_ = 1;
    int resize_height_ = 1;
    int resize_observed_width_ = 0;
    int resize_observed_height_ = 0;
    std::array<char, 96> layer_name_{};
    std::size_t layer_name_index_ = static_cast<std::size_t>(-1);
    float world_zoom_ = 1.0f;
    float world_pan_x_ = 0.0f;
    float world_pan_y_ = 0.0f;
    bool world_initialized_ = false;
    std::string world_drag_map_id_;
    int world_drag_start_x_ = 0;
    int world_drag_start_y_ = 0;
    int world_drag_current_x_ = 0;
    int world_drag_current_y_ = 0;
    bool create_map_popup_ = false;
    NewMapRequest pending_new_map_;
    std::array<char, 96> new_map_id_{};
    std::array<char, 128> new_map_name_{};
    std::array<char, 128> world_search_{};
    PlayInputCapture play_input_;

    void drawHeader(EditorUiModel& model, EditorUiEvents& events);
    void drawMapTabs(EditorUiModel& model, EditorUiEvents& events);
    void drawAssetBrowser(EditorUiModel& model, EditorUiEvents& events, float height);
    void drawToolRail(EditorUiModel& model, EditorUiEvents& events, float height);
    void drawToolPanel(EditorUiModel& model, EditorUiEvents& events, float height);
    void drawSelectionInspector(EditorUiModel& model, EditorUiEvents& events, float height);
    void drawContextBar(EditorUiModel& model, EditorUiEvents& events);
    void drawViewport(EditorUiModel& model, EditorUiEvents& events);
    void drawWorldViewport(EditorUiModel& model, EditorUiEvents& events);
    void beginWorldMapCreation(
        const EditorUiModel& model,
        std::string source_map_id = {},
        int direction_x = 0,
        int direction_y = 0,
        const char* direction_name = nullptr);
    void drawWorldMapCreationModal(EditorUiModel& model, EditorUiEvents& events);
    void drawTopDownViewport(EditorUiModel& model, EditorUiEvents& events);
    void drawGameViewport(EditorUiModel& model, EditorUiEvents& events);
    void drawInspector(EditorUiModel& model, EditorUiEvents& events, float height);
    void drawDoorEditor(EditorUiModel& model, EditorUiEvents& events);
    void drawTravelObjects(EditorUiModel& model, EditorUiEvents& events);
    void drawSelectionPosition(EditorUiModel& model, EditorUiEvents& events);
    void drawDiagnostics(EditorUiModel& model, EditorUiEvents& events);
};

void applyMapMakerStyle();

} // namespace pr::mapmaker
