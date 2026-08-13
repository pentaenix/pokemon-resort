#include "mapmaker/assets/BgfxThumbnailCache.hpp"

#include <SDL_image.h>
#include <bgfx/bgfx.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace pr::mapmaker {

BgfxThumbnailCache::BgfxThumbnailCache(const RtpksEditorCatalog& catalog)
    : catalog_(catalog) {}

BgfxThumbnailCache::~BgfxThumbnailCache() { clear(); }

void BgfxThumbnailCache::beginFrame(std::size_t upload_budget) {
    upload_budget_ = upload_budget;
    uploads_this_frame_ = 0;
}

std::uint16_t BgfxThumbnailCache::textureForTile(int resort_tile_id) {
    const auto cached = textures_.find(resort_tile_id);
    if (cached != textures_.end()) return cached->second;
    if (failed_tiles_.contains(resort_tile_id)) return UINT16_MAX;
    if (uploads_this_frame_ >= upload_budget_) return UINT16_MAX;
    ++uploads_this_frame_;

    const std::vector<std::uint8_t> png = catalog_.previewPng(resort_tile_id);
    if (png.empty() || png.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        failed_tiles_.insert(resort_tile_id);
        return UINT16_MAX;
    }
    SDL_RWops* input = SDL_RWFromConstMem(png.data(), static_cast<int>(png.size()));
    SDL_Surface* loaded = input ? IMG_Load_RW(input, 1) : nullptr;
    if (!loaded) {
        failed_tiles_.insert(resort_tile_id);
        return UINT16_MAX;
    }
    SDL_Surface* rgba = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!rgba || rgba->w <= 0 || rgba->h <= 0 ||
        rgba->w > std::numeric_limits<std::uint16_t>::max() ||
        rgba->h > std::numeric_limits<std::uint16_t>::max()) {
        if (rgba) SDL_FreeSurface(rgba);
        failed_tiles_.insert(resort_tile_id);
        return UINT16_MAX;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(rgba->w) * 4U;
    std::vector<std::uint8_t> packed(row_bytes * static_cast<std::size_t>(rgba->h));
    for (int y = 0; y < rgba->h; ++y) {
        std::memcpy(
            packed.data() + static_cast<std::size_t>(y) * row_bytes,
            static_cast<const std::uint8_t*>(rgba->pixels) +
                static_cast<std::size_t>(y) * static_cast<std::size_t>(rgba->pitch),
            row_bytes);
    }
    const auto width = static_cast<std::uint16_t>(rgba->w);
    const auto height = static_cast<std::uint16_t>(rgba->h);
    SDL_FreeSurface(rgba);
    if (packed.size() > std::numeric_limits<std::uint32_t>::max()) {
        failed_tiles_.insert(resort_tile_id);
        return UINT16_MAX;
    }

    const bgfx::TextureHandle texture = bgfx::createTexture2D(
        width,
        height,
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT,
        bgfx::copy(packed.data(), static_cast<std::uint32_t>(packed.size())));
    if (!bgfx::isValid(texture)) {
        failed_tiles_.insert(resort_tile_id);
        return UINT16_MAX;
    }
    bgfx::setName(texture, ("map-maker-tile-" + std::to_string(resort_tile_id)).c_str());
    textures_[resort_tile_id] = texture.idx;
    return texture.idx;
}

void BgfxThumbnailCache::clear() {
    for (const auto& [tile_id, handle_index] : textures_) {
        (void)tile_id;
        const bgfx::TextureHandle handle{handle_index};
        if (bgfx::isValid(handle)) bgfx::destroy(handle);
    }
    textures_.clear();
    failed_tiles_.clear();
    uploads_this_frame_ = 0;
}

} // namespace pr::mapmaker
