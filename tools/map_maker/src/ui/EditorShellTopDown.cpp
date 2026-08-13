#include "mapmaker/ui/EditorShell.hpp"

#include <bgfx/bgfx.h>
#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace pr::mapmaker {
namespace {

ImU32 markerColor(TopDownMarkerKind kind) {
    switch (kind) {
        case TopDownMarkerKind::Door: return IM_COL32(83, 211, 255, 255);
        case TopDownMarkerKind::Anchor: return IM_COL32(255, 190, 72, 255);
        case TopDownMarkerKind::Model: return IM_COL32(181, 119, 255, 255);
    }
    return IM_COL32_WHITE;
}

const char* markerLabel(TopDownMarkerKind kind) {
    switch (kind) {
        case TopDownMarkerKind::Door: return "D";
        case TopDownMarkerKind::Anchor: return "A";
        case TopDownMarkerKind::Model: return "M";
    }
    return "?";
}

bool validCellOrCardinalHalo(int x, int y, int width, int height) {
    const bool inside = x >= 0 && y >= 0 && x < width && y < height;
    const bool horizontal_halo = (x == -1 || x == width) && y >= 0 && y < height;
    const bool vertical_halo = (y == -1 || y == height) && x >= 0 && x < width;
    return inside || horizontal_halo || vertical_halo;
}

} // namespace

