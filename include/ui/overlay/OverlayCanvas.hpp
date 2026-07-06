#pragma once

#include "core/Types.hpp"
#include "core/assets/Assets.hpp"
#include "core/assets/Font.hpp"

#include <SDL.h>
#include <string>
#include <unordered_map>

namespace pr {

enum class OverlayAnchor {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

struct OverlayButtonStyle {
    Color fill{18, 58, 86, 205};
    Color stroke{230, 248, 255, 230};
    Color text{245, 252, 255, 255};
    int width = 190;
    int height = 40;
    int margin_x = 24;
    int margin_y = 22;
    int padding_x = 14;
    int corner_radius = 10;
    int stroke_width = 2;
    int font_size = 18;
};

struct OverlayButton {
    std::string id;
    std::string label;
    OverlayAnchor anchor = OverlayAnchor::TopRight;
    OverlayButtonStyle style{};
};

class OverlayCanvas {
public:
    OverlayCanvas(int logical_w, int logical_h);

    void setLogicalSize(int logical_w, int logical_h);
    SDL_Rect buttonRect(const OverlayButton& button) const;
    bool hitButton(const OverlayButton& button, int logical_x, int logical_y) const;
    void renderButton(
        SDL_Renderer* renderer,
        const std::string& project_root,
        const OverlayButton& button);
    void invalidateText();

private:
    int logical_w_ = 1280;
    int logical_h_ = 800;
    FontHandle font_{};
    int font_size_ = 0;
    std::unordered_map<std::string, TextureHandle> text_cache_;
};

} // namespace pr
