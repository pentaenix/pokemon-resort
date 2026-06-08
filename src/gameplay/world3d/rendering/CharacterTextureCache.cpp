#include "gameplay/world3d/rendering/CharacterTextureCache.hpp"

#include <SDL_image.h>

#include <cstring>

namespace pr::gameplay::world3d::rendering {

namespace {

RgbaImage surfaceToRgba(SDL_Surface* surface) {
    RgbaImage out{};
    if (!surface) {
        return out;
    }
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    if (!rgba) {
        return out;
    }
    out.width = rgba->w;
    out.height = rgba->h;
    out.pixels.resize(static_cast<std::size_t>(rgba->w * rgba->h * 4));
    SDL_LockSurface(rgba);
    std::memcpy(
        out.pixels.data(),
        rgba->pixels,
        static_cast<std::size_t>(rgba->w * rgba->h * 4));
    SDL_UnlockSurface(rgba);
    SDL_FreeSurface(rgba);
    return out;
}

SDL_Surface* loadPngSurface(const std::vector<std::uint8_t>& png_bytes) {
    if (png_bytes.empty()) {
        return nullptr;
    }
    SDL_RWops* rw = SDL_RWFromConstMem(png_bytes.data(), static_cast<int>(png_bytes.size()));
    if (!rw) {
        return nullptr;
    }
    SDL_Surface* surface = IMG_Load_RW(rw, 1);
    return surface;
}

} // namespace

RgbaImage decodePngToRgba(const std::vector<std::uint8_t>& png_bytes) {
    SDL_Surface* surface = loadPngSurface(png_bytes);
    if (!surface) {
        return {};
    }
    RgbaImage out = surfaceToRgba(surface);
    SDL_FreeSurface(surface);
    return out;
}

RgbaImage buildWhiteSilhouetteFromPngBytes(const std::vector<std::uint8_t>& png_bytes) {
    SDL_Surface* surface = loadPngSurface(png_bytes);
    if (!surface) {
        return {};
    }
    SDL_Surface* white = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(surface);
    if (!white) {
        return {};
    }
    SDL_LockSurface(white);
    auto* pixels = static_cast<Uint32*>(white->pixels);
    const std::size_t count =
        static_cast<std::size_t>(white->w) * static_cast<std::size_t>(white->h);
    for (std::size_t i = 0; i < count; ++i) {
        Uint8 r = 0;
        Uint8 g = 0;
        Uint8 b = 0;
        Uint8 a = 0;
        SDL_GetRGBA(pixels[i], white->format, &r, &g, &b, &a);
        pixels[i] = SDL_MapRGBA(white->format, 255, 255, 255, a);
    }
    SDL_UnlockSurface(white);
    RgbaImage out = surfaceToRgba(white);
    SDL_FreeSurface(white);
    return out;
}

} // namespace pr::gameplay::world3d::rendering
