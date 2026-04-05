#include "core/Assets.hpp"
#include "core/Font.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace pr {

TextureHandle loadTexture(SDL_Renderer* renderer, const fs::path& path) {
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        throw std::runtime_error("Failed to load texture: " + path.string() + " | " + IMG_GetError());
    }

    TextureHandle out;
    out.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(out.texture.get(), nullptr, nullptr, &out.width, &out.height) != 0) {
        throw std::runtime_error("Failed to query texture: " + path.string() + " | " + SDL_GetError());
    }
    return out;
}

TextureHandle renderTextTexture(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, const Color& color) {
    SDL_Color sdl_color{
        static_cast<Uint8>(color.r),
        static_cast<Uint8>(color.g),
        static_cast<Uint8>(color.b),
        static_cast<Uint8>(color.a)
    };

    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), sdl_color);
    if (!surface) {
        throw std::runtime_error(std::string("Failed to render text: ") + TTF_GetError());
    }

    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface);
    if (!raw) {
        SDL_FreeSurface(surface);
        throw std::runtime_error(std::string("Failed to create text texture: ") + SDL_GetError());
    }

    TextureHandle out;
    out.texture.reset(raw, SDL_DestroyTexture);
    out.width = surface->w;
    out.height = surface->h;
    SDL_FreeSurface(surface);
    return out;
}

namespace {

fs::path resolvePath(const std::string& root, const std::string& configured) {
    fs::path p(configured);
    if (p.is_absolute()) {
        return p;
    }
    return fs::path(root) / p;
}

AlphaMask loadAlphaMask(const fs::path& path) {
    SDL_Surface* loaded = IMG_Load(path.string().c_str());
    if (!loaded) {
        throw std::runtime_error("Failed to load mask image: " + path.string() + " | " + IMG_GetError());
    }

    SDL_Surface* converted = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);

    if (!converted) {
        throw std::runtime_error("Failed to convert mask image: " + path.string() + " | " + SDL_GetError());
    }

    AlphaMask mask;
    mask.width = converted->w;
    mask.height = converted->h;
    mask.alpha.resize(static_cast<std::size_t>(mask.width) * static_cast<std::size_t>(mask.height));

    const auto* pixels = static_cast<const std::uint32_t*>(converted->pixels);
    const int pitch_pixels = converted->pitch / 4;

    for (int y = 0; y < mask.height; ++y) {
        for (int x = 0; x < mask.width; ++x) {
            const std::uint32_t pixel = pixels[y * pitch_pixels + x];
            Uint8 r = 0, g = 0, b = 0, a = 0;
            SDL_GetRGBA(pixel, converted->format, &r, &g, &b, &a);
            mask.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(mask.width) + static_cast<std::size_t>(x)] = a;
        }
    }

    SDL_FreeSurface(converted);
    return mask;
}

} // namespace

Assets loadAssets(SDL_Renderer* renderer, const TitleScreenConfig& config, const std::string& project_root) {
    Assets assets;
    assets.background_a = loadTexture(renderer, resolvePath(project_root, config.assets.background_a));
    assets.background_b = loadTexture(renderer, resolvePath(project_root, config.assets.background_b));
    assets.button_main = loadTexture(renderer, resolvePath(project_root, config.assets.button_main));
    assets.logo_splash = loadTexture(renderer, resolvePath(project_root, config.assets.logo_splash));
    assets.logo_main = loadTexture(renderer, resolvePath(project_root, config.assets.logo_main));

    const double font_scale =
        static_cast<double>(config.window.virtual_height) /
        static_cast<double>(config.window.design_height);

    const int scaled_font_size = std::max(
        1,
        static_cast<int>(std::round(config.prompt.font_pt_size * font_scale)));

    auto font = loadFont(config.assets.font, scaled_font_size, project_root);
    assets.press_start = renderTextTexture(renderer, font.get(), config.prompt.text, config.prompt.color);

    auto menu_font = loadFont(config.assets.font, config.menu.font_pt_size, project_root);
    assets.ui_font = menu_font;
    assets.menu_labels.resize(config.menu.items.size());
    for (std::size_t i = 0; i < config.menu.items.size(); ++i) {
        assets.menu_labels[i] = renderTextTexture(renderer, menu_font.get(), config.menu.items[i], config.menu.text_color);
    }

    fs::path mask_path;
    if (!config.assets.logo_main_mask.empty()) {
        mask_path = resolvePath(project_root, config.assets.logo_main_mask);
    } else {
        mask_path = resolvePath(project_root, config.assets.logo_main);
    }
    assets.logo_main_mask = loadAlphaMask(mask_path);

    if (assets.logo_main_mask.width != assets.logo_main.width ||
        assets.logo_main_mask.height != assets.logo_main.height) {
        throw std::runtime_error(
            "logo_main_mask dimensions must match logo_main dimensions. "
            "Mask: " + std::to_string(assets.logo_main_mask.width) + "x" + std::to_string(assets.logo_main_mask.height) +
            ", Logo: " + std::to_string(assets.logo_main.width) + "x" + std::to_string(assets.logo_main.height));
    }

    return assets;
}

} // namespace pr
