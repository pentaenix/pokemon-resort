#include "ui/transfer_system/detail/TextureLoad.hpp"

#include <SDL_image.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace pr::transfer_system::detail {

std::filesystem::path resolvePath(const std::string& root, const std::string& configured) {
    std::filesystem::path path(configured);
    return path.is_absolute() ? path : (std::filesystem::path(root) / path);
}

TextureHandle loadTexture(SDL_Renderer* renderer, const std::filesystem::path& path) {
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        throw std::runtime_error("Failed to load texture: " + path.string() + " | " + IMG_GetError());
    }

    TextureHandle texture;
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        throw std::runtime_error("Failed to query texture: " + path.string() + " | " + SDL_GetError());
    }
    return texture;
}

TextureHandle loadTextureOptional(SDL_Renderer* renderer, const std::filesystem::path& path) {
    TextureHandle texture;
    if (!std::filesystem::exists(path)) {
        return texture;
    }
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        std::cerr << "Warning: failed to load texture: " << path.string() << " | " << IMG_GetError() << '\n';
        return texture;
    }
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        texture.texture.reset();
        return texture;
    }
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    return texture;
}

void setTextureNearestNeighbor(TextureHandle& tex) {
    if (!tex.texture) {
        return;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(tex.texture.get(), SDL_ScaleModeNearest);
#else
    // Fallback: best effort; global hint is avoided to not affect UI.
#endif
}

} // namespace pr::transfer_system::detail

