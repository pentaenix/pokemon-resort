#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include "aquarium_geometry/Kernel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pr::gameplay::world3d::aquarium::construction {
namespace geo = pr::aquarium::geometry;
namespace {

ConstructionHudRect* rectForAction(ConstructionHudLayout& layout, ConstructionHudAction action) {
    switch (action) {
        case ConstructionHudAction::Place: return &layout.place;
        case ConstructionHudAction::Select: return &layout.select;
        case ConstructionHudAction::Move: return &layout.move;
        case ConstructionHudAction::Resize: return &layout.resize;
        case ConstructionHudAction::Shape: return &layout.shape;
        case ConstructionHudAction::Height: return &layout.height;
        case ConstructionHudAction::Roundness: return &layout.roundness;
        case ConstructionHudAction::Rotate: return &layout.rotate;
        case ConstructionHudAction::NotchWidth: return &layout.notch_width;
        case ConstructionHudAction::NotchDepth: return &layout.notch_depth;
        case ConstructionHudAction::Review: return &layout.review;
        case ConstructionHudAction::Build: return &layout.build;
        case ConstructionHudAction::Adjust: return &layout.adjust;
        case ConstructionHudAction::Delete: return &layout.remove;
        case ConstructionHudAction::Undo: return &layout.undo;
        case ConstructionHudAction::Redo: return &layout.redo;
        case ConstructionHudAction::Cancel: return &layout.cancel;
        case ConstructionHudAction::Done: return &layout.done;
        case ConstructionHudAction::Exit: return &layout.exit;
        case ConstructionHudAction::None: return nullptr;
    }
    return nullptr;
}

const ConstructionHudRect* rectForAction(
    const ConstructionHudLayout& layout, ConstructionHudAction action) {
    return rectForAction(const_cast<ConstructionHudLayout&>(layout), action);
}

bool propertyAction(ConstructionHudAction action) {
    return action == ConstructionHudAction::Shape ||
        action == ConstructionHudAction::Height ||
        action == ConstructionHudAction::Roundness ||
        action == ConstructionHudAction::Rotate ||
        action == ConstructionHudAction::NotchWidth ||
        action == ConstructionHudAction::NotchDepth;
}

std::vector<ConstructionHudChoice> choicesForValues(
    const ConstructionHudRect& area,
    ConstructionHudAction action,
    const std::vector<int>& values,
    int current) {
    std::vector<ConstructionHudChoice> out;
    if (values.empty() || area.width <= 0 || area.height <= 0) return out;
    const int gap = std::clamp(area.width / 100, 4, 10);
    const int available = area.width - gap * (static_cast<int>(values.size()) - 1);
    const int width = std::max(12, available / static_cast<int>(values.size()));
    const int total = width * static_cast<int>(values.size()) +
        gap * (static_cast<int>(values.size()) - 1);
    const int left = area.x + std::max(0, (area.width - total) / 2);
    int selected_index = 0;
    int selected_distance = std::numeric_limits<int>::max();
    for (std::size_t index = 0; index < values.size(); ++index) {
        const int distance = std::abs(values[index] - current);
        if (distance < selected_distance) {
            selected_distance = distance;
            selected_index = static_cast<int>(index);
        }
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        out.push_back({
            {left + static_cast<int>(index) * (width + gap), area.y, width, area.height},
            action,
            values[index],
            static_cast<int>(index) == selected_index,
            true,
        });
    }
    return out;
}

std::vector<int> stepperValues(int minimum, int maximum, int current) {
    const int selected = std::clamp(current, minimum, maximum);
    std::vector<int> values;
    if (selected > minimum) values.push_back(selected - 1);
    values.push_back(selected);
    if (selected < maximum) values.push_back(selected + 1);
    return values;
}

} // namespace

bool ConstructionHudRect::contains(int point_x, int point_y) const {
    return width > 0 && height > 0 && point_x >= x && point_y >= y &&
        point_x < x + width && point_y < y + height;
}

bool aquariumConstructionPropertyPanelVisible(ConstructionState state, bool has_tank) {
    return has_tank &&
        (state == ConstructionState::Selected || state == ConstructionState::DraftReview);
}

