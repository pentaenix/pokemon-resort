#include "mapmaker/ui/EditorShell.hpp"

#include <dear-imgui/imgui.h>

#include <array>
#include <string>

namespace pr::mapmaker {
namespace {

void drawTravelList(
    const char* table_id,
    const char* empty_message,
    const std::vector<TravelObjectView>& objects,
    std::optional<std::string>& selection_event) {
    if (objects.empty()) {
        ImGui::TextDisabled("%s", empty_message);
        return;
    }

    if (!ImGui::BeginTable(table_id, 2,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                ImGuiTableFlags_SizingStretchProp)) {
        return;
    }
    ImGui::TableSetupColumn("Object");
    ImGui::TableSetupColumn("Tile", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    for (const TravelObjectView& object : objects) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const std::string label = object.id + "##" + table_id + object.id;
        if (ImGui::Selectable(label.c_str(), object.selected,
                ImGuiSelectableFlags_SpanAllColumns)) {
            selection_event = object.id;
        }
        if (ImGui::IsItemHovered() && !object.summary.empty()) {
            ImGui::SetTooltip("%s", object.summary.c_str());
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%d, %d", object.tile_x, object.tile_y);
    }
    ImGui::EndTable();
}

} // namespace

void EditorShell::drawTravelObjects(EditorUiModel& model, EditorUiEvents& events) {
    ImGui::Spacing();
    ImGui::SeparatorText("Travel objects");

    if (ImGui::CollapsingHeader("Placed doors", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawTravelList("##placed_doors", "No doors on this map.", model.placed_doors,
            events.select_door_id);
    }
    if (ImGui::CollapsingHeader("Entry anchors", ImGuiTreeNodeFlags_DefaultOpen)) {
        drawTravelList("##placed_anchors", "No entry anchors on this map.",
            model.placed_anchors, events.select_anchor_id);
        const bool terrain_selected =
            model.selection.kind == InspectorSelectionKind::TerrainCell;
        ImGui::BeginDisabled(!terrain_selected);
        if (ImGui::Button("Add anchor at selected tile", ImVec2(-1.0f, 0.0f))) {
            events.add_anchor_at_selection = true;
        }
        ImGui::EndDisabled();
        if (!terrain_selected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Select a terrain tile first.");
        }
    }
}

void EditorShell::drawSelectionPosition(EditorUiModel& model, EditorUiEvents& events) {
    const bool movable = model.selection.kind == InspectorSelectionKind::Door ||
        model.selection.kind == InspectorSelectionKind::Anchor;
    if (!movable) return;

    ImGui::Spacing();
    ImGui::SeparatorText("Position");
    std::array<int, 2> tile{model.selection.tile_x, model.selection.tile_y};
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputInt2("##selection_tile", tile.data())) {
        events.move_selection_tile = std::pair<int, int>{tile[0], tile[1]};
    }
    ImGui::TextDisabled("Drag in the viewport or enter X, Y here.");
}

} // namespace pr::mapmaker
