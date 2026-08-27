#include "mapmaker/ui/EditorShell.hpp"

#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace pr::mapmaker {
namespace {

struct ToolButton {
    EditorTool tool;
    const char* label;
    const char* name;
    const char* shortcut;
    int group;
};

constexpr std::array<ToolButton, 12> kMapTools{{
    {EditorTool::Select, ICON_FA_MOUSE_POINTER "##select", "Select / move", "Q", 0},
    {EditorTool::Hand, ICON_FA_HAND_PAPER_O "##hand", "Pan canvas", "Space", 0},
    {EditorTool::Paint, ICON_FA_PAINT_BRUSH "##terrain", "Terrain tiles", "B", 1},
    {EditorTool::EraseLayer, ICON_FA_ERASER "##erase", "Erase active layer", "E", 1},
    {EditorTool::ClearCell, ICON_FA_TRASH "##clear", "Clear entire cell", "C", 1},
    {EditorTool::Height, ICON_FA_SLIDERS "##height", "Height brush", "H", 2},
    {EditorTool::Collision, ICON_FA_BAN "##collision", "Collision brush", "X", 2},
    {EditorTool::Objects, ICON_FA_CUBE "##objects", "Objects", "O", 3},
    {EditorTool::Doors, ICON_FA_SIGN_IN "##doors", "Doors and triggers", "D", 3},
    {EditorTool::Anchors, ICON_FA_ANCHOR "##anchors", "Arrival anchors", "A", 3},
    {EditorTool::SmartObjects, ICON_FA_MAGIC "##smart", "Smart objects", "S", 3},
    {EditorTool::Eyedropper, ICON_FA_EYEDROPPER "##eyedropper", "Eyedropper", "I", 4},
}};

void drawRailButton(const ToolButton& button, bool active, EditorUiEvents& events) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.46f, 0.66f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.13f, 0.55f, 0.76f, 1.0f));
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.075f, 0.086f, 0.105f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.19f, 0.24f, 1.0f));
    }
    ImGui::PushFont(ImGui::Font::Regular, 21.0f);
    if (ImGui::Button(button.label, ImVec2(42.0f, 42.0f))) events.activate_tool = button.tool;
    ImGui::PopFont();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    if (active) {
        const ImVec2 minimum = ImGui::GetItemRectMin();
        const ImVec2 maximum = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2{minimum.x - 5.0f, minimum.y + 8.0f},
            ImVec2{minimum.x - 2.0f, maximum.y - 8.0f}, IM_COL32(93, 213, 255, 255), 2.0f);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(button.name);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", button.shortcut);
        ImGui::EndTooltip();
    }
}

const char* selectionLabel(InspectorSelectionKind kind) {
    switch (kind) {
        case InspectorSelectionKind::TerrainCell: return "CELL";
        case InspectorSelectionKind::Tile: return "TILE";
        case InspectorSelectionKind::Model: return "OBJECT";
        case InspectorSelectionKind::Door: return "DOOR";
        case InspectorSelectionKind::Anchor: return "ANCHOR";
        case InspectorSelectionKind::Trigger: return "TRIGGER";
        default: return "SELECTION";
    }
}

bool containsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    const std::string needle(filter);
    return std::search(value.begin(), value.end(), needle.begin(), needle.end(),
        [](unsigned char left, unsigned char right) {
            return std::tolower(left) == std::tolower(right);
        }) != value.end();
}

void layerStack(EditorUiModel& model, EditorUiEvents& events) {
    ImGui::SeparatorText(ICON_FA_CLONE "  Layers");
    for (auto iterator = model.layers.rbegin(); iterator != model.layers.rend(); ++iterator) {
        const LayerView& layer = *iterator;
        ImGui::PushID(static_cast<int>(layer.index));
        bool visible = layer.visible;
        const char* visibility = visible ? ICON_FA_EYE "##visible" : ICON_FA_EYE_SLASH "##visible";
        if (ImGui::SmallButton(visibility)) {
            visible = !visible;
            events.set_layer_visible = std::pair{layer.index, visible};
        }
        ImGui::SameLine();
        if (ImGui::Selectable(layer.name.c_str(), layer.active)) {
            events.activate_layer_index = layer.index;
        }
        ImGui::PopID();
    }
    if (ImGui::Button(ICON_FA_PLUS "  Layer")) events.add_layer = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_UP "##layer_up")) events.move_layer_up = true;
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_ARROW_DOWN "##layer_down")) events.move_layer_down = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(model.layers.size() <= 1U);
    if (ImGui::Button(ICON_FA_TRASH "##layer_delete")) events.delete_layer = true;
    ImGui::EndDisabled();
}

} // namespace

