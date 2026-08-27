#pragma once

#include "mapmaker/app/AutosaveRecovery.hpp"
#include "mapmaker/app/FrameMetrics.hpp"
#include "mapmaker/app/ProjectWorkspace.hpp"
#include "mapmaker/assets/EditorAssetCatalog.hpp"
#include "mapmaker/document/DocumentCommands.hpp"
#include "mapmaker/document/MapMetadataEditing.hpp"
#include "mapmaker/logging/StructuredLogger.hpp"
#include "mapmaker/preview/ExactWorldPreview.hpp"
#include "mapmaker/preview/ModelTopDownProjection.hpp"
#include "mapmaker/selection/SelectionModel.hpp"
#include "mapmaker/ui/EditorShell.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pr::mapmaker {

class BgfxThumbnailCache;
class EditorController {
public:
    EditorController(
        std::filesystem::path resort_root,
        ProjectWorkspace& workspace,
        ExactWorldPreview& preview,
        RtpksEditorCatalog& tile_catalog,
        std::vector<ModelAsset> model_catalog,
        BgfxThumbnailCache& thumbnails,
        AutosaveRecovery& recovery,
        StructuredLogger& logger);
    void setViewportTexture(const ExactWorldPreview::ViewportTexture& texture);
    EditorUiModel buildUiModel(const FrameMetrics& metrics);
    void handle(const EditorUiEvents& events);
    void tickRecovery();

    bool previewReloadPending() const { return preview_reload_pending_; }
    bool reloadPreview();
    bool gamePreviewVisible() const { return view_mode_ == EditorViewMode::GamePreview; }
    bool quitRequested() const { return quit_requested_; }
    const std::string& status() const { return status_; }

private:
    struct PaintStroke {
        std::vector<std::pair<int, int>> cells;
        bool active = false;
    };

    struct DragState {
        SelectionItem item;
        int start_x = -1;
        int start_y = -1;
        int current_x = -1;
        int current_y = -1;
        int grab_offset_x = 0;
        int grab_offset_y = 0;
        bool active = false;
    };

    std::filesystem::path resort_root_;
    ProjectWorkspace* workspace_ = nullptr;
    ExactWorldPreview* preview_ = nullptr;
    RtpksEditorCatalog* tile_catalog_ = nullptr;
    std::vector<ModelAsset> model_catalog_;
    std::vector<AssetView> asset_views_;
    std::vector<std::string> tile_categories_;
    std::vector<ChoiceView> door_script_choices_;
    BgfxThumbnailCache* thumbnails_ = nullptr;
    AutosaveRecovery* recovery_ = nullptr;
    StructuredLogger* logger_ = nullptr;
    ExactWorldPreview::ViewportTexture viewport_texture_{};
    SelectionModel selection_;
    std::vector<ValidationDiagnostic> diagnostics_;
    AssetKind active_asset_kind_ = AssetKind::Tile;
    std::string active_asset_id_;
    std::string active_category_;
    std::size_t active_layer_index_ = 0;
    PaintStroke paint_;
    DragState drag_;
    std::optional<std::pair<int, int>> hovered_cell_;
    float previous_middle_x_ = 0.0f;
    float previous_middle_y_ = 0.0f;
    bool middle_was_down_ = false;
    bool preview_reload_pending_ = false;
    std::string preview_source_key_;
    bool top_down_cache_dirty_ = true;
    bool grid_overlay_ = true;
    bool collision_overlay_ = false;
    bool quit_requested_ = false;
    EditorTool active_tool_ = EditorTool::Select;
    EditorViewMode view_mode_ = EditorViewMode::World;
    int height_brush_value_ = 0;
    bool collision_brush_value_ = true;
    std::vector<int> composed_tiles_;
    std::vector<int> active_layer_tiles_;
    std::vector<std::uint8_t> automatic_collision_;
    std::vector<TileLayerProjection> cached_layers_;
    std::vector<TopDownMarkerView> top_down_markers_;
    std::unordered_map<std::string, ModelTopDownProjection> model_top_down_projections_;
    std::vector<WorldMapNodeView> world_maps_cache_;
    std::vector<WorldConnectionView> world_connections_cache_;
    bool world_cache_dirty_ = true;
    int top_down_focus_x_ = 0;
    int top_down_focus_y_ = 0;
    std::uint64_t top_down_focus_serial_ = 0;
    std::string status_ = "Ready";

    const AssetView* activeAsset(const std::vector<AssetView>& assets) const;
    const ModelAsset* modelAsset(const std::string& id) const;
    std::optional<std::size_t> placedModelIndex(const SelectionItem& item) const;
    void rebuildAssetViews();
    const std::vector<AssetView>& assets() const { return asset_views_; }
    std::optional<std::pair<int, int>> pickCell(const ViewportGesture& gesture) const;
    SelectionItem selectionAt(int tile_x, int tile_y) const;
    void handleViewport(const ViewportGesture& gesture);
    void handleLayerEvents(const EditorUiEvents& events);
    void handleTerrainInspectorEvents(const EditorUiEvents& events);
    void handleTravelEvents(const EditorUiEvents& events);
    void handleWorldEvents(const EditorUiEvents& events);
    void populateWorldUiModel(EditorUiModel& model);
    void commitPaint();
    void commitDrag();
    void placeActiveAsset(int tile_x, int tile_y);
    void deleteSelection();
    void duplicateSelection();
    void focusSelection();
    void executeMutation(const std::string& label, OwmapMutationCommand::Mutation mutation);
    void refreshDiagnostics();
    void requestPreviewReload();
    void rebuildTopDownCache();
    const ModelTopDownProjection& modelTopDownProjection(const std::filesystem::path& glb_path);
    std::filesystem::path previewPath() const;
    void log(LogLevel level, const std::string& category, const std::string& message);
};

} // namespace pr::mapmaker