void EditorShell::drawTopDownViewport(EditorUiModel& model, EditorUiEvents& events) {
    const ImVec2 available{
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(1.0f, ImGui::GetContentRegionAvail().y)};
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##top_down_canvas", available,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 canvas_max{canvas_min.x + available.x, canvas_min.y + available.y};
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(11, 13, 17, 255));
    draw->PushClipRect(canvas_min, canvas_max, true);

    if (top_down_map_id_ != model.top_down.map_id) {
        top_down_map_id_ = model.top_down.map_id;
        top_down_pan_initialized_ = false;
    }
    if (!top_down_pan_initialized_ && model.top_down.width > 0 && model.top_down.height > 0) {
        const float fit_x = (available.x - 32.0f) /
            static_cast<float>(model.top_down.width + 2);
        const float fit_y = (available.y - 32.0f) /
            static_cast<float>(model.top_down.height + 2);
        top_down_cell_size_ = std::clamp(std::min(fit_x, fit_y), 12.0f, 48.0f);
        top_down_pan_x_ = 0.0f;
        top_down_pan_y_ = 0.0f;
        top_down_pan_initialized_ = true;
    }
    if (top_down_focus_serial_ != model.top_down.focus_serial) {
        top_down_focus_serial_ = model.top_down.focus_serial;
        top_down_pan_x_ = (static_cast<float>(model.top_down.width) * 0.5f -
            static_cast<float>(model.top_down.focus_tile_x) - 0.5f) * top_down_cell_size_;
        top_down_pan_y_ = (static_cast<float>(model.top_down.height) * 0.5f -
            static_cast<float>(model.top_down.focus_tile_y) - 0.5f) * top_down_cell_size_;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    auto originFor = [&](float cell_size) {
        return ImVec2{
            canvas_min.x + available.x * 0.5f + top_down_pan_x_ -
                static_cast<float>(model.top_down.width) * cell_size * 0.5f,
            canvas_min.y + available.y * 0.5f + top_down_pan_y_ -
                static_cast<float>(model.top_down.height) * cell_size * 0.5f};
    };

    ImVec2 origin = originFor(top_down_cell_size_);
    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const float old_size = top_down_cell_size_;
        const float world_x = (mouse.x - origin.x) / old_size;
        const float world_y = (mouse.y - origin.y) / old_size;
        top_down_cell_size_ = std::clamp(
            old_size * std::pow(1.14f, ImGui::GetIO().MouseWheel), 6.0f, 128.0f);
        const ImVec2 centered = originFor(top_down_cell_size_);
        top_down_pan_x_ += mouse.x - world_x * top_down_cell_size_ - centered.x;
        top_down_pan_y_ += mouse.y - world_y * top_down_cell_size_ - centered.y;
        origin = originFor(top_down_cell_size_);
    }
    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
        top_down_pan_x_ += ImGui::GetIO().MouseDelta.x;
        top_down_pan_y_ += ImGui::GetIO().MouseDelta.y;
        origin = originFor(top_down_cell_size_);
    }

    const int width = model.top_down.width;
    const int height = model.top_down.height;
    const int min_x = std::clamp(static_cast<int>(std::floor(
        (canvas_min.x - origin.x) / top_down_cell_size_)), 0, std::max(0, width));
    const int min_y = std::clamp(static_cast<int>(std::floor(
        (canvas_min.y - origin.y) / top_down_cell_size_)), 0, std::max(0, height));
    const int max_x = std::clamp(static_cast<int>(std::ceil(
        (canvas_max.x - origin.x) / top_down_cell_size_)), 0, std::max(0, width));
    const int max_y = std::clamp(static_cast<int>(std::ceil(
        (canvas_max.y - origin.y) / top_down_cell_size_)), 0, std::max(0, height));

    const ImU32 halo_color = IM_COL32(48, 59, 75, 185);
    for (int x = 0; x < width; ++x) {
        for (int y : {-1, height}) {
            const ImVec2 cell_min{origin.x + x * top_down_cell_size_,
                origin.y + y * top_down_cell_size_};
            const ImVec2 cell_max{cell_min.x + top_down_cell_size_,
                cell_min.y + top_down_cell_size_};
            draw->AddRectFilled(cell_min, cell_max, IM_COL32(20, 24, 31, 255));
            draw->AddRect(cell_min, cell_max, halo_color, 0.0f, ImDrawFlags_None, 1.0f);
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x : {-1, width}) {
            const ImVec2 cell_min{origin.x + x * top_down_cell_size_,
                origin.y + y * top_down_cell_size_};
            const ImVec2 cell_max{cell_min.x + top_down_cell_size_,
                cell_min.y + top_down_cell_size_};
            draw->AddRectFilled(cell_min, cell_max, IM_COL32(20, 24, 31, 255));
            draw->AddRect(cell_min, cell_max, halo_color, 0.0f, ImDrawFlags_None, 1.0f);
        }
    }

    for (int y = min_y; y < max_y; ++y) {
        for (int x = min_x; x < max_x; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const ImVec2 cell_min{origin.x + x * top_down_cell_size_,
                origin.y + y * top_down_cell_size_};
            const ImVec2 cell_max{cell_min.x + top_down_cell_size_,
                cell_min.y + top_down_cell_size_};
            const int tile = index < model.top_down.composed_tiles.size()
                ? model.top_down.composed_tiles[index] : -1;
            std::uint16_t texture = UINT16_MAX;
            if (tile >= 0 && model.tile_thumbnail) texture = model.tile_thumbnail(tile);
            if (texture != UINT16_MAX) {
                draw->AddImage(ImGui::toId(bgfx::TextureHandle{texture},
                    IMGUI_FLAGS_ALPHA_BLEND, 0), cell_min, cell_max);
            } else {
                const std::uint8_t height_value = index < model.top_down.heights.size()
                    ? model.top_down.heights[index] : 0U;
                const int shade = std::min(65, static_cast<int>(height_value) * 5);
                draw->AddRectFilled(cell_min, cell_max,
                    IM_COL32(36 + shade, 40 + shade, 48 + shade, 255));
                if (tile >= 0 && top_down_cell_size_ >= 28.0f) {
                    char label[20]{};
                    std::snprintf(label, sizeof(label), "%d", tile);
                    draw->AddText(ImVec2(cell_min.x + 3.0f, cell_min.y + 3.0f),
                        IM_COL32(210, 214, 220, 220), label);
                }
            }
            if (index < model.top_down.collision.size() && model.top_down.collision[index] != 0U) {
                draw->AddRectFilled(cell_min, cell_max, IM_COL32(235, 65, 65, 72));
                draw->AddLine(cell_min, cell_max, IM_COL32(255, 92, 92, 190), 1.5f);
                draw->AddLine(ImVec2(cell_max.x, cell_min.y), ImVec2(cell_min.x, cell_max.y),
                    IM_COL32(255, 92, 92, 190), 1.5f);
            }
            if (index < model.top_down.specials.size() && model.top_down.specials[index] != 0U) {
                const ImVec2 triangle[3]{{cell_min.x, cell_min.y},
                    {cell_min.x + std::min(12.0f, top_down_cell_size_), cell_min.y},
                    {cell_min.x, cell_min.y + std::min(12.0f, top_down_cell_size_)}};
                draw->AddTriangleFilled(triangle[0], triangle[1], triangle[2],
                    IM_COL32(255, 189, 70, 230));
            }
            if (model.grid_overlay || top_down_cell_size_ >= 10.0f) {
                draw->AddRect(cell_min, cell_max, IM_COL32(77, 87, 105, 125));
            }
        }
    }

    for (const TopDownMarkerView& marker : model.top_down.markers) {
        const ImVec2 center{origin.x + (marker.tile_x + 0.5f) * top_down_cell_size_,
            origin.y + (marker.tile_y + 0.5f) * top_down_cell_size_};
        if (center.x < canvas_min.x || center.x > canvas_max.x ||
            center.y < canvas_min.y || center.y > canvas_max.y) continue;
        const float radius = std::clamp(top_down_cell_size_ * 0.22f, 5.0f, 13.0f);
        draw->AddCircleFilled(center, radius, markerColor(marker.kind));
        if (marker.selected) draw->AddCircle(center, radius + 3.0f,
            IM_COL32(255, 232, 125, 255), 0, 2.0f);
        const ImVec2 text_size = ImGui::CalcTextSize(markerLabel(marker.kind));
        draw->AddText(ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f),
            IM_COL32(12, 15, 20, 255), markerLabel(marker.kind));
    }

    int tile_x = -1;
    int tile_y = -1;
    bool valid_hit = false;
    if (hovered && top_down_cell_size_ > 0.0f) {
        tile_x = static_cast<int>(std::floor((mouse.x - origin.x) / top_down_cell_size_));
        tile_y = static_cast<int>(std::floor((mouse.y - origin.y) / top_down_cell_size_));
        valid_hit = validCellOrCardinalHalo(tile_x, tile_y, width, height);
        if (!valid_hit) {
            tile_x = -1;
            tile_y = -1;
        }
    }
    if (valid_hit) {
        const ImVec2 cell_min{origin.x + tile_x * top_down_cell_size_,
            origin.y + tile_y * top_down_cell_size_};
        const ImVec2 cell_max{cell_min.x + top_down_cell_size_,
            cell_min.y + top_down_cell_size_};
        draw->AddRect(cell_min, cell_max, IM_COL32(77, 208, 255, 255), 0.0f, 0, 2.0f);
    }
    if (validCellOrCardinalHalo(model.selection.tile_x, model.selection.tile_y, width, height)) {
        const ImVec2 cell_min{origin.x + model.selection.tile_x * top_down_cell_size_,
            origin.y + model.selection.tile_y * top_down_cell_size_};
        const ImVec2 cell_max{cell_min.x + top_down_cell_size_,
            cell_min.y + top_down_cell_size_};
        draw->AddRect(cell_min, cell_max, IM_COL32(255, 214, 77, 255), 0.0f, 0, 3.0f);
    }
    draw->PopClipRect();

    auto& gesture = events.viewport;
    gesture.top_down = true;
    gesture.hovered = hovered && valid_hit;
    gesture.tile_x = tile_x;
    gesture.tile_y = tile_y;
    gesture.left_clicked = gesture.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    gesture.left_down = gesture.hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    gesture.left_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    gesture.right_clicked = gesture.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    gesture.middle_down = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    gesture.double_clicked = gesture.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    gesture.local_x = mouse.x - canvas_min.x;
    gesture.local_y = mouse.y - canvas_min.y;
    gesture.width = available.x;
    gesture.height = available.y;
    gesture.wheel = 0.0f;
}

} // namespace pr::mapmaker
