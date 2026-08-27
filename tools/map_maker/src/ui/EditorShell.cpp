#include "mapmaker/ui/EditorShell.hpp"

#include <bgfx/bgfx.h>
#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>

namespace pr::mapmaker {
namespace {

constexpr float kHeaderHeight = 50.0f;
constexpr float kStatusHeight = 26.0f;
constexpr float kSplitterWidth = 5.0f;
constexpr float kContextHeight = 44.0f;

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
    style.WindowPadding = ImVec2(8.0f, 8.0f);
    style.FramePadding = ImVec2(10.0f, 7.0f);
    style.ItemSpacing = ImVec2(8.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.WindowRounding = 0.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 10.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 7.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.91f, 0.93f, 0.96f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.48f, 0.54f, 0.63f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.039f, 0.045f, 0.058f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.057f, 0.066f, 0.083f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.065f, 0.075f, 0.095f, 0.98f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.15f, 0.18f, 0.23f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.09f, 0.105f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.13f, 0.17f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.09f, 0.105f, 0.13f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.13f, 0.30f, 0.40f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.10f, 0.48f, 0.68f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.09f, 0.25f, 0.34f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.36f, 0.47f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.10f, 0.48f, 0.68f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.36f, 0.80f, 0.98f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.14f, 0.17f, 0.22f, 1.0f);
}

EditorUiEvents EditorShell::draw(EditorUiModel& model) {
    EditorUiEvents events;
    play_input_.setPlayVisible(model.view_mode == EditorViewMode::GamePreview);
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("Pokemon Resort Map Maker", nullptr, flags);

    drawHeader(model, events);
    const float content_height = std::max(100.0f,
        ImGui::GetContentRegionAvail().y - kStatusHeight - ImGui::GetStyle().ItemSpacing.y);

    constexpr float tool_rail_width = 60.0f;
    ImGui::BeginChild("##tool_rail", ImVec2(tool_rail_width, content_height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawToolRail(model, events, content_height);
    ImGui::EndChild();
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
    ImGui::BeginChild("##inspector", ImVec2(0.0f, content_height), true,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float tool_height = std::clamp(content_height * inspector_split_, 180.0f,
        std::max(180.0f, content_height - 150.0f));
    ImGui::BeginChild("##tool_options", ImVec2(0.0f, tool_height), false);
    drawToolPanel(model, events, tool_height);
    ImGui::EndChild();
    ImGui::InvisibleButton("##inspector_split", ImVec2(-1.0f, 5.0f));
    if (ImGui::IsItemActive()) {
        inspector_split_ = std::clamp(inspector_split_ +
            ImGui::GetIO().MouseDelta.y / std::max(1.0f, content_height), 0.25f, 0.78f);
    }
    ImGui::BeginChild("##selection_inspector", ImVec2(0.0f, 0.0f), false);
    drawSelectionInspector(model, events, ImGui::GetContentRegionAvail().y);
    ImGui::EndChild();
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
        if (ImGui::Shortcut(ImGuiKey_F6)) {
            events.view_mode = model.view_mode == EditorViewMode::GamePreview
                ? EditorViewMode::TopDown : EditorViewMode::GamePreview;
        }
    }
    if (!ImGui::GetIO().WantTextInput && model.view_mode != EditorViewMode::GamePreview) {
        if (ImGui::Shortcut(ImGuiKey_Q)) events.activate_tool = EditorTool::Select;
        if (ImGui::Shortcut(ImGuiKey_B)) events.activate_tool = EditorTool::Paint;
        if (ImGui::Shortcut(ImGuiKey_E)) events.activate_tool = EditorTool::EraseLayer;
        if (ImGui::Shortcut(ImGuiKey_C)) events.activate_tool = EditorTool::ClearCell;
        if (ImGui::Shortcut(ImGuiKey_H)) events.activate_tool = EditorTool::Height;
        if (ImGui::Shortcut(ImGuiKey_X)) events.activate_tool = EditorTool::Collision;
        if (ImGui::Shortcut(ImGuiKey_O)) events.activate_tool = EditorTool::Objects;
        if (ImGui::Shortcut(ImGuiKey_D)) events.activate_tool = EditorTool::Doors;
        if (ImGui::Shortcut(ImGuiKey_A)) events.activate_tool = EditorTool::Anchors;
        if (ImGui::Shortcut(ImGuiKey_I)) events.activate_tool = EditorTool::Eyedropper;
        if (ImGui::Shortcut(ImGuiKey_S)) events.activate_tool = EditorTool::SmartObjects;
    }
    ImGui::End();
    return events;
}

void EditorShell::drawHeader(EditorUiModel& model, EditorUiEvents& events) {
    ImGui::BeginChild("##header", ImVec2(0.0f, kHeaderHeight), false);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::PushFont(ImGui::Font::Regular, 19.0f);
    ImGui::TextUnformatted("POKEMON RESORT");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("/  %s%s", model.project_name.c_str(), model.dirty ? "  • Unsaved" : "");
    ImGui::SameLine(330.0f);
    const auto workspaceButton = [&](const char* icon, const char* name, EditorViewMode mode) {
        if (model.view_mode == mode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.43f, 0.60f, 1.0f));
        }
        const std::string label = std::string(icon) + "  " + name;
        if (ImGui::Button(label.c_str(), ImVec2(104.0f, 0.0f))) events.view_mode = mode;
        if (model.view_mode == mode) ImGui::PopStyleColor();
    };
    workspaceButton(ICON_FA_GLOBE, "World", EditorViewMode::World);
    ImGui::SameLine();
    workspaceButton(ICON_FA_MAP_O, "Map", EditorViewMode::TopDown);
    ImGui::SameLine();
    workspaceButton(ICON_FA_PLAY, model.preview_stale ? "Play •" : "Play",
        EditorViewMode::GamePreview);
    ImGui::SameLine(ImGui::GetWindowWidth() - 300.0f);
    ImGui::BeginDisabled(!model.can_undo);
    if (ImGui::Button(ICON_FA_UNDO "##undo")) events.undo = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Undo  Cmd+Z");
    ImGui::SameLine();
    ImGui::BeginDisabled(!model.can_redo);
    if (ImGui::Button(ICON_FA_REPEAT "##redo")) events.redo = true;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Redo  Cmd+Shift+Z");
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CHECK_CIRCLE_O "##validate")) events.validate = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Validate project");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, model.dirty
        ? ImVec4(0.08f, 0.48f, 0.39f, 1.0f) : ImVec4(0.09f, 0.16f, 0.17f, 1.0f));
    if (ImGui::Button(ICON_FA_FLOPPY_O "  Save")) events.save = true;
    ImGui::PopStyleColor();
    ImGui::EndChild();
}

