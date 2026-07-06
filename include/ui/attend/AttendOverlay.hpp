#pragma once

#include "gameplay/attend/AttendSceneConfig.hpp"
#include "ui/overlay/OverlayCanvas.hpp"

#include <SDL.h>
#include <string>

namespace pr {

class AttendOverlay {
public:
    AttendOverlay(int logical_w, int logical_h);

    void setLogicalSize(int logical_w, int logical_h);
    void setConfig(const gameplay::attend::AttendUiConfig& config);
    void render(
        SDL_Renderer* renderer,
        const std::string& project_root,
        const std::string& weather_label,
        const std::string& view_label,
        const std::string& pokemon_label,
        const std::string& texture_variant_label,
        const std::string& form_variant_label);
    std::string weatherButtonLabel(const std::string& weather_label) const;
    std::string viewButtonLabel(const std::string& view_label) const;
    std::string pokemonButtonLabel(const std::string& pokemon_label) const;
    std::string textureVariantButtonLabel(const std::string& texture_variant_label) const;
    std::string formVariantButtonLabel(const std::string& form_variant_label) const;
    SDL_Rect weatherButtonRect() const;
    SDL_Rect viewButtonRect() const;
    SDL_Rect pokemonButtonRect() const;
    SDL_Rect textureVariantButtonRect() const;
    SDL_Rect formVariantButtonRect() const;
    bool hitWeatherButton(int logical_x, int logical_y) const;
    bool hitViewButton(int logical_x, int logical_y) const;
    bool hitPokemonButton(int logical_x, int logical_y) const;
    bool hitTextureVariantButton(int logical_x, int logical_y) const;
    bool hitFormVariantButton(int logical_x, int logical_y) const;
    void invalidate();

private:
    OverlayButton weatherButton(const std::string& weather_label) const;
    OverlayButton viewButton(const std::string& view_label) const;
    OverlayButton pokemonButton(const std::string& pokemon_label) const;
    OverlayButton textureVariantButton(const std::string& texture_variant_label) const;
    OverlayButton formVariantButton(const std::string& form_variant_label) const;

    OverlayCanvas canvas_;
    gameplay::attend::AttendUiConfig config_{};
};

} // namespace pr
