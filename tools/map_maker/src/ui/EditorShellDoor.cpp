#include "mapmaker/ui/EditorShell.hpp"

#include <dear-imgui/imgui.h>

#include <algorithm>
#include <array>

namespace pr::mapmaker {
namespace {

const char* selectedName(
    const std::vector<ChoiceView>& choices,
    const std::string& selected,
    const char* empty_label) {
    for (const ChoiceView& choice : choices) {
        if (choice.id == selected) return choice.name.c_str();
    }
    return selected.empty() ? empty_label : selected.c_str();
}

bool choiceCombo(
    const char* id,
    const std::vector<ChoiceView>& choices,
    const std::string& selected,
    const char* empty_label,
    std::optional<std::string>& event) {
    bool changed = false;
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo(id, selectedName(choices, selected, empty_label))) {
        for (const ChoiceView& choice : choices) {
            if (ImGui::Selectable(choice.name.c_str(), choice.id == selected)) {
                event = choice.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace

void EditorShell::drawDoorEditor(EditorUiModel& model, EditorUiEvents& events) {
    DoorEditorView& door = model.door_editor;
    ImGui::Spacing();
    ImGui::SeparatorText("Door travel");
    ImGui::TextDisabled("Destination map");
    (void)choiceCombo("##door_map", door.map_choices, door.destination_map_id,
        "Choose map...", events.door_destination_map_id);
    ImGui::TextDisabled("Destination anchor");
    ImGui::BeginDisabled(door.destination_map_id.empty());
    (void)choiceCombo("##door_anchor", door.anchor_choices, door.destination_anchor_id,
        door.automatic_arrival ? door.automatic_arrival_label.c_str() : "Choose anchor...",
        events.door_destination_anchor_id);
    ImGui::EndDisabled();

    ImGui::TextDisabled("Approach direction");
    static const std::vector<ChoiceView> directions{
        {"north", "North"}, {"east", "East"},
        {"south", "South"}, {"west", "West"}};
    (void)choiceCombo("##door_direction", directions, door.direction,
        "Choose direction...", events.door_direction);

    ImGui::TextDisabled("Script sequence");
    (void)choiceCombo("##door_script", door.script_choices, door.script_id,
        "Choose script...", events.door_script_id);
    const bool destination_map_exists = std::any_of(
        door.map_choices.begin(), door.map_choices.end(), [&](const ChoiceView& choice) {
            return choice.id == door.destination_map_id;
        });
    const bool destination_anchor_exists = std::any_of(
        door.anchor_choices.begin(), door.anchor_choices.end(), [&](const ChoiceView& choice) {
            return choice.id == door.destination_anchor_id;
        });
    if (door.destination_map_id.empty() ||
        (door.destination_anchor_id.empty() && !door.automatic_arrival)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.28f, 1.0f));
        ImGui::TextWrapped(
            "Inactive door: choose a destination. An arrival is automatic only when that map has one door or one anchor.");
        ImGui::PopStyleColor();
    } else if (!destination_map_exists ||
        (!destination_anchor_exists && !door.automatic_arrival)) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.42f, 0.38f, 1.0f));
        ImGui::TextWrapped(
            "Inactive door: the selected destination map or anchor no longer exists.");
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.42f, 0.86f, 0.66f, 1.0f));
        ImGui::TextUnformatted("Destination is ready");
        ImGui::PopStyleColor();
    }
}

} // namespace pr::mapmaker
