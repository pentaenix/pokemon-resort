#include "gameplay/world3d/rendering/BillboardSpriteRenderer.hpp"

#include "gameplay/world3d/rendering/CharacterTextureCache.hpp"

#include <SDL_image.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace pr::gameplay::world3d::rendering {

namespace {

SpriteShadowConfig defaultGen4ShadowConfig() {
    SpriteShadowConfig c;
    c.enabled = true;
    c.opacity = 1.0f;
    c.pixel_coherent = true;
    c.feet_to_shadow_bottom_px = 3;
    c.screen_offset_x_px = 0;
    c.screen_offset_y_px = 0;
    c.color_r = 0x37;
    c.color_g = 0x38;
    c.color_b = 0x3B;
    c.color_a = 0x94;
    c.radius_x_tiles = 0.32f;
    c.radius_z_tiles = 0.20f;
    c.world_y_lift = 0.10f;
    c.texture_width_px = 17;
    c.texture_height_px = 9;
    c.mask_rows = {
        "OOOOOXXXXXXXOOOOO",
        "OOXXXXXXXXXXXXXOO",
        "OXXXXXXXXXXXXXXXO",
        "XXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXX",
        "XXXXXXXXXXXXXXXXX",
        "OXXXXXXXXXXXXXXXO",
        "OOXXXXXXXXXXXXXOO",
        "OOOOOXXXXXXXOOOOO",
    };
    return c;
}

bool isMaskValid(const std::vector<std::string>& mask, int w, int h) {
    if (mask.empty() || static_cast<int>(mask.size()) != h) return false;
    for (const std::string& row : mask) {
        if (static_cast<int>(row.size()) != w) return false;
    }
    return true;
}

} // namespace

