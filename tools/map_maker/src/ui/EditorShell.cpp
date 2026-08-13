#include "mapmaker/ui/EditorShell.hpp"

#include <bgfx/bgfx.h>
#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

namespace pr::mapmaker {
namespace {

constexpr float kHeaderHeight = 42.0f;
constexpr float kMapTabsHeight = 35.0f;
constexpr float kStatusHeight = 26.0f;
constexpr float kSplitterWidth = 5.0f;
constexpr float kContextHeight = 41.0f;

const char* selectionKindLabel(InspectorSelectionKind kind) {
    switch (kind) {
        case InspectorSelectionKind::TerrainCell: return "Terrain cell";
        case InspectorSelectionKind::Tile: return "Tile";
        case InspectorSelectionKind::Model: return "Object";
        case InspectorSelectionKind::Door: return "Door";
        case InspectorSelectionKind::Anchor: return "Anchor";
        case InspectorSelectionKind::Trigger: return "Trigger";
        default: return "Nothing selected";
    }
}

void drawSplitter(const char* id, float& width, float min_width, float max_width, bool reverse) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.50f, 0.68f, 1.0f));
    ImGui::Button(id, ImVec2(kSplitterWidth, -1.0f));
    if (ImGui::IsItemActive()) {
        const float delta = ImGui::GetIO().MouseDelta.x * (reverse ? -1.0f : 1.0f);
        width = std::clamp(width + delta, min_width, max_width);
    }
    ImGui::PopStyleColor(2);
}

} // namespace

void applyMapMakerStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(10.0f, 6.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 5.0f;
    style.PopupRounding = 6.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.063f, 0.080f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.086f, 0.110f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.16f, 0.19f, 0.24f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.11f, 0.125f, 0.16f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.16f, 0.22f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.15f, 0.19f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.39f, 0.54f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.55f, 0.76f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.28f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.18f, 0.43f, 0.58f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.55f, 0.76f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.36f, 0.78f, 0.96f, 1.0f);
}

EditorUiEvents EditorShell::draw(EditorUiModel& model) {
    EditorUiEvents events;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("Pokemon Resort Map Maker", nullptr, flags);

    drawHeader(model, events);
    drawMapTabs(model, events);
    const float content_height = std::max(100.0f,
        ImGui::GetContentRegionAvail().y - kStatusHeight - ImGui::GetStyle().ItemSpacing.y);

    ImGui::BeginChild("##assets", ImVec2(left_panel_width_, content_height), true);
    drawAssetBrowser(model, events, content_height);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    drawSplitter("##left_split", left_panel_width_, 220.0f, 460.0f, false);
    ImGui::SameLine(0.0f, 0.0f);

    const float center_width = std::max(200.0f,
        ImGui::GetContentRegionAvail().x - right_panel_width_ - kSplitterWidth);
    ImGui::BeginChild("##center", ImVec2(center_width, content_height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawContextBar(model, events);
    drawViewport(model, events);
    ImGui::EndChild();
    ImGui::SameLine(0.0f, 0.0f);
    drawSplitter("##right_split", right_panel_width_, 250.0f, 480.0f, true);
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::BeginChild("##inspector", ImVec2(0.0f, content_height), true);
    drawInspector(model, events, content_height);
    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextUnformatted(model.status_text.c_str());
    ImGui::SameLine(ImGui::GetWindowWidth() - 330.0f);
    ImGui::Text("%.1f FPS  |  p95 %.1f ms  |  Input %.1f ms",
        model.fps, model.frame_ms_p95, model.input_latency_ms);

    drawDiagnostics(model, events);
    // With ConfigMacOSXBehaviors enabled, ImGui routes Ctrl shortcuts to Cmd.
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) events.save = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) events.undo = true;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)) events.redo = true;
    if (model.selection.kind != InspectorSelectionKind::None &&
        ImGui::Shortcut(ImGuiKey_Delete)) events.delete_selection = true;
    if (model.selection.kind != InspectorSelectionKind::None &&
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) events.duplicate_selection = true;
    if (model.selection.kind != InspectorSelectionKind::None &&
        ImGui::Shortcut(ImGuiKey_F)) events.focus_selection = true;
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::Shortcut(ImGuiKey_Q)) events.activate_tool = EditorTool::Select;
        if (ImGui::Shortcut(ImGuiKey_B)) events.activate_tool = EditorTool::Paint;
        if (ImGui::Shortcut(ImGuiKey_E)) events.activate_tool = EditorTool::EraseLayer;
        if (ImGui::Shortcut(ImGuiKey_C)) events.activate_tool = EditorTool::ClearCell;
        if (ImGui::Shortcut(ImGuiKey_H)) events.activate_tool = EditorTool::Height;
        if (ImGui::Shortcut(ImGuiKey_X)) events.activate_tool = EditorTool::Collision;
    }
    ImGui::End();
    return events;
}

