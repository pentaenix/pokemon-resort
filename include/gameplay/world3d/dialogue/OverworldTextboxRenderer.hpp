#pragma once

#include "gameplay/world3d/dialogue/OverworldTextboxConfig.hpp"
#include "core/assets/Assets.hpp"
#include "core/assets/Font.hpp"
#include "ui/overlay/OverlaySliceLayout.hpp"

#include <SDL.h>

#include <memory>
#include <string>

namespace pr::gameplay::world3d::dialogue {

class OverworldTextboxRenderer {
public:
    OverworldTextboxRenderer(std::string project_root, OverworldTextboxConfig config);

    void configure(OverworldTextboxConfig config);
    const OverworldTextboxConfig& config() const { return config_; }

    bool initialize(SDL_Renderer* renderer);
    void render(SDL_Renderer* renderer, int viewport_w, int viewport_h, const std::string& text = {});
    void render(SDL_Renderer* renderer, const SDL_Rect& viewport_dst, int base_viewport_w, int base_viewport_h,
        const std::string& text = {});
    bool ready() const { return texture_ != nullptr; }

    static SDL_Rect sourceRectForSkin(const OverworldTextboxConfig& config, int sheet_w, int sheet_h);
    static pr::OverlayThreeSliceConfig sliceConfig(const OverworldTextboxConfig& config);
    static pr::OverlayThreeSliceLayout buildLayout(
        const OverworldTextboxConfig& config,
        int viewport_w,
        int viewport_h,
        int sheet_w,
        int sheet_h);
    static SDL_Point measureWrappedText(TTF_Font* font, const std::string& text, int wrap_width);

private:
    std::string project_root_;
    OverworldTextboxConfig config_{};
    std::shared_ptr<SDL_Texture> texture_;
    int texture_w_ = 0;
    int texture_h_ = 0;
    bool warned_load_failure_ = false;
    bool warned_invalid_index_ = false;
    FontHandle font_;
    TextureHandle text_texture_{};
    std::string cached_text_;
    int cached_wrap_width_ = 0;
};

} // namespace pr::gameplay::world3d::dialogue
