#pragma once

#include "core/assets/Assets.hpp"
#include "core/assets/Font.hpp"
#include "core/Types.hpp"
#include "ui/loading/LoadingScreenBase.hpp"

#include <SDL.h>
#include <random>
#include <string>
#include <vector>

namespace pr {

class PokeballLoadingScreen : public LoadingScreenBase {
public:
    PokeballLoadingScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& fallback_font_path,
        const std::string& project_root);

    LoadingScreenType loadingScreenType() const override { return LoadingScreenType::Pokeball; }
    void setMinimumLoopSeconds(double minimum_loop_seconds) override;
    void enter() override;
    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;
    void markLoadingComplete() override;
    bool isLoadingAnimationComplete() const override;

private:
    struct LoadingConfig {
        std::string balls_directory = "assets/loading/balls";
        std::string text = "";
        Point text_center{640, 521};
        int font_size = 20;
        Color text_color{255, 255, 255, 255};
        Point ball_center{1248, 768};
        double ball_scale = 1.0;
        double lap_seconds = 0.95;
        double partial_spin_degrees = -24.0;
        double full_spin_degrees = 360.0;
        double partial_spin_fraction = 0.28;
    };

    void loadConfig();
    void loadBallTextures(SDL_Renderer* renderer);
    void chooseNextBall();

    double scaleX() const;
    double scaleY() const;
    int sx(int value) const;
    int sy(int value) const;

    WindowConfig window_config_;
    std::string fallback_font_path_;
    std::string project_root_;
    LoadingConfig config_;
    FontHandle font_;
    TextureHandle text_texture_;
    std::vector<TextureHandle> ball_textures_;
    std::mt19937 rng_;
    int current_ball_index_ = -1;
    double lap_elapsed_seconds_ = 0.0;
    double display_elapsed_seconds_ = 0.0;
    double minimum_loop_seconds_ = 0.0;
    bool loading_complete_requested_ = false;
    bool loading_animation_complete_ = false;
};

} // namespace pr
