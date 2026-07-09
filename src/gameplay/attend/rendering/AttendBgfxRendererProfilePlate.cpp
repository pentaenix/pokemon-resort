#include "AttendBgfxRendererInternal.hpp"

namespace pr::gameplay::attend::rendering {

namespace {

std::string profilePlateStyleKey(const AttendBgfxProfilePlate& plate, bool text_only, bool include_text) {
    const AttendProfilePlateConfig& s = plate.style;
    std::ostringstream out;
    out << (text_only ? "text" : "plate") << '|' << include_text << '|'
        << plate.enabled << '|' << plate.w << 'x' << plate.h << '|'
        << plate.name << '|' << plate.characteristic << '|' << plate.nature << '|'
        << plate.sprite_path << '|' << s.font_path << '|'
        << s.padding_x << ',' << s.padding_y << ',' << s.corner_radius << '|'
        << s.outer_stroke_width << ',' << s.inner_stroke_width << '|' << s.show_sprite << '|'
        << s.sprite_size << ',' << s.sprite_scale << ',' << s.sprite_gap << '|'
        << s.sprite_offset_x << ',' << s.sprite_offset_y << '|'
        << s.name_text_offset_x << ',' << s.name_text_offset_y << '|'
        << s.detail_text_offset_x << ',' << s.detail_text_offset_y << '|'
        << s.name_font_size << ',' << s.detail_font_size << '|'
        << s.name_row_height << ',' << s.detail_row_height << '|'
        << s.name_row_width << ',' << s.characteristic_row_width << ',' << s.nature_row_width << '|'
        << s.row_gap << '|'
        << s.left_fade_width << '|'
        << s.outer_border.r << ',' << s.outer_border.g << ',' << s.outer_border.b << ',' << s.outer_border.a << '|'
        << s.inner_border.r << ',' << s.inner_border.g << ',' << s.inner_border.b << ',' << s.inner_border.a << '|'
        << s.fill_top.r << ',' << s.fill_top.g << ',' << s.fill_top.b << ',' << s.fill_top.a << '|'
        << s.fill_bottom.r << ',' << s.fill_bottom.g << ',' << s.fill_bottom.b << ',' << s.fill_bottom.a << '|'
        << s.divider.r << ',' << s.divider.g << ',' << s.divider.b << ',' << s.divider.a << '|'
        << s.name_text.r << ',' << s.name_text.g << ',' << s.name_text.b << ',' << s.name_text.a << '|'
        << s.detail_text.r << ',' << s.detail_text.g << ',' << s.detail_text.b << ',' << s.detail_text.a;
    return out.str();
}

float leftFadeMultiplier(int x, int fade_width) {
    if (fade_width <= 0 || x >= fade_width) return 1.0f;
    const float t = std::clamp(static_cast<float>(x) / static_cast<float>(std::max(1, fade_width)), 0.0f, 1.0f);
    return t * t;
}

void setRgbaFaded(std::vector<std::uint8_t>& pixels, int w, int x, int y, Color4 color, float fade_multiplier) {
    color.a *= std::clamp(fade_multiplier, 0.0f, 1.0f);
    setRgba(pixels, w, x, y, color);
}

bool insideRightAttachedRow(int x, int y, int w, int h, int radius) {
    if (w <= 0 || h <= 0 || x < 0 || x >= w || y < 0 || y >= h) return false;
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    if (radius <= 0 || x >= radius) return true;
    const int cy = y < radius ? radius : h - radius - 1;
    if (y >= radius && y <= h - radius - 1) return true;
    const int dx = x - radius;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

TTF_Font* openFitFont(
    const fs::path& font_path,
    int start_size,
    int min_size,
    const std::string& text,
    int max_width,
    int style = TTF_STYLE_NORMAL) {
    for (int size = std::max(min_size, start_size); size >= min_size; --size) {
        TTF_Font* font = TTF_OpenFont(font_path.string().c_str(), size);
        if (!font) continue;
        TTF_SetFontStyle(font, style);
        int text_w = 0;
        int text_h = 0;
        if (TTF_SizeUTF8(font, text.c_str(), &text_w, &text_h) == 0 && text_w <= max_width) {
            return font;
        }
        TTF_CloseFont(font);
    }
    TTF_Font* font = TTF_OpenFont(font_path.string().c_str(), min_size);
    if (font) TTF_SetFontStyle(font, style);
    return font;
}

SDL_Surface* renderTextSurface(TTF_Font* font, const std::string& text, const Color4& color, bool solid) {
    if (!font || text.empty()) return nullptr;
    const SDL_Color sdl_color{
        byteChannel(color.r),
        byteChannel(color.g),
        byteChannel(color.b),
        byteChannel(color.a)};
    SDL_Surface* rendered = TTF_RenderUTF8_Blended(font, text.c_str(), sdl_color);
    if (!rendered) return nullptr;
    if (solid) {
        SDL_Surface* converted = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(rendered);
        if (!converted) return nullptr;
        auto* src_pixels = static_cast<std::uint32_t*>(converted->pixels);
        const int pitch_pixels = converted->pitch / 4;
        for (int y = 0; y < converted->h; ++y) {
            for (int x = 0; x < converted->w; ++x) {
                const std::size_t index = static_cast<std::size_t>(y * pitch_pixels + x);
                Uint8 r = 0;
                Uint8 g = 0;
                Uint8 b = 0;
                Uint8 a = 0;
                SDL_GetRGBA(src_pixels[index], converted->format, &r, &g, &b, &a);
                const bool is_text = a >= 96;
                src_pixels[index] = SDL_MapRGBA(
                    converted->format,
                    is_text ? sdl_color.r : 0,
                    is_text ? sdl_color.g : 0,
                    is_text ? sdl_color.b : 0,
                    is_text ? sdl_color.a : 0);
            }
        }
        return converted;
    }
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(rendered, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(rendered);
    return converted;
}

void blendSurface(
    std::vector<std::uint8_t>& pixels,
    int w,
    int h,
    SDL_Surface* surface,
    int dst_x,
    int dst_y,
    int fade_width) {
    if (!surface) return;
    const auto* src_pixels = static_cast<const std::uint32_t*>(surface->pixels);
    const int pitch_pixels = surface->pitch / 4;
    for (int y = 0; y < surface->h; ++y) {
        const int py = dst_y + y;
        if (py < 0 || py >= h) continue;
        for (int x = 0; x < surface->w; ++x) {
            const int px = dst_x + x;
            if (px < 0 || px >= w) continue;
            Uint8 r = 0;
            Uint8 g = 0;
            Uint8 b = 0;
            Uint8 a = 0;
            SDL_GetRGBA(src_pixels[y * pitch_pixels + x], surface->format, &r, &g, &b, &a);
            a = static_cast<Uint8>(static_cast<float>(a) * leftFadeMultiplier(px, fade_width) + 0.5f);
            blendRgba(pixels, w, px, py, r, g, b, a);
        }
    }
}

void blendScaledSprite(
    std::vector<std::uint8_t>& pixels,
    int w,
    int h,
    SDL_Surface* sprite,
    int dst_x,
    int dst_y,
    int dst_w,
    int dst_h,
    int fade_width) {
    if (!sprite || dst_w <= 0 || dst_h <= 0) return;
    const auto* src_pixels = static_cast<const std::uint32_t*>(sprite->pixels);
    const int pitch_pixels = sprite->pitch / 4;
    for (int y = 0; y < dst_h; ++y) {
        const int py = dst_y + y;
        if (py < 0 || py >= h) continue;
        const int sy = std::clamp(
            static_cast<int>((static_cast<float>(y) + 0.5f) * static_cast<float>(sprite->h) / static_cast<float>(dst_h)),
            0,
            std::max(0, sprite->h - 1));
        for (int x = 0; x < dst_w; ++x) {
            const int px = dst_x + x;
            if (px < 0 || px >= w) continue;
            const int sx = std::clamp(
                static_cast<int>((static_cast<float>(x) + 0.5f) * static_cast<float>(sprite->w) / static_cast<float>(dst_w)),
                0,
                std::max(0, sprite->w - 1));
            Uint8 r = 0;
            Uint8 g = 0;
            Uint8 b = 0;
            Uint8 a = 0;
            SDL_GetRGBA(src_pixels[sy * pitch_pixels + sx], sprite->format, &r, &g, &b, &a);
            a = static_cast<Uint8>(static_cast<float>(a) * leftFadeMultiplier(px, fade_width) + 0.5f);
            blendRgba(pixels, w, px, py, r, g, b, a);
        }
    }
}

void submitTextureQuad(
    bgfx::VertexLayout& layout,
    bgfx::ProgramHandle program,
    bgfx::UniformHandle tex_uniform,
    bgfx::UniformHandle tint_uniform,
    bgfx::UniformHandle adjust_uniform,
    bgfx::UniformHandle blur_uniform,
    bgfx::UniformHandle light_dir_uniform,
    bgfx::UniformHandle light_params_uniform,
    bgfx::TextureHandle texture,
    bool origin_bottom_left,
    float x0,
    float y0,
    float x1,
    float y1,
    std::uint8_t view_id) {
    bgfx::TransientVertexBuffer tvb;
    bgfx::TransientIndexBuffer tib;
    if (!bgfx::allocTransientBuffers(&tvb, layout, 4, &tib, 6)) return;
    const float v_top = origin_bottom_left ? 1.0f : 0.0f;
    const float v_bottom = origin_bottom_left ? 0.0f : 1.0f;
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
    bgfx::setTexture(0, tex_uniform, texture, samplerFlags(33071, 33071));
    bgfx::setUniform(tint_uniform, tint);
    bgfx::setUniform(adjust_uniform, adjust);
    bgfx::setUniform(blur_uniform, texture_blur);
    bgfx::setUniform(light_dir_uniform, light_dir);
    bgfx::setUniform(light_params_uniform, light_params);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);
    bgfx::submit(view_id, program);
}

} // namespace

bool AttendBgfxRenderer::Impl::ensureProfilePlateTexture(float texture_scale, bool text_only, bool include_text) {
    if (!profile_plate_.enabled || profile_plate_.w <= 0 || profile_plate_.h <= 0) return false;
    texture_scale = std::clamp(texture_scale, 0.05f, 8.0f);
    AttendBgfxProfilePlate plate = profile_plate_;
    auto scale_int = [texture_scale](int value) {
        return std::max(0, static_cast<int>(std::round(static_cast<float>(value) * texture_scale)));
    };
    plate.w = std::max(1, scale_int(plate.w));
    plate.h = std::max(1, scale_int(plate.h));
    plate.style.width = std::max(1, scale_int(plate.style.width));
    plate.style.height = std::max(1, scale_int(plate.style.height));
    plate.style.padding_x = scale_int(plate.style.padding_x);
    plate.style.padding_y = scale_int(plate.style.padding_y);
    plate.style.corner_radius = scale_int(plate.style.corner_radius);
    plate.style.outer_stroke_width = scale_int(plate.style.outer_stroke_width);
    plate.style.inner_stroke_width = scale_int(plate.style.inner_stroke_width);
    plate.style.sprite_size = std::max(1, scale_int(plate.style.sprite_size));
    plate.style.sprite_gap = scale_int(plate.style.sprite_gap);
    plate.style.sprite_offset_x = scale_int(plate.style.sprite_offset_x);
    plate.style.sprite_offset_y = scale_int(plate.style.sprite_offset_y);
    plate.style.name_text_offset_x = scale_int(plate.style.name_text_offset_x);
    plate.style.name_text_offset_y = scale_int(plate.style.name_text_offset_y);
    plate.style.detail_text_offset_x = scale_int(plate.style.detail_text_offset_x);
    plate.style.detail_text_offset_y = scale_int(plate.style.detail_text_offset_y);
    plate.style.name_font_size = std::max(8, scale_int(plate.style.name_font_size));
    plate.style.detail_font_size = std::max(8, scale_int(plate.style.detail_font_size));
    plate.style.name_row_height = std::max(1, scale_int(plate.style.name_row_height));
    plate.style.detail_row_height = std::max(1, scale_int(plate.style.detail_row_height));
    plate.style.name_row_width = std::max(1, scale_int(plate.style.name_row_width));
    plate.style.characteristic_row_width = std::max(1, scale_int(plate.style.characteristic_row_width));
    plate.style.nature_row_width = std::max(1, scale_int(plate.style.nature_row_width));
    plate.style.row_gap = scale_int(plate.style.row_gap);
    plate.style.left_fade_width = scale_int(plate.style.left_fade_width);
    const int w = plate.w;
    const int h = plate.h;
    const std::string key = profilePlateStyleKey(plate, text_only, include_text);
    OverlayButtonTexture& cache = text_only ? profile_plate_text_texture_ : profile_plate_texture_;
    if (cache.texture.valid() && cache.key == key) {
        return true;
    }

    cache.destroy();
    const AttendProfilePlateConfig& style = plate.style;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w * h * 4), 0);
    const int border = 0;
    const int sprite_px = std::max(1, static_cast<int>(std::round(static_cast<float>(style.sprite_size) * style.sprite_scale)));
    const int row1_h = std::max(1, style.name_row_height);
    const int row2_h = std::max(1, style.detail_row_height);
    const int row3_h = std::max(1, style.detail_row_height);
    const int row_gap = std::max(0, style.row_gap);
    const int row1_y = std::max(border, style.padding_y);
    const int row2_y = row1_y + row1_h + row_gap;
    const int row3_y = row2_y + row2_h + row_gap;
    const auto draw_row = [&](int row_y, int row_h, int row_w) {
        row_w = std::clamp(row_w, 1, w - border * 2);
        const int row_x = std::max(border, w - border - row_w);
        const int radius = std::clamp(style.corner_radius, 0, std::min(row_w, row_h) / 2);
        for (int local_y = 0; local_y < row_h; ++local_y) {
            const int y = row_y + local_y;
            if (y < 0 || y >= h) continue;
            for (int local_x = 0; local_x < row_w; ++local_x) {
                const int x = row_x + local_x;
                if (!insideRightAttachedRow(local_x, local_y, row_w, row_h, radius)) continue;
                const float fade = leftFadeMultiplier(local_x, style.left_fade_width);
                const float vertical = row_h > 1 ? static_cast<float>(local_y) / static_cast<float>(row_h - 1) : 0.0f;
                const float horizontal = row_w > 1 ? static_cast<float>(local_x) / static_cast<float>(row_w - 1) : 1.0f;
                Color4 color = mixColor(style.fill_top, style.fill_bottom, std::clamp(horizontal * 0.78f + vertical * 0.22f, 0.0f, 1.0f));
                color.a *= std::clamp(0.25f + horizontal * 0.75f, 0.0f, 1.0f);
                setRgbaFaded(pixels, w, x, y, color, fade);
            }
        }
    };
    if (!text_only) {
        draw_row(row1_y, row1_h, style.name_row_width);
        draw_row(row2_y, row2_h, style.characteristic_row_width);
        draw_row(row3_y, row3_h, style.nature_row_width);
    }