BillboardSpriteRenderer::BillboardSpriteRenderer(
    SDL_Renderer* renderer,
    const CharacterSpriteDefinition& def,
    const SpriteShadowConfig& shadow_config)
    : def_(def), shadow_config_(shadow_config.enabled ? shadow_config : defaultGen4ShadowConfig()) {
    SDL_Surface* surface = nullptr;
    if (!def.texture_png_bytes.empty()) {
        SDL_RWops* rw = SDL_RWFromConstMem(
            def.texture_png_bytes.data(),
            static_cast<int>(def.texture_png_bytes.size()));
        if (rw) {
            surface = IMG_Load_RW(rw, 1);
        }
    }
    if (!surface) {
        surface = IMG_Load(def.texture_path.c_str());
    }
    if (!surface) {
        std::string fallback = def.texture_path;
        const auto pos = fallback.find_last_of('.');
        if (pos != std::string::npos) {
            fallback = fallback.substr(0, pos) + ".png";
            surface = IMG_Load(fallback.c_str());
        }
    }
    if (!surface) {
        return;
    }
    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface);
    if (!raw) {
        SDL_FreeSurface(surface);
        return;
    }
    texture_.reset(raw, SDL_DestroyTexture);
    SDL_SetTextureBlendMode(texture_.get(), SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    auto uploadWhiteSurface = [&](SDL_Surface* white_surface) {
        if (!white_surface) {
            return;
        }
        SDL_Texture* white_raw = SDL_CreateTextureFromSurface(renderer, white_surface);
        if (white_raw) {
            white_texture_.reset(white_raw, SDL_DestroyTexture);
            SDL_SetTextureBlendMode(white_texture_.get(), SDL_BLENDMODE_BLEND);
        }
        SDL_FreeSurface(white_surface);
    };

    const RgbaImage white_rgba = buildWhiteSilhouetteFromPngBytes(def.texture_png_bytes);
    if (white_rgba.valid()) {
        SDL_Surface* white_surface = SDL_CreateRGBSurfaceFrom(
            0,
            white_rgba.width,
            white_rgba.height,
            32,
            white_rgba.width * 4,
            0x000000ff,
            0x0000ff00,
            0x00ff0000,
            0xff000000);
        if (white_surface) {
            std::memcpy(
                white_surface->pixels,
                white_rgba.pixels.data(),
                white_rgba.pixels.size());
            uploadWhiteSurface(white_surface);
        }
    } else {
        SDL_Surface* white_surface = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
        if (white_surface) {
            SDL_LockSurface(white_surface);
            auto* pixels = static_cast<Uint32*>(white_surface->pixels);
            const std::size_t pixel_count =
                static_cast<std::size_t>(white_surface->w) * static_cast<std::size_t>(white_surface->h);
            for (std::size_t i = 0; i < pixel_count; ++i) {
                Uint8 r = 0;
                Uint8 g = 0;
                Uint8 b = 0;
                Uint8 a = 0;
                SDL_GetRGBA(pixels[i], white_surface->format, &r, &g, &b, &a);
                pixels[i] = SDL_MapRGBA(white_surface->format, 255, 255, 255, a);
            }
            SDL_UnlockSurface(white_surface);
            uploadWhiteSurface(white_surface);
        }
    }
    SDL_FreeSurface(surface);

    const int sw = std::max(8, shadow_config_.texture_width_px);
    const int sh = std::max(8, shadow_config_.texture_height_px);
    SDL_Surface* shadow_surface = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_RGBA32);
    if (!shadow_surface) {
        return;
    }
    SDL_LockSurface(shadow_surface);
    auto* pixels = static_cast<Uint32*>(shadow_surface->pixels);
    const Uint8 peak_alpha = static_cast<Uint8>(
        std::clamp(shadow_config_.opacity, 0.0f, 1.0f) * static_cast<float>(shadow_config_.color_a));
    const SpriteShadowConfig default_cfg = defaultGen4ShadowConfig();
    const std::vector<std::string>& mask = isMaskValid(shadow_config_.mask_rows, sw, sh)
        ? shadow_config_.mask_rows
        : default_cfg.mask_rows;
    if (isMaskValid(mask, sw, sh)) {
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                const bool on = mask[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == 'X';
                const Uint8 a = on ? peak_alpha : 0;
                pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(x)] =
                    SDL_MapRGBA(shadow_surface->format, shadow_config_.color_r, shadow_config_.color_g, shadow_config_.color_b, a);
            }
        }
    } else {
        const float cx = static_cast<float>(sw) * 0.5f;
        const float cy = static_cast<float>(sh) * 0.5f;
        const float rx = std::max(1.0f, static_cast<float>(sw) * 0.46f);
        const float ry = std::max(1.0f, static_cast<float>(sh) * 0.42f);
        for (int y = 0; y < sh; ++y) {
            for (int x = 0; x < sw; ++x) {
                const float dx = (static_cast<float>(x) + 0.5f - cx) / rx;
                const float dy = (static_cast<float>(y) + 0.5f - cy) / ry;
                const float d2 = dx * dx + dy * dy;
                const Uint8 a = (d2 <= 1.0f) ? peak_alpha : 0;
                pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(x)] =
                    SDL_MapRGBA(shadow_surface->format, shadow_config_.color_r, shadow_config_.color_g, shadow_config_.color_b, a);
            }
        }
    }
    SDL_UnlockSurface(shadow_surface);
    SDL_Texture* shadow_raw = SDL_CreateTextureFromSurface(renderer, shadow_surface);
    SDL_FreeSurface(shadow_surface);
    if (shadow_raw) {
        shadow_texture_.reset(shadow_raw, SDL_DestroyTexture);
        SDL_SetTextureBlendMode(shadow_texture_.get(), SDL_BLENDMODE_BLEND);
    }
}

