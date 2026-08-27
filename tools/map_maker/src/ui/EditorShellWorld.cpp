#include "mapmaker/ui/EditorShell.hpp"

#include <dear-imgui/imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_map>

namespace pr::mapmaker {
namespace {

// Exterior maps occupy touching cells. This makes grid adjacency read as one
// continuous overworld; interiors use a smaller, floating card language.
constexpr float kGridX = 224.0f;
constexpr float kGridY = 144.0f;
constexpr float kCardWidth = 220.0f;
constexpr float kCardHeight = 140.0f;
constexpr float kInteriorWidth = 168.0f;
constexpr float kInteriorHeight = 82.0f;

bool containsInsensitive(const std::string& value, const char* filter) {
    if (!filter || !*filter) return true;
    const std::string needle(filter);
    return std::search(value.begin(), value.end(), needle.begin(), needle.end(),
        [](unsigned char left, unsigned char right) {
            return std::tolower(left) == std::tolower(right);
        }) != value.end();
}

const WorldMapNodeView* findNode(
    const std::vector<WorldMapNodeView>& nodes, const std::string& id) {
    const auto found = std::find_if(nodes.begin(), nodes.end(),
        [&](const WorldMapNodeView& node) { return node.id == id; });
    return found == nodes.end() ? nullptr : &*found;
}

bool pointInRect(const ImVec2& point, const ImVec2& minimum, const ImVec2& maximum) {
    return point.x >= minimum.x && point.y >= minimum.y &&
        point.x <= maximum.x && point.y <= maximum.y;
}

ImVec2 cardSize(const WorldMapNodeView& node, float zoom) {
    const bool interior = node.type == "interior";
    return ImVec2{(interior ? kInteriorWidth : kCardWidth) * zoom,
        (interior ? kInteriorHeight : kCardHeight) * zoom};
}

bool spatialNeighbors(const WorldMapNodeView& a, const WorldMapNodeView& b) {
    if (!a.linked || !b.linked || a.type == "interior" || b.type == "interior") return false;
    return std::abs(a.grid_x - b.grid_x) + std::abs(a.grid_y - b.grid_y) == 1;
}

} // namespace

void EditorShell::drawWorldViewport(EditorUiModel& model, EditorUiEvents& events) {
    const ImVec2 available{
        std::max(1.0f, ImGui::GetContentRegionAvail().x),
        std::max(1.0f, ImGui::GetContentRegionAvail().y)};
    const ImVec2 canvas_min = ImGui::GetCursorScreenPos();
    const ImVec2 canvas_max{canvas_min.x + available.x, canvas_min.y + available.y};
    ImGui::InvisibleButton("##world_canvas", available,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
        ImGuiButtonFlags_MouseButtonRight);
    const bool canvas_hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvas_min, canvas_max, IM_COL32(10, 13, 18, 255));
    draw->PushClipRect(canvas_min, canvas_max, true);