void EditorShell::drawHeader(EditorUiModel& model, EditorUiEvents& events) {
    ImGui::BeginChild("##header", ImVec2(0.0f, kHeaderHeight), false);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("POKEMON RESORT");
    ImGui::SameLine();
    ImGui::TextDisabled("Map Maker  /  %s%s", model.project_name.c_str(), model.dirty ? "  *" : "");
    ImGui::SameLine(ImGui::GetWindowWidth() - 420.0f);
    ImGui::BeginDisabled(!model.can_undo);
    if (ImGui::Button("Undo")) events.undo = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!model.can_redo);
    if (ImGui::Button("Redo")) events.redo = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Validate")) events.validate = true;
    ImGui::SameLine();
    if (ImGui::Button(model.dirty ? "Save *" : "Save")) events.save = true;
    ImGui::EndChild();
}

void EditorShell::drawMapTabs(EditorUiModel& model, EditorUiEvents& events) {
    ImGui::BeginChild("##map_tabs", ImVec2(0.0f, kMapTabsHeight), false,
        ImGuiWindowFlags_HorizontalScrollbar);
    for (std::size_t index = 0; index < model.maps.size(); ++index) {
        if (index > 0) ImGui::SameLine();
        const MapTabView& map = model.maps[index];
        if (map.active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.46f, 0.63f, 1.0f));
        }
        const std::string label = map.name + (map.dirty ? " *" : "") +
            (map.shared_source ? "  [linked]" : "") + "##" + map.id;
        if (ImGui::Button(label.c_str())) events.activate_map_id = map.id;
        if (map.active) ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

void EditorShell::drawContextBar(EditorUiModel& model, EditorUiEvents& events) {
    ImGui::BeginChild("##context", ImVec2(0.0f, kContextHeight), false);
    ImGui::AlignTextToFramePadding();
    const auto toolButton = [&](const char* label, EditorTool tool) {
        if (model.active_tool == tool) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.46f, 0.63f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(58.0f, 0.0f))) events.activate_tool = tool;
        if (model.active_tool == tool) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    toolButton("Select", EditorTool::Select);
    toolButton("Paint", EditorTool::Paint);
    toolButton("Erase", EditorTool::EraseLayer);
    toolButton("Clear", EditorTool::ClearCell);
    toolButton("Height", EditorTool::Height);
    toolButton("Block", EditorTool::Collision);

    if (model.active_tool == EditorTool::Height) {
        int height = model.height_brush_value;
        ImGui::SetNextItemWidth(74.0f);
        if (ImGui::InputInt("##height_brush", &height, 1, 4)) {
            events.height_brush_value = std::clamp(height, 0, 255);
        }
        ImGui::SameLine();
    } else if (model.active_tool == EditorTool::Collision) {
        bool blocked = model.collision_brush_value;
        if (ImGui::Checkbox("Blocked", &blocked)) events.collision_brush_value = blocked;
        ImGui::SameLine();
    }

    ImGui::SameLine(ImGui::GetWindowWidth() - 248.0f);
    const auto modeButton = [&](const char* label, EditorViewMode mode, float width) {
        if (model.view_mode == mode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.46f, 0.63f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(width, 0.0f))) events.view_mode = mode;
        if (model.view_mode == mode) ImGui::PopStyleColor();
    };
    modeButton("Top down", EditorViewMode::TopDown, 88.0f);
    ImGui::SameLine();
    modeButton(model.preview_stale ? "Game preview *" : "Game preview",
        EditorViewMode::GamePreview, 116.0f);
    ImGui::EndChild();
}

void EditorShell::drawViewport(EditorUiModel& model, EditorUiEvents& events) {
    if (model.view_mode == EditorViewMode::TopDown) drawTopDownViewport(model, events);
    else drawGameViewport(model, events);
}

