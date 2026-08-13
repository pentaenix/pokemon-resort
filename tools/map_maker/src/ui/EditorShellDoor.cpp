#include "mapmaker/ui/EditorShell.hpp"

#include <dear-imgui/imgui.h>

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
        "Choose anchor...", events.door_destination_anchor_id);
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
    if (door.destination_map_id.empty() || door.destination_anchor_id.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.64f, 0.28f, 1.0f));
        ImGui::TextWrapped("Choose both a map and an anchor before saving a usable door link.");
        ImGui::PopStyleColor();
    }
}

} // namespace pr::mapmaker
