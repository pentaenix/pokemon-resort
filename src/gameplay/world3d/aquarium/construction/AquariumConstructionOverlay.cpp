#include "gameplay/world3d/aquarium/construction/AquariumConstructionOverlay.hpp"

#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pr::gameplay::world3d::aquarium::construction {
namespace {

struct Rgba { Uint8 r, g, b, a; };

void color(SDL_Renderer* renderer, Rgba value) {
    SDL_SetRenderDrawColor(renderer, value.r, value.g, value.b, value.a);
}

void fillCircle(SDL_Renderer* renderer, const ConstructionHudRect& rect, Rgba value) {
    color(renderer, value);
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    const int radius = std::max(1, std::min(rect.width, rect.height) / 2);
    for (int y = -radius; y <= radius; ++y) {
        const int x = static_cast<int>(std::sqrt(
            static_cast<double>(radius * radius - y * y)));
        SDL_RenderDrawLine(renderer, cx - x, cy + y, cx + x, cy + y);
    }
}

ConstructionHudRect inset(ConstructionHudRect rect, int amount) {
    return {rect.x + amount, rect.y + amount,
        std::max(0, rect.width - amount * 2), std::max(0, rect.height - amount * 2)};
}

void drawButtonBase(
    SDL_Renderer* renderer, const ConstructionHudRect& rect,
    bool enabled, bool focused, Rgba accent = {54, 171, 224, 255}) {
    ConstructionHudRect shadow = rect;
    shadow.y += 5;
    fillCircle(renderer, shadow, {20, 35, 60, 150});
    fillCircle(renderer, rect,
        focused ? Rgba{255, 207, 67, 255} : Rgba{246, 242, 221, 255});
    fillCircle(renderer, inset(rect, 4), {34, 59, 91, 255});
    fillCircle(renderer, inset(rect, 9), enabled ? accent : Rgba{92, 107, 120, 235});
}

void drawArrow(SDL_Renderer* renderer, const ConstructionHudRect& rect, bool clockwise) {
    color(renderer, {255, 255, 246, 255});
    const int cx = rect.x + rect.width / 2;
    const int cy = rect.y + rect.height / 2;
    const float radius = static_cast<float>(rect.width) * 0.21f;
    const float start = clockwise ? -2.6f : -0.55f;
    const float direction = clockwise ? 1.0f : -1.0f;
    int previous_x = cx + static_cast<int>(std::cos(start) * radius);
    int previous_y = cy + static_cast<int>(std::sin(start) * radius);
    for (int step = 1; step <= 12; ++step) {
        const float angle = start + direction * static_cast<float>(step) * 0.23f;
        const int x = cx + static_cast<int>(std::cos(angle) * radius);
        const int y = cy + static_cast<int>(std::sin(angle) * radius);
        for (int thickness = -2; thickness <= 2; ++thickness) {
            SDL_RenderDrawLine(renderer, previous_x, previous_y + thickness, x, y + thickness);
        }
        previous_x = x;
        previous_y = y;
    }
    const int side = clockwise ? -1 : 1;
    SDL_RenderDrawLine(renderer, previous_x, previous_y, previous_x + side * 9, previous_y - 5);
    SDL_RenderDrawLine(renderer, previous_x, previous_y, previous_x + side * 7, previous_y + 7);
}

void drawCheck(SDL_Renderer* renderer, const ConstructionHudRect& rect) {
    color(renderer, {255, 255, 246, 255});
    const int x = rect.x + rect.width / 4;
    const int y = rect.y + rect.height / 2;
    for (int offset = -2; offset <= 2; ++offset) {
        SDL_RenderDrawLine(renderer, x, y + offset,
            x + rect.width / 6, y + rect.height / 7 + offset);
        SDL_RenderDrawLine(renderer, x + rect.width / 6, y + rect.height / 7 + offset,
            x + rect.width / 2, y - rect.height / 6 + offset);
    }
}

void drawTrash(SDL_Renderer* renderer, const ConstructionHudRect& rect) {
    color(renderer, {255, 255, 246, 255});
    SDL_Rect body{rect.x + rect.width * 34 / 100, rect.y + rect.height * 39 / 100,
        rect.width * 32 / 100, rect.height * 34 / 100};
    SDL_RenderFillRect(renderer, &body);
    SDL_Rect lid{rect.x + rect.width * 29 / 100, rect.y + rect.height * 31 / 100,
        rect.width * 42 / 100, 4};
    SDL_RenderFillRect(renderer, &lid);
    SDL_Rect grip{rect.x + rect.width * 43 / 100, rect.y + rect.height * 25 / 100,
        rect.width * 14 / 100, 5};
    SDL_RenderFillRect(renderer, &grip);
}

void drawPill(SDL_Renderer* renderer, SDL_Rect rect, Rgba fill) {
    const int radius = rect.h / 2;
    SDL_Rect middle{rect.x + radius, rect.y, std::max(0, rect.w - radius * 2), rect.h};
    color(renderer, fill);
    SDL_RenderFillRect(renderer, &middle);
    fillCircle(renderer, {rect.x, rect.y, rect.h, rect.h}, fill);
    fillCircle(renderer, {rect.x + rect.w - rect.h, rect.y, rect.h, rect.h}, fill);
}

std::string hintFor(const AquariumConstructionSession& session) {
    if (!session.validationMessage().empty()) {
        return aquariumConstructionHintForValidation(session.validationMessage());
    }
    switch (session.state()) {
    case ConstructionState::Browse:
        return "Draw a tank  •  Left/A add  •  Right/ZL+A subtract";
    case ConstructionState::Selected:
        return "Drag handles  •  RT + ↑↓ height  •  RT + ←→ corner";
    case ConstructionState::ResizeFootprint: return "Drag to draw  •  Release to build";
    case ConstructionState::PaintFootprint:
        return session.draftOperation() == ConstructionDraftOperation::Subtract
            ? "Paint cells away  •  Release to apply"
            : "Paint beside the tank to expand it";
    case ConstructionState::MoveTank: return "Drag the centre handle to move";
    case ConstructionState::ResizeTank: return "Drag an edge handle to resize";
    case ConstructionState::SubtractFootprint: return "Paint cells away";
    case ConstructionState::DraftReview:
    case ConstructionState::DeleteConfirm:
    case ConstructionState::Building: return "Building tank…";
    case ConstructionState::Dormant: return {};
    }
    return {};
}

} // namespace

