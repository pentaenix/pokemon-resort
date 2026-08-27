#include "gameplay/world3d/dialogue/OverworldTextboxRenderer.hpp"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <utility>

namespace pr::gameplay::world3d::dialogue {

namespace fs = std::filesystem;

namespace {

fs::path resolvePath(const std::string& project_root, const std::string& configured) {
    fs::path path(configured);
    if (path.is_absolute()) {
        return path;
    }
    return fs::path(project_root) / path;
}

} // namespace

SDL_Surface* renderWrappedSurface(TTF_Font* font, const std::string& text, int wrap_width) {
    if (!font || text.empty() || wrap_width <= 0) return nullptr;
    const SDL_Color color{32, 32, 32, 255};
    return TTF_RenderUTF8_Solid_Wrapped(
        font, text.c_str(), color, static_cast<Uint32>(wrap_width));
}

TextureHandle renderWrappedText(
    SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int wrap_width) {
    if (!renderer || !font || text.empty() || wrap_width <= 0) return {};
    SDL_Surface* surface = renderWrappedSurface(font, text, wrap_width);
    if (!surface) return {};
    SDL_Texture* raw = SDL_CreateTextureFromSurface(renderer, surface);
    TextureHandle out{};
    if (raw) {
        out.texture = std::shared_ptr<SDL_Texture>(raw, SDL_DestroyTexture);
        out.width = surface->w;
        out.height = surface->h;
    }
    SDL_FreeSurface(surface);
    return out;
}

SDL_Point OverworldTextboxRenderer::measureWrappedText(
    TTF_Font* font, const std::string& text, int wrap_width) {
    SDL_Surface* surface = renderWrappedSurface(font, text, wrap_width);
    if (!surface) return {};
    const SDL_Point size{surface->w, surface->h};
    SDL_FreeSurface(surface);
    return size;
}

OverworldTextboxRenderer::OverworldTextboxRenderer(std::string project_root, OverworldTextboxConfig config)
    : project_root_(std::move(project_root)),
      config_(std::move(config)) {}

void OverworldTextboxRenderer::configure(OverworldTextboxConfig config) {
    config_ = std::move(config);
    font_.reset();
    text_texture_ = {};
    cached_text_.clear();
    cached_wrap_width_ = 0;
}

bool OverworldTextboxRenderer::initialize(SDL_Renderer* renderer) {
    if (texture_) {
        return true;
    }
    if (!renderer || !overworldTextboxEnabled(config_)) {
        return false;
    }

    const fs::path path = resolvePath(project_root_, config_.sprite_sheet_path);
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        if (!warned_load_failure_) {
            std::cerr << "[Overworld3D][Textbox] Could not load text box sprite sheet '"
                      << path << "': " << IMG_GetError() << '\n';
            warned_load_failure_ = true;
        }
        return false;
    }
    texture_.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture_.get(), nullptr, nullptr, &texture_w_, &texture_h_) != 0) {
        std::cerr << "[Overworld3D][Textbox] Could not query text box sprite sheet '"
                  << path << "': " << SDL_GetError() << '\n';
        texture_.reset();
        texture_w_ = 0;
        texture_h_ = 0;
        return false;
    }
    SDL_SetTextureBlendMode(texture_.get(), SDL_BLENDMODE_BLEND);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    try {
        font_ = loadFont(
            config_.text_font_path, config_.text_font_size_px, project_root_);
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D][Textbox] Could not load text font: " << ex.what() << '\n';
    }
    return true;
}

SDL_Rect OverworldTextboxRenderer::sourceRectForSkin(
    const OverworldTextboxConfig& config,
    int sheet_w,
    int sheet_h) {
    return pr::overlaySliceSourceRect(sliceConfig(config), sheet_w, sheet_h);
}

pr::OverlayThreeSliceConfig OverworldTextboxRenderer::sliceConfig(const OverworldTextboxConfig& config) {
    return pr::OverlayThreeSliceConfig{
        config.selected_skin_index,
        config.valid_skin_count,
        config.source_cell_width_px,
        config.source_cell_height_px,
        config.sheet_columns,
        config.side_padding_px,
        config.bottom_padding_px,
        config.stretch_strip_width_px,
        config.stretch_strip_center_x_px};
}

pr::OverlayThreeSliceLayout OverworldTextboxRenderer::buildLayout(
    const OverworldTextboxConfig& config,
    int viewport_w,
    int viewport_h,
    int sheet_w,
    int sheet_h) {
    if (!overworldTextboxEnabled(config) || viewport_w <= 0 || viewport_h <= 0) {
        return {};
    }
    return pr::buildOverlayThreeSliceLayout(sliceConfig(config), viewport_w, viewport_h, sheet_w, sheet_h);
}

