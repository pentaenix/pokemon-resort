#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include <algorithm>

namespace pr::gameplay::world3d::aquarium::construction {
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

} // namespace

bool ConstructionHudRect::contains(int point_x, int point_y) const {
    return width > 0 && height > 0 && point_x >= x && point_y >= y &&
        point_x < x + width && point_y < y + height;
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
    const int size = std::clamp(std::min(viewport_width, viewport_height) / 10, 48, 76);
    const int gap = std::max(10, size / 5);
    const int bottom = std::max(14, viewport_height / 28);
    const auto actions = aquariumConstructionHudActions(state, property_draft);
    if (!actions.empty()) {
        const int maximum_columns = std::max(1, (viewport_width - gap) / (size + gap));
        for (std::size_t index = 0; index < actions.size(); ++index) {
            const int row = static_cast<int>(index) / maximum_columns;
            const int row_begin = row * maximum_columns;
            const int row_count = std::min(maximum_columns,
                static_cast<int>(actions.size()) - row_begin);
            const int column = static_cast<int>(index) - row_begin;
            const int total = size * row_count + gap * (row_count - 1);
            const int start = (viewport_width - total) / 2;
            if (auto* rect = rectForAction(layout, actions[index])) {
                *rect = {start + column * (size + gap),
                    viewport_height - bottom - size - row * (size + gap), size, size};
            }
        }
    }
    return layout;
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

ConstructionHudAction defaultAquariumConstructionHudAction(
    ConstructionState state, bool property_draft) {
    const auto actions = aquariumConstructionHudActions(state, property_draft);
    return actions.empty() ? ConstructionHudAction::None : actions.front();
}

} // namespace pr::gameplay::world3d::aquarium::construction