void AquariumConstructionOverlay::configure(
    const AquariumConstructionConfig& config, std::string project_root) {
    (void)config;
    project_root_ = std::move(project_root);
    cached_hint_.clear();
    hint_texture_ = {};
    font_ = {};
}

void AquariumConstructionOverlay::render(
    SDL_Renderer* renderer, int width, int height,
    const AquariumConstructionSession& session,
    ConstructionHudAction focused_action) const {
    if (!renderer || !session.active()) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const bool property_draft = session.draftOperation() == ConstructionDraftOperation::Properties;
    const auto layout = aquariumConstructionHudLayout(width, height, session.state(), property_draft);
    if (layout.undo.width > 0) {
        drawButtonBase(renderer, layout.undo, session.canUndo(),
            focused_action == ConstructionHudAction::Undo);
        drawArrow(renderer, layout.undo, false);
    }
    if (layout.redo.width > 0) {
        drawButtonBase(renderer, layout.redo, session.canRedo(),
            focused_action == ConstructionHudAction::Redo);
        drawArrow(renderer, layout.redo, true);
    }
    if (layout.remove.width > 0) {
        drawButtonBase(renderer, layout.remove, true,
            focused_action == ConstructionHudAction::Delete, {225, 91, 87, 255});
        drawTrash(renderer, layout.remove);
    }
    const ConstructionHudRect finish = layout.build.width > 0 ? layout.build : layout.exit;
    if (finish.width > 0) {
        const ConstructionHudAction finish_action = layout.build.width > 0
            ? ConstructionHudAction::Build : ConstructionHudAction::Exit;
        drawButtonBase(renderer, finish, true, focused_action == finish_action,
            {66, 183, 126, 255});
        drawCheck(renderer, finish);
    }

    const std::string hint = hintFor(session);
    if (!font_) {
        font_ = loadFontPreferringUnicode(
            "assets/fonts/power clear bold.ttf", 20, project_root_);
    }
    if (font_ && hint != cached_hint_) {
        cached_hint_ = hint;
        hint_texture_ = renderTextTexture(renderer, font_.get(), hint, {255, 251, 230, 255});
    }
    if (hint_texture_.texture) {
        SDL_Rect pill{layout.status.x, layout.status.y,
            std::min(layout.status.width, hint_texture_.width + 34),
            std::max(layout.status.height, hint_texture_.height + 16)};
        SDL_Rect shadow = pill;
        shadow.y += 4;
        drawPill(renderer, shadow, {16, 29, 52, 135});
        drawPill(renderer, pill, {30, 54, 83, 232});
        SDL_Rect dst{pill.x + 17, pill.y + (pill.h - hint_texture_.height) / 2,
            hint_texture_.width, hint_texture_.height};
        SDL_RenderCopy(renderer, hint_texture_.texture.get(), nullptr, &dst);
    }
}

} // namespace pr::gameplay::world3d::aquarium::construction
