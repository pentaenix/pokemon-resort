#pragma once

#include "core/assets/Font.hpp"
#include "core/Types.hpp"
#include <SDL.h>
#include <memory>
#include <string>
#include <vector>

namespace pr {

struct TextureHandle {
    std::shared_ptr<SDL_Texture> texture;
    int width = 0;
    int height = 0;
};

struct AlphaMask {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> alpha;
};

struct Assets {
    TextureHandle background_a;
    TextureHandle background_b;
    TextureHandle button_main;
    TextureHandle logo_splash;
    TextureHandle logo_main;
    TextureHandle press_start;
    std::vector<TextureHandle> menu_labels;
    FontHandle ui_font;
    AlphaMask logo_main_mask;
};

TextureHandle renderTextTexture(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, const Color& color);
Assets loadAssets(SDL_Renderer* renderer, const TitleScreenConfig& config, const std::string& project_root);

} // namespace pr