void EditorShell::drawToolRail(
    EditorUiModel& model, EditorUiEvents& events, float) {
    if (model.view_mode == EditorViewMode::World) {
        for (const ToolButton& button : {kMapTools[0], kMapTools[1]}) {
            drawRailButton(button, model.active_tool == button.tool, events);
        }
        return;
    }
    if (model.view_mode == EditorViewMode::GamePreview) {
        ImGui::PushFont(ImGui::Font::Regular, 20.0f);
        ImGui::TextDisabled(ICON_FA_GAMEPAD);
        ImGui::PopFont();
        return;
    }
    int previous_group = kMapTools.front().group;
    for (const ToolButton& button : kMapTools) {
        if (button.group != previous_group) {
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 4.0f));
            previous_group = button.group;
        }
        drawRailButton(button, model.active_tool == button.tool, events);
    }
}

void EditorShell::drawToolPanel(
    EditorUiModel& model, EditorUiEvents& events, float height) {
    if (model.view_mode == EditorViewMode::World) {
        ImGui::TextUnformatted(ICON_FA_GLOBE "  World navigator");
        ImGui::TextDisabled("%zu maps   •   %zu travel links",
            model.world_maps.size(), model.world_connections.size());
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##world_search", ICON_FA_SEARCH "  Find map or ID...",
            world_search_.data(), world_search_.size());
        ImGui::SeparatorText("Maps");
        ImGui::BeginChild("##world_list", ImVec2(0.0f, 0.0f), false);
        for (const WorldMapNodeView& map : model.world_maps) {
            if (!containsInsensitive(map.name, world_search_.data()) &&
                !containsInsensitive(map.id, world_search_.data())) continue;
            const char* icon = map.type == "interior" ? ICON_FA_BUILDING_O : ICON_FA_MAP_O;
            const std::string label = std::string(icon) + "  " + map.name + "##" + map.id;
            if (ImGui::Selectable(label.c_str(), map.active)) {
                events.select_world_map_id = map.id;
            }
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                events.activate_map_id = map.id;
            }
            ImGui::SameLine(ImGui::GetWindowWidth() - 88.0f);
            if (map.error_count > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f),
                    ICON_FA_EXCLAMATION_TRIANGLE " %d", map.error_count);
            } else {
                ImGui::TextDisabled("%d,%d", map.grid_x, map.grid_y);
            }
        }
        ImGui::EndChild();
        return;
    }
    if (model.view_mode == EditorViewMode::GamePreview) {
        ImGui::TextUnformatted(ICON_FA_GAMEPAD "  Play test");
        if (play_input_.captured()) {
            ImGui::TextColored(ImVec4(0.37f, 0.88f, 0.67f, 1.0f),
                ICON_FA_CIRCLE "  Input captured");
            ImGui::TextWrapped("WASD, arrows, and movement keys stay with the game. Press Escape to return control to the editor.");
        } else {
            ImGui::TextDisabled(ICON_FA_CIRCLE_O "  Input released");
            ImGui::TextWrapped("Click the game viewport to resume controlling the player.");
        }
        if (ImGui::Button(ICON_FA_REPEAT "  Reset player", ImVec2(-1.0f, 0.0f))) {
            events.play_reset = true;
        }
        if (ImGui::Button(ICON_FA_MAP_MARKER "  Start at selected cell", ImVec2(-1.0f, 0.0f))) {
            events.play_start_from_selection = true;
        }
        if (ImGui::Button(ICON_FA_CROSSHAIRS "  Focus player", ImVec2(-1.0f, 0.0f))) {
            events.play_focus_player = true;
        }
        ImGui::SeparatorText("Preview");
        ImGui::Text("Renderer rebuilds: %d", model.scene_rebuild_count);
        ImGui::TextDisabled(model.preview_stale ? "Unsynced map changes" : "Matches saved game scene");
        return;
    }

    switch (model.active_tool) {
        case EditorTool::Height: {
            ImGui::TextUnformatted(ICON_FA_SLIDERS "  Height brush");
            ImGui::TextDisabled("Terrain elevation");
            int value = model.height_brush_value;
            ImGui::PushFont(ImGui::Font::Regular, 28.0f);
            ImGui::Text("%d", value);
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::TextDisabled("active height");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::SliderInt("##height", &value, 0, 31, "Height %d")) {
                events.height_brush_value = value;
            }
            if (ImGui::InputInt("Exact height", &value, 1, 4)) {
                events.height_brush_value = std::clamp(value, 0, 255);
            }
            ImGui::TextDisabled(ICON_FA_MOUSE_POINTER "  Click-drag to paint");
            break;
        }
        case EditorTool::Collision: {
            ImGui::TextUnformatted(ICON_FA_BAN "  Collision brush");
            bool blocked = model.collision_brush_value;
            if (blocked) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.56f, 0.18f, 0.20f, 1.0f));
            if (ImGui::Button(ICON_FA_BAN "  Blocked", ImVec2(135.0f, 38.0f))) {
                events.collision_brush_value = true;
            }
            if (blocked) ImGui::PopStyleColor();
            ImGui::SameLine();
            if (!blocked) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.43f, 0.34f, 1.0f));
            if (ImGui::Button(ICON_FA_CHECK "  Walkable", ImVec2(135.0f, 38.0f))) {
                events.collision_brush_value = false;
            }
            if (!blocked) ImGui::PopStyleColor();
            ImGui::TextDisabled("Collision overlay shows blocked cells in red.");
            break;
        }
        case EditorTool::Select:
            ImGui::TextUnformatted(ICON_FA_MOUSE_POINTER "  Select and move");
            ImGui::TextWrapped("Click a cell or object to inspect it. Drag doors, anchors, and objects directly. Right-click returns to Select.");
            layerStack(model, events);
            break;
        case EditorTool::Hand:
            ImGui::TextUnformatted(ICON_FA_HAND_PAPER_O "  Pan canvas");
            ImGui::TextWrapped("Drag to move around the map without editing. Middle-drag always pans in any tool.");
            break;
        case EditorTool::EraseLayer:
            ImGui::TextUnformatted(ICON_FA_ERASER "  Erase active layer");
            ImGui::TextWrapped("Removes only the tile on the active layer. Height, collision, objects, and travel data are preserved.");
            layerStack(model, events);
            break;
        case EditorTool::ClearCell:
            ImGui::TextUnformatted(ICON_FA_TRASH "  Clear entire cell");
            ImGui::TextWrapped("Clears every tile layer and all local objects, doors, anchors, collision, and terrain data on a cell.");
            layerStack(model, events);
            break;
        case EditorTool::Anchors:
            ImGui::TextUnformatted(ICON_FA_ANCHOR "  Arrival anchors");
            ImGui::TextWrapped("Click a cell for one anchor, or add a complete three-node south entrance.");
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.43f, 0.60f, 1.0f));
            if (ImGui::Button(ICON_FA_PLUS "  Add south entry (3 nodes)", ImVec2(-1.0f, 38.0f))) {
                events.add_south_entry_anchors = true;
            }
            ImGui::PopStyleColor();
            ImGui::TextDisabled("Adds missing left / center / right nodes. Existing adjustments stay unchanged.");
            ImGui::SeparatorText("Placed anchors");
            for (const TravelObjectView& anchor : model.placed_anchors) {
                const std::string label = anchor.id == "entry"
                    ? anchor.id + "  (default)" : anchor.id;
                if (ImGui::Selectable(label.c_str(), anchor.selected)) events.select_anchor_id = anchor.id;
            }
            break;
        case EditorTool::Eyedropper:
            ImGui::TextUnformatted(ICON_FA_EYEDROPPER "  Eyedropper");
            ImGui::TextWrapped("Click a populated cell to make its tile and layer active.");
            break;
        case EditorTool::Doors:
            drawAssetBrowser(model, events, height);
            ImGui::SeparatorText("Placed doors");
            for (const TravelObjectView& door : model.placed_doors) {
                const std::string label = door.id + "  " + door.summary + "##placed";
                if (ImGui::Selectable(label.c_str(), door.selected)) events.select_door_id = door.id;
            }
            break;
        default:
            drawAssetBrowser(model, events, height);
            break;
    }
}

