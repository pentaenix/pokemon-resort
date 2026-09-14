#include "gameplay/world3d/aquarium/construction/AquariumStockingOverlay.hpp"

#include "gameplay/world3d/aquarium/AquariumExhibitPreset.hpp"
#include "gameplay/world3d/aquarium/AquariumSubstratePreset.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium::construction {
namespace {

constexpr int kPanelX = 40;
constexpr int kPanelY = 100;
constexpr int kInset = 16;
constexpr int kGap = 14;

Color colorFrom(const std::array<float, 4>& color, Uint8 alpha = 255) {
    return {
        static_cast<Uint8>(std::lround(std::clamp(color[0], 0.0f, 1.0f) * 255.0f)),
        static_cast<Uint8>(std::lround(std::clamp(color[1], 0.0f, 1.0f) * 255.0f)),
        static_cast<Uint8>(std::lround(std::clamp(color[2], 0.0f, 1.0f) * 255.0f)),
        alpha};
}

void fill(SDL_Renderer* renderer, const SDL_Rect& rect, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void fillRound(SDL_Renderer* renderer, SDL_Rect rect, int radius, Color color) {
    radius = std::clamp(radius, 0, std::min(rect.w, rect.h) / 2);
    for (int y = 0; y < rect.h; ++y) {
        const int dy = y < radius ? radius - y
            : y >= rect.h - radius ? y - (rect.h - radius - 1) : 0;
        const int inset = dy > 0
            ? radius - static_cast<int>(std::sqrt(
                std::max(0, radius * radius - dy * dy))) : 0;
        fill(renderer, {rect.x + inset, rect.y + y, rect.w - inset * 2, 1}, color);
    }
}

int panelWidth(int width) { return std::max(400, width - kPanelX * 2); }

SDL_Rect colorRect(int width, std::size_t index) {
    const int card_w = (panelWidth(width) - kInset * 2 - kGap * 3) / 4;
    return {kPanelX + kInset + static_cast<int>(index) * (card_w + kGap),
        kPanelY + kInset, card_w, 94};
}

SDL_Rect sliderRow(int width, bool murkiness) {
    return {kPanelX + kInset, kPanelY + 132 + (murkiness ? 78 : 0),
        panelWidth(width) - kInset * 2, 62};
}

SDL_Rect sliderTrack(int width, bool murkiness) {
    const SDL_Rect row = sliderRow(width, murkiness);
    return {row.x + 210, row.y + 18, row.w - 238, 26};
}

SDL_Rect substrateRect(int width, std::size_t index) {
    const int card_w = (panelWidth(width) - kInset * 2 - kGap * 3) / 4;
    return {kPanelX + kInset + static_cast<int>(index) * (card_w + kGap),
        kPanelY + 304, card_w, 142};
}

std::optional<int> sliderLevelAt(
    int width, int point_x, int point_y, bool murkiness, int level_count) {
    const SDL_Rect row = sliderRow(width, murkiness);
    const SDL_Point point{point_x, point_y};
    const SDL_Rect track = sliderTrack(width, murkiness);
    const SDL_Rect hit{track.x - 18, row.y, track.w + 36, row.h};
    if (!SDL_PointInRect(&point, &hit)) return std::nullopt;
    const int x = std::clamp(point_x, track.x, track.x + track.w);
    return std::clamp(static_cast<int>(std::lround(
        static_cast<double>(x - track.x) * (level_count - 1) /
        std::max(1, track.w))), 0, level_count - 1);
}

void drawFocusRing(SDL_Renderer* renderer, SDL_Rect rect, Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int inset = 0; inset < 4; ++inset) {
        const SDL_Rect ring{rect.x - inset, rect.y - inset,
            rect.w + inset * 2, rect.h + inset * 2};
        SDL_RenderDrawRect(renderer, &ring);
    }
}

void drawSlider(
    SDL_Renderer* renderer,
    int width,
    bool murkiness,
    int level,
    int level_count,
    bool focused,
    TTF_Font* font) {
    const SDL_Rect row = sliderRow(width, murkiness);
    fillRound(renderer, row, 16, {226, 247, 252, 245});
    if (focused) drawFocusRing(renderer, row, {255, 214, 72, 255});

    const TextureHandle label = renderTextTexture(renderer, font,
        murkiness ? "MURKINESS" : "BRIGHTNESS", {18, 74, 119, 255});
    if (label.texture) {
        const SDL_Rect dst{row.x + 22, row.y + (row.h - label.height) / 2,
            label.width, label.height};
        SDL_RenderCopy(renderer, label.texture.get(), nullptr, &dst);
    }

    const SDL_Rect track = sliderTrack(width, murkiness);
    fillRound(renderer, {track.x, track.y + 8, track.w, 10}, 5,
        {44, 116, 155, 255});
    for (int tick = 0; tick < level_count; ++tick) {
        const int x = track.x + tick * track.w / (level_count - 1);
        fillRound(renderer, {x - 4, track.y + 9, 8, 8}, 4,
            {235, 251, 255, 255});
    }
    const int knob_x = track.x + level * track.w / (level_count - 1);
    fillRound(renderer, {knob_x - 15, track.y - 2, 30, 30}, 15,
        {255, 214, 72, 255});
    fillRound(renderer, {knob_x - 8, track.y + 5, 16, 16}, 8,
        murkiness ? Color{43, 87, 107, 255} : Color{255, 248, 188, 255});
}

} // namespace

