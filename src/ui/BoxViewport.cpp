#include "ui/BoxViewport.hpp"

#include "core/Assets.hpp"
#include "core/Font.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace fs = std::filesystem;

namespace pr {

namespace {

constexpr int kNamePillW = 360;
constexpr int kNamePillH = 70;
constexpr int kNameTopPad = 18;
constexpr int kNameToArrowGap = 50;
constexpr int kNameToGridGap = 18;

constexpr int kSlotW = 78;
constexpr int kSlotH = 72;
constexpr int kSlotGapX = 12;
constexpr int kSlotGapY = 12;
constexpr int kCols = 6;
constexpr int kRows = 5;

constexpr int kGameIconSize = 45;
constexpr int kFooterEdgePad = 17;
constexpr int kFooterBelowSlots = 14;
constexpr int kScrollBelowSlots = 23;
constexpr int kBoxSpaceBtnW = 166;
constexpr int kBoxSpaceBtnH = 40;

constexpr int kViewportCornerRadius = 16;
constexpr int kPillCornerRadius = 35;
constexpr int kSlotCornerRadius = 8;
constexpr int kButtonStripH = 3;

constexpr Color kViewportBg{224, 224, 224, 255};
constexpr Color kPillBg{251, 251, 251, 255};
constexpr Color kSlotBg{251, 251, 251, 255};
constexpr Color kButtonMain{191, 191, 191, 255};
constexpr Color kButtonUnderline{175, 175, 175, 255};

void setDrawColor(SDL_Renderer* renderer, const Color& c) {
    SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(c.r), static_cast<Uint8>(c.g), static_cast<Uint8>(c.b),
        static_cast<Uint8>(c.a));
}

void fillRoundedRectScanlines(SDL_Renderer* renderer, int x, int y, int w, int h, int radius, const Color& c) {
    if (w <= 0 || h <= 0) {
        return;
    }
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    setDrawColor(renderer, c);
    for (int yy = y; yy < y + h; ++yy) {
        int x0 = x;
        int x1 = x + w;
        if (yy < y + radius) {
            const int dy = yy - (y + radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        } else if (yy >= y + h - radius) {
            const int dy = yy - (y + h - radius);
            const int disc = radius * radius - dy * dy;
            if (disc >= 0) {
                const int s = static_cast<int>(std::lround(std::sqrt(static_cast<double>(disc))));
                x0 = (x + radius) - s;
                x1 = (x + w - radius) + s;
            }
        }
        if (x1 > x0) {
            SDL_Rect row{x0, yy, x1 - x0, 1};
            SDL_RenderFillRect(renderer, &row);
        }
    }
}

void drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& tex, int cx, int cy, int max_w, int max_h) {
    if (!tex.texture || max_w <= 0 || max_h <= 0) {
        return;
    }
    const double sx = static_cast<double>(max_w) / static_cast<double>(tex.width);
    const double sy = static_cast<double>(max_h) / static_cast<double>(tex.height);
    const double scale = std::min(1.0, std::min(sx, sy));
    const int dw = std::max(1, static_cast<int>(std::round(static_cast<double>(tex.width) * scale)));
    const int dh = std::max(1, static_cast<int>(std::round(static_cast<double>(tex.height) * scale)));
    const SDL_Rect dst{cx - dw / 2, cy - dh / 2, dw, dh};
    SDL_SetTextureBlendMode(tex.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex.texture.get(), 255);
    SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
}

void drawTextureCenteredScaled(
    SDL_Renderer* renderer,
    const TextureHandle& tex,
    int cx,
    int cy,
    int max_w,
    int max_h,
    double desired_scale) {
    if (!tex.texture || max_w <= 0 || max_h <= 0) {
        return;
    }
    desired_scale = std::max(0.01, desired_scale);
    const double dw0 = static_cast<double>(tex.width) * desired_scale;
    const double dh0 = static_cast<double>(tex.height) * desired_scale;
    const double sx = static_cast<double>(max_w) / std::max(1.0, dw0);
    const double sy = static_cast<double>(max_h) / std::max(1.0, dh0);
    const double clamp_scale = std::min(1.0, std::min(sx, sy));
    const int dw = std::max(1, static_cast<int>(std::round(dw0 * clamp_scale)));
    const int dh = std::max(1, static_cast<int>(std::round(dh0 * clamp_scale)));
    const SDL_Rect dst{cx - dw / 2, cy - dh / 2, dw, dh};
    SDL_SetTextureBlendMode(tex.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex.texture.get(), 255);
    SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
}

void drawTextureCenteredScaledRaw(
    SDL_Renderer* renderer,
    const TextureHandle& tex,
    int cx,
    int cy,
    double desired_scale) {
    if (!tex.texture) {
        return;
    }
    desired_scale = std::clamp(desired_scale, 0.01, 32.0);
    const int dw = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.width) * desired_scale)));
    const int dh = std::max(1, static_cast<int>(std::lround(static_cast<double>(tex.height) * desired_scale)));
    const SDL_Rect dst{cx - dw / 2, cy - dh / 2, dw, dh};
    SDL_SetTextureBlendMode(tex.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(tex.texture.get(), 255);
    SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
    SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
}