void EditorShell::drawMapTabs(EditorUiModel& model, EditorUiEvents& events) {
    const auto active = std::find_if(model.maps.begin(), model.maps.end(),
        [](const MapTabView& map) { return map.active; });
    const char* label = active == model.maps.end() ? "No map" : active->name.c_str();
    if (ImGui::BeginCombo("##active_map", label)) {
        for (const MapTabView& map : model.maps) {
            const std::string item = map.name + (map.dirty ? " *" : "") + "##" + map.id;
            if (ImGui::Selectable(item.c_str(), map.active)) events.activate_map_id = map.id;
        }
        ImGui::EndCombo();
    }
}

void EditorShell::drawContextBar(EditorUiModel& model, EditorUiEvents& events) {
    ImGui::BeginChild("##context", ImVec2(0.0f, kContextHeight), false);
    ImGui::AlignTextToFramePadding();
    if (model.view_mode == EditorViewMode::World) {
        ImGui::TextUnformatted(ICON_FA_COMPASS "  WORLD LAYOUT");
        ImGui::SameLine();
        ImGui::TextDisabled("Exterior edges meet in-world  •  Curves are door travel  •  Double-click opens");
        ImGui::SameLine(ImGui::GetWindowWidth() - 246.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.43f, 0.60f, 1.0f));
        if (ImGui::Button(ICON_FA_PLUS "  New map")) beginWorldMapCreation(model);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create a standalone interior or exterior map");
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CROSSHAIRS "  Center")) world_initialized_ = false;
    } else {
        ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.35f));
        drawMapTabs(model, events);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", model.active_tool_text.c_str());
        if (model.active_tool == EditorTool::Height) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.35f, 0.82f, 1.0f, 1.0f),
                "HEIGHT = %d", model.height_brush_value);
        }
    }
    ImGui::EndChild();
}

void EditorShell::drawViewport(EditorUiModel& model, EditorUiEvents& events) {
    if (model.view_mode == EditorViewMode::World) drawWorldViewport(model, events);
    else if (model.view_mode == EditorViewMode::TopDown) drawTopDownViewport(model, events);
    else drawGameViewport(model, events);
}