std::vector<ConstructionHudAction> aquariumConstructionHudActions(
    ConstructionState state, bool property_draft) {
    switch (state) {
        case ConstructionState::Browse:
            return {ConstructionHudAction::Place, ConstructionHudAction::Select,
                ConstructionHudAction::Undo, ConstructionHudAction::Redo,
                ConstructionHudAction::Exit};
        case ConstructionState::Selected:
            return {ConstructionHudAction::Move, ConstructionHudAction::Resize,
                ConstructionHudAction::Shape, ConstructionHudAction::Height,
                ConstructionHudAction::Roundness, ConstructionHudAction::Rotate,
                ConstructionHudAction::NotchWidth, ConstructionHudAction::NotchDepth,
                ConstructionHudAction::Delete, ConstructionHudAction::Undo,
                ConstructionHudAction::Redo, ConstructionHudAction::Done};
        case ConstructionState::ResizeFootprint:
        case ConstructionState::MoveTank:
        case ConstructionState::ResizeTank:
            return {ConstructionHudAction::Review, ConstructionHudAction::Cancel};
        case ConstructionState::DraftReview:
            if (property_draft) return {ConstructionHudAction::Build,
                ConstructionHudAction::Shape, ConstructionHudAction::Height,
                ConstructionHudAction::Roundness, ConstructionHudAction::Rotate,
                ConstructionHudAction::NotchWidth, ConstructionHudAction::NotchDepth,
                ConstructionHudAction::Cancel};
            return {ConstructionHudAction::Build, ConstructionHudAction::Adjust,
                ConstructionHudAction::Shape, ConstructionHudAction::Height,
                ConstructionHudAction::Roundness, ConstructionHudAction::Rotate,
                ConstructionHudAction::NotchWidth, ConstructionHudAction::NotchDepth,
                ConstructionHudAction::Cancel};
        case ConstructionState::DeleteConfirm:
            return {ConstructionHudAction::Delete, ConstructionHudAction::Cancel};
        case ConstructionState::Dormant:
        case ConstructionState::Building:
            return {};
    }
    return {};
}

ConstructionHudLayout aquariumConstructionHudLayout(
    int viewport_width, int viewport_height, ConstructionState state,
    bool property_draft) {
    ConstructionHudLayout layout;
    const int width = std::max(1, viewport_width);
    const int height = std::max(1, viewport_height);
    const int margin = std::clamp(std::min(width, height) / 40, 10, 20);
    const int button = std::clamp(height / 10, 48, 72);
    const int gap = std::clamp(button / 7, 6, 10);
    const int top = std::max(46, margin + 30);
    const auto actions = aquariumConstructionHudActions(state, property_draft);
    std::vector<ConstructionHudAction> tools;
    std::vector<ConstructionHudAction> properties;
    for (const auto action : actions) {
        (propertyAction(action) ? properties : tools).push_back(action);
    }

    const int tool_height = tools.empty() ? 0 :
        button * static_cast<int>(tools.size()) + gap * (static_cast<int>(tools.size()) - 1);
    layout.tool_panel = {margin, top - gap,
        tools.empty() ? 0 : button + gap * 2,
        tools.empty() ? 0 : std::min(height - top - margin + gap, tool_height + gap * 2)};
    for (std::size_t index = 0; index < tools.size(); ++index) {
        if (auto* rect = rectForAction(layout, tools[index])) {
            *rect = {margin + gap, top + static_cast<int>(index) * (button + gap),
                button, button};
        }
    }

    const bool show_properties = !properties.empty() &&
        (state == ConstructionState::Selected || state == ConstructionState::DraftReview);
    if (show_properties) {
        const int panel_left = margin + button + gap * 3;
        const int panel_height = std::clamp(height * 27 / 100, 124, 184);
        layout.property_panel = {
            panel_left, height - margin - panel_height,
            std::max(1, width - panel_left - margin), panel_height};
        const int group_gap = std::clamp(gap, 6, 12);
        const int group_height = std::clamp(panel_height * 37 / 100, 46, 64);
        const int group_width = std::max(38, std::min(82,
            (layout.property_panel.width - group_gap * (static_cast<int>(properties.size()) + 1)) /
                std::max(1, static_cast<int>(properties.size()))));
        const int groups_total = group_width * static_cast<int>(properties.size()) +
            group_gap * (static_cast<int>(properties.size()) - 1);
        const int group_left = layout.property_panel.x +
            std::max(group_gap, (layout.property_panel.width - groups_total) / 2);
        for (std::size_t index = 0; index < properties.size(); ++index) {
            if (auto* rect = rectForAction(layout, properties[index])) {
                *rect = {group_left + static_cast<int>(index) * (group_width + group_gap),
                    layout.property_panel.y + group_gap, group_width, group_height};
            }
        }
        const int options_top = layout.property_panel.y + group_gap + group_height + group_gap;
        layout.property_options = {
            layout.property_panel.x + group_gap, options_top,
            layout.property_panel.width - group_gap * 2,
            std::max(26, layout.property_panel.y + layout.property_panel.height -
                group_gap - options_top)};
    }

    const int world_left = margin + (layout.tool_panel.width > 0 ? layout.tool_panel.width + gap : 0);
    const int world_bottom = layout.property_panel.height > 0
        ? layout.property_panel.y - gap : height - margin;
    layout.safe_world = {
        world_left, top,
        std::max(1, width - margin - world_left),
        std::max(1, world_bottom - top)};
    layout.status = {layout.safe_world.x, margin,
        layout.safe_world.width, std::max(24, top - margin - gap)};
    return layout;
}