SDL_Rect AquariumStockingOverlay::exhibitPresetRect(
    int width, int, std::size_t index) const {
    return colorRect(width, index);
}

std::optional<int> AquariumStockingOverlay::exhibitBrightnessAt(
    int width, int, int point_x, int point_y) const {
    return sliderLevelAt(width, point_x, point_y, false, kAquariumBrightnessLevelCount);
}

std::optional<int> AquariumStockingOverlay::exhibitMurkinessAt(
    int width, int, int point_x, int point_y) const {
    return sliderLevelAt(
        width, point_x, point_y, true, kAquariumMurkinessControlLevelCount);
}

int AquariumStockingOverlay::exhibitBrightnessLevelAtX(int width, int point_x) const {
    const SDL_Rect track = sliderTrack(width, false);
    const int x = std::clamp(point_x, track.x, track.x + track.w);
    return std::clamp(static_cast<int>(std::lround(
        static_cast<double>(x - track.x) * (kAquariumBrightnessLevelCount - 1) /
        std::max(1, track.w))), 0, kAquariumBrightnessLevelCount - 1);
}

int AquariumStockingOverlay::exhibitMurkinessLevelAtX(int width, int point_x) const {
    const SDL_Rect track = sliderTrack(width, true);
    const int x = std::clamp(point_x, track.x, track.x + track.w);
    return std::clamp(static_cast<int>(std::lround(
        static_cast<double>(x - track.x) * (kAquariumMurkinessControlLevelCount - 1) /
        std::max(1, track.w))), 0, kAquariumMurkinessControlLevelCount - 1);
}

std::optional<std::size_t> AquariumStockingOverlay::exhibitSubstrateAt(
    int width, int, int point_x, int point_y) const {
    const SDL_Point point{point_x, point_y};
    for (std::size_t index = 0; index < kAquariumSubstratePresets.size(); ++index) {
        const SDL_Rect rect = substrateRect(width, index);
        if (SDL_PointInRect(&point, &rect)) return index;
    }
    return std::nullopt;
}

