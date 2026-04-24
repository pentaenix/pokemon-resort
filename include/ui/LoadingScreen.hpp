#pragma once

#include "core/Assets.hpp"
#include "core/Font.hpp"
#include "core/Types.hpp"
#include "ui/Screen.hpp"

#include <SDL.h>
#include <random>
#include <string>
#include <vector>

namespace pr {

class LoadingScreen : public Screen {
public:
    LoadingScreen(
        SDL_Renderer* renderer,
        const WindowConfig& window_config,
        const std::string& fallback_font_path,
        const std::string& project_root);

    void enter();
    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;

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
};

} // namespace pr
