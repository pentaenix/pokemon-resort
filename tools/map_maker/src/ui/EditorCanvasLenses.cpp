#include "EditorCanvasLenses.hpp"

#include "mapmaker/ui/CanvasLens.hpp"

#include <algorithm>
#include <array>
#include <cstdio>

namespace pr::mapmaker::canvas_lenses {
namespace {

void centeredText(ImDrawList& draw, const ImVec2& min, const ImVec2& max,
    ImU32 color, const char* text) {
    const ImVec2 size = ImGui::CalcTextSize(text);
    draw.AddText(ImVec2{min.x + (max.x - min.x - size.x) * 0.5F,
        min.y + (max.y - min.y - size.y) * 0.5F}, color, text);
}

void drawSpecialDirection(ImDrawList& draw, std::uint8_t special,
    const ImVec2& min, const ImVec2& max) {
    if (special == 14U) {
        draw.AddCircleFilled(ImVec2{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F},
            std::min(max.x - min.x, max.y - min.y) * 0.30F,
            IM_COL32(14, 165, 233, 230));
        centeredText(draw, min, max, IM_COL32(255, 255, 255, 255), "S");
        return;
    }
    const ImVec2 center{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F};
    const float extent = std::min(max.x - min.x, max.y - min.y) * 0.28F;
    ImVec2 tip = center;
    if (special == 2U) tip.y -= extent;
    else if (special == 3U) tip.x += extent;
    else if (special == 4U) tip.y += extent;
    else if (special == 5U) tip.x -= extent;
    else {
        draw.AddQuad(ImVec2{center.x, center.y - extent},
            ImVec2{center.x + extent, center.y}, ImVec2{center.x, center.y + extent},
            ImVec2{center.x - extent, center.y}, IM_COL32(255, 245, 206, 230), 1.5F);
        return;
    }
    draw.AddLine(center, tip, IM_COL32(255, 245, 206, 245), 2.0F);
    const ImVec2 perpendicular{-(tip.y - center.y), tip.x - center.x};
    const float scale = 0.32F;
    const ImVec2 base{center.x + (tip.x - center.x) * 0.58F,
        center.y + (tip.y - center.y) * 0.58F};
    draw.AddTriangleFilled(tip,
        ImVec2{base.x + perpendicular.x * scale, base.y + perpendicular.y * scale},
        ImVec2{base.x - perpendicular.x * scale, base.y - perpendicular.y * scale},
        IM_COL32(255, 245, 206, 245));
}

void legendPanel(ImDrawList& draw, const ImVec2& min, const ImVec2& max) {
    draw.AddRectFilled(min, max, IM_COL32(13, 17, 23, 235), 6.0F);
    draw.AddRect(min, max, IM_COL32(74, 87, 105, 220), 6.0F);
}

} // namespace

void drawCell(const EditorUiModel& model, std::size_t index, const ImVec2& cell_min,
    const ImVec2& cell_max, float cell_size, ImDrawList& draw) {
    if (model.active_tool == EditorTool::Height) {
        const std::uint8_t value = index < model.top_down.heights.size()
            ? model.top_down.heights[index] : 0U;
        draw.AddRectFilled(cell_min, cell_max, heightHeatColor(value, 184U));
        if (cell_size >= 18.0F) {
            char label[8]{};
            std::snprintf(label, sizeof(label), "%u", static_cast<unsigned>(value));
            centeredText(draw, cell_min, cell_max, heatMapTextColor(value), label);
        }
        if (index < model.top_down.specials.size() && model.top_down.specials[index] != 0U &&
            cell_size >= 16.0F) {
            drawSpecialDirection(draw, model.top_down.specials[index], cell_min, cell_max);
        }
        return;
    }

    if (model.active_tool == EditorTool::Collision || model.collision_overlay) {
        const bool authored_blocked = index < model.top_down.collision.size() &&
            model.top_down.collision[index] != 0U;
        const bool room_wall_blocked = index < model.top_down.automatic_collision.size() &&
            model.top_down.automatic_collision[index] != 0U;
        const bool blocked = authored_blocked || room_wall_blocked;
        draw.AddRectFilled(cell_min, cell_max, room_wall_blocked
            ? IM_COL32(176, 113, 238, 112)
            : collisionLensColor(blocked, blocked ? 118U : 66U));
        if (blocked && cell_size >= 10.0F) {
            const ImU32 mark = room_wall_blocked
                ? IM_COL32(222, 183, 255, 225)
                : IM_COL32(255, 118, 124, 215);
            draw.AddLine(cell_min, cell_max, mark, 1.4F);
            draw.AddLine(ImVec2{cell_max.x, cell_min.y}, ImVec2{cell_min.x, cell_max.y},
                mark, 1.4F);
        }
        return;
    }

    const bool terrain_tool = model.active_tool == EditorTool::Paint ||
        model.active_tool == EditorTool::EraseLayer || model.active_tool == EditorTool::ClearCell ||
        model.active_tool == EditorTool::Eyedropper;
    if (!terrain_tool || index >= model.top_down.active_layer_tiles.size()) return;
    if (model.top_down.active_layer_tiles[index] < 0) {
        draw.AddRectFilled(cell_min, cell_max, IM_COL32(4, 7, 11, 112));
    } else {
        draw.AddRect(cell_min, cell_max, IM_COL32(72, 201, 242, 165), 0.0F,
            ImDrawFlags_None, 1.25F);
    }
}

void drawLegend(const EditorUiModel& model, const ImVec2& canvas_min,
    const ImVec2& canvas_max, ImDrawList& draw) {
    if (model.active_tool == EditorTool::Height) {
        constexpr std::array<std::uint8_t, 5> values{0U, 8U, 16U, 24U, 31U};
        const ImVec2 panel_min{canvas_max.x - 282.0F, canvas_max.y - 58.0F};
        const ImVec2 panel_max{canvas_max.x - 12.0F, canvas_max.y - 12.0F};
        legendPanel(draw, panel_min, panel_max);
        draw.AddText(ImVec2{panel_min.x + 10.0F, panel_min.y + 6.0F},
            IM_COL32(224, 231, 240, 255), "HEIGHT");
        const float start_x = panel_min.x + 76.0F;
        for (std::size_t i = 0; i < values.size(); ++i) {
            const float x = start_x + static_cast<float>(i) * 36.0F;
            draw.AddRectFilled(ImVec2{x, panel_min.y + 7.0F},
                ImVec2{x + 28.0F, panel_min.y + 21.0F}, heightHeatColor(values[i], 255U), 2.0F);
            char label[8]{};
            std::snprintf(label, sizeof(label), i + 1U == values.size() ? "%u+" : "%u",
                static_cast<unsigned>(values[i]));
            draw.AddText(ImVec2{x + 4.0F, panel_min.y + 25.0F},
                IM_COL32(185, 196, 211, 255), label);
        }
        return;
    }
    if (model.active_tool == EditorTool::Collision || model.collision_overlay) {
        const ImVec2 panel_min{canvas_max.x - 338.0F, canvas_max.y - 44.0F};
        const ImVec2 panel_max{canvas_max.x - 12.0F, canvas_max.y - 12.0F};
        legendPanel(draw, panel_min, panel_max);
        draw.AddRectFilled(ImVec2{panel_min.x + 10.0F, panel_min.y + 9.0F},
            ImVec2{panel_min.x + 24.0F, panel_min.y + 23.0F}, collisionLensColor(false, 255U), 2.0F);
        draw.AddText(ImVec2{panel_min.x + 30.0F, panel_min.y + 7.0F},
            IM_COL32(218, 227, 237, 255), "Walkable");
        draw.AddRectFilled(ImVec2{panel_min.x + 112.0F, panel_min.y + 9.0F},
            ImVec2{panel_min.x + 126.0F, panel_min.y + 23.0F}, collisionLensColor(true, 255U), 2.0F);
        draw.AddText(ImVec2{panel_min.x + 132.0F, panel_min.y + 7.0F},
            IM_COL32(218, 227, 237, 255), "Blocked");
        draw.AddRectFilled(ImVec2{panel_min.x + 212.0F, panel_min.y + 9.0F},
            ImVec2{panel_min.x + 226.0F, panel_min.y + 23.0F},
            IM_COL32(176, 113, 238, 255), 2.0F);
        draw.AddText(ImVec2{panel_min.x + 232.0F, panel_min.y + 7.0F},
            IM_COL32(218, 227, 237, 255), "Room wall");
        return;
    }
    const bool terrain_tool = model.active_tool == EditorTool::Paint ||
        model.active_tool == EditorTool::EraseLayer || model.active_tool == EditorTool::ClearCell ||
        model.active_tool == EditorTool::Eyedropper;
    if (terrain_tool) {
        const ImVec2 panel_min{canvas_max.x - 278.0F, canvas_max.y - 44.0F};
        const ImVec2 panel_max{canvas_max.x - 12.0F, canvas_max.y - 12.0F};
        legendPanel(draw, panel_min, panel_max);
        draw.AddRectFilled(ImVec2{panel_min.x + 10.0F, panel_min.y + 9.0F},
            ImVec2{panel_min.x + 24.0F, panel_min.y + 23.0F}, IM_COL32(72, 201, 242, 255), 2.0F);
        draw.AddText(ImVec2{panel_min.x + 30.0F, panel_min.y + 7.0F},
            IM_COL32(218, 227, 237, 255), "Active layer");
        draw.AddRectFilled(ImVec2{panel_min.x + 142.0F, panel_min.y + 9.0F},
            ImVec2{panel_min.x + 156.0F, panel_min.y + 23.0F}, IM_COL32(10, 13, 18, 255), 2.0F);
        draw.AddText(ImVec2{panel_min.x + 162.0F, panel_min.y + 7.0F},
            IM_COL32(218, 227, 237, 255), "Other layers");
        return;
    }
    (void)canvas_min;
}

float markerOpacity(EditorTool tool, TopDownMarkerKind kind) {
    if (tool == EditorTool::Doors) return kind == TopDownMarkerKind::Door ? 1.0F : 0.2F;
    if (tool == EditorTool::Anchors) return kind == TopDownMarkerKind::Anchor ? 1.0F : 0.2F;
    if (tool == EditorTool::Objects || tool == EditorTool::SmartObjects) {
        return kind == TopDownMarkerKind::Model ? 1.0F : 0.2F;
    }
    return 1.0F;
}

} // namespace pr::mapmaker::canvas_lenses