void AquariumStockingOverlay::renderExhibit(
    SDL_Renderer* renderer,
    int width,
    int,
    const AquariumStockingController& controller) const {
    for (std::size_t index = 0; index < kAquariumExhibitPresets.size(); ++index) {
        const AquariumExhibitPreset& preset = kAquariumExhibitPresets[index];
        const SDL_Rect card = colorRect(width, index);
        const bool focused = controller.focusedExhibitControl() ==
                AquariumStockingController::ExhibitControl::Color &&
            controller.focusedExhibitPresetIndex() == index;
        const bool active = controller.currentExhibitPresetId() == preset.id;
        fillRound(renderer, card, 15, active ? Color{255, 214, 72, 255}
                                             : Color{34, 112, 162, 255});
        const SDL_Rect swatch{card.x + 6, card.y + 6, card.w - 12, card.h - 12};
        fillRound(renderer, swatch, 11, colorFrom(preset.water_surface));
        fillRound(renderer, {swatch.x + 8, swatch.y + swatch.h - 22,
            swatch.w - 16, 12}, 6, colorFrom(preset.water_volume));
        const TextureHandle label = renderTextTexture(renderer, summary_small_font_.get(),
            std::string(preset.display_name), {247, 253, 255, 255});
        if (label.texture) {
            fillRound(renderer, {swatch.x + 8, swatch.y + 8,
                std::min(swatch.w - 16, label.width + 18), label.height + 8},
                8, {9, 43, 68, 210});
            const SDL_Rect dst{swatch.x + 17, swatch.y + 12, label.width, label.height};
            SDL_RenderCopy(renderer, label.texture.get(), nullptr, &dst);
        }
        if (focused) drawFocusRing(renderer, card, carousel_style_.frame_basic);
    }

    drawSlider(renderer, width, false, controller.focusedBrightnessLevel(),
        kAquariumBrightnessLevelCount,
        controller.focusedExhibitControl() ==
            AquariumStockingController::ExhibitControl::Brightness,
        summary_font_.get());
    drawSlider(renderer, width, true, controller.focusedMurkinessLevel(),
        kAquariumMurkinessControlLevelCount,
        controller.focusedExhibitControl() ==
            AquariumStockingController::ExhibitControl::Murkiness,
        summary_font_.get());

    for (std::size_t index = 0; index < kAquariumSubstratePresets.size(); ++index) {
        const auto& preset = kAquariumSubstratePresets[index];
        const SDL_Rect card = substrateRect(width, index);
        const bool focused = controller.focusedExhibitControl() ==
                AquariumStockingController::ExhibitControl::Substrate &&
            controller.focusedSubstrateIndex() == index;
        const bool selected = controller.focusedSubstrateIndex() == index;
        fillRound(renderer, card, 15, selected ? Color{255, 214, 72, 255}
                                               : Color{34, 112, 162, 255});
        const SDL_Rect sample{card.x + 6, card.y + 6, card.w - 12, card.h - 42};
        fillRound(renderer, sample, 11,
            {preset.swatch_base[0], preset.swatch_base[1], preset.swatch_base[2], 255});
        for (int y = sample.y + 8; y < sample.y + sample.h - 5; y += 18) {
            const int phase = ((y - sample.y) / 18) % 2 == 0 ? 0 : 10;
            for (int x = sample.x + 8 + phase; x < sample.x + sample.w - 5; x += 24) {
                fillRound(renderer, {x, y, 11, 7}, 3,
                    {preset.swatch_detail[0], preset.swatch_detail[1],
                     preset.swatch_detail[2], 210});
            }
        }
        const TextureHandle label = renderTextTexture(renderer, summary_small_font_.get(),
            std::string(preset.display_name), {245, 252, 255, 255});
        if (label.texture) {
            const SDL_Rect dst{card.x + (card.w - label.width) / 2,
                card.y + card.h - 32, label.width, label.height};
            SDL_RenderCopy(renderer, label.texture.get(), nullptr, &dst);
        }
        if (focused) drawFocusRing(renderer, card, carousel_style_.frame_basic);
    }
}

} // namespace pr::gameplay::world3d::aquarium::construction