    if (!include_text && !text_only) {
        // Background-only pass.
    } else if (!TTF_WasInit() && TTF_Init() != 0) {
        std::cerr << "[AttendBgfxRenderer] TTF_Init failed for profile plate: " << TTF_GetError() << '\n';
    } else {
        fs::path font_path(style.font_path);
        if (!font_path.is_absolute()) font_path = fs::path(project_root_) / font_path;
        const int content_w = std::max(1, style.name_row_width - style.padding_x * 2);
        const int max_name_w = std::max(1, content_w - (style.show_sprite ? sprite_px + style.sprite_gap : 0));
        TTF_Font* name_font = openFitFont(font_path, style.name_font_size, 8, plate.name, max_name_w, TTF_STYLE_BOLD);
        const int characteristic_content_w = std::max(1, style.characteristic_row_width - style.padding_x * 2);
        const int nature_content_w = std::max(1, style.nature_row_width - style.padding_x * 2);
        TTF_Font* detail_font = openFitFont(
            font_path,
            style.detail_font_size,
            9,
            plate.characteristic.size() >= plate.nature.size() ? plate.characteristic : plate.nature,
            std::min(characteristic_content_w, nature_content_w));
        const bool solid_text = config_.ui.pixelated_overlay;
        SDL_Surface* name = renderTextSurface(name_font, plate.name, style.name_text, solid_text);
        SDL_Surface* characteristic = renderTextSurface(detail_font, plate.characteristic, style.detail_text, solid_text);
        SDL_Surface* nature = renderTextSurface(detail_font, plate.nature, style.detail_text, solid_text);
        SDL_Surface* sprite = nullptr;
        if (style.show_sprite) {
            fs::path sprite_path(plate.sprite_path);
            if (!sprite_path.is_absolute()) sprite_path = fs::path(project_root_) / sprite_path;
            SDL_Surface* loaded_sprite = IMG_Load(sprite_path.string().c_str());
            sprite = loaded_sprite ? SDL_ConvertSurfaceFormat(loaded_sprite, SDL_PIXELFORMAT_RGBA32, 0) : nullptr;
            if (loaded_sprite) SDL_FreeSurface(loaded_sprite);
        }

        const int text_right = std::max(style.padding_x, w - style.padding_x - border);
        const int characteristic_right = std::max(style.padding_x, w - style.padding_x - border);
        const int nature_right = std::max(style.padding_x, w - style.padding_x - border);
        const int sprite_w = sprite_px;
        const int sprite_h = sprite && sprite->w > 0
            ? std::max(1, static_cast<int>(std::round(static_cast<float>(sprite_px) * static_cast<float>(sprite->h) / static_cast<float>(sprite->w))))
            : sprite_px;
        const int group_w = (style.show_sprite ? sprite_w + style.sprite_gap : 0) + (name ? name->w : 0);
        const int group_x = std::clamp(text_right - group_w, style.padding_x, std::max(style.padding_x, w - group_w));
        const int sprite_x = group_x + style.sprite_offset_x;
        const int sprite_y = row1_y + (row1_h - sprite_h) / 2 + style.sprite_offset_y;
        const int name_x = group_x + (style.show_sprite ? sprite_w + style.sprite_gap : 0) + style.name_text_offset_x;
        const int name_y = row1_y + (row1_h - (name ? name->h : 0)) / 2 + style.name_text_offset_y;

        if (sprite) {
            blendScaledSprite(pixels, w, h, sprite, sprite_x, sprite_y, sprite_w, sprite_h, style.left_fade_width);
            SDL_FreeSurface(sprite);
        }

        blendSurface(pixels, w, h, name, name_x, name_y, style.left_fade_width);
        const int detail1_y = row2_y + (row2_h - (characteristic ? characteristic->h : 0)) / 2 + style.detail_text_offset_y;
        const int detail2_y = row3_y + (row3_h - (nature ? nature->h : 0)) / 2 + style.detail_text_offset_y;
        blendSurface(pixels, w, h, characteristic, characteristic_right - (characteristic ? characteristic->w : 0) + style.detail_text_offset_x, detail1_y, style.left_fade_width);
        blendSurface(pixels, w, h, nature, nature_right - (nature ? nature->w : 0) + style.detail_text_offset_x, detail2_y, style.left_fade_width);

        if (name) SDL_FreeSurface(name);
        if (characteristic) SDL_FreeSurface(characteristic);
        if (nature) SDL_FreeSurface(nature);
        if (name_font) TTF_CloseFont(name_font);
        if (detail_font) TTF_CloseFont(detail_font);
    }

