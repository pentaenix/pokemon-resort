#pragma once

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
    std::vector<InspectorField> fields;
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
    bool grid_overlay = true;
    bool collision_overlay = false;
    double fps = 0.0;
    double frame_ms_p95 = 0.0;
    double input_latency_ms = 0.0;
    int scene_rebuild_count = 0;

    // Called only for visible tile cards, enabling lazy thumbnail decode/upload.
    std::function<std::uint16_t(int)> tile_thumbnail;
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
};

struct EditorUiEvents {
    std::optional<std::string> activate_map_id;
    std::optional<std::string> activate_asset_id;
    std::optional<AssetKind> activate_asset_kind;
    std::optional<std::string> activate_category;
    std::optional<std::size_t> activate_layer_index;
    std::optional<std::string> door_destination_map_id;
    std::optional<std::string> door_destination_anchor_id;
    std::optional<std::string> door_direction;
    std::optional<std::string> door_script_id;
    std::optional<std::string> select_door_id;
    std::optional<std::string> select_anchor_id;
    std::optional<std::pair<int, int>> move_selection_tile;
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
    bool open_project = false;
    bool inspect_mode = false;
    bool add_anchor_at_selection = false;
    ViewportGesture viewport;
};

class EditorShell {
public:
    EditorUiEvents draw(EditorUiModel& model);

private:
    float left_panel_width_ = 286.0f;
    float right_panel_width_ = 318.0f;
    char search_[128]{};
    bool diagnostics_open_ = false;
    bool validation_open_ = false;

    void drawHeader(EditorUiModel& model, EditorUiEvents& events);
    void drawMapTabs(EditorUiModel& model, EditorUiEvents& events);
    void drawAssetBrowser(EditorUiModel& model, EditorUiEvents& events, float height);
    void drawContextBar(EditorUiModel& model, EditorUiEvents& events);
    void drawViewport(EditorUiModel& model, EditorUiEvents& events);
    void drawInspector(EditorUiModel& model, EditorUiEvents& events, float height);
    void drawDoorEditor(EditorUiModel& model, EditorUiEvents& events);
    void drawTravelObjects(EditorUiModel& model, EditorUiEvents& events);
    void drawSelectionPosition(EditorUiModel& model, EditorUiEvents& events);
    void drawDiagnostics(EditorUiModel& model, EditorUiEvents& events);
};

void applyMapMakerStyle();

} // namespace pr::mapmaker