/// `angle` clockwise degrees; texture points left at 0°. `mod` tints the arrow for contrast on `#E0E0E0` / `#FBFBFB` UI.
void drawArrowRotated(
    SDL_Renderer* renderer,
    const TextureHandle& tex,
    int center_x,
    int center_y,
    double angle_degrees,
    const Color& mod) {
    if (!tex.texture) {
        return;
    }
    SDL_SetTextureBlendMode(tex.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(
        tex.texture.get(),
        static_cast<Uint8>(std::clamp(mod.r, 0, 255)),
        static_cast<Uint8>(std::clamp(mod.g, 0, 255)),
        static_cast<Uint8>(std::clamp(mod.b, 0, 255)));
    SDL_SetTextureAlphaMod(tex.texture.get(), static_cast<Uint8>(std::clamp(mod.a, 0, 255)));

    SDL_Rect dst{center_x - tex.width / 2, center_y - tex.height / 2, tex.width, tex.height};
    SDL_Point rot_center{tex.width / 2, tex.height / 2};
    SDL_RenderCopyEx(
        renderer,
        tex.texture.get(),
        nullptr,
        &dst,
        angle_degrees,
        &rot_center,
        SDL_FLIP_NONE);
}

TextureHandle loadTextureOrThrow(SDL_Renderer* renderer, const fs::path& path) {
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        throw std::runtime_error(std::string("IMG_LoadTexture: ") + path.string());
    }
    TextureHandle out;
    out.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(out.texture.get(), nullptr, nullptr, &out.width, &out.height) != 0) {
        throw std::runtime_error(std::string("SDL_QueryTexture: ") + path.string());
    }
    return out;
}

void approachExponential(double& v, double target, double dt, double lambda) {
    if (lambda <= 1e-9) {
        v = target;
        return;
    }
    const double alpha = 1.0 - std::exp(-lambda * std::max(0.0, dt));
    v += (target - v) * alpha;
    if (std::fabs(target - v) < 0.0005) {
        v = target;
    }
}

SDL_Rect prevArrowBounds(
    int vx,
    int vy,
    int arrow_w,
    int arrow_h) {
    const int pill_x = vx + (BoxViewport::kViewportWidth - kNamePillW) / 2;
    const int pill_y = vy + kNameTopPad;
    const int pill_cy = pill_y + kNamePillH / 2;
    const int left_cx = pill_x - kNameToArrowGap - arrow_w / 2;
    return SDL_Rect{left_cx - arrow_w / 2, pill_cy - arrow_h / 2, arrow_w, arrow_h};
}

SDL_Rect nextArrowBounds(
    int vx,
    int vy,
    int arrow_w,
    int arrow_h) {
    const int pill_x = vx + (BoxViewport::kViewportWidth - kNamePillW) / 2;
    const int pill_y = vy + kNameTopPad;
    const int pill_cy = pill_y + kNamePillH / 2;
    const int right_cx = pill_x + kNamePillW + kNameToArrowGap + arrow_w / 2;
    return SDL_Rect{right_cx - arrow_w / 2, pill_cy - arrow_h / 2, arrow_w, arrow_h};
}