void EditorShell::drawGameViewport(EditorUiModel& model, EditorUiEvents& events) {
    if (ImGui::Button(model.animations_enabled ? "Pause animation" : "Play animation")) {
        events.toggle_animations = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) events.restart_animation = true;
    ImGui::SameLine();
    if (ImGui::Button(model.preview_stale ? "Refresh *" : "Refresh")) events.refresh_preview = true;
    ImGui::SameLine();
    if (ImGui::Button(model.grid_overlay ? "Grid on" : "Grid off")) events.toggle_grid = true;
    ImGui::SameLine();
    if (ImGui::Button(model.collision_overlay ? "Collision on" : "Collision off")) {
        events.toggle_collision = true;
    }
    double animation_time = model.animation_time_seconds;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x));
    if (ImGui::DragScalar("##animation_time", ImGuiDataType_Double, &animation_time,
        0.02f, nullptr, nullptr, "%.2f s")) {
        events.animation_time_seconds = std::max(0.0, animation_time);
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float source_width = static_cast<float>(std::max(1, model.viewport_texture_width));
    const float source_height = static_cast<float>(std::max(1, model.viewport_texture_height));
    const float scale = std::max(0.01f, std::min(available.x / source_width, available.y / source_height));
    const ImVec2 image_size{std::floor(source_width * scale), std::floor(source_height * scale)};
    const ImVec2 cursor = ImGui::GetCursorPos();
    ImGui::SetCursorPos(ImVec2(
        cursor.x + std::max(0.0f, (available.x - image_size.x) * 0.5f),
        cursor.y + std::max(0.0f, (available.y - image_size.y) * 0.5f)));
    const ImVec2 image_min = ImGui::GetCursorScreenPos();
    if (model.viewport_texture != UINT16_MAX) {
        const ImVec2 uv0 = model.viewport_origin_bottom_left ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
        const ImVec2 uv1 = model.viewport_origin_bottom_left ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
        ImGui::Image(bgfx::TextureHandle{model.viewport_texture}, image_size, uv0, uv1);
    } else {
        ImGui::Button("Loading exact game viewport...", image_size);
    }

    auto& gesture = events.viewport;
    gesture.hovered = ImGui::IsItemHovered();
    gesture.left_clicked = gesture.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    gesture.left_down = gesture.hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    gesture.left_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    gesture.right_clicked = gesture.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    gesture.middle_down = gesture.hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    gesture.double_clicked = gesture.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    gesture.local_x = ImGui::GetIO().MousePos.x - image_min.x;
    gesture.local_y = ImGui::GetIO().MousePos.y - image_min.y;
    gesture.width = image_size.x;
    gesture.height = image_size.y;
    gesture.wheel = gesture.hovered ? ImGui::GetIO().MouseWheel : 0.0f;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const float overlay_scale_x = image_size.x / source_width;
    const float overlay_scale_y = image_size.y / source_height;
    for (const ViewportOverlayQuad& overlay : model.viewport_overlays) {
        ImVec2 points[4];
        for (int index = 0; index < 4; ++index) {
            points[index] = ImVec2{
                image_min.x + overlay.xy[static_cast<std::size_t>(index * 2)] * overlay_scale_x,
                image_min.y + overlay.xy[static_cast<std::size_t>(index * 2 + 1)] * overlay_scale_y};
        }
        draw->AddPolyline(points, 4, overlay.color, ImDrawFlags_Closed, overlay.thickness);
    }
}

