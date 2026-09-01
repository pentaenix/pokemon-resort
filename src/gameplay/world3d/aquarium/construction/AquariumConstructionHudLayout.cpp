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
        case ConstructionHudAction::Subtract: return &layout.subtract;
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
    return action == ConstructionHudAction::Height ||
        action == ConstructionHudAction::Roundness;
}

bool containsWithPadding(
    const ConstructionHudRect& rect, int point_x, int point_y, int padding) {
    return rect.width > 0 && rect.height > 0 &&
        point_x >= rect.x - padding && point_y >= rect.y - padding &&
        point_x < rect.x + rect.width + padding &&
        point_y < rect.y + rect.height + padding;
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

std::vector<ConstructionHudChoice> stepperChoices(
    const ConstructionHudRect& area,
    ConstructionHudAction action,
    int minimum,
    int maximum,
    int current) {
    if (area.width <= 0 || area.height <= 0) return {};
    current = std::clamp(current, minimum, maximum);
    const int gap = std::clamp(area.height / 8, 4, 8);
    const int arrow_width = std::clamp(area.height, 42, 58);
    const int rail_width = std::max(48, area.width - arrow_width * 2 - gap * 2);
    return {
        {{area.x, area.y, arrow_width, area.height}, action,
            std::max(minimum, current - 1), false, current > minimum},
        {{area.x + arrow_width + gap, area.y, rail_width, area.height}, action,
            current, true, false},
        {{area.x + arrow_width + gap + rail_width + gap, area.y,
            arrow_width, area.height}, action,
            std::min(maximum, current + 1), false, current < maximum},
    };
}

} // namespace

bool ConstructionHudRect::contains(int point_x, int point_y) const {
    return width > 0 && height > 0 && point_x >= x && point_y >= y &&
        point_x < x + width && point_y < y + height;
}

bool aquariumConstructionPropertyPanelVisible(ConstructionState state, bool has_tank) {
    (void)state;
    (void)has_tank;
    return false;
}

std::vector<ConstructionHudAction> aquariumConstructionHudActions(
    ConstructionState state, bool property_draft) {
    (void)property_draft;
    switch (state) {
        case ConstructionState::Browse:
            return {ConstructionHudAction::Place, ConstructionHudAction::Subtract,
                ConstructionHudAction::Undo, ConstructionHudAction::Redo,
                ConstructionHudAction::Exit};
        case ConstructionState::Selected:
            return {ConstructionHudAction::Place, ConstructionHudAction::Subtract,
                ConstructionHudAction::Undo, ConstructionHudAction::Redo,
                ConstructionHudAction::Delete, ConstructionHudAction::Exit};
        case ConstructionState::ResizeFootprint:
        case ConstructionState::MoveTank:
        case ConstructionState::ResizeTank:
        case ConstructionState::SubtractFootprint:
        case ConstructionState::PaintFootprint:
        case ConstructionState::TunnelRoute:
            return {ConstructionHudAction::Build, ConstructionHudAction::Cancel};
        case ConstructionState::DraftReview:
            return {ConstructionHudAction::Build, ConstructionHudAction::Cancel};
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
    (void)property_draft;
    const int margin = std::clamp(std::min(width, height) / 36, 12, 22);
    const int icon = std::clamp(height / 11, 52, 68);
    const int gap = std::clamp(icon / 6, 8, 12);
    layout.safe_world = {0, 0, width, height};
    if (state == ConstructionState::Browse || state == ConstructionState::Selected) {
        layout.redo = {width - margin - icon, margin, icon, icon};
        layout.undo = {layout.redo.x - gap - icon, margin, icon, icon};
        layout.status = {margin, margin,
            std::max(180, layout.undo.x - gap - margin), 44};
    } else {
        layout.status = {margin, margin, std::max(180, width - margin * 2), 44};
    }
    const int bottom = height - margin - icon;
    if (state == ConstructionState::Browse || state == ConstructionState::Selected) {
        layout.place = {margin, bottom, icon, icon};
        layout.subtract = {margin + icon + gap, bottom, icon, icon};
    }
    if (state == ConstructionState::ResizeFootprint ||
        state == ConstructionState::MoveTank || state == ConstructionState::ResizeTank ||
        state == ConstructionState::SubtractFootprint ||
        state == ConstructionState::PaintFootprint ||
        state == ConstructionState::TunnelRoute || state == ConstructionState::DraftReview) {
        layout.build = {width - margin - icon, bottom, icon, icon};
        layout.cancel = {layout.build.x - gap - icon, bottom, icon, icon};
    } else {
        layout.exit = {width - margin - icon, bottom, icon, icon};
    }
    if (state == ConstructionState::Selected) {
        layout.remove = {width - margin - icon * 2 - gap, bottom, icon, icon};
    }
    return layout;
}

std::vector<ConstructionHudChoice> aquariumConstructionPropertyChoices(
    const ConstructionHudLayout& layout,
    const AquariumConstructionVisual& visual) {
    const auto tank = visual.preview_tank ? visual.preview_tank : visual.selected_tank;
    if (!tank || layout.property_options.width <= 0) return {};
    switch (visual.focused_action) {
    case ConstructionHudAction::Height:
        return stepperChoices(layout.property_options, visual.focused_action,
            4, 12, tank->height_steps);
    case ConstructionHudAction::Roundness: {
        const int maximum = geo::fittedCornerRadiusSteps(tank->footprint, 64);
        return stepperChoices(layout.property_options, visual.focused_action,
            0, maximum, tank->corner_radius_steps);
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
    // History buttons are small corner icons and need a forgiving mouse target.
    // Test them explicitly so edge clicks never fall through into the world.
    const auto actions = aquariumConstructionHudActions(state, property_draft);
    constexpr int kHistoryHitPadding = 7;
    if (std::find(actions.begin(), actions.end(), ConstructionHudAction::Undo) != actions.end() &&
        containsWithPadding(layout.undo, screen_x, screen_y, kHistoryHitPadding)) {
        return ConstructionHudAction::Undo;
    }
    if (std::find(actions.begin(), actions.end(), ConstructionHudAction::Redo) != actions.end() &&
        containsWithPadding(layout.redo, screen_x, screen_y, kHistoryHitPadding)) {
        return ConstructionHudAction::Redo;
    }
    for (const auto action : actions) {
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
    if (layout.property_options.contains(screen_x, screen_y) &&
        propertyAction(visual.focused_action)) {
        return {visual.focused_action, std::nullopt};
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
    return layout.status.contains(screen_x, screen_y) ||
        layout.place.contains(screen_x, screen_y) ||
        layout.subtract.contains(screen_x, screen_y) ||
        layout.undo.contains(screen_x, screen_y) ||
        layout.redo.contains(screen_x, screen_y) ||
        layout.build.contains(screen_x, screen_y) ||
        layout.cancel.contains(screen_x, screen_y) ||
        layout.remove.contains(screen_x, screen_y) ||
        layout.exit.contains(screen_x, screen_y);
}

ConstructionHudAction defaultAquariumConstructionHudAction(
    ConstructionState state, bool property_draft) {
    const auto actions = aquariumConstructionHudActions(state, property_draft);
    return actions.empty() ? ConstructionHudAction::None : actions.front();
}

} // namespace pr::gameplay::world3d::aquarium::construction