bool pointInRect(int x, int y, const SDL_Rect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/// Extra margin around the slot grid for SDL clip so scaled / offset sprites are not cut off.
/// Negative `sprite_offset_y` moves art upward (needs more top padding); scale > 1 enlarges the drawn quad.
int spriteGridClipPadding(const GameTransferBoxViewportStyle& s) {
    const int from_offset = std::max(0, -s.sprite_offset_y);
    const double scale_excess = std::max(0.0, s.sprite_scale - 1.0);
    const int from_scale = static_cast<int>(std::ceil(scale_excess * 36.0));
    return std::clamp(8 + from_offset + from_scale, 8, 120);
}

SDL_Rect computeSpriteGridClipRect(
    int vx,
    int vy,
    int grid_x,
    int grid_y,
    int grid_w,
    int grid_h,
    int pad,
    bool content_slide_active) {
    if (content_slide_active) {
        // Sliding columns use horizontal offsets up to ±viewport width; clip to full panel width for this band.
        SDL_Rect r;
        r.x = vx;
        r.w = BoxViewport::kViewportWidth;
        r.y = std::max(vy, grid_y - pad);
        const int bottom = std::min(vy + BoxViewport::kViewportHeight, grid_y + grid_h + pad);
        r.h = std::max(0, bottom - r.y);
        return r;
    }
    int clip_x = grid_x - pad;
    int clip_y = grid_y - pad;
    int clip_w = grid_w + 2 * pad;
    int clip_h = grid_h + 2 * pad;
    clip_x = std::max(vx, clip_x);
    clip_w = std::min(vx + BoxViewport::kViewportWidth, clip_x + clip_w) - clip_x;
    clip_y = std::max(vy, clip_y);
    const int clip_bottom = std::min(vy + BoxViewport::kViewportHeight, clip_y + clip_h);
    clip_h = std::max(0, clip_bottom - clip_y);
    return SDL_Rect{clip_x, clip_y, clip_w, clip_h};
}

} // namespace

BoxViewport::BoxViewport(
    SDL_Renderer* renderer,
    const std::string& project_root,
    const std::string& font_path,
    const GameTransferBoxViewportStyle& style,
    BoxViewportRole role,
    int viewport_x,
    int viewport_y)
    : project_root_(project_root),
      style_(style),
      role_(role),
      viewport_x_(viewport_x),
      viewport_y_(viewport_y),
      title_font_(loadFontPreferringUnicode(font_path, std::max(1, style_.box_name_font_pt), project_root)),
      label_font_(loadFontPreferringUnicode(font_path, std::max(1, style_.box_space_font_pt), project_root)) {
    const fs::path arrow_path = fs::path(project_root_) / style_.arrow_texture;
    try {
        arrow_tex_ = loadTextureOrThrow(renderer, arrow_path);
    } catch (const std::exception& ex) {
        std::cerr << "[BoxViewport] arrow texture load failed (" << arrow_path << "): " << ex.what() << '\n';
    }
    box_space_label_tex_ = renderTextTexture(renderer, label_font_.get(), "Box space", style_.box_space_color);
    box_space_label_tex_white_ = renderTextTexture(renderer, label_font_.get(), "Box space", Color{255, 255, 255, 255});
}

void BoxViewport::setViewportOrigin(int viewport_x, int viewport_y) {
    viewport_x_ = viewport_x;
    viewport_y_ = viewport_y;
}

void BoxViewport::setHeaderMode(HeaderMode mode, bool show_down_arrow) {
    header_mode_ = mode;
    box_space_scroll_arrow_visible_ = show_down_arrow;
}

void BoxViewport::setBoxSpaceActive(bool active) {
    box_space_active_ = active;
}

void BoxViewport::setModel(BoxViewportModel model) {
    if (model.box_name != model_.box_name) {
        title_dirty_ = true;
    }
    model_ = std::move(model);
}

void BoxViewport::snapContentToModel(BoxViewportModel model) {
    content_slide_queue_.clear();
    content_slide_active_ = false;
    content_slide_dir_ = 0;
    content_slide_offset_x_ = 0.0;
    content_slide_target_x_ = 0.0;
    incoming_model_ = {};
    if (model.box_name != model_.box_name) {
        title_dirty_ = true;
    }
    model_ = std::move(model);
}

