#include "gameplay/world3d/dialogue/OverworldTextboxRenderer.hpp"

#include <SDL_image.h>

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

OverworldTextboxRenderer::OverworldTextboxRenderer(std::string project_root, OverworldTextboxConfig config)
    : project_root_(std::move(project_root)),
      config_(std::move(config)) {}

void OverworldTextboxRenderer::configure(OverworldTextboxConfig config) {
    config_ = std::move(config);
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

void OverworldTextboxRenderer::render(SDL_Renderer* renderer, int viewport_w, int viewport_h) {
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
}

void OverworldTextboxRenderer::render(
    SDL_Renderer* renderer,
    const SDL_Rect& viewport_dst,
    int base_viewport_w,
    int base_viewport_h) {
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
}

} // namespace pr::gameplay::world3d::dialogue
