#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

bool AttendBgfxRenderer::Impl::ensureOverlayButtonTexture(std::size_t index) {
    if (index >= overlay_buttons_.size()) return false;
    if (index >= overlay_button_textures_.size()) overlay_button_textures_.resize(index + 1);
    const AttendBgfxOverlayButton& button = overlay_buttons_[index];
    if (button.w <= 0 || button.h <= 0) return false;
    const AttendOverlayButtonConfig& style = button.style;
    OverlayButtonTexture& resource = overlay_button_textures_[index];
    const int w = std::max(1, button.w);
    const int h = std::max(1, button.h);
    const std::string key = overlayStyleKey(style, button.label, w, h);
    if (resource.texture.valid() && resource.key == key) {
        return true;
    }

    resource.destroy();
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4), 0);
    const int radius = std::clamp(style.corner_radius, 0, std::min(w, h) / 2);
    const int stroke_width = std::clamp(style.stroke_width, 0, std::min(w, h) / 2);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!insideRoundedRect(x, y, w, h, radius)) continue;
            const bool inner = stroke_width <= 0 ||
                insideRoundedRect(
                    x - stroke_width,
                    y - stroke_width,
                    w - stroke_width * 2,
                    h - stroke_width * 2,
                    std::max(0, radius - stroke_width));
            setRgba(pixels, w, x, y, inner ? style.fill : style.stroke);
        }
    }

    if (!button.label.empty()) {
        if (!TTF_WasInit() && TTF_Init() != 0) {
            std::cerr << "[AttendBgfxRenderer] TTF_Init failed for overlay text: " << TTF_GetError() << '\n';
        } else {
            const fs::path font_path = fs::path(project_root_) / "assets" / "fonts" / "Arial.ttf";
            TTF_Font* font = TTF_OpenFont(font_path.string().c_str(), std::max(8, style.font_size));
            if (!font) {
                std::cerr << "[AttendBgfxRenderer] Could not open overlay font: " << font_path << " | " << TTF_GetError() << '\n';
            } else {
                const SDL_Color text_color{
                    byteChannel(style.text.r),
                    byteChannel(style.text.g),
                    byteChannel(style.text.b),
                    byteChannel(style.text.a)};
                SDL_Surface* surface = TTF_RenderUTF8_Blended(font, button.label.c_str(), text_color);
                if (surface) {
                    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
                    SDL_FreeSurface(surface);
                    if (converted) {
                        const int text_x = std::clamp(style.padding_x, 0, std::max(0, w - converted->w));
                        const int text_y = std::max(0, (h - converted->h) / 2);
                        const auto* src_pixels = static_cast<const std::uint32_t*>(converted->pixels);
                        const int pitch_pixels = converted->pitch / 4;
                        for (int ty = 0; ty < converted->h && text_y + ty < h; ++ty) {
                            for (int tx = 0; tx < converted->w && text_x + tx < w; ++tx) {
                                Uint8 r = 0, g = 0, b = 0, a = 0;
                                SDL_GetRGBA(src_pixels[ty * pitch_pixels + tx], converted->format, &r, &g, &b, &a);
                                if (a == 0) continue;
                                const int dst_x = text_x + tx;
                                const int dst_y = text_y + ty;
                                const std::size_t offset = static_cast<std::size_t>((dst_y * w + dst_x) * 4);
                                const float src_a = static_cast<float>(a) / 255.0f;
                                const float dst_a = static_cast<float>(pixels[offset + 3]) / 255.0f;
                                const float out_a = src_a + dst_a * (1.0f - src_a);
                                if (out_a <= 0.0f) continue;
                                pixels[offset + 0] = static_cast<std::uint8_t>(
                                    (static_cast<float>(r) * src_a + static_cast<float>(pixels[offset + 0]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
                                pixels[offset + 1] = static_cast<std::uint8_t>(
                                    (static_cast<float>(g) * src_a + static_cast<float>(pixels[offset + 1]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
                                pixels[offset + 2] = static_cast<std::uint8_t>(
                                    (static_cast<float>(b) * src_a + static_cast<float>(pixels[offset + 2]) * dst_a * (1.0f - src_a)) / out_a + 0.5f);
                                pixels[offset + 3] = static_cast<std::uint8_t>(out_a * 255.0f + 0.5f);
                            }
                        }
                        SDL_FreeSurface(converted);
                    }
                }
                TTF_CloseFont(font);
            }
        }
    }

    const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    resource.texture.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(w),
        static_cast<std::uint16_t>(h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        0,
        mem);
    resource.texture.width = w;
    resource.texture.height = h;
    resource.key = key;
    return resource.texture.valid();
}

void AttendBgfxRenderer::Impl::submitPixelSceneToBackbuffer(
    int framebuffer_w,
    int framebuffer_h,
    int source_w,
    int source_h,
    std::uint8_t view_id) {
    if (!pixel_scene_target_.valid() || !bgfx::isValid(program_)) return;
    const bgfx::TextureHandle texture = bgfx::getTexture(pixel_scene_target_.frame_buffer, 0);
    if (!bgfx::isValid(texture)) return;

    framebuffer_w = std::max(1, framebuffer_w);
    framebuffer_h = std::max(1, framebuffer_h);
    source_w = std::max(1, source_w);
    source_h = std::max(1, source_h);
    const int integer_scale = std::max(1, std::min(framebuffer_w / source_w, framebuffer_h / source_h));
    const int dest_w = source_w * integer_scale;
    const int dest_h = source_h * integer_scale;
    const int dest_x = (framebuffer_w - dest_w) / 2;
    const int dest_y = (framebuffer_h - dest_h) / 2;

    float view[16];
    float proj[16];
    identity(view);
    bx::mtxOrtho(
        proj,
        0.0f,
        static_cast<float>(framebuffer_w),
        static_cast<float>(framebuffer_h),
        0.0f,
        0.0f,
        100.0f,
        0.0f,
        backend_.homogeneousDepth());
    bgfx::setViewTransform(view_id, view, proj);
    bgfx::setViewRect(
        view_id,
        0,
        0,
        static_cast<std::uint16_t>(framebuffer_w),
        static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewFrameBuffer(view_id, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(view_id, BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout_, 4, &tib, 6)) return;

    const float x0 = static_cast<float>(dest_x);
    const float y0 = static_cast<float>(dest_y);
    const float x1 = static_cast<float>(dest_x + dest_w);
    const float y1 = static_cast<float>(dest_y + dest_h);
    const float v_top = backend_.originBottomLeft() ? 1.0f : 0.0f;
    const float v_bottom = backend_.originBottomLeft() ? 0.0f : 1.0f;
    auto* verts = reinterpret_cast<Vertex*>(tvb.data);
    verts[0] = Vertex{x0, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_top};
    verts[1] = Vertex{x1, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_top};
    verts[2] = Vertex{x1, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_bottom};
    verts[3] = Vertex{x0, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_bottom};
    auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    std::copy(std::begin(indices), std::end(indices), idx);

    float model[16];
    identity(model);
    const float tint[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
    const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setTransform(model);
    bgfx::setVertexBuffer(0, &tvb);
    bgfx::setIndexBuffer(&tib);
    bgfx::setTexture(0, tex_uniform_, texture, samplerFlags());
    bgfx::setUniform(tint_cutoff_uniform_, tint);
    bgfx::setUniform(color_adjust_uniform_, adjust);
    bgfx::setUniform(texture_blur_uniform_, texture_blur);
    bgfx::setUniform(light_dir_uniform_, light_dir);
    bgfx::setUniform(light_params_uniform_, light_params);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(view_id, program_);
}

void AttendBgfxRenderer::Impl::submitOverlayButtons(
    int framebuffer_w,
    int framebuffer_h,
    bgfx::FrameBufferHandle target,
    std::uint8_t view_id) {
    if (overlay_buttons_.empty() || !bgfx::isValid(program_)) return;
    framebuffer_w = std::max(1, framebuffer_w);
    framebuffer_h = std::max(1, framebuffer_h);
    const SDL_Rect rect{
        std::clamp(presentation_rect_.x, 0, std::max(0, framebuffer_w - 1)),
        std::clamp(presentation_rect_.y, 0, std::max(0, framebuffer_h - 1)),
        std::clamp(presentation_rect_.w, 1, framebuffer_w),
        std::clamp(presentation_rect_.h, 1, framebuffer_h)};
    const int logical_w = std::max(1, overlay_logical_w_);
    const int logical_h = std::max(1, overlay_logical_h_);
    const float sx = static_cast<float>(rect.w) / static_cast<float>(logical_w);
    const float sy = static_cast<float>(rect.h) / static_cast<float>(logical_h);
    const float scale = std::max(0.05f, std::min(sx, sy));

    float view[16];
    float proj[16];
    identity(view);
    bx::mtxOrtho(
        proj,
        0.0f,
        static_cast<float>(framebuffer_w),
        static_cast<float>(framebuffer_h),
        0.0f,
        0.0f,
        100.0f,
        0.0f,
        backend_.homogeneousDepth());
    bgfx::setViewTransform(view_id, view, proj);
    bgfx::setViewRect(
        view_id,
        0,
        0,
        static_cast<std::uint16_t>(framebuffer_w),
        static_cast<std::uint16_t>(framebuffer_h));
    bgfx::setViewFrameBuffer(view_id, target);
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    for (std::size_t index = 0; index < overlay_buttons_.size(); ++index) {
        if (!ensureOverlayButtonTexture(index)) continue;
        const AttendBgfxOverlayButton& button = overlay_buttons_[index];
        const OverlayButtonTexture& resource = overlay_button_textures_[index];
        if (!resource.texture.valid()) continue;
        const float button_w = static_cast<float>(button.w) * scale;
        const float button_h = static_cast<float>(button.h) * scale;
        const bool right = button.x + button.w / 2 >= logical_w / 2;
        const bool bottom = button.y + button.h / 2 >= logical_h / 2;
        const float x0 = right
            ? static_cast<float>(rect.x + rect.w) -
                static_cast<float>(logical_w - (button.x + button.w)) * scale - button_w
            : static_cast<float>(rect.x) + static_cast<float>(button.x) * scale;
        const float y0 = bottom
            ? static_cast<float>(rect.y + rect.h) -
                static_cast<float>(logical_h - (button.y + button.h)) * scale - button_h
            : static_cast<float>(rect.y) + static_cast<float>(button.y) * scale;
        const float x1 = x0 + button_w;
        const float y1 = y0 + button_h;

        bgfx::TransientVertexBuffer tvb;
        bgfx::TransientIndexBuffer tib;
        if (!bgfx::allocTransientBuffers(&tvb, layout_, 4, &tib, 6)) return;

        const float v_top = backend_.originBottomLeft() ? 1.0f : 0.0f;
        const float v_bottom = backend_.originBottomLeft() ? 0.0f : 1.0f;
        auto* verts = reinterpret_cast<Vertex*>(tvb.data);
        verts[0] = Vertex{x0, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_top};
        verts[1] = Vertex{x1, y0, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_top};
        verts[2] = Vertex{x1, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 1.0f, v_bottom};
        verts[3] = Vertex{x0, y1, 0.0f, 0.0f, 1.0f, 0.0f, 0xffffffffu, 0.0f, v_bottom};
        auto* idx = reinterpret_cast<std::uint16_t*>(tib.data);
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        std::copy(std::begin(indices), std::end(indices), idx);

        float model[16];
        identity(model);
        const float tint[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        const float adjust[4] = {1.0f, 1.0f, 1.0f, 0.0f};
        const float texture_blur[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const float light_dir[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        const float light_params[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        bgfx::setTransform(model);
        bgfx::setVertexBuffer(0, &tvb);
        bgfx::setIndexBuffer(&tib);
        bgfx::setTexture(0, tex_uniform_, resource.texture.handle, samplerFlags(33071, 33071));
        bgfx::setUniform(tint_cutoff_uniform_, tint);
        bgfx::setUniform(color_adjust_uniform_, adjust);
        bgfx::setUniform(texture_blur_uniform_, texture_blur);
        bgfx::setUniform(light_dir_uniform_, light_dir);
        bgfx::setUniform(light_params_uniform_, light_params);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(view_id, program_);
    }
}

} // namespace pr::gameplay::attend::rendering
