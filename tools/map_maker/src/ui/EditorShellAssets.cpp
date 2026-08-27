#include "mapmaker/ui/EditorShell.hpp"

#include <bgfx/bgfx.h>
#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string_view>

namespace pr::mapmaker {
namespace {

bool containsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    const std::string_view needle(filter);
    return std::search(value.begin(), value.end(), needle.begin(), needle.end(),
        [](unsigned char left, unsigned char right) {
            return std::tolower(left) == std::tolower(right);
        }) != value.end();
}

const char* assetKindLabel(AssetKind kind) {
    switch (kind) {
        case AssetKind::Tile: return "Tiles";
        case AssetKind::Door: return "Doors";
        case AssetKind::Model: return "Objects";
        case AssetKind::SmartSet: return "Smart";
    }
    return "Assets";
}

} // namespace

void EditorShell::drawAssetBrowser(EditorUiModel& model, EditorUiEvents& events, float) {
    const bool show_layers = model.active_tool == EditorTool::Paint ||
        model.active_tool == EditorTool::EraseLayer || model.active_tool == EditorTool::ClearCell;
    if (show_layers) {
        ImGui::SeparatorText("Layer stack");
        if (model.layers.empty()) {
            ImGui::TextDisabled("No tile layers");
            if (ImGui::Button(ICON_FA_PLUS "  Layer", ImVec2(78.0f, 0.0f))) events.add_layer = true;
        }
        for (auto iterator = model.layers.rbegin(); iterator != model.layers.rend(); ++iterator) {
            const LayerView& layer = *iterator;
            ImGui::PushID(static_cast<int>(layer.index));
            bool visible = layer.visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                events.set_layer_visible = std::pair{layer.index, visible};
            }
            ImGui::SameLine();
            if (ImGui::Selectable(layer.name.c_str(), layer.active)) {
                events.activate_layer_index = layer.index;
            }
            ImGui::PopID();
        }
        const auto active = std::find_if(model.layers.begin(), model.layers.end(),
            [](const LayerView& layer) { return layer.active; });
        if (active != model.layers.end()) {
            if (layer_name_index_ != active->index) {
                std::fill(layer_name_.begin(), layer_name_.end(), '\0');
                std::strncpy(layer_name_.data(), active->name.c_str(), layer_name_.size() - 1U);
                layer_name_index_ = active->index;
            }
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##layer_name", layer_name_.data(), layer_name_.size(),
                ImGuiInputTextFlags_EnterReturnsTrue)) {
                events.rename_layer = std::pair{active->index, std::string(layer_name_.data())};
            }
        }
        if (ImGui::Button(ICON_FA_PLUS "  Layer", ImVec2(78.0f, 0.0f))) events.add_layer = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_UP "##layer_up", ImVec2(38.0f, 0.0f))) events.move_layer_up = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_DOWN "##layer_down", ImVec2(38.0f, 0.0f))) events.move_layer_down = true;
        ImGui::SameLine();
        ImGui::BeginDisabled(model.layers.size() <= 1U);
        if (ImGui::Button(ICON_FA_TRASH "##layer_delete", ImVec2(38.0f, 0.0f))) events.delete_layer = true;
        ImGui::EndDisabled();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted(assetKindLabel(model.active_asset_kind));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##asset_search", "Search tiles, objects, doors...",
        search_, sizeof(search_));
    if ((model.active_asset_kind == AssetKind::Tile || model.active_asset_kind == AssetKind::Door) &&
        !model.tile_categories.empty()) {
        ImGui::SetNextItemWidth(-1.0f);
        const char* current = model.active_category.empty()
            ? "All tile sets" : model.active_category.c_str();
        if (ImGui::BeginCombo("##category", current)) {
            if (ImGui::Selectable("All tile sets", model.active_category.empty())) {
                events.activate_category = std::string{};
            }
            for (const std::string& category : model.tile_categories) {
                if (ImGui::Selectable(category.c_str(), model.active_category == category)) {
                    events.activate_category = category;
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::Separator();

    std::vector<const AssetView*> filtered;
    filtered.reserve(model.assets.size());
    for (const AssetView& asset : model.assets) {
        if (asset.kind != model.active_asset_kind) continue;
        if (!model.active_category.empty() && asset.category != model.active_category) continue;
        if (!containsInsensitive(asset.name, search_) && !containsInsensitive(asset.id, search_)) continue;
        filtered.push_back(&asset);
    }
    constexpr float card_width = 76.0f;
    constexpr float card_height = 100.0f;
    const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / card_width));
    const int rows = static_cast<int>((filtered.size() + static_cast<std::size_t>(columns) - 1U) /
        static_cast<std::size_t>(columns));
    ImGuiListClipper clipper;
    clipper.Begin(rows, card_height);
    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            for (int column = 0; column < columns; ++column) {
                const std::size_t index = static_cast<std::size_t>(row * columns + column);
                if (index >= filtered.size()) break;
                if (column > 0) ImGui::SameLine();
                const AssetView& asset = *filtered[index];
                ImGui::PushID(asset.id.c_str());
                ImGui::BeginGroup();
                const bool selected = model.active_asset_id == asset.id;
                if (selected) ImGui::PushStyleColor(ImGuiCol_Button,
                    ImVec4(0.17f, 0.46f, 0.63f, 1.0f));
                std::uint16_t texture = UINT16_MAX;
                if ((asset.kind == AssetKind::Tile || asset.kind == AssetKind::Door) &&
                    model.tile_thumbnail) texture = model.tile_thumbnail(asset.tile_id);
                bool clicked = false;
                if (texture != UINT16_MAX) {
                    clicked = ImGui::ImageButton(bgfx::TextureHandle{texture}, ImVec2(60.0f, 60.0f));
                } else {
                    const char* badge = asset.door ? "DOOR"
                        : asset.kind == AssetKind::Model ? "OBJ" : "SMART";
                    clicked = ImGui::Button(badge, ImVec2(60.0f, 60.0f));
                }
                if (selected) ImGui::PopStyleColor();
                if (clicked) events.activate_asset_id = asset.id;
                const std::string short_name = asset.name.size() > 12
                    ? asset.name.substr(0, 11) + "..." : asset.name;
                ImGui::TextUnformatted(short_name.c_str());
                ImGui::EndGroup();
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%d x %d%s", asset.name.c_str(), asset.footprint_width,
                        asset.footprint_height, asset.door ? "\nAnimated door" : "");
                }
                ImGui::PopID();
            }
        }
    }
}

} // namespace pr::mapmaker
