#include "gameplay/world3d/dialogue/OverworldTextboxRenderer.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
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
    const int cell_w = std::max(1, config.source_cell_width_px);
    const int cell_h = std::max(1, config.source_cell_height_px);
    const int columns = std::max(1, config.sheet_columns);
    const int rows = std::max(1, sheet_h / cell_h);
    const int valid_index = clampedTextboxSkinIndex(config);
    const int col = valid_index / rows;
    const int row = valid_index % rows;
    if (col >= columns || ((col + 1) * cell_w) > sheet_w || ((row + 1) * cell_h) > sheet_h) {
        return SDL_Rect{0, 0, 0, 0};
    }
    return SDL_Rect{col * cell_w, row * cell_h, cell_w, cell_h};
}

OverworldTextboxLayout OverworldTextboxRenderer::buildLayout(
    const OverworldTextboxConfig& config,
    int viewport_w,
    int viewport_h,
    int sheet_w,
    int sheet_h) {
    OverworldTextboxLayout layout{};
    if (!overworldTextboxEnabled(config) || viewport_w <= 0 || viewport_h <= 0) {
        return layout;
    }

    const SDL_Rect skin = sourceRectForSkin(config, sheet_w, sheet_h);
    if (skin.w <= 0 || skin.h <= 0) {
        return layout;
    }

    const int box_w = std::max(1, viewport_w - (std::max(0, config.side_padding_px) * 2));
    const int box_h = std::max(1, config.source_cell_height_px);
    const int box_x = std::max(0, config.side_padding_px);
    const int box_y = std::max(0, viewport_h - std::max(0, config.bottom_padding_px) - box_h);

    const int strip_w = std::clamp(config.stretch_strip_width_px, 1, skin.w);
    const int strip_x = skin.x + std::clamp(
        config.stretch_strip_center_x_px - (strip_w / 2),
        0,
        std::max(0, skin.w - strip_w));
    const int left_w = std::max(0, strip_x - skin.x);
    const int right_x = strip_x + strip_w;
    const int right_w = std::max(0, (skin.x + skin.w) - right_x);
    const int middle_dst_w = std::max(1, box_w - left_w - right_w);

    layout.left_src = SDL_Rect{skin.x, skin.y, left_w, skin.h};
    layout.middle_src = SDL_Rect{strip_x, skin.y, strip_w, skin.h};
    layout.right_src = SDL_Rect{right_x, skin.y, right_w, skin.h};
    layout.left_dst = SDL_Rect{box_x, box_y, left_w, box_h};
    layout.middle_dst = SDL_Rect{box_x + left_w, box_y, middle_dst_w, box_h};
    layout.right_dst = SDL_Rect{box_x + left_w + middle_dst_w, box_y, right_w, box_h};
    layout.visible = left_w > 0 && right_w > 0 && box_w > left_w + right_w;
    return layout;
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

    const OverworldTextboxLayout layout = buildLayout(config_, viewport_w, viewport_h, texture_w_, texture_h_);
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

    const OverworldTextboxLayout layout =
        buildLayout(config_, base_viewport_w, base_viewport_h, texture_w_, texture_h_);
    if (!layout.visible) {
        return;
    }

    auto scaleRect = [&](const SDL_Rect& src) {
        const double sx = static_cast<double>(viewport_dst.w) / static_cast<double>(std::max(1, base_viewport_w));
        const double sy = static_cast<double>(viewport_dst.h) / static_cast<double>(std::max(1, base_viewport_h));
        const int x0 = viewport_dst.x + static_cast<int>(std::lround(static_cast<double>(src.x) * sx));
        const int y0 = viewport_dst.y + static_cast<int>(std::lround(static_cast<double>(src.y) * sy));
        const int x1 = viewport_dst.x + static_cast<int>(std::lround(static_cast<double>(src.x + src.w) * sx));
        const int y1 = viewport_dst.y + static_cast<int>(std::lround(static_cast<double>(src.y + src.h) * sy));
        return SDL_Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)};
    };

    const SDL_Rect left_dst = scaleRect(layout.left_dst);
    const SDL_Rect middle_dst = scaleRect(layout.middle_dst);
    const SDL_Rect right_dst = scaleRect(layout.right_dst);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderCopy(renderer, texture_.get(), &layout.left_src, &left_dst);
    SDL_RenderCopy(renderer, texture_.get(), &layout.middle_src, &middle_dst);
    SDL_RenderCopy(renderer, texture_.get(), &layout.right_src, &right_dst);
}

} // namespace pr::gameplay::world3d::dialogue