void BoxViewport::refreshTitleTexture(SDL_Renderer* renderer) const {
    const std::string active_name =
        (content_slide_active_ && !incoming_model_.box_name.empty())
            ? incoming_model_.box_name
            : model_.box_name;
    const std::string& name = active_name.empty() ? std::string("BOX 1") : active_name;
    if (!title_dirty_ && name == cached_title_text_ && cached_title_tex_.texture) {
        return;
    }
    cached_title_tex_ = renderTextTexture(renderer, title_font_.get(), name, style_.box_name_color);
    cached_title_text_ = name;
    title_dirty_ = false;
}

namespace {

std::string normalizeGameId(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

bool isSafeGameIdStem(const std::string& s) {
    if (s.empty()) {
        return false;
    }
    for (unsigned char uc : s) {
        if (!std::isalnum(uc) && uc != '_') {
            return false;
        }
    }
    return true;
}

} // namespace

std::string BoxViewport::gameIconFilenameForGameId(const std::string& game_id) {
    const std::string n = normalizeGameId(game_id);
    if (!isSafeGameIdStem(n)) {
        return "pokemon_resort_icon.png";
    }
    return n + "_icon.png";
}

void BoxViewport::reloadResortIcon(SDL_Renderer* renderer) {
    const fs::path path = fs::path(project_root_) / "assets" / "game_icons" / "pokemon_resort_icon.png";
    try {
        game_icon_tex_ = loadTextureOrThrow(renderer, path);
    } catch (const std::exception& ex) {
        std::cerr << "[BoxViewport] resort icon load failed (" << path << "): " << ex.what() << '\n';
        game_icon_tex_ = {};
    }
}

void BoxViewport::reloadGameIcon(SDL_Renderer* renderer, const std::string& game_key) {
    const fs::path primary = fs::path(project_root_) / "assets" / "game_icons" / gameIconFilenameForGameId(game_key);
    try {
        game_icon_tex_ = loadTextureOrThrow(renderer, primary);
        return;
    } catch (const std::exception& ex) {
        std::cerr << "[BoxViewport] game icon load failed (" << primary << "): " << ex.what() << '\n';
    }
    const fs::path fallback = fs::path(project_root_) / "assets" / "game_icons" / "pokemon_resort_icon.png";
    try {
        game_icon_tex_ = loadTextureOrThrow(renderer, fallback);
    } catch (const std::exception& ex) {
        std::cerr << "[BoxViewport] game icon fallback failed (" << fallback << "): " << ex.what() << '\n';
        game_icon_tex_ = {};
    }
}

bool BoxViewport::hitTestPrevBoxArrow(int logical_x, int logical_y) const {
    if (header_mode_ != HeaderMode::Normal) {
        return false;
    }
    if (!arrow_tex_.texture) {
        return false;
    }
    const SDL_Rect r = prevArrowBounds(viewport_x_, viewport_y_, arrow_tex_.width, arrow_tex_.height);
    return pointInRect(logical_x, logical_y, r);
}

bool BoxViewport::hitTestNextBoxArrow(int logical_x, int logical_y) const {
    if (header_mode_ != HeaderMode::Normal) {
        return false;
    }
    if (!arrow_tex_.texture) {
        return false;
    }
    const SDL_Rect r = nextArrowBounds(viewport_x_, viewport_y_, arrow_tex_.width, arrow_tex_.height);
    return pointInRect(logical_x, logical_y, r);
}

bool BoxViewport::getPrevArrowBounds(SDL_Rect& out) const {
    if (header_mode_ != HeaderMode::Normal) {
        return false;
    }
    if (!arrow_tex_.texture) {
        return false;
    }
    out = prevArrowBounds(viewport_x_, viewport_y_, arrow_tex_.width, arrow_tex_.height);
    return true;
}

bool BoxViewport::getNextArrowBounds(SDL_Rect& out) const {
    if (header_mode_ != HeaderMode::Normal) {
        return false;
    }
    if (!arrow_tex_.texture) {
        return false;
    }
    out = nextArrowBounds(viewport_x_, viewport_y_, arrow_tex_.width, arrow_tex_.height);
    return true;
}

bool BoxViewport::hitTestBoxSpaceScrollArrow(int logical_x, int logical_y) const {
    SDL_Rect r{};
    if (!getBoxSpaceScrollArrowBounds(r)) {
        return false;
    }
    return pointInRect(logical_x, logical_y, r);
}

bool BoxViewport::getBoxSpaceScrollArrowBounds(SDL_Rect& out) const {
    if (role_ != BoxViewportRole::ExternalGameSave || header_mode_ != HeaderMode::BoxSpace || !arrow_tex_.texture ||
        !box_space_scroll_arrow_visible_) {
        return false;
    }
    // Match the Resort scroll arrow placement (bottom-center below the grid).
    const int vx = viewport_x_;
    const int vy = viewport_y_;
    const int pill_y = vy + kNameTopPad;
    const int grid_w = kCols * kSlotW + (kCols - 1) * kSlotGapX;
    const int grid_h = kRows * kSlotH + (kRows - 1) * kSlotGapY;
    const int grid_x = vx + (BoxViewport::kViewportWidth - grid_w) / 2;
    const int grid_y = pill_y + kNamePillH + kNameToGridGap;
    const int grid_bottom = grid_y + grid_h;
    const int grid_mid_x = grid_x + grid_w / 2;
    const int scroll_cy =
        grid_bottom + kScrollBelowSlots + arrow_tex_.height / 2 + style_.footer_scroll_arrow_offset_y;
    out = SDL_Rect{grid_mid_x - arrow_tex_.width / 2, scroll_cy - arrow_tex_.height / 2, arrow_tex_.width, arrow_tex_.height};
    return true;
}

bool BoxViewport::getSlotBounds(int slot_index, SDL_Rect& out) const {
    if (slot_index < 0 || slot_index >= 30) {
        return false;
    }
    const int vx = viewport_x_;
    const int vy = viewport_y_;
    const int pill_y = vy + kNameTopPad;

    const int grid_w = kCols * kSlotW + (kCols - 1) * kSlotGapX;
    const int grid_x = vx + (BoxViewport::kViewportWidth - grid_w) / 2;
    const int grid_y = pill_y + kNamePillH + kNameToGridGap;
    const int row = slot_index / kCols;
    const int col = slot_index % kCols;
    out = SDL_Rect{
        grid_x + col * (kSlotW + kSlotGapX),
        grid_y + row * (kSlotH + kSlotGapY),
        kSlotW,
        kSlotH};
    return true;
}

bool BoxViewport::getNamePlateBounds(SDL_Rect& out) const {
    const int vx = viewport_x_;
    const int vy = viewport_y_;
    const int pill_x = vx + (BoxViewport::kViewportWidth - kNamePillW) / 2;
    const int pill_y = vy + kNameTopPad;
    out = SDL_Rect{pill_x, pill_y, kNamePillW, kNamePillH};
    return true;
}

bool BoxViewport::getFooterBoxSpaceBounds(SDL_Rect& out) const {
    const int vx = viewport_x_;
    const int vy = viewport_y_;
    const int pill_y = vy + kNameTopPad;
    const int grid_h = kRows * kSlotH + (kRows - 1) * kSlotGapY;
    const int grid_y = pill_y + kNamePillH + kNameToGridGap;
    const int grid_bottom = grid_y + grid_h;
    const int footer_row_y = grid_bottom + kFooterBelowSlots;
    const int btn_y = footer_row_y;
    const int btn_left_x =
        (role_ == BoxViewportRole::ResortStorage)
            ? (vx + BoxViewport::kViewportWidth - kFooterEdgePad - kBoxSpaceBtnW)
            : (vx + kFooterEdgePad);
    out = SDL_Rect{btn_left_x, btn_y, kBoxSpaceBtnW, kBoxSpaceBtnH};
    return true;
}

bool BoxViewport::getFooterGameIconBounds(SDL_Rect& out) const {
    const int vx = viewport_x_;
    const int vy = viewport_y_;
    const int pill_y = vy + kNameTopPad;
    const int grid_h = kRows * kSlotH + (kRows - 1) * kSlotGapY;
    const int grid_y = pill_y + kNamePillH + kNameToGridGap;
    const int grid_bottom = grid_y + grid_h;
    const int footer_row_y = grid_bottom + kFooterBelowSlots;
    const int icon_y = footer_row_y + (kBoxSpaceBtnH - kGameIconSize) / 2;
    const int icon_left_x =
        (role_ == BoxViewportRole::ResortStorage)
            ? (vx + kFooterEdgePad)
            : (vx + BoxViewport::kViewportWidth - kFooterEdgePad - kGameIconSize);
    out = SDL_Rect{icon_left_x, icon_y, kGameIconSize, kGameIconSize};
    return true;
}

bool BoxViewport::getResortScrollArrowBounds(SDL_Rect& out) const {
    if (role_ != BoxViewportRole::ResortStorage || !arrow_tex_.texture) {
        return false;
    }
    const int vx = viewport_x_;
    const int vy = viewport_y_;
    const int pill_y = vy + kNameTopPad;
    const int grid_w = kCols * kSlotW + (kCols - 1) * kSlotGapX;
    const int grid_h = kRows * kSlotH + (kRows - 1) * kSlotGapY;
    const int grid_x = vx + (BoxViewport::kViewportWidth - grid_w) / 2;
    const int grid_y = pill_y + kNamePillH + kNameToGridGap;
    const int grid_bottom = grid_y + grid_h;
    const int grid_mid_x = grid_x + grid_w / 2;
    const int scroll_cy =
        grid_bottom + kScrollBelowSlots + arrow_tex_.height / 2 + style_.footer_scroll_arrow_offset_y;
    // The rendered arrow is rotated, but bounds use the raw texture rect.
    out = SDL_Rect{grid_mid_x - arrow_tex_.width / 2, scroll_cy - arrow_tex_.height / 2, arrow_tex_.width, arrow_tex_.height};
    return true;
}

void BoxViewport::queueContentSlide(BoxViewportModel incoming, int dir) {
    if (dir == 0) {
        return;
    }
    const int requested_dir = dir > 0 ? 1 : -1;
    if (!content_slide_active_) {
        incoming_model_ = std::move(incoming);
        // Update name plate during the transition (frame stays fixed).
        title_dirty_ = true;
        content_slide_active_ = true;
        content_slide_dir_ = requested_dir;
        content_slide_offset_x_ = 0.0;
        content_slide_target_x_ = -static_cast<double>(content_slide_dir_ * BoxViewport::kViewportWidth);
        content_slide_queue_.clear();
        return;
    }
    // Allow spam navigation: enqueue additional steps in the same direction.
    if (requested_dir == content_slide_dir_) {
        content_slide_queue_.push_back(std::move(incoming));
        // Give the slide a small immediate push so rapid spam feels responsive.
        // (Exponential easing can otherwise feel like it "lags" at the start.)
        if (std::fabs(content_slide_offset_x_) < 32.0) {
            content_slide_offset_x_ -= static_cast<double>(content_slide_dir_) * 24.0;
        }
    }
}

void BoxViewport::update(double dt) {
    if (!content_slide_active_) {
        return;
    }
    // Ramp speed when the player is spamming (queued steps).
    const double base = std::max(1.0, style_.content_slide_smoothing);
    const double speed_boost = 1.0 + 1.25 * std::min<std::size_t>(10, content_slide_queue_.size());
    const double lambda = base * speed_boost;
    approachExponential(content_slide_offset_x_, content_slide_target_x_, dt, lambda);
    if (std::fabs(content_slide_offset_x_ - content_slide_target_x_) < 0.75) {
        // Commit.
        setModel(std::move(incoming_model_));
        if (!content_slide_queue_.empty()) {
            incoming_model_ = std::move(content_slide_queue_.front());
            content_slide_queue_.pop_front();
            title_dirty_ = true;
            content_slide_offset_x_ = 0.0;
            content_slide_target_x_ = -static_cast<double>(content_slide_dir_ * BoxViewport::kViewportWidth);
        } else {
            incoming_model_ = {};
            content_slide_active_ = false;
            content_slide_dir_ = 0;
            content_slide_offset_x_ = 0.0;
            content_slide_target_x_ = 0.0;
        }
    }
}

void BoxViewport::renderBelowNamePlate(SDL_Renderer* renderer) const {
    refreshTitleTexture(renderer);

    const int vx = viewport_x_;
    const int vy = viewport_y_;

    fillRoundedRectScanlines(renderer, vx, vy, BoxViewport::kViewportWidth, BoxViewport::kViewportHeight, kViewportCornerRadius,
        kViewportBg);

    const int pill_x = vx + (BoxViewport::kViewportWidth - kNamePillW) / 2;
    const int pill_y = vy + kNameTopPad;

    const int grid_w = kCols * kSlotW + (kCols - 1) * kSlotGapX;
    const int grid_h = kRows * kSlotH + (kRows - 1) * kSlotGapY;
    const int grid_x = vx + (BoxViewport::kViewportWidth - grid_w) / 2;
    const int grid_y = pill_y + kNamePillH + kNameToGridGap;

    const int clip_pad = spriteGridClipPadding(style_);
    const SDL_Rect grid_clip =
        computeSpriteGridClipRect(vx, vy, grid_x, grid_y, grid_w, grid_h, clip_pad, content_slide_active_);
    SDL_RenderSetClipRect(renderer, &grid_clip);

    // Two passes so overflow sprites paint above every slot’s chrome (neighbors’ rounded rects included).
    auto draw_slot_backgrounds_only = [&](int dx) {
        for (int row = 0; row < kRows; ++row) {
            for (int col = 0; col < kCols; ++col) {
                const int sx = grid_x + col * (kSlotW + kSlotGapX) + dx;
                const int sy = grid_y + row * (kSlotH + kSlotGapY);
                fillRoundedRectScanlines(renderer, sx, sy, kSlotW, kSlotH, kSlotCornerRadius, kSlotBg);
            }
        }
    };

    auto draw_slot_sprites_only = [&](const BoxViewportModel& m, int dx) {
        const bool box_space_grid = (header_mode_ == HeaderMode::BoxSpace);
        const double sprite_scale = box_space_grid ? style_.box_space_sprite_scale : style_.sprite_scale;
        const int sprite_off_x = box_space_grid ? style_.box_space_sprite_offset_x : 0;
        const int sprite_off_y = box_space_grid ? style_.box_space_sprite_offset_y : style_.sprite_offset_y;
        for (int row = 0; row < kRows; ++row) {
            for (int col = 0; col < kCols; ++col) {
                const int sx = grid_x + col * (kSlotW + kSlotGapX) + dx;
                const int sy = grid_y + row * (kSlotH + kSlotGapY);
                const std::size_t idx = static_cast<std::size_t>(row * kCols + col);
                if (idx < m.slot_sprites.size()) {
                    const auto& slot = m.slot_sprites[idx];
                    if (slot.has_value() && slot->texture) {
                        drawTextureCenteredScaledRaw(
                            renderer,
                            *slot,
                            sx + kSlotW / 2 + sprite_off_x,
                            sy + kSlotH / 2 + sprite_off_y,
                            sprite_scale);
                    }
                }
            }
        }
    };

    const int base_dx = static_cast<int>(std::lround(content_slide_offset_x_));
    if (!content_slide_active_) {
        draw_slot_backgrounds_only(0);
        draw_slot_sprites_only(model_, 0);
    } else {
        const int incoming_dx = content_slide_dir_ * BoxViewport::kViewportWidth;
        draw_slot_backgrounds_only(base_dx);
        draw_slot_backgrounds_only(base_dx + incoming_dx);
        draw_slot_sprites_only(model_, base_dx);
        draw_slot_sprites_only(incoming_model_, base_dx + incoming_dx);
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    const int grid_bottom = grid_y + grid_h;
    const int footer_row_y = grid_bottom + kFooterBelowSlots;

    const int icon_y = footer_row_y + (kBoxSpaceBtnH - kGameIconSize) / 2;

    auto draw_game_icon_at = [&](int icon_left_x) {
        if (game_icon_tex_.texture) {
            SDL_Rect idst{icon_left_x, icon_y, kGameIconSize, kGameIconSize};
            SDL_RenderCopy(renderer, game_icon_tex_.texture.get(), nullptr, &idst);
        } else {
            fillRoundedRectScanlines(renderer, icon_left_x, icon_y, kGameIconSize, kGameIconSize, 8, kSlotBg);
        }
    };

    auto draw_box_space_button = [&](int btn_left_x) {
        const int btn_y = footer_row_y;
        const Color active_fill{46, 176, 92, 255};
        const Color active_underline{36, 150, 78, 255};
        const bool active = box_space_active_;
        const Color fill = active ? active_fill : kButtonMain;
        const Color underline = active ? active_underline : kButtonUnderline;
        fillRoundedRectScanlines(renderer, btn_left_x, btn_y, kBoxSpaceBtnW, kBoxSpaceBtnH - kButtonStripH, 4, fill);
        SDL_Rect under{btn_left_x, btn_y + kBoxSpaceBtnH - kButtonStripH, kBoxSpaceBtnW, kButtonStripH};
        setDrawColor(renderer, underline);
        SDL_RenderFillRect(renderer, &under);

        const TextureHandle& label = active ? box_space_label_tex_white_ : box_space_label_tex_;
        if (label.texture) {
            const int bx = btn_left_x + kBoxSpaceBtnW / 2 - label.width / 2;
            const int by = btn_y + (kBoxSpaceBtnH - kButtonStripH) / 2 - label.height / 2;
            SDL_Rect bd{bx, by, label.width, label.height};
            SDL_RenderCopy(renderer, label.texture.get(), nullptr, &bd);
        }
    };

    if (role_ == BoxViewportRole::ResortStorage) {
        draw_game_icon_at(vx + kFooterEdgePad);
        draw_box_space_button(vx + BoxViewport::kViewportWidth - kFooterEdgePad - kBoxSpaceBtnW);
    } else {
        draw_box_space_button(vx + kFooterEdgePad);
        draw_game_icon_at(vx + BoxViewport::kViewportWidth - kFooterEdgePad - kGameIconSize);
    }

    if (arrow_tex_.texture) {
        if (role_ == BoxViewportRole::ResortStorage) {
            const int grid_mid_x = grid_x + grid_w / 2;
            const int scroll_cy =
                grid_bottom + kScrollBelowSlots + arrow_tex_.height / 2 + style_.footer_scroll_arrow_offset_y;
            // Left-pointing asset; counter-clockwise 90° is downward (SDL angle is clockwise, so use -90°).
            drawArrowRotated(renderer, arrow_tex_, grid_mid_x, scroll_cy, -90.0, style_.arrow_mod_color);
        } else if (role_ == BoxViewportRole::ExternalGameSave && header_mode_ == HeaderMode::BoxSpace &&
                   box_space_scroll_arrow_visible_) {
            const int grid_mid_x = grid_x + grid_w / 2;
            const int scroll_cy =
                grid_bottom + kScrollBelowSlots + arrow_tex_.height / 2 + style_.footer_scroll_arrow_offset_y;
            drawArrowRotated(renderer, arrow_tex_, grid_mid_x, scroll_cy, -90.0, style_.arrow_mod_color);
        }
    }
}

void BoxViewport::renderNamePlate(SDL_Renderer* renderer) const {
    const int vx = viewport_x_;
    const int vy = viewport_y_;
    const int pill_x = vx + (BoxViewport::kViewportWidth - kNamePillW) / 2;
    const int pill_y = vy + kNameTopPad;

    fillRoundedRectScanlines(renderer, pill_x, pill_y, kNamePillW, kNamePillH, kPillCornerRadius, kPillBg);

    if (cached_title_tex_.texture) {
        const int tcx = pill_x + kNamePillW / 2 - cached_title_tex_.width / 2;
        const int tcy = pill_y + kNamePillH / 2 - cached_title_tex_.height / 2;
        SDL_Rect td{tcx, tcy, cached_title_tex_.width, cached_title_tex_.height};
        SDL_RenderCopy(renderer, cached_title_tex_.texture.get(), nullptr, &td);
    }

    const int pill_cy = pill_y + kNamePillH / 2;
    if (arrow_tex_.texture && header_mode_ == HeaderMode::Normal) {
        const int left_cx = pill_x - kNameToArrowGap - arrow_tex_.width / 2;
        drawArrowRotated(renderer, arrow_tex_, left_cx, pill_cy, 0.0, style_.arrow_mod_color);
        const int right_cx = pill_x + kNamePillW + kNameToArrowGap + arrow_tex_.width / 2;
        drawArrowRotated(renderer, arrow_tex_, right_cx, pill_cy, 180.0, style_.arrow_mod_color);
    }
}

void BoxViewport::render(SDL_Renderer* renderer) const {
    renderBelowNamePlate(renderer);
    renderNamePlate(renderer);
}

} // namespace pr
