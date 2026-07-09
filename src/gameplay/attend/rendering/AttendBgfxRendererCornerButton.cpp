#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

namespace {

void blendCornerIcon(
    std::vector<std::uint8_t>& pixels,
    int w,
    int h,
    const AttendBgfxCornerButton& button,
    SDL_Surface* icon) {
    if (!icon) return;
    const AttendCornerButtonConfig& style = button.style;
    const int extension = std::clamp(w - h, 0, std::max(0, w - 1));
    const int max_w = std::max(1, static_cast<int>(std::round(static_cast<float>(w) * style.icon_scale)));
    const int max_h = std::max(1, static_cast<int>(std::round(static_cast<float>(h) * style.icon_scale)));
    const float aspect = icon->h > 0 ? static_cast<float>(icon->w) / static_cast<float>(icon->h) : 1.0f;
    int icon_w = max_w;
    int icon_h = std::max(1, static_cast<int>(std::round(static_cast<float>(icon_w) / std::max(0.01f, aspect))));
    if (icon_h > max_h) {
        icon_h = max_h;
        icon_w = std::max(1, static_cast<int>(std::round(static_cast<float>(icon_h) * aspect)));
    }
    const int icon_x = std::clamp(
        static_cast<int>(std::round(static_cast<float>(w) * (0.50f + style.icon_offset_x) - static_cast<float>(icon_w) * 0.5f)),
        0,
        std::max(0, w - icon_w));
    const int icon_y = std::clamp(
        static_cast<int>(std::round(static_cast<float>(h) * (0.50f + style.icon_offset_y) - static_cast<float>(icon_h) * 0.5f)),
        0,
        std::max(0, h - icon_h));
    const auto* src = static_cast<const std::uint32_t*>(icon->pixels);
    const int pitch = icon->pitch / 4;
    for (int ty = 0; ty < icon_h; ++ty) {
        const int sy = std::clamp(static_cast<int>((static_cast<float>(ty) + 0.5f) * icon->h / icon_h), 0, std::max(0, icon->h - 1));
        for (int tx = 0; tx < icon_w; ++tx) {
            const int dx = icon_x + tx;
            const int dy = icon_y + ty;
            if (!insideCornerButtonShape(dx, dy, w, h, button.left, button.top, extension)) continue;
            const int sx = std::clamp(static_cast<int>((static_cast<float>(tx) + 0.5f) * icon->w / icon_w), 0, std::max(0, icon->w - 1));
            Uint8 r = 0, g = 0, b = 0, a = 0;
            SDL_GetRGBA(src[sy * pitch + sx], icon->format, &r, &g, &b, &a);
            const std::uint8_t brightness = static_cast<std::uint8_t>(std::max({r, g, b}));
            if (a < 128 || brightness < 96) continue;
            blendRgba(pixels, w, dx, dy, 255, 255, 255, a);
        }
    }
}

} // namespace

bool AttendBgfxRenderer::Impl::ensureCornerButtonTexture(std::size_t index) {
    if (index >= corner_buttons_.size()) return false;
    if (index >= corner_button_textures_.size()) corner_button_textures_.resize(index + 1);
    const AttendBgfxCornerButton& button = corner_buttons_[index];
    if (button.w <= 0 || button.h <= 0) return false;
    const AttendCornerButtonConfig& style = button.style;
    OverlayButtonTexture& resource = corner_button_textures_[index];
    const int w = std::max(1, button.w);
    const int h = std::max(1, button.h);
    const std::string key = cornerButtonStyleKey(button);
    if (resource.texture.valid() && resource.key == key) return true;

    resource.destroy();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4), 0);
    const int extension = std::clamp(w - h, 0, std::max(0, w - 1));
    const float pixel_scale = static_cast<float>(std::max(1, button.h)) / 112.0f;
    const int outer_w = std::max(1, static_cast<int>(std::round(4.0f * pixel_scale)));
    const int border_w = outer_w + std::max(1, static_cast<int>(std::round(3.0f * pixel_scale)));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!insideCornerButtonShape(x, y, w, h, button.left, button.top, extension)) continue;
            const int edge = inwardBoundaryDistance(x, y, w, h, button.left, button.top, extension, border_w);
            const Color4 color = edge <= outer_w ? style.outer_border :
                (edge <= border_w ? style.inner_border : mixColor(style.fill_top, style.fill_bottom, h > 1 ? static_cast<float>(y) / (h - 1) : 0.0f));
            setRgba(pixels, w, x, y, color);
        }
    }

    fs::path icon_path(style.icon_path);
    if (!icon_path.is_absolute()) icon_path = fs::path(project_root_) / icon_path;
    SDL_Surface* loaded = IMG_Load(icon_path.string().c_str());
    SDL_Surface* icon = loaded ? SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
    if (!loaded) std::cerr << "[AttendBgfxRenderer] Could not load corner button icon: " << icon_path << " | " << IMG_GetError() << '\n';
    if (loaded) SDL_FreeSurface(loaded);
    blendCornerIcon(pixels, w, h, button, icon);
    if (icon) SDL_FreeSurface(icon);

    const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    resource.texture.handle = bgfx::createTexture2D(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), false, 1, bgfx::TextureFormat::RGBA8, 0, mem);
    resource.texture.width = w;
    resource.texture.height = h;
    resource.key = key;
    return resource.texture.valid();
}

