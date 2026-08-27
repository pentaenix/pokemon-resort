#include "mapmaker/ui/EditorShell.hpp"

#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <utility>

namespace pr::mapmaker {
namespace {

std::string identifierBase(const std::string& value) {
    std::string result;
    for (unsigned char character : value) {
        if (std::isalnum(character) || character == '_') {
            result.push_back(static_cast<char>(std::tolower(character)));
        } else if (result.empty() || result.back() != '_') {
            result.push_back('_');
        }
    }
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? "map" : result;
}

bool mapIdExists(const EditorUiModel& model, const std::string& id) {
    return std::any_of(model.world_maps.begin(), model.world_maps.end(),
        [&](const WorldMapNodeView& node) { return node.id == id; });
}

void chooseType(NewMapRequest& request, const char* type) {
    if (request.type == type) return;
    if (std::strcmp(type, "interior") == 0 &&
        request.width == 32 && request.height == 32) {
        request.width = 16;
        request.height = 12;
    } else if (std::strcmp(type, "exterior") == 0 &&
        request.width == 16 && request.height == 12) {
        request.width = 32;
        request.height = 32;
    }
    request.type = type;
}

void drawSizePreset(const char* label, int width, int height, NewMapRequest& request) {
    const bool selected = request.width == width && request.height == height;
    if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.43f, 0.60f, 1.0f));
    if (ImGui::Button(label)) {
        request.width = width;
        request.height = height;
    }
    if (selected) ImGui::PopStyleColor();
}

} // namespace

void EditorShell::beginWorldMapCreation(
    const EditorUiModel& model,
    std::string source_map_id,
    int direction_x,
    int direction_y,
    const char* direction_name) {
    pending_new_map_ = {};
    pending_new_map_.source_map_id = std::move(source_map_id);
    pending_new_map_.direction_x = direction_x;
    pending_new_map_.direction_y = direction_y;
    pending_new_map_.linked = !pending_new_map_.source_map_id.empty();

    std::string base = "new_map";
    if (!pending_new_map_.source_map_id.empty()) {
        base = identifierBase(pending_new_map_.source_map_id);
        if (direction_name && *direction_name) base += "_" + std::string(direction_name);
    }
    std::string candidate = base;
    int suffix = 2;
    while (mapIdExists(model, candidate)) candidate = base + "_" + std::to_string(suffix++);

    std::fill(new_map_id_.begin(), new_map_id_.end(), '\0');
    std::fill(new_map_name_.begin(), new_map_name_.end(), '\0');
    std::strncpy(new_map_id_.data(), candidate.c_str(), new_map_id_.size() - 1U);
    std::strncpy(new_map_name_.data(), candidate.c_str(), new_map_name_.size() - 1U);
    create_map_popup_ = true;
}

void EditorShell::drawWorldMapCreationModal(
    EditorUiModel& model, EditorUiEvents& events) {
    if (create_map_popup_) {
        ImGui::OpenPopup("Create map");
        create_map_popup_ = false;
    }
    if (!ImGui::BeginPopupModal("Create map", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;

    const bool standalone = pending_new_map_.source_map_id.empty();
    ImGui::PushFont(ImGui::Font::Regular, 20.0f);
    ImGui::TextUnformatted(standalone ? "New standalone map" : "New adjacent map");
    ImGui::PopFont();
    if (standalone) {
        ImGui::TextDisabled("It starts without world adjacency or travel links.");
    } else {
        ImGui::TextDisabled("Adjacent to %s", pending_new_map_.source_map_id.c_str());
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("Name", new_map_name_.data(), new_map_name_.size());
    ImGui::SetNextItemWidth(340.0f);
    ImGui::InputText("Map ID", new_map_id_.data(), new_map_id_.size());

    ImGui::SeparatorText("Map type");
    const bool exterior = pending_new_map_.type == "exterior";
    if (ImGui::RadioButton(ICON_FA_MAP_O "  Exterior", exterior)) {
        chooseType(pending_new_map_, "exterior");
    }
    ImGui::SameLine(180.0f);
    if (ImGui::RadioButton(ICON_FA_BUILDING_O "  Interior", !exterior)) {
        chooseType(pending_new_map_, "interior");
    }
    ImGui::TextDisabled(pending_new_map_.type == "interior"
        ? "Starts with the default room floor and walls."
        : "Starts as an empty outdoor tile map.");

    const bool can_reuse = !standalone;
    bool reuse_source = !pending_new_map_.reuse_source_map_id.empty();
    if (can_reuse && ImGui::Checkbox("Reuse an existing map source", &reuse_source)) {
        pending_new_map_.reuse_source_map_id = reuse_source
            ? pending_new_map_.source_map_id : std::string{};
    }
    if (reuse_source) {
        const WorldMapNodeView* reused = nullptr;
        for (const WorldMapNodeView& map : model.world_maps) {
            if (map.id == pending_new_map_.reuse_source_map_id) reused = &map;
        }
        const char* reused_name = reused ? reused->name.c_str() : "Choose source";
        if (ImGui::BeginCombo("Source", reused_name)) {
            for (const WorldMapNodeView& map : model.world_maps) {
                if (ImGui::Selectable(map.name.c_str(),
                    map.id == pending_new_map_.reuse_source_map_id)) {
                    pending_new_map_.reuse_source_map_id = map.id;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Both map cards will edit the same OWMAP file.");
    } else {
        ImGui::SeparatorText("Size in tiles");
        if (pending_new_map_.type == "interior") {
            drawSizePreset("Small  12 x 10", 12, 10, pending_new_map_);
            ImGui::SameLine();
            drawSizePreset("Medium  16 x 12", 16, 12, pending_new_map_);
            ImGui::SameLine();
            drawSizePreset("Large  24 x 18", 24, 18, pending_new_map_);
        } else {
            drawSizePreset("Small  16 x 16", 16, 16, pending_new_map_);
            ImGui::SameLine();
            drawSizePreset("Medium  32 x 32", 32, 32, pending_new_map_);
            ImGui::SameLine();
            drawSizePreset("Large  64 x 64", 64, 64, pending_new_map_);
        }
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Width", &pending_new_map_.width);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::InputInt("Height", &pending_new_map_.height);
    }
    pending_new_map_.width = std::clamp(pending_new_map_.width, 1, 512);
    pending_new_map_.height = std::clamp(pending_new_map_.height, 1, 512);
    if (!reuse_source && pending_new_map_.type == "interior") {
        ImGui::TextColored(ImVec4(0.48f, 0.82f, 0.96f, 1.0f),
            ICON_FA_ANCHOR "  South entry nodes: %d, %d / %d, %d / %d, %d",
            pending_new_map_.width / 2 - 1, pending_new_map_.height - 1,
            pending_new_map_.width / 2, pending_new_map_.height - 1,
            pending_new_map_.width / 2 + 1, pending_new_map_.height - 1);
        ImGui::TextDisabled("Doors propose the middle entry node; all three remain editable.");
    }

    const std::string id = new_map_id_.data();
    const bool duplicate_id = !id.empty() && mapIdExists(model, id);
    if (duplicate_id) {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.38f, 1.0f),
            "A map with this ID already exists.");
    }
    ImGui::Separator();
    ImGui::BeginDisabled(id.empty() || duplicate_id);
    if (ImGui::Button(ICON_FA_PLUS "  Create and open", ImVec2(170.0f, 0.0f))) {
        pending_new_map_.id = id;
        pending_new_map_.name = new_map_name_[0] ? new_map_name_.data() : id;
        events.create_map = pending_new_map_;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

} // namespace pr::mapmaker
