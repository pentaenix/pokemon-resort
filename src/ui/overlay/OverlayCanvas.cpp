#include "ui/overlay/OverlayCanvas.hpp"

#include <algorithm>

namespace pr {

namespace {

void setDrawColor(SDL_Renderer* renderer, const Color& color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

bool insideRoundedRect(int x, int y, int w, int h, int radius) {
    if (w <= 0 || h <= 0) return false;
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    if (radius <= 0) return x >= 0 && x < w && y >= 0 && y < h;
    const int left = radius;
    const int right = w - radius - 1;
    const int top = radius;
    const int bottom = h - radius - 1;
    if ((x >= left && x <= right) || (y >= top && y <= bottom)) return true;
    const int cx = x < left ? left : right;
    const int cy = y < top ? top : bottom;
    const int dx = x - cx;
    const int dy = y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

void fillRoundedRect(SDL_Renderer* renderer, const SDL_Rect& rect, int radius, const Color& color) {
    if (!renderer || rect.w <= 0 || rect.h <= 0) return;
    setDrawColor(renderer, color);
    for (int y = 0; y < rect.h; ++y) {
        int start = 0;
        while (start < rect.w && !insideRoundedRect(start, y, rect.w, rect.h, radius)) ++start;
        int end = rect.w - 1;
        while (end >= start && !insideRoundedRect(end, y, rect.w, rect.h, radius)) --end;
        if (start <= end) {
            SDL_RenderDrawLine(renderer, rect.x + start, rect.y + y, rect.x + end, rect.y + y);
        }
    }
}

} // namespace

OverlayCanvas::OverlayCanvas(int logical_w, int logical_h) {
    setLogicalSize(logical_w, logical_h);
}

void OverlayCanvas::setLogicalSize(int logical_w, int logical_h) {
    logical_w_ = std::max(1, logical_w);
    logical_h_ = std::max(1, logical_h);
}

SDL_Rect OverlayCanvas::buttonRect(const OverlayButton& button) const {
    const OverlayButtonStyle& style = button.style;
    const int x = [&]() {
        switch (button.anchor) {
            case OverlayAnchor::TopRight:
            case OverlayAnchor::BottomRight:
                return logical_w_ - style.margin_x - style.width;
            case OverlayAnchor::TopLeft:
            case OverlayAnchor::BottomLeft:
            default:
                return style.margin_x;
        }
    }();
    const int y = [&]() {
        switch (button.anchor) {
            case OverlayAnchor::BottomLeft:
            case OverlayAnchor::BottomRight:
                return logical_h_ - style.margin_y - style.height;
            case OverlayAnchor::TopLeft:
            case OverlayAnchor::TopRight:
            default:
                return style.margin_y;
        }
    }();
    return SDL_Rect{
        std::clamp(x, 0, std::max(0, logical_w_ - style.width)),
        std::clamp(y, 0, std::max(0, logical_h_ - style.height)),
        style.width,
        style.height};
}

bool OverlayCanvas::hitButton(const OverlayButton& button, int logical_x, int logical_y) const {
    const SDL_Rect r = buttonRect(button);
    return logical_x >= r.x && logical_x < r.x + r.w && logical_y >= r.y && logical_y < r.y + r.h;
}

void OverlayCanvas::renderButton(
    SDL_Renderer* renderer,
    const std::string& project_root,
    const OverlayButton& button) {
    if (!renderer) return;
    const SDL_Rect rect = buttonRect(button);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    if (button.style.stroke_width > 0) {
        fillRoundedRect(renderer, rect, button.style.corner_radius, button.style.stroke);
        const int inset = std::min(button.style.stroke_width, std::min(rect.w, rect.h) / 2);
        SDL_Rect inner{rect.x + inset, rect.y + inset, rect.w - inset * 2, rect.h - inset * 2};
        fillRoundedRect(renderer, inner, std::max(0, button.style.corner_radius - inset), button.style.fill);
    } else {
        fillRoundedRect(renderer, rect, button.style.corner_radius, button.style.fill);
    }

    if (!font_ || font_size_ != button.style.font_size) {
        text_cache_.clear();
        font_size_ = button.style.font_size;
        font_ = loadFontPreferringUnicode("assets/fonts/Arial.ttf", button.style.font_size, project_root);
    }
    TextureHandle* text_texture = nullptr;
    if (font_) {
        const std::string cache_key = button.label + "|" +
            std::to_string(button.style.text.r) + "," +
            std::to_string(button.style.text.g) + "," +
            std::to_string(button.style.text.b) + "," +
            std::to_string(button.style.text.a);
        auto [it, inserted] = text_cache_.try_emplace(cache_key);
        if (inserted || !it->second.texture) {
            it->second = renderTextTexture(renderer, font_.get(), button.label, button.style.text);
        }
        text_texture = &it->second;
    }
    if (text_texture && text_texture->texture) {
        SDL_Rect dst{
            rect.x + button.style.padding_x,
            rect.y + (rect.h - text_texture->height) / 2,
            text_texture->width,
            text_texture->height};
        SDL_RenderCopy(renderer, text_texture->texture.get(), nullptr, &dst);
    }
}

void OverlayCanvas::invalidateText() {
    text_cache_.clear();
}

} // namespace pr