    const bgfx::Memory* mem = bgfx::copy(pixels.data(), static_cast<std::uint32_t>(pixels.size()));
    cache.texture.handle = bgfx::createTexture2D(
        static_cast<std::uint16_t>(w),
        static_cast<std::uint16_t>(h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        0,
        mem);
    cache.texture.width = w;
    cache.texture.height = h;
    cache.key = key;
    return cache.texture.valid();
}

void AttendBgfxRenderer::Impl::submitProfilePlate(
    int framebuffer_w,
    int framebuffer_h,
    bgfx::FrameBufferHandle target,
    std::uint8_t view_id,
    bool text_only,
    bool include_text) {
    if (!profile_plate_.enabled || !bgfx::isValid(program_)) return;
    const SDL_Rect rect{
        std::clamp(presentation_rect_.x, 0, std::max(0, framebuffer_w - 1)),
        std::clamp(presentation_rect_.y, 0, std::max(0, framebuffer_h - 1)),
        std::clamp(presentation_rect_.w, 1, std::max(1, framebuffer_w)),
        std::clamp(presentation_rect_.h, 1, std::max(1, framebuffer_h))};
    const int logical_w = std::max(1, profile_plate_logical_w_);
    const int logical_h = std::max(1, profile_plate_logical_h_);
    const float sx = static_cast<float>(rect.w) / static_cast<float>(logical_w);
    const float sy = static_cast<float>(rect.h) / static_cast<float>(logical_h);
    const float scale = std::max(0.05f, std::min(sx, sy));
    const float texture_scale = bgfx::isValid(target) ? scale : 1.0f;
    if (!ensureProfilePlateTexture(texture_scale, text_only, include_text)) return;
    const OverlayButtonTexture& cache = text_only ? profile_plate_text_texture_ : profile_plate_texture_;

    float view[16];
    float proj[16];
    identity(view);
    bx::mtxOrtho(
        proj,
        0.0f,
        static_cast<float>(std::max(1, framebuffer_w)),
        static_cast<float>(std::max(1, framebuffer_h)),
        0.0f,
        0.0f,
        100.0f,
        0.0f,
        backend_.homogeneousDepth());
    bgfx::setViewTransform(view_id, view, proj);
    bgfx::setViewRect(view_id, 0, 0, static_cast<std::uint16_t>(std::max(1, framebuffer_w)), static_cast<std::uint16_t>(std::max(1, framebuffer_h)));
    bgfx::setViewFrameBuffer(view_id, target);
    bgfx::setViewMode(view_id, bgfx::ViewMode::Sequential);

    const float plate_w = static_cast<float>(profile_plate_.w) * scale;
    const float plate_h = static_cast<float>(profile_plate_.h) * scale;
    const float right_gap = static_cast<float>(logical_w - (profile_plate_.x + profile_plate_.w)) * scale;
    const float x0 = static_cast<float>(rect.x + rect.w) - right_gap - plate_w;
    const float y0 = static_cast<float>(rect.y) + static_cast<float>(profile_plate_.y) * scale;
    const float x1 = x0 + plate_w;
    const float y1 = y0 + plate_h;
    submitTextureQuad(
        layout_,
        program_,
        tex_uniform_,
        tint_cutoff_uniform_,
        color_adjust_uniform_,
        texture_blur_uniform_,
        light_dir_uniform_,
        light_params_uniform_,
        cache.texture.handle,
        backend_.originBottomLeft(),
        x0,
        y0,
        x1,
        y1,
        view_id);
}

} // namespace pr::gameplay::attend::rendering