void BillboardSpriteRenderer::render(
    SDL_Renderer* renderer,
    const camera::Gen4FollowCamera& camera,
    const camera::Vec3& world_pos,
    const SDL_Rect& source_rect,
    int viewport_w,
    int viewport_h,
    float tint_r,
    float tint_g,
    float tint_b,
    float brightness,
    float sprite_scale_multiplier,
    float alpha_multiplier,
    float white_overlay_alpha,
    const camera::Vec3* shadow_world_override,
    int extra_screen_offset_x_px,
    int extra_screen_offset_y_px) {
    if (!texture_) {
        return;
    }

    float sx = 0.0f;
    float sy = 0.0f;
    float depth = 0.0f;
    camera::Vec3 world_anchor = world_pos;
    world_anchor.x += def_.world_offset_x;
    world_anchor.y += def_.world_offset_y;
    world_anchor.z += def_.world_offset_z;
    if (!camera.worldToScreen(world_anchor, viewport_w, viewport_h, sx, sy, depth)) {
        return;
    }

    const float scale = camera.perspectiveScale(depth) * std::max(0.1f, def_.sprite_scale) * std::max(0.1f, sprite_scale_multiplier);
    const int w = std::max(2, static_cast<int>(std::round(source_rect.w * scale * 0.60f)));
    const int h = std::max(2, static_cast<int>(std::round(source_rect.h * scale * 0.60f)));
    const int anchor_y = (def_.anchor == "center") ? (h / 2) : h;
    const int sprite_screen_x = static_cast<int>(std::round(sx));
    const int sprite_screen_y = static_cast<int>(std::round(sy));

    if (shadow_texture_) {
        camera::Vec3 shadow_world = shadow_world_override ? *shadow_world_override : world_pos;
        shadow_world.y += shadow_config_.world_y_lift;
        float shx = 0.0f;
        float shy = 0.0f;
        float shd = 0.0f;
        if (camera.worldToScreen(shadow_world, viewport_w, viewport_h, shx, shy, shd)) {
            int shadow_w = 2;
            int shadow_h = 2;
            if (shadow_config_.pixel_coherent) {
                shadow_w = std::max(
                    2,
                    static_cast<int>(std::round(static_cast<float>(shadow_config_.texture_width_px) * scale * 0.60f)));
                shadow_h = std::max(
                    2,
                    static_cast<int>(std::round(static_cast<float>(shadow_config_.texture_height_px) * scale * 0.60f)));
            } else {
                const float shadow_scale = camera.perspectiveScale(shd);
                const float sprite_w_px = static_cast<float>(source_rect.w) * std::max(0.1f, def_.sprite_scale) * 0.60f;
                const float sprite_h_px = static_cast<float>(source_rect.h) * std::max(0.1f, def_.sprite_scale) * 0.60f;
                shadow_w = std::max(
                    2,
                    static_cast<int>(std::round(sprite_w_px * shadow_scale * (shadow_config_.radius_x_tiles * 2.0f))));
                shadow_h = std::max(
                    2,
                    static_cast<int>(std::round(sprite_h_px * shadow_scale * (shadow_config_.radius_z_tiles * 2.0f))));
            }
            const int shadow_anchor_y = shadow_world_override
                ? static_cast<int>(std::round(shy))
                : (sprite_screen_y + def_.screen_offset_y_px);
            const int shadow_bottom =
                shadow_anchor_y + shadow_config_.feet_to_shadow_bottom_px + shadow_config_.screen_offset_y_px;
            SDL_Rect shadow_dst{
                static_cast<int>(std::round(shx)) - (shadow_w / 2) + shadow_config_.screen_offset_x_px,
                shadow_bottom - shadow_h,
                shadow_w,
                shadow_h};
            SDL_SetTextureColorMod(shadow_texture_.get(), 255, 255, 255);
            SDL_RenderCopy(renderer, shadow_texture_.get(), nullptr, &shadow_dst);
        }
    }

    SDL_Rect dst{
        sprite_screen_x - (w / 2) + def_.screen_offset_x_px + extra_screen_offset_x_px,
        sprite_screen_y - anchor_y + def_.screen_offset_y_px + extra_screen_offset_y_px,
        w,
        h};
    const float br = std::max(0.0f, brightness);
    SDL_SetTextureColorMod(
        texture_.get(),
        static_cast<Uint8>(std::clamp(tint_r * br, 0.0f, 1.0f) * 255.0f),
        static_cast<Uint8>(std::clamp(tint_g * br, 0.0f, 1.0f) * 255.0f),
        static_cast<Uint8>(std::clamp(tint_b * br, 0.0f, 1.0f) * 255.0f));
    SDL_SetTextureAlphaMod(
        texture_.get(),
        static_cast<Uint8>(std::clamp(alpha_multiplier, 0.0f, 1.0f) * 255.0f));
    SDL_RenderCopy(renderer, texture_.get(), &source_rect, &dst);

    if (white_texture_ && white_overlay_alpha > 0.0f) {
        SDL_SetTextureColorMod(white_texture_.get(), 255, 255, 255);
        SDL_SetTextureAlphaMod(
            white_texture_.get(),
            static_cast<Uint8>(std::clamp(white_overlay_alpha, 0.0f, 1.0f) * 255.0f));
        SDL_RenderCopy(renderer, white_texture_.get(), &source_rect, &dst);
    }
}

} // namespace pr::gameplay::world3d::rendering
