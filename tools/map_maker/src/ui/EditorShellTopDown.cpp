#include "mapmaker/ui/EditorShell.hpp"

#include "EditorCanvasLenses.hpp"

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
        case TopDownMarkerKind::Door: return ICON_FA_SIGN_IN;
        case TopDownMarkerKind::Anchor: return ICON_FA_ANCHOR;
        case TopDownMarkerKind::Model: return ICON_FA_CUBE;
    }
    return ICON_FA_QUESTION;
}

bool validCellOrCardinalHalo(int x, int y, int width, int height) {
    const bool inside = x >= 0 && y >= 0 && x < width && y < height;
    const bool horizontal_halo = (x == -1 || x == width) && y >= 0 && y < height;
    const bool vertical_halo = (y == -1 || y == height) && x >= 0 && x < width;
    return inside || horizontal_halo || vertical_halo;
}

bool openingCovers(
    const TopDownMapView& map,
    const char* edge,
    int along) {
    return std::any_of(
        map.interior_openings.begin(), map.interior_openings.end(),
        [&](const TopDownOpeningView& opening) {
            return opening.edge == edge && along >= opening.from && along <= opening.to;
        });
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
    if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) ||
        ((model.active_tool == EditorTool::Hand || ImGui::IsKeyDown(ImGuiKey_Space)) &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)))) {
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
            ImVec2 floor_min = cell_min;
            ImVec2 floor_max = cell_max;
            ImVec2 floor_uv0{0.0f, 0.0f};
            ImVec2 floor_uv1{1.0f, 1.0f};
            if (model.top_down.default_interior_room) {
                const float cut = std::clamp(
                    model.top_down.default_wall_offset_tiles, 0.0f, 1.0f);
                if (x == 0 && !openingCovers(model.top_down, "west", y)) {
                    floor_min.x += cut * top_down_cell_size_;
                    floor_uv0.x = cut;
                }
                if (x == width - 1 && !openingCovers(model.top_down, "east", y)) {
                    floor_max.x -= cut * top_down_cell_size_;
                    floor_uv1.x = 1.0f - cut;
                }
                if (y == 0 && !openingCovers(model.top_down, "north", x)) {
                    floor_min.y += cut * top_down_cell_size_;
                    floor_uv0.y = cut;
                }
                if (y == height - 1 && !openingCovers(model.top_down, "south", x)) {
                    floor_max.y -= cut * top_down_cell_size_;
                    floor_uv1.y = 1.0f - cut;
                }
            }
            const int tile = index < model.top_down.composed_tiles.size()
                ? model.top_down.composed_tiles[index] : -1;
            std::uint16_t texture = UINT16_MAX;
            if (tile >= 0 && model.tile_thumbnail) texture = model.tile_thumbnail(tile);
            if (texture != UINT16_MAX) {
                draw->AddImage(ImGui::toId(bgfx::TextureHandle{texture},
                    IMGUI_FLAGS_ALPHA_BLEND, 0), floor_min, floor_max, floor_uv0, floor_uv1);
            } else {
                const std::uint8_t height_value = index < model.top_down.heights.size()
                    ? model.top_down.heights[index] : 0U;
                const int shade = std::min(65, static_cast<int>(height_value) * 5);
                if (model.top_down.default_interior_room) {
                    const bool checker = ((x + y) & 1) == 0;
                    draw->AddRectFilled(floor_min, floor_max, checker
                        ? IM_COL32(48 + shade, 53 + shade, 64 + shade, 255)
                        : IM_COL32(55 + shade, 61 + shade, 73 + shade, 255));
                } else {
                    draw->AddRectFilled(floor_min, floor_max,
                        IM_COL32(36 + shade, 40 + shade, 48 + shade, 255));
                }
                if (tile >= 0 && top_down_cell_size_ >= 28.0f) {
                    char label[20]{};
                    std::snprintf(label, sizeof(label), "%d", tile);
                    draw->AddText(ImVec2(cell_min.x + 3.0f, cell_min.y + 3.0f),
                        IM_COL32(210, 214, 220, 220), label);
                }
            }
            canvas_lenses::drawCell(model, index, cell_min, cell_max,
                top_down_cell_size_, *draw);
            if (model.active_tool != EditorTool::Height &&
                index < model.top_down.specials.size() && model.top_down.specials[index] != 0U) {
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

    if (model.top_down.default_interior_room &&
        model.top_down.default_entry_extension_depth_tiles > 0.0f) {
        const float south = origin.y + height * top_down_cell_size_;
        const int entry_rows = std::max(0, static_cast<int>(std::lround(
            model.top_down.default_entry_extension_depth_tiles)));
        for (const TopDownOpeningView& opening : model.top_down.interior_openings) {
            if (opening.edge != "south") continue;
            const int from = std::clamp(opening.from, 0, std::max(0, width - 1));
            const int to = std::clamp(opening.to, 0, std::max(0, width - 1));
            for (int row = 0; row < entry_rows; ++row) {
                for (int x = from; x <= to; ++x) {
                    const ImVec2 extension_min{
                        origin.x + x * top_down_cell_size_,
                        south + row * top_down_cell_size_};
                    const ImVec2 extension_max{
                        extension_min.x + top_down_cell_size_,
                        extension_min.y + top_down_cell_size_};
                    const bool checker = ((x + height + row) & 1) == 0;
                    draw->AddRectFilled(extension_min, extension_max, checker
                        ? IM_COL32(48, 53, 64, 255)
                        : IM_COL32(55, 61, 73, 255));
                    draw->AddRect(extension_min, extension_max,
                        IM_COL32(77, 87, 105, 125));
                }
            }
        }
    }

    if (model.top_down.default_interior_room && width > 0 && height > 0) {
        const float full_wall = std::clamp(top_down_cell_size_ * 0.18f, 3.0f, 10.0f);
        const float front_wall = std::max(2.0f, full_wall * 0.45f);
        const float wall_offset =
            std::max(0.0f, model.top_down.default_wall_offset_tiles) * top_down_cell_size_;
        const float north = origin.y + wall_offset;
        const float south = origin.y + height * top_down_cell_size_ - wall_offset;
        const float west = origin.x + wall_offset;
        const float east = origin.x + width * top_down_cell_size_ - wall_offset;
        const ImU32 wall = IM_COL32(103, 111, 132, 255);
        const ImU32 front = IM_COL32(72, 79, 96, 245);
        for (int x = 0; x < width; ++x) {
            const float cell_x0 = origin.x + x * top_down_cell_size_;
            const float cell_x1 = cell_x0 + top_down_cell_size_;
            const float x0 = std::max(cell_x0, west);
            const float x1 = std::min(cell_x1, east);
            if (!openingCovers(model.top_down, "north", x)) {
                draw->AddLine({x0, north}, {x1, north}, wall, full_wall);
            }
            if (!openingCovers(model.top_down, "south", x)) {
                draw->AddLine({x0, south}, {x1, south}, front, front_wall);
            }
        }
        for (int y = 0; y < height; ++y) {
            const float cell_y0 = origin.y + y * top_down_cell_size_;
            const float cell_y1 = cell_y0 + top_down_cell_size_;
            const float y0 = std::max(cell_y0, north);
            const float y1 = std::min(cell_y1, south);
            if (!openingCovers(model.top_down, "west", y)) {
                draw->AddLine({west, y0}, {west, y1}, wall, full_wall);
            }
            if (!openingCovers(model.top_down, "east", y)) {
                draw->AddLine({east, y0}, {east, y1}, wall, full_wall);
            }
        }
    }

    for (const TopDownMarkerView& marker : model.top_down.markers) {
        if (marker.kind != TopDownMarkerKind::Model || marker.model_outline_tiles.empty()) continue;
        const ImVec2 center{
            origin.x + marker.center_tile_x * top_down_cell_size_,
            origin.y + marker.center_tile_y * top_down_cell_size_};
        std::vector<ImVec2> outline;
        outline.reserve(marker.model_outline_tiles.size());
        for (const auto& point : marker.model_outline_tiles) {
            outline.push_back({
                center.x + point[0] * top_down_cell_size_,
                center.y + point[1] * top_down_cell_size_,
            });
        }
        const ImU32 fill = marker.selected
            ? IM_COL32(199, 150, 255, 112) : IM_COL32(167, 105, 234, 72);
        const ImU32 edge = marker.selected
            ? IM_COL32(255, 225, 128, 255) : IM_COL32(200, 153, 255, 220);
        if (outline.size() >= 3U) {
            draw->AddConvexPolyFilled(outline.data(), static_cast<int>(outline.size()), fill);
            draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), edge,
                ImDrawFlags_Closed, marker.selected ? 2.5f : 1.5f);
        } else if (outline.size() == 2U) {
            draw->AddLine(outline[0], outline[1], edge, marker.selected ? 3.0f : 2.0f);
        } else {
            draw->AddCircleFilled(outline[0], 3.0f, edge);
        }
        if (marker.selected || top_down_cell_size_ >= 42.0f) {
            char dimensions[96]{};
            std::snprintf(dimensions, sizeof(dimensions), "%s  %.2f x %.2f tiles",
                marker.id.c_str(), marker.projected_width_tiles, marker.projected_depth_tiles);
            const ImVec2 text_size = ImGui::CalcTextSize(dimensions);
            const ImVec2 label_min{center.x - text_size.x * 0.5f - 4.0f,
                center.y - text_size.y - 10.0f};
            const ImVec2 label_max{label_min.x + text_size.x + 8.0f,
                label_min.y + text_size.y + 5.0f};
            draw->AddRectFilled(label_min, label_max, IM_COL32(18, 15, 27, 225), 3.0f);
            draw->AddText(ImVec2{label_min.x + 4.0f, label_min.y + 2.0f},
                IM_COL32(238, 226, 255, 255), dimensions);
        }
    }

    for (const TopDownMarkerView& marker : model.top_down.markers) {
        const ImVec2 center{origin.x + (marker.kind == TopDownMarkerKind::Model
                ? marker.center_tile_x : marker.tile_x + 0.5f) * top_down_cell_size_,
            origin.y + (marker.kind == TopDownMarkerKind::Model
                ? marker.center_tile_y : marker.tile_y + 0.5f) * top_down_cell_size_};
        if (center.x < canvas_min.x || center.x > canvas_max.x ||
            center.y < canvas_min.y || center.y > canvas_max.y) continue;
        const float radius = std::clamp(top_down_cell_size_ * 0.22f, 5.0f, 13.0f);
        const float opacity = marker.selected ? 1.0F
            : canvas_lenses::markerOpacity(model.active_tool, marker.kind);
        const ImU32 base_color = markerColor(marker.kind);
        const ImU32 color = (base_color & IM_COL32(255, 255, 255, 0)) |
            (static_cast<ImU32>(255.0F * opacity) << IM_COL32_A_SHIFT);
        draw->AddCircleFilled(center, radius, color);
        if (marker.selected) draw->AddCircle(center, radius + 3.0f,
            IM_COL32(255, 232, 125, 255), 0, 2.0f);
        const ImVec2 text_size = ImGui::CalcTextSize(markerLabel(marker.kind));
        draw->AddText(ImVec2(center.x - text_size.x * 0.5f, center.y - text_size.y * 0.5f),
            IM_COL32(12, 15, 20, static_cast<int>(255.0F * opacity)), markerLabel(marker.kind));
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
        if (model.active_tool == EditorTool::ClearCell) {
            draw->AddRectFilled(cell_min, cell_max, IM_COL32(235, 62, 73, 116));
            draw->AddLine(cell_min, cell_max, IM_COL32(255, 149, 155, 245), 2.0F);
            draw->AddLine(ImVec2{cell_max.x, cell_min.y}, ImVec2{cell_min.x, cell_max.y},
                IM_COL32(255, 149, 155, 245), 2.0F);
        } else if (model.active_tool == EditorTool::EraseLayer) {
            const std::size_t index = tile_x >= 0 && tile_y >= 0
                ? static_cast<std::size_t>(tile_y * width + tile_x)
                : std::numeric_limits<std::size_t>::max();
            if (index < model.top_down.active_layer_tiles.size() &&
                model.top_down.active_layer_tiles[index] >= 0) {
                draw->AddRectFilled(cell_min, cell_max, IM_COL32(235, 92, 70, 105));
            }
        }
        draw->AddRect(cell_min, cell_max, IM_COL32(77, 208, 255, 255), 0.0f, 0, 2.0f);
        std::string cursor_label;
        if (model.active_tool == EditorTool::Height) {
            cursor_label = "Height " + std::to_string(model.height_brush_value);
        } else if (model.active_tool == EditorTool::Collision) {
            cursor_label = model.collision_brush_value ? "Blocked" : "Walkable";
        } else if (model.active_tool == EditorTool::EraseLayer) {
            cursor_label = "Erase active layer";
        } else if (model.active_tool == EditorTool::ClearCell) {
            cursor_label = "Clear all cell data";
        } else if (!model.active_asset_id.empty()) {
            cursor_label = model.active_asset_id;
        }
        if (!cursor_label.empty()) {
            const ImVec2 text_size = ImGui::CalcTextSize(cursor_label.c_str());
            const ImVec2 label_min{mouse.x + 14.0f, mouse.y + 18.0f};
            const ImVec2 label_max{label_min.x + text_size.x + 12.0f,
                label_min.y + text_size.y + 8.0f};
            draw->AddRectFilled(label_min, label_max, IM_COL32(17, 24, 32, 238), 4.0f);
            draw->AddText(ImVec2{label_min.x + 6.0f, label_min.y + 4.0f},
                IM_COL32(225, 241, 250, 255), cursor_label.c_str());
        }
    }
    if (validCellOrCardinalHalo(model.selection.tile_x, model.selection.tile_y, width, height)) {
        const ImVec2 cell_min{origin.x + model.selection.tile_x * top_down_cell_size_,
            origin.y + model.selection.tile_y * top_down_cell_size_};
        const ImVec2 cell_max{cell_min.x + top_down_cell_size_,
            cell_min.y + top_down_cell_size_};
        draw->AddRect(cell_min, cell_max, IM_COL32(255, 214, 77, 255), 0.0f, 0, 3.0f);
    }
    canvas_lenses::drawLegend(model, canvas_min, canvas_max, *draw);
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