void OverworldTextboxRenderer::render(SDL_Renderer* renderer, int viewport_w, int viewport_h, const std::string& text) {
    if (!renderer || !overworldTextboxEnabled(config_) || !initialize(renderer)) {
        return;
    }
    if (config_.selected_skin_index < 0 || config_.selected_skin_index >= config_.valid_skin_count) {
        if (!warned_invalid_index_) {
            std::cerr << "[Overworld3D][Textbox] selectedSkinIndex "
                      << config_.selected_skin_index
                      << " is outside 0.." << (config_.valid_skin_count - 1)
                      << "; clamping for render.\n";
            warned_invalid_index_ = true;
        }
    }

    const pr::OverlayThreeSliceLayout layout = buildLayout(config_, viewport_w, viewport_h, texture_w_, texture_h_);
    if (!layout.visible) {
        return;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderCopy(renderer, texture_.get(), &layout.left_src, &layout.left_dst);
    SDL_RenderCopy(renderer, texture_.get(), &layout.middle_src, &layout.middle_dst);
    SDL_RenderCopy(renderer, texture_.get(), &layout.right_src, &layout.right_dst);
    const int wrap_width = std::max(
        1, viewport_w - config_.text_left_inset_px - config_.text_right_inset_px);
    if (text != cached_text_ || wrap_width != cached_wrap_width_) {
        cached_text_ = text;
        cached_wrap_width_ = wrap_width;
        text_texture_ = renderWrappedText(renderer, font_.get(), text, wrap_width);
    }
    if (text_texture_.texture) {
        const SDL_Rect text_dst{
            config_.text_left_inset_px,
            layout.left_dst.y + config_.text_top_inset_px,
            text_texture_.width,
            text_texture_.height};
        SDL_RenderCopy(renderer, text_texture_.texture.get(), nullptr, &text_dst);
    }
}

void OverworldTextboxRenderer::render(
    SDL_Renderer* renderer,
    const SDL_Rect& viewport_dst,
    int base_viewport_w,
    int base_viewport_h,
    const std::string& text) {
    if (!renderer || viewport_dst.w <= 0 || viewport_dst.h <= 0 ||
        base_viewport_w <= 0 || base_viewport_h <= 0 ||
        !overworldTextboxEnabled(config_) || !initialize(renderer)) {
        return;
    }
    if (config_.selected_skin_index < 0 || config_.selected_skin_index >= config_.valid_skin_count) {
        if (!warned_invalid_index_) {
            std::cerr << "[Overworld3D][Textbox] selectedSkinIndex "
                      << config_.selected_skin_index
                      << " is outside 0.." << (config_.valid_skin_count - 1)
                      << "; clamping for render.\n";
            warned_invalid_index_ = true;
        }
    }

    const pr::OverlayThreeSliceLayout layout =
        buildLayout(config_, base_viewport_w, base_viewport_h, texture_w_, texture_h_);
    if (!layout.visible) {
        return;
    }

    const SDL_Rect left_dst = pr::scaleOverlayRect(layout.left_dst, viewport_dst, base_viewport_w, base_viewport_h);
    const SDL_Rect middle_dst = pr::scaleOverlayRect(layout.middle_dst, viewport_dst, base_viewport_w, base_viewport_h);
    const SDL_Rect right_dst = pr::scaleOverlayRect(layout.right_dst, viewport_dst, base_viewport_w, base_viewport_h);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderCopy(renderer, texture_.get(), &layout.left_src, &left_dst);
    SDL_RenderCopy(renderer, texture_.get(), &layout.middle_src, &middle_dst);
    SDL_RenderCopy(renderer, texture_.get(), &layout.right_src, &right_dst);

    const int wrap_width = std::max(
        1, base_viewport_w - config_.text_left_inset_px - config_.text_right_inset_px);
    if (text != cached_text_ || wrap_width != cached_wrap_width_) {
        cached_text_ = text;
        cached_wrap_width_ = wrap_width;
        text_texture_ = renderWrappedText(renderer, font_.get(), text, wrap_width);
    }
    if (text_texture_.texture) {
        const SDL_Rect base_text{
            config_.text_left_inset_px,
            layout.left_dst.y + config_.text_top_inset_px,
            text_texture_.width,
            text_texture_.height};
        const SDL_Rect text_dst = pr::scaleOverlayRect(base_text, viewport_dst, base_viewport_w, base_viewport_h);
        SDL_RenderCopy(renderer, text_texture_.texture.get(), nullptr, &text_dst);
    }
}

} // namespace pr::gameplay::world3d::dialogue