void EditorShell::drawSelectionInspector(
    EditorUiModel& model, EditorUiEvents& events, float) {
    ImGui::TextDisabled("INSPECTOR");
    ImGui::SameLine();
    ImGui::TextUnformatted(model.view_mode == EditorViewMode::World
        ? "MAP" : selectionLabel(model.selection.kind));
    ImGui::Separator();
    if (model.view_mode == EditorViewMode::World) {
        const auto active = std::find_if(model.world_maps.begin(), model.world_maps.end(),
            [](const WorldMapNodeView& map) { return map.active; });
        if (active == model.world_maps.end()) {
            ImGui::TextWrapped("Select a map to inspect its world placement and open it for editing.");
        } else {
            ImGui::PushFont(ImGui::Font::Regular, 20.0f);
            ImGui::Text("%s  %s", active->type == "interior"
                ? ICON_FA_BUILDING_O : ICON_FA_MAP_O, active->name.c_str());
            ImGui::PopFont();
            ImGui::TextDisabled("%s", active->id.c_str());
            ImGui::Dummy(ImVec2(0.0f, 3.0f));
            if (ImGui::BeginTable("##world_properties", 2,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                const auto row = [](const char* label, const std::string& value) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", label);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(value.c_str());
                };
                row("World cell", std::to_string(active->grid_x) + ", " +
                    std::to_string(active->grid_y));
                row("World layout", active->linked ? "Spatially linked" : "Standalone");
                row("Map size", std::to_string(active->width) + " x " +
                    std::to_string(active->height));
                row("Travel", std::to_string(active->door_count) + " doors");
                row("Source", active->shared_source ? "Reused OWMAP" : "Unique OWMAP");
                ImGui::EndTable();
            }
            if (active->dirty) {
                ImGui::TextColored(ImVec4(0.97f, 0.73f, 0.33f, 1.0f), "• Unsaved changes");
            }
            if (ImGui::Button(ICON_FA_PENCIL "  Open in Map", ImVec2(-1.0f, 38.0f))) {
                events.activate_map_id = active->id;
            }
        }
    } else if (model.selection.kind == InspectorSelectionKind::None) {
        if (resize_map_id_ != model.top_down.map_id ||
            resize_observed_width_ != model.top_down.width ||
            resize_observed_height_ != model.top_down.height) {
            resize_map_id_ = model.top_down.map_id;
            resize_width_ = std::max(1, model.top_down.width);
            resize_height_ = std::max(1, model.top_down.height);
            resize_observed_width_ = model.top_down.width;
            resize_observed_height_ = model.top_down.height;
        }
        ImGui::TextUnformatted("Map size");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt("Width", &resize_width_, 1, 8);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputInt("Height", &resize_height_, 1, 8);
        resize_width_ = std::clamp(resize_width_, 1, 256);
        resize_height_ = std::clamp(resize_height_, 1, 256);
        const bool unchanged = resize_width_ == model.top_down.width &&
            resize_height_ == model.top_down.height;
        ImGui::BeginDisabled(unchanged);
        if (ImGui::Button("Apply size", ImVec2(-1.0f, 32.0f))) {
            events.resize_map = MapResizeRequest{resize_width_, resize_height_};
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("The north-west area is preserved. Shrinking trims cells outside the new size.");
        if (model.top_down.default_interior_room) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.72f, 0.65f, 0.88f, 1.0f),
                ICON_FA_BUILDING_O "  Default interior room active");
            ImGui::TextWrapped(
                "%.1f-tile walls sit in the center of the %d-cell boundary band; cells keep "
                "their original size. South openings carve three reachable cells and receive "
                "a %.1f-cell-deep entry extension made from complete tiles. Black top cap %s.",
                model.top_down.default_wall_height_tiles,
                model.top_down.default_room_inset_tiles,
                model.top_down.default_entry_extension_depth_tiles,
                model.top_down.default_room_black_top_cap ? "on" : "off");
        }
        ImGui::Separator();
        ImGui::TextWrapped("Select a cell, door, anchor, or object to edit its exact properties here.");
    } else {
        ImGui::TextWrapped("%s", model.selection.title.c_str());
        ImGui::TextDisabled("Tile %d, %d", model.selection.tile_x, model.selection.tile_y);
        if (ImGui::Button(ICON_FA_CROSSHAIRS "  Focus")) events.focus_selection = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CLONE "##duplicate")) events.duplicate_selection = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate  Cmd+D");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_TRASH "##delete")) events.delete_selection = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete");
        if (model.selection.terrain_editable) {
            int value = model.selection.height;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputInt("Cell height", &value, 1, 4)) {
                events.set_cell_height = std::clamp(value, 0, 255);
            }
            bool blocked = model.selection.collision;
            if (ImGui::Checkbox("Collision blocked", &blocked)) {
                events.set_cell_collision = blocked;
            }
            if (model.selection.automatic_collision) {
                ImGui::TextColored(ImVec4(0.83f, 0.66f, 1.0f, 1.0f),
                    ICON_FA_INFO_CIRCLE "  This cell remains blocked by the room wall.");
                ImGui::TextWrapped("Add a wall opening or reduce walkableInsetTiles to make it walkable.");
            }
            constexpr std::array<std::pair<int, const char*>, 14> specials{{
                {0, "Flat"}, {2, "Ramp north"}, {3, "Ramp east"},
                 {4, "Ramp south"}, {5, "Ramp west"}, {6, "Convex NE"},
                 {7, "Convex SE"}, {8, "Convex SW"}, {9, "Convex NW"},
                 {10, "Concave NE"}, {11, "Concave SE"}, {12, "Concave SW"},
                 {13, "Concave NW"}, {14, "Actor spawn"}}};
            const auto current = std::find_if(specials.begin(), specials.end(),
                [&](const auto& item) { return item.first == model.selection.special; });
            const char* current_label = current == specials.end() ? "Unknown" : current->second;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("Terrain shape", current_label)) {
                for (const auto& [special, label] : specials) {
                    if (ImGui::Selectable(label, special == model.selection.special)) {
                        events.set_cell_special = special;
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
        }
        if (model.door_editor.active) drawDoorEditor(model, events);
        drawSelectionPosition(model, events);
        for (const InspectorField& field : model.selection.fields) {
            ImGui::TextDisabled("%s", field.label.c_str());
            ImGui::SameLine(115.0f);
            if (field.warning) ImGui::PushStyleColor(ImGuiCol_Text,
                ImVec4(1.0f, 0.62f, 0.25f, 1.0f));
            ImGui::TextWrapped("%s", field.value.c_str());
            if (field.warning) ImGui::PopStyleColor();
        }
    }
    ImGui::SeparatorText(ICON_FA_CHECK_CIRCLE_O "  Map health");
    int errors = 0;
    int warnings = 0;
    for (const ValidationView& diagnostic : model.validation) {
        errors += diagnostic.severity == "error";
        warnings += diagnostic.severity == "warning";
    }
    if (errors > 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f),
            ICON_FA_EXCLAMATION_CIRCLE "  %d errors", errors);
    } else {
        ImGui::TextColored(ImVec4(0.37f, 0.82f, 0.66f, 1.0f),
            ICON_FA_CHECK "  No errors");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d warnings", warnings);
    if (ImGui::Button(ICON_FA_LIST_ALT "  Diagnostics", ImVec2(-1.0f, 0.0f))) {
        validation_open_ = true;
    }
}

} // namespace pr::mapmaker