void AttendBgfxRenderer::Impl::submitCornerButtons(
    int framebuffer_w,
    int framebuffer_h,
    bgfx::FrameBufferHandle target,
    std::uint8_t view_id) {
    if (corner_buttons_.empty() || !bgfx::isValid(program_)) return;
    framebuffer_w = std::max(1, framebuffer_w);
    framebuffer_h = std::max(1, framebuffer_h);
    const SDL_Rect rect{
        std::clamp(presentation_rect_.x, 0, std::max(0, framebuffer_w - 1)),
        std::clamp(presentation_rect_.y, 0, std::max(0, framebuffer_h - 1)),
        std::clamp(presentation_rect_.w, 1, framebuffer_w),
        std::clamp(presentation_rect_.h, 1, framebuffer_h)};
    const int logical_w = std::max(1, corner_logical_w_);
    const int logical_h = std::max(1, corner_logical_h_);
    const float scale = std::max(0.05f, std::min(static_cast<float>(rect.w) / logical_w, static_cast<float>(rect.h) / logical_h));

    float view[16];
    float proj[16];
    identity(view);
    bx::mtxOrtho(proj, 0.0f, static_cast<float>(framebuffer_w), static_cast<float>(framebuffer_h), 0.0f, 0.0f, 100.0f, 0.0f, backend_.homogeneousDepth());
    bgfx::setViewTransform(view_id, view, proj);
    bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(framebuffer_w), static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewFrameBuffer(view_id, target);
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    for (std::size_t index = 0; index < corner_buttons_.size(); ++index) {
        const AttendBgfxCornerButton& button = corner_buttons_[index];
        if (!ensureCornerButtonTexture(index)) continue;
        const OverlayButtonTexture& resource = corner_button_textures_[index];
        if (!resource.texture.valid()) continue;
        const float bw = static_cast<float>(button.w) * scale;
        const float bh = static_cast<float>(button.h) * scale;
        const float x0 = button.left ? static_cast<float>(rect.x) + button.x * scale
            : static_cast<float>(rect.x + rect.w) - static_cast<float>(logical_w - (button.x + button.w)) * scale - bw;
        const float y0 = button.top ? static_cast<float>(rect.y) + button.y * scale
            : static_cast<float>(rect.y + rect.h) - static_cast<float>(logical_h - (button.y + button.h)) * scale - bh;

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        if (!bgfx::allocTransientBuffers(&tvb, layout_, 4, &tib, 6)) return;
        const float v_top = backend_.originBottomLeft() ? 1.0f : 0.0f;
        const float v_bottom = backend_.originBottomLeft() ? 0.0f : 1.0f;
        auto* verts = reinterpret_cast<Vertex*>(tvb.data);
        verts[0] = Vertex{x0, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_top};
        verts[1] = Vertex{x0 + bw, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_top};
        verts[2] = Vertex{x0 + bw, y0 + bh, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_bottom};
        verts[3] = Vertex{x0, y0 + bh, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_bottom};
        auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        std::copy(std::begin(indices), std::end(indices), idx);

        float model[16];
        identity(model);
        const float tint[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float light[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        const float unlit[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setTexture(0, tex_uniform_, resource.texture.handle, samplerFlags(33071, 33071));
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, zero);
        bgfx::setUniform(light_dir_uniform_, light);
        bgfx::setUniform(light_params_uniform_, unlit);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(view_id, program_);
    }
}

} // namespace pr::gameplay::attend::rendering