std::vector<ConstructionHudChoice> aquariumConstructionPropertyChoices(
    const ConstructionHudLayout& layout,
    const AquariumConstructionVisual& visual) {
    const auto tank = visual.preview_tank ? visual.preview_tank : visual.selected_tank;
    if (!tank || layout.property_options.width <= 0) return {};
    switch (visual.focused_action) {
    case ConstructionHudAction::Shape:
        return choicesForValues(layout.property_options, visual.focused_action,
            {0, 1, 2}, static_cast<int>(tank->footprint.shape));
    case ConstructionHudAction::Height: {
        std::vector<int> values;
        for (int value = 4; value <= 12; ++value) values.push_back(value);
        return choicesForValues(layout.property_options, visual.focused_action,
            values, tank->height_steps);
    }
    case ConstructionHudAction::Roundness: {
        const int maximum = geo::fittedCornerRadiusSteps(tank->footprint, 64);
        std::vector<int> values{0};
        if (maximum > 0) {
            values.push_back(static_cast<int>(std::lround(maximum / 3.0)));
            values.push_back(static_cast<int>(std::lround(maximum * 2.0 / 3.0)));
            values.push_back(maximum);
            values.push_back(std::clamp(tank->corner_radius_steps, 0, maximum));
        }
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        return choicesForValues(layout.property_options, visual.focused_action,
            values, tank->corner_radius_steps);
    }
    case ConstructionHudAction::Rotate:
        return choicesForValues(layout.property_options, visual.focused_action,
            {0, 1, 2, 3}, tank->footprint.rotation_quarter_turns);
    case ConstructionHudAction::NotchWidth: {
        if (tank->footprint.shape == geo::FootprintShape::Rectangle) return {};
        const int maximum = std::max(1, tank->footprint.width_cells -
            (tank->footprint.shape == geo::FootprintShape::L ? 2 : 4));
        return choicesForValues(layout.property_options, visual.focused_action,
            stepperValues(1, maximum, tank->footprint.notch_width_cells),
            tank->footprint.notch_width_cells);
    }
    case ConstructionHudAction::NotchDepth: {
        if (tank->footprint.shape == geo::FootprintShape::Rectangle) return {};
        const int maximum = std::max(1, tank->footprint.depth_cells - 2);
        return choicesForValues(layout.property_options, visual.focused_action,
            stepperValues(1, maximum, tank->footprint.notch_depth_cells),
            tank->footprint.notch_depth_cells);
    }
    default:
        return {};
    }
}

ConstructionHudAction hitTestAquariumConstructionHud(
    const ConstructionHudLayout& layout,
    int screen_x, int screen_y, ConstructionState state, bool property_draft) {
    for (const auto action : aquariumConstructionHudActions(state, property_draft)) {
        const auto* rect = rectForAction(layout, action);
        if (rect && rect->contains(screen_x, screen_y)) return action;
    }
    return ConstructionHudAction::None;
}

ConstructionHudHit hitTestAquariumConstructionHud(
    const ConstructionHudLayout& layout,
    const AquariumConstructionVisual& visual,
    int screen_x, int screen_y) {
    for (const auto& choice : aquariumConstructionPropertyChoices(layout, visual)) {
        if (choice.enabled && choice.rect.contains(screen_x, screen_y)) {
            return {choice.action, choice.value};
        }
    }
    const auto action = hitTestAquariumConstructionHud(layout, screen_x, screen_y,
        visual.state, visual.property_draft);
    const auto tank = visual.preview_tank ? visual.preview_tank : visual.selected_tank;
    if (tank && tank->footprint.shape == geo::FootprintShape::Rectangle &&
        (action == ConstructionHudAction::NotchWidth ||
         action == ConstructionHudAction::NotchDepth)) {
        return {};
    }
    return {action, std::nullopt};
}

bool aquariumConstructionHudContainsUi(
    const ConstructionHudLayout& layout,
    int screen_x, int screen_y) {
    return layout.tool_panel.contains(screen_x, screen_y) ||
        layout.property_panel.contains(screen_x, screen_y) ||
        layout.status.contains(screen_x, screen_y);
}

ConstructionHudAction defaultAquariumConstructionHudAction(
    ConstructionState state, bool property_draft) {
    const auto actions = aquariumConstructionHudActions(state, property_draft);
    return actions.empty() ? ConstructionHudAction::None : actions.front();
}

} // namespace pr::gameplay::world3d::aquarium::construction