void EditorShell::drawInspector(EditorUiModel& model, EditorUiEvents& events, float) {
    ImGui::TextUnformatted("Inspector");
    ImGui::Separator();
    ImGui::TextDisabled("%s", selectionKindLabel(model.selection.kind));
    if (model.selection.kind == InspectorSelectionKind::None) {
        ImGui::Spacing();
        ImGui::TextWrapped("Click any tile, object, or door in the viewport. Drag selected items directly; no separate move tool is required.");
    } else {
        ImGui::TextWrapped("%s", model.selection.title.c_str());
        if (model.selection.tile_x >= -1 && model.selection.tile_y >= -1) {
            ImGui::Text("Tile: %d, %d", model.selection.tile_x, model.selection.tile_y);
        }
        ImGui::Spacing();
        if (ImGui::Button("Focus", ImVec2(92.0f, 0.0f))) events.focus_selection = true;
        ImGui::SameLine();
        if (ImGui::Button("Duplicate", ImVec2(92.0f, 0.0f))) events.duplicate_selection = true;
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(80.0f, 0.0f))) events.delete_selection = true;
        ImGui::Separator();
        if (model.selection.terrain_editable) {
            int height = model.selection.height;
            bool collision = model.selection.collision;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt("Height", &height, 1, 4)) {
                events.set_cell_height = std::clamp(height, 0, 255);
            }
            constexpr std::array<std::pair<int, const char*>, 13> specials{{
                {0, "Flat"}, {2, "Ramp north"}, {3, "Ramp east"},
                {4, "Ramp south"}, {5, "Ramp west"}, {6, "Convex NE"},
                {7, "Convex SE"}, {8, "Convex SW"}, {9, "Convex NW"},
                {10, "Concave NE"}, {11, "Concave SE"}, {12, "Concave SW"},
                {13, "Concave NW"}}};
            const auto current_special = std::find_if(specials.begin(), specials.end(),
                [&](const auto& value) { return value.first == model.selection.special; });
            const char* special_label = current_special == specials.end()
                ? "Unknown" : current_special->second;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("Special", special_label)) {
                for (const auto& [value, label] : specials) {
                    if (ImGui::Selectable(label, value == model.selection.special)) {
                        events.set_cell_special = value;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::Checkbox("Collision blocked", &collision)) {
                events.set_cell_collision = collision;
            }
            ImGui::Separator();
        }
        if (ImGui::BeginTable("##properties", 2,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 105.0f);
            ImGui::TableSetupColumn("Value");
            for (const InspectorField& field : model.selection.fields) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", field.label.c_str());
                ImGui::TableSetColumnIndex(1);
                if (field.warning) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.62f, 0.25f, 1.0f));
                ImGui::TextWrapped("%s", field.value.c_str());
                if (field.warning) ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        if (model.door_editor.active) drawDoorEditor(model, events);
        drawSelectionPosition(model, events);
    }

    drawTravelObjects(model, events);

    ImGui::Spacing();
    ImGui::SeparatorText("Validation");
    int errors = 0;
    int warnings = 0;
    for (const auto& diagnostic : model.validation) {
        errors += diagnostic.severity == "error" ? 1 : 0;
        warnings += diagnostic.severity == "warning" ? 1 : 0;
    }
    ImGui::Text("%d errors  |  %d warnings", errors, warnings);
    if (ImGui::Button("View diagnostics")) validation_open_ = true;
}

void EditorShell::drawDiagnostics(EditorUiModel& model, EditorUiEvents& events) {
    if (validation_open_) {
        ImGui::SetNextWindowSize(ImVec2(720.0f, 410.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Validation", &validation_open_)) {
            if (model.validation.empty()) ImGui::TextUnformatted("No diagnostics.");
            for (const auto& diagnostic : model.validation) {
                const ImVec4 color = diagnostic.severity == "error"
                    ? ImVec4(1.0f, 0.35f, 0.32f, 1.0f)
                    : ImVec4(1.0f, 0.68f, 0.25f, 1.0f);
                ImGui::TextColored(color, "%s", diagnostic.severity.c_str());
                ImGui::SameLine(90.0f);
                ImGui::TextWrapped("[%s] %s", diagnostic.code.c_str(), diagnostic.message.c_str());
            }
        }
        ImGui::End();
    }
    if (diagnostics_open_) {
        ImGui::SetNextWindowSize(ImVec2(410.0f, 240.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Performance diagnostics", &diagnostics_open_)) {
            ImGui::Text("FPS: %.1f", model.fps);
            ImGui::Text("Frame p95: %.2f ms", model.frame_ms_p95);
            ImGui::Text("Input latency: %.2f ms", model.input_latency_ms);
            ImGui::Text("Scene rebuilds: %d", model.scene_rebuild_count);
            ImGui::TextWrapped("Edit mode freezes authored animations. Scene rebuilds occur only after a gesture commits, never for each pointer event.");
            if (ImGui::Button("Reveal log")) events.reveal_log = true;
        }
        ImGui::End();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_D) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Super | ImGuiMod_Shift | ImGuiKey_D)) {
        diagnostics_open_ = !diagnostics_open_;
    }
}

} // namespace pr::mapmaker