void EditorShell::drawGameViewport(EditorUiModel& model, EditorUiEvents& events) {
    if (ImGui::Button(model.animations_enabled
        ? ICON_FA_PAUSE "  Pause" : ICON_FA_PLAY "  Animate")) {
        events.toggle_animations = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_REPEAT "  Reset")) events.play_reset = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CROSSHAIRS "  Focus")) events.play_focus_player = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_STEP_BACKWARD "##restart")) events.restart_animation = true;
    ImGui::SameLine();
    if (ImGui::Button(model.preview_stale
        ? ICON_FA_REFRESH "  Sync •" : ICON_FA_REFRESH "  Sync")) events.refresh_preview = true;
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
    const bool pointer_clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    play_input_.handlePointer(gesture.hovered && pointer_clicked,
        !gesture.hovered && pointer_clicked);
    play_input_.handleEscape(ImGui::IsKeyPressed(ImGuiKey_Escape, false));
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
    const ImVec2 image_max{image_min.x + image_size.x, image_min.y + image_size.y};
    draw->AddRect(image_min, image_max, play_input_.captured()
        ? IM_COL32(66, 214, 157, 255) : IM_COL32(92, 110, 134, 220),
        2.0f, 0, play_input_.captured() ? 3.0f : 1.0f);
    const char* capture_label = play_input_.captured()
        ? ICON_FA_GAMEPAD "  PLAYING  •  Esc to release"
        : ICON_FA_MOUSE_POINTER "  Click viewport to control";
    const ImVec2 capture_size = ImGui::CalcTextSize(capture_label);
    const ImVec2 capture_min{image_min.x + 12.0f, image_min.y + 12.0f};
    draw->AddRectFilled(capture_min,
        ImVec2{capture_min.x + capture_size.x + 18.0f, capture_min.y + capture_size.y + 12.0f},
        play_input_.captured() ? IM_COL32(19, 74, 58, 238) : IM_COL32(18, 23, 31, 225), 7.0f);
    draw->AddText(ImVec2{capture_min.x + 9.0f, capture_min.y + 6.0f},
        play_input_.captured() ? IM_COL32(145, 244, 202, 255) : IM_COL32(190, 201, 216, 255),
        capture_label);
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
    if (play_input_.captured() && !ImGui::GetIO().WantTextInput) {
        events.play_move_x = (ImGui::IsKeyDown(ImGuiKey_D) || ImGui::IsKeyDown(ImGuiKey_RightArrow)) -
            (ImGui::IsKeyDown(ImGuiKey_A) || ImGui::IsKeyDown(ImGuiKey_LeftArrow));
        events.play_move_y = (ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_DownArrow)) -
            (ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_UpArrow));
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
            constexpr std::array<std::pair<int, const char*>, 14> specials{{
                {0, "Flat"}, {2, "Ramp north"}, {3, "Ramp east"},
                {4, "Ramp south"}, {5, "Ramp west"}, {6, "Convex NE"},
                {7, "Convex SE"}, {8, "Convex SW"}, {9, "Convex NW"},
                {10, "Concave NE"}, {11, "Concave SE"}, {12, "Concave SW"},
                {13, "Concave NW"}, {14, "Actor spawn"}}};
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
            if (model.selection.special == 14) {
                constexpr std::array<std::pair<const char*, const char*>, 3> uses{{
                    {"pokemon_random_from_boxes", "Random Pokémon from boxes"},
                    {"npc_with_partner", "NPC with partner Pokémon"},
                    {"npc_without_pokemon", "NPC without Pokémon"}}};
                const auto selected = std::find_if(uses.begin(), uses.end(), [&](const auto& item) {
                    return model.selection.spawn_tile_use == item.first;
                });
                const char* label = selected == uses.end() ? uses[0].second : selected->second;
                ImGui::SetNextItemWidth(-1.0f);
                if (ImGui::BeginCombo("Allowed occupant", label)) {
                    for (const auto& [id, name] : uses) {
                        if (ImGui::Selectable(name, model.selection.spawn_tile_use == id)) {
                            events.set_spawn_tile_use = id;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            if (ImGui::Checkbox("Collision blocked", &collision)) {
                events.set_cell_collision = collision;
            }
            if (model.selection.automatic_collision) {
                ImGui::TextColored(ImVec4(0.83f, 0.66f, 1.0f, 1.0f),
                    "Blocked by the default room wall");
                ImGui::TextWrapped(
                    "Add an opening or reduce walkableInsetTiles to make this cell walkable.");
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
