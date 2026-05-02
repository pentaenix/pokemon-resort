#pragma once

#include "core/Assets.hpp"
#include "ui/loading/ResortTransferLoadingConfig.hpp"

#include <SDL.h>

namespace pr {

struct ResortTransferLoadingTextures {
    TextureHandle boat;
    TextureHandle cloud1;
    TextureHandle cloud2;
    TextureHandle message;
};

struct ResortTransferLoadingFrame {
    bool idle = true;
    int viewport_w = 1280;
    int viewport_h = 800;
    double loop_time = 0.0;
    double water_sun = 0.0;
    double boat_enter = 0.0;
    double boat_exit = 0.0;
    double clouds_enter = 0.0;
    double clouds_exit = 0.0;
    double cloud_drift = 0.0;
    double foam_phase = 0.0;
    double speed_crest_phase = 0.0;
    double boat_velocity_x = 0.0;
    double message_alpha = 0.0;
    double message_y_offset = 0.0;
    bool use_boat_center_x = false;
    double boat_center_x = 0.0;
};

class ResortTransferLoadingRenderer {
public:
    ResortTransferLoadingRenderer(
        ResortTransferLoadingConfig config,
        ResortTransferLoadingTextures textures);

    void render(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const;

private:
    struct BoatPlacement {
        double center_x = 0.0;
        double bottom_y = 0.0;
        double width = 0.0;
        double height = 0.0;
    };

    void drawBorder(SDL_Renderer* renderer, int w, int h) const;
    void drawSun(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const;
    void drawClouds(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const;
    void drawMessage(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame) const;
    void drawWave(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame, int index) const;
    void drawBoat(SDL_Renderer* renderer, const BoatPlacement& boat) const;
    void drawFoam(SDL_Renderer* renderer, const ResortTransferLoadingFrame& frame, const BoatPlacement& boat) const;
    BoatPlacement boatPlacement(const ResortTransferLoadingFrame& frame) const;

    ResortTransferLoadingConfig config_;
    ResortTransferLoadingTextures textures_;
};

} // namespace pr
