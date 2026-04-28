#pragma once

#include "core/Assets.hpp"

#include <SDL.h>

#include <filesystem>
#include <string>

namespace pr::transfer_system::detail {

std::filesystem::path resolvePath(const std::string& root, const std::string& configured);

TextureHandle loadTexture(SDL_Renderer* renderer, const std::filesystem::path& path);

TextureHandle loadTextureOptional(SDL_Renderer* renderer, const std::filesystem::path& path);

void setTextureNearestNeighbor(TextureHandle& tex);

} // namespace pr::transfer_system::detail