    if (!world_initialized_) {
        world_initialized_ = true;
        world_zoom_ = 0.9f;
        const auto active = std::find_if(model.world_maps.begin(), model.world_maps.end(),
            [](const WorldMapNodeView& node) { return node.active; });
        if (active != model.world_maps.end()) {
            world_pan_x_ = -active->grid_x * kGridX * world_zoom_;
            world_pan_y_ = -active->grid_y * kGridY * world_zoom_;
        }
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const auto worldToScreen = [&](ImVec2 world) {
        return ImVec2{canvas_min.x + available.x * 0.5f + world_pan_x_ + world.x * world_zoom_,
            canvas_min.y + available.y * 0.5f + world_pan_y_ + world.y * world_zoom_};
    };
    const auto screenToWorld = [&](ImVec2 screen) {
        return ImVec2{(screen.x - canvas_min.x - available.x * 0.5f - world_pan_x_) / world_zoom_,
            (screen.y - canvas_min.y - available.y * 0.5f - world_pan_y_) / world_zoom_};
    };

    if (canvas_hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        const ImVec2 before = screenToWorld(mouse);
        world_zoom_ = std::clamp(world_zoom_ *
            std::pow(1.12f, ImGui::GetIO().MouseWheel), 0.28f, 2.2f);
        const ImVec2 after = worldToScreen(before);
        world_pan_x_ += mouse.x - after.x;
        world_pan_y_ += mouse.y - after.y;
    }
    const bool hand_drag = (model.active_tool == EditorTool::Hand ||
        ImGui::IsKeyDown(ImGuiKey_Space)) &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f);
    if (canvas_hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f) || hand_drag)) {
        world_pan_x_ += ImGui::GetIO().MouseDelta.x;
        world_pan_y_ += ImGui::GetIO().MouseDelta.y;
    }

    const float minor_x = kGridX * world_zoom_;
    const float minor_y = kGridY * world_zoom_;
    if (minor_x >= 36.0f && minor_y >= 28.0f) {
        const float offset_x = std::fmod(world_pan_x_ + available.x * 0.5f, minor_x);
        const float offset_y = std::fmod(world_pan_y_ + available.y * 0.5f, minor_y);
        for (float x = canvas_min.x + offset_x; x < canvas_max.x; x += minor_x) {
            draw->AddLine(ImVec2{x, canvas_min.y}, ImVec2{x, canvas_max.y},
                IM_COL32(43, 51, 65, 82));
        }
        for (float y = canvas_min.y + offset_y; y < canvas_max.y; y += minor_y) {
            draw->AddLine(ImVec2{canvas_min.x, y}, ImVec2{canvas_max.x, y},
                IM_COL32(43, 51, 65, 82));
        }
    }

    std::unordered_map<std::string, ImVec2> world_positions;
    std::unordered_map<std::string, int> satellite_counts;
    world_positions.reserve(model.world_maps.size());
    for (const WorldMapNodeView& node : model.world_maps) {
        world_positions.emplace(node.id,
            ImVec2{node.grid_x * kGridX, node.grid_y * kGridY});
    }
    for (const WorldMapNodeView& node : model.world_maps) {
        if (node.type != "interior" || node.grid_x != 0 || node.grid_y != 0) continue;
        const auto incoming = std::find_if(model.world_connections.begin(),
            model.world_connections.end(), [&](const WorldConnectionView& connection) {
                return connection.destination_map_id == node.id &&
                    world_positions.contains(connection.source_map_id);
            });
        if (incoming == model.world_connections.end()) continue;
        const ImVec2 parent = world_positions[incoming->source_map_id];
        const int slot = satellite_counts[incoming->source_map_id]++;
        world_positions[node.id] = ImVec2{parent.x + kGridX * (0.66f + 0.82f * (slot % 2)),
            parent.y + kGridY * (0.66f + 0.70f * (slot / 2))};
    }

    // A shared edge is a continuous piece of the overworld, not a graph link.
    // Draw it first so neighboring exterior maps read as one spatial surface.
    for (std::size_t left = 0; left < model.world_maps.size(); ++left) {
        for (std::size_t right = left + 1; right < model.world_maps.size(); ++right) {
            const WorldMapNodeView& a_node = model.world_maps[left];
            const WorldMapNodeView& b_node = model.world_maps[right];
            if (!spatialNeighbors(a_node, b_node)) continue;
            const ImVec2 a = worldToScreen(world_positions[a_node.id]);
            const ImVec2 b = worldToScreen(world_positions[b_node.id]);
            draw->AddLine(a, b, IM_COL32(36, 83, 99, 150), 8.0f * world_zoom_);
        }
    }

    struct SeamMarker { ImVec2 midpoint; bool horizontal; ImU32 color; };
    std::vector<SeamMarker> seam_markers;
    for (const WorldConnectionView& connection : model.world_connections) {
        const auto source = world_positions.find(connection.source_map_id);
        if (source == world_positions.end()) continue;
        ImVec2 destination = source->second;
        const auto target = world_positions.find(connection.destination_map_id);
        if (target != world_positions.end()) destination = target->second;
        else destination.x += kGridX * 0.8f;
        ImVec2 a = worldToScreen(source->second);
        ImVec2 b = worldToScreen(destination);
        const ImU32 color = connection.broken ? IM_COL32(245, 83, 83, 225)
            : connection.reciprocal ? IM_COL32(86, 211, 255, 210)
            : IM_COL32(153, 128, 255, 190);
        const WorldMapNodeView* source_node = findNode(model.world_maps, connection.source_map_id);
        const WorldMapNodeView* target_node = findNode(model.world_maps, connection.destination_map_id);
        if (source_node && target_node && spatialNeighbors(*source_node, *target_node)) {
            // Travel between spatial neighbors is marked at their shared seam.
            const ImVec2 midpoint{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            const bool horizontal = source_node->grid_y == target_node->grid_y;
            seam_markers.push_back({midpoint, horizontal, color});
        } else {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float length = std::max(1.0f, std::hypot(dx, dy));
            const ImVec2 normal{dx / length, dy / length};
            a.x += normal.x * 58.0f * world_zoom_;
            a.y += normal.y * 40.0f * world_zoom_;
            b.x -= normal.x * 58.0f * world_zoom_;
            b.y -= normal.y * 40.0f * world_zoom_;
            const float bend = std::clamp(length * 0.22f, 34.0f, 110.0f);
            const ImVec2 perpendicular{-normal.y * bend, normal.x * bend};
            draw->AddBezierCubic(a,
                ImVec2{a.x + dx * 0.25f + perpendicular.x, a.y + dy * 0.25f + perpendicular.y},
                ImVec2{b.x - dx * 0.25f + perpendicular.x, b.y - dy * 0.25f + perpendicular.y},
                b, color, connection.reciprocal ? 3.0f : 2.0f);
            draw->AddCircleFilled(a, 4.0f, color);
            draw->AddCircleFilled(b, 5.0f, color);
        }
    }

    const WorldMapNodeView* hovered_node = nullptr;
    ImVec2 hovered_min{};
    ImVec2 hovered_max{};
    for (const WorldMapNodeView& node : model.world_maps) {
        ImVec2 world = world_positions[node.id];
        if (world_drag_map_id_ == node.id) {
            world = ImVec2{world_drag_current_x_ * kGridX, world_drag_current_y_ * kGridY};
        }
        const ImVec2 center = worldToScreen(world);
        const ImVec2 size = cardSize(node, world_zoom_);
        const ImVec2 half{size.x * 0.5f, size.y * 0.5f};
        const ImVec2 minimum{center.x - half.x, center.y - half.y};
        const ImVec2 maximum{center.x + half.x, center.y + half.y};
        if (maximum.x < canvas_min.x || minimum.x > canvas_max.x ||
            maximum.y < canvas_min.y || minimum.y > canvas_max.y) continue;
        const bool matches = containsInsensitive(node.name, world_search_.data()) ||
            containsInsensitive(node.id, world_search_.data());
        const bool hovered = canvas_hovered && pointInRect(mouse, minimum, maximum);
        if (hovered) {
            hovered_node = &node;
            hovered_min = minimum;
            hovered_max = maximum;
        }
        bool occupied = false;
        if (world_drag_map_id_ == node.id) {
            occupied = std::any_of(model.world_maps.begin(), model.world_maps.end(),
                [&](const WorldMapNodeView& other) {
                    return other.id != node.id && other.grid_x == world_drag_current_x_ &&
                        other.grid_y == world_drag_current_y_;
                });
        }
        const bool interior = node.type == "interior";
        const ImU32 fill = occupied ? IM_COL32(98, 32, 40, 250)
            : node.active ? IM_COL32(22, 82, 110, 252)
            : interior ? IM_COL32(49, 40, 72, matches ? 248 : 90)
            : IM_COL32(24, 39, 48, matches ? 248 : 90);
        const ImU32 border = node.error_count > 0 ? IM_COL32(245, 83, 83, 255)
            : hovered ? IM_COL32(100, 220, 255, 255)
            : node.active ? IM_COL32(78, 207, 255, 255)
            : IM_COL32(89, 103, 126, matches ? 220 : 80);
        const float rounding = interior ? 11.0f * world_zoom_ : 3.0f * world_zoom_;
        draw->AddRectFilled(minimum, maximum, IM_COL32(0, 0, 0, 90), rounding);
        const ImVec2 surface_min{minimum.x + 2.0f, minimum.y + 2.0f};
        const ImVec2 surface_max{maximum.x - 2.0f, maximum.y - 2.0f};
        draw->AddRectFilled(surface_min, surface_max, fill, rounding);
        draw->AddRectFilled(surface_min,
            ImVec2{surface_max.x, surface_min.y + 4.0f * world_zoom_},
            interior ? IM_COL32(176, 133, 255, matches ? 230 : 80)
                     : IM_COL32(66, 184, 167, matches ? 230 : 80), rounding);
        draw->AddRect(minimum, maximum, border, rounding, 0,
            node.active ? 3.0f : 1.5f);
        const float pad = 12.0f * world_zoom_;
        draw->PushClipRect(surface_min, surface_max, true);
        const char* map_icon = interior ? ICON_FA_BUILDING_O : ICON_FA_MAP_O;
        draw->AddText(ImVec2{minimum.x + pad, minimum.y + 13.0f * world_zoom_},
            interior ? IM_COL32(204, 177, 255, matches ? 255 : 100)
                     : IM_COL32(109, 222, 203, matches ? 255 : 100), map_icon);
        draw->AddText(ImVec2{minimum.x + pad + 24.0f * world_zoom_,
            minimum.y + 13.0f * world_zoom_}, IM_COL32(236, 242, 250, matches ? 255 : 100),
            node.name.c_str());
        const std::string details = std::to_string(node.width) + " x " +
            std::to_string(node.height) + "   " + std::to_string(node.door_count) + " doors";
        draw->AddText(ImVec2{minimum.x + pad, minimum.y + 43.0f * world_zoom_},
            IM_COL32(151, 164, 185, matches ? 255 : 85), details.c_str());
        if (!interior) {
            const std::string coordinate = (node.linked ? "World " : "Standalone  ") +
                std::to_string(node.grid_x) + ", " + std::to_string(node.grid_y);
            draw->AddText(ImVec2{minimum.x + pad, maximum.y - 27.0f * world_zoom_},
                IM_COL32(112, 185, 191, matches ? 255 : 85), coordinate.c_str());
        }
        if (node.shared_source || node.dirty || node.error_count > 0) {
            std::string badges;
            if (node.error_count > 0) badges += ICON_FA_EXCLAMATION_TRIANGLE;
            if (node.shared_source) badges += std::string("  ") + ICON_FA_LINK;
            if (node.dirty) badges += "  •";
            draw->AddText(ImVec2{maximum.x - 55.0f * world_zoom_,
                maximum.y - 27.0f * world_zoom_}, node.error_count > 0
                    ? IM_COL32(255, 124, 110, 255) : IM_COL32(117, 202, 226, 255), badges.c_str());
        }
        draw->PopClipRect();
    }

    // Portal notches sit above both neighboring surfaces so they cannot be
    // hidden by the cards that meet at the shared world edge.
    for (const SeamMarker& marker : seam_markers) {
        const ImVec2 half = marker.horizontal ? ImVec2{5.0f, 16.0f} : ImVec2{16.0f, 5.0f};
        draw->AddRectFilled(ImVec2{marker.midpoint.x - half.x, marker.midpoint.y - half.y},
            ImVec2{marker.midpoint.x + half.x, marker.midpoint.y + half.y}, marker.color, 4.0f);
        draw->AddCircle(marker.midpoint, 6.0f, IM_COL32(226, 248, 255, 235), 0, 1.5f);
    }

    if (hovered_node && model.active_tool != EditorTool::Hand) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            events.select_world_map_id = hovered_node->id;
            world_drag_map_id_ = hovered_node->id;
            world_drag_start_x_ = hovered_node->grid_x;
            world_drag_start_y_ = hovered_node->grid_y;
            world_drag_current_x_ = world_drag_start_x_;
            world_drag_current_y_ = world_drag_start_y_;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            events.activate_map_id = hovered_node->id;
            world_drag_map_id_.clear();
        }
        const float radius = 11.0f;
        const struct DirectionButton { ImVec2 center; int dx; int dy; const char* name; } buttons[] = {
            {{(hovered_min.x + hovered_max.x) * 0.5f, hovered_min.y + 16.0f}, 0, -1, "north"},
            {{hovered_max.x - 16.0f, (hovered_min.y + hovered_max.y) * 0.5f}, 1, 0, "east"},
            {{(hovered_min.x + hovered_max.x) * 0.5f, hovered_max.y - 16.0f}, 0, 1, "south"},
            {{hovered_min.x + 16.0f, (hovered_min.y + hovered_max.y) * 0.5f}, -1, 0, "west"},
        };
        for (const auto& button : buttons) {
            if (hovered_node->type == "interior") break;
            const bool occupied_direction = std::any_of(model.world_maps.begin(),
                model.world_maps.end(), [&](const WorldMapNodeView& other) {
                    return other.type != "interior" &&
                        other.grid_x == hovered_node->grid_x + button.dx &&
                        other.grid_y == hovered_node->grid_y + button.dy;
                });
            if (occupied_direction) continue;
            const float distance = std::hypot(mouse.x - button.center.x, mouse.y - button.center.y);
            const bool hot = distance <= radius;
            draw->AddCircleFilled(button.center, radius,
                hot ? IM_COL32(64, 174, 220, 255) : IM_COL32(38, 54, 70, 255));
            draw->AddText(ImVec2{button.center.x - 4.0f, button.center.y - 8.0f},
                IM_COL32_WHITE, "+");
            if (hot && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                beginWorldMapCreation(
                    model, hovered_node->id, button.dx, button.dy, button.name);
                world_drag_map_id_.clear();
            }
        }
    }

    if (!world_drag_map_id_.empty() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const ImVec2 world = screenToWorld(mouse);
        world_drag_current_x_ = static_cast<int>(std::lround(world.x / kGridX));
        world_drag_current_y_ = static_cast<int>(std::lround(world.y / kGridY));
    }
    if (!world_drag_map_id_.empty() && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (world_drag_current_x_ != world_drag_start_x_ ||
            world_drag_current_y_ != world_drag_start_y_) {
            events.move_map = MapMoveRequest{
                world_drag_map_id_, world_drag_current_x_, world_drag_current_y_};
        }
        world_drag_map_id_.clear();
    }

    const ImVec2 legend_min{canvas_min.x + 14.0f, canvas_max.y - 37.0f};
    const ImVec2 legend_max{canvas_min.x + 455.0f, canvas_max.y - 10.0f};
    draw->AddRectFilled(legend_min, legend_max, IM_COL32(13, 17, 23, 225), 7.0f);
    draw->AddLine(ImVec2{legend_min.x + 14.0f, legend_min.y + 13.0f},
        ImVec2{legend_min.x + 38.0f, legend_min.y + 13.0f}, IM_COL32(66, 184, 167, 255), 5.0f);
    draw->AddText(ImVec2{legend_min.x + 45.0f, legend_min.y + 5.0f},
        IM_COL32(176, 188, 205, 255), "Spatial neighbor");
    draw->AddLine(ImVec2{legend_min.x + 170.0f, legend_min.y + 13.0f},
        ImVec2{legend_min.x + 194.0f, legend_min.y + 13.0f}, IM_COL32(86, 211, 255, 255), 3.0f);
    draw->AddText(ImVec2{legend_min.x + 201.0f, legend_min.y + 5.0f},
        IM_COL32(176, 188, 205, 255), "Two-way door");
    draw->AddLine(ImVec2{legend_min.x + 310.0f, legend_min.y + 13.0f},
        ImVec2{legend_min.x + 334.0f, legend_min.y + 13.0f}, IM_COL32(153, 128, 255, 255), 2.0f);
    draw->AddText(ImVec2{legend_min.x + 341.0f, legend_min.y + 5.0f},
        IM_COL32(176, 188, 205, 255), "One-way");

    const std::string zoom_label = std::to_string(static_cast<int>(world_zoom_ * 100.0f)) + "%";
    draw->AddText(ImVec2{canvas_max.x - 50.0f, canvas_max.y - 29.0f},
        IM_COL32(132, 146, 165, 255), zoom_label.c_str());
    draw->AddText(ImVec2{canvas_max.x - 39.0f, canvas_min.y + 13.0f},
        IM_COL32(132, 146, 165, 255), "N");
    draw->AddTriangleFilled(ImVec2{canvas_max.x - 34.0f, canvas_min.y + 31.0f},
        ImVec2{canvas_max.x - 41.0f, canvas_min.y + 43.0f},
        ImVec2{canvas_max.x - 27.0f, canvas_min.y + 43.0f}, IM_COL32(88, 195, 220, 220));
    draw->PopClipRect();

    drawWorldMapCreationModal(model, events);
}

} // namespace pr::mapmaker
