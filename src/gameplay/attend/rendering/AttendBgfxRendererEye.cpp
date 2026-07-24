#include "AttendBgfxRendererInternal.hpp"

#include "gameplay/attend/rendering/AttendEyeSocketMask.hpp"

namespace pr::gameplay::attend::rendering {

AttendBgfxRenderer::Impl::TextureResource AttendBgfxRenderer::Impl::buildEyeSocketMaskTexture(
    const std::vector<std::uint8_t>& bytes,
    const AttendEyeSheet& sheet,
    const char* debug_name) {
    TextureResource out;
    if (bytes.empty() || !sheet.enabled) return out;

    SDL_RWops* rw = SDL_RWFromConstMem(bytes.data(), static_cast<int>(bytes.size()));
    if (!rw) return out;
    SDL_Surface* loaded = IMG_Load_RW(rw, 1);
    if (!loaded) return out;
    SDL_Surface* surface = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(loaded);
    if (!surface) return out;

    std::vector<std::uint8_t> source(static_cast<std::size_t>(surface->w * surface->h * 4));
    const auto* surface_pixels = static_cast<const std::uint8_t*>(surface->pixels);
    for (int y = 0; y < surface->h; ++y) {
        std::copy_n(
            surface_pixels + static_cast<std::size_t>(y * surface->pitch),
            static_cast<std::size_t>(surface->w * 4),
            source.data() + static_cast<std::size_t>(y * surface->w * 4));
    }
    const int width = surface->w;
    const int height = surface->h;
    SDL_FreeSurface(surface);

    std::vector<std::uint8_t> mask = buildAttendEyeSocketMaskRgba(
        source.data(),
        width,
        height,
        sheet.cols,
        sheet.rows);
    bool has_aperture = false;
    for (std::size_t alpha = 3; alpha < mask.size(); alpha += 4) {
        has_aperture = has_aperture || mask[alpha] != 0;
    }
    if (mask.empty() || !has_aperture) return out;

    out.width = width;
    out.height = height;
    out.has_zero_alpha = true;
    out.minimum_alpha = 0;
    const bgfx::Memory* memory = bgfx::copy(mask.data(), static_cast<std::uint32_t>(mask.size()));
    out.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(width),
        static_cast<std::uint16_t>(height),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        samplerFlags(),
        memory);
    if (out.valid()) bgfx::setName(out.handle, debug_name);
    return out;
}

} // namespace pr::gameplay::attend::rendering
