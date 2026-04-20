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
      title_font_(loadFont(font_path, std::max(1, style_.box_name_font_pt), project_root)),
      label_font_(loadFont(font_path, std::max(1, style_.box_space_font_pt), project_root)) {
    const fs::path arrow_path = fs::path(project_root_) / style_.arrow_texture;
    try {
        arrow_tex_ = loadTextureOrThrow(renderer, arrow_path);
    } catch (const std::exception& ex) {
        std::cerr << "[BoxViewport] arrow texture load failed (" << arrow_path << "): " << ex.what() << '\n';
    }
    box_space_label_tex_ = renderTextTexture(renderer, label_font_.get(), "Box space", style_.box_space_color);
}

void BoxViewport::setViewportOrigin(int viewport_x, int viewport_y) {
    viewport_x_ = viewport_x;
    viewport_y_ = viewport_y;
}

void BoxViewport::setModel(BoxViewportModel model) {
    if (model.box_name != model_.box_name) {
        title_dirty_ = true;
    }
    model_ = std::move(model);
}

void BoxViewport::refreshTitleTexture(SDL_Renderer* renderer) const {
    const std::string& name = model_.box_name.empty() ? std::string("BOX 1") : model_.box_name;
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

void BoxViewport::render(SDL_Renderer* renderer) const {
    refreshTitleTexture(renderer);

    const int vx = viewport_x_;
    const int vy = viewport_y_;

    fillRoundedRectScanlines(renderer, vx, vy, BoxViewport::kViewportWidth, BoxViewport::kViewportHeight, kViewportCornerRadius,
        kViewportBg);

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
    if (arrow_tex_.texture) {
        const int left_cx = pill_x - kNameToArrowGap - arrow_tex_.width / 2;
        drawArrowRotated(renderer, arrow_tex_, left_cx, pill_cy, 0.0, style_.arrow_mod_color);
        const int right_cx = pill_x + kNamePillW + kNameToArrowGap + arrow_tex_.width / 2;
        drawArrowRotated(renderer, arrow_tex_, right_cx, pill_cy, 180.0, style_.arrow_mod_color);
    }

    const int grid_w = kCols * kSlotW + (kCols - 1) * kSlotGapX;
    const int grid_h = kRows * kSlotH + (kRows - 1) * kSlotGapY;
    const int grid_x = vx + (BoxViewport::kViewportWidth - grid_w) / 2;
    const int grid_y = pill_y + kNamePillH + kNameToGridGap;

    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            const int sx = grid_x + col * (kSlotW + kSlotGapX);
            const int sy = grid_y + row * (kSlotH + kSlotGapY);
            fillRoundedRectScanlines(renderer, sx, sy, kSlotW, kSlotH, kSlotCornerRadius, kSlotBg);
            const std::size_t idx = static_cast<std::size_t>(row * kCols + col);
            if (idx < model_.slot_sprites.size()) {
                const auto& slot = model_.slot_sprites[idx];
                if (slot.has_value() && slot->texture) {
                    drawTextureCentered(renderer, *slot, sx + kSlotW / 2, sy + kSlotH / 2, kSlotW - 4, kSlotH - 4);
                }
            }
        }
    }

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
        fillRoundedRectScanlines(renderer, btn_left_x, btn_y, kBoxSpaceBtnW, kBoxSpaceBtnH - kButtonStripH, 4, kButtonMain);
        SDL_Rect under{btn_left_x, btn_y + kBoxSpaceBtnH - kButtonStripH, kBoxSpaceBtnW, kButtonStripH};
        setDrawColor(renderer, kButtonUnderline);
        SDL_RenderFillRect(renderer, &under);

        if (box_space_label_tex_.texture) {
            const int bx = btn_left_x + kBoxSpaceBtnW / 2 - box_space_label_tex_.width / 2;
            const int by = btn_y + (kBoxSpaceBtnH - kButtonStripH) / 2 - box_space_label_tex_.height / 2;
            SDL_Rect bd{bx, by, box_space_label_tex_.width, box_space_label_tex_.height};
            SDL_RenderCopy(renderer, box_space_label_tex_.texture.get(), nullptr, &bd);
        }
    };

    if (role_ == BoxViewportRole::ResortStorage) {
        draw_game_icon_at(vx + kFooterEdgePad);
        draw_box_space_button(vx + BoxViewport::kViewportWidth - kFooterEdgePad - kBoxSpaceBtnW);
    } else {
        draw_box_space_button(vx + kFooterEdgePad);
        draw_game_icon_at(vx + BoxViewport::kViewportWidth - kFooterEdgePad - kGameIconSize);
    }

    if (role_ == BoxViewportRole::ResortStorage && arrow_tex_.texture) {
        const int grid_mid_x = grid_x + grid_w / 2;
        const int scroll_cy =
            grid_bottom + kScrollBelowSlots + arrow_tex_.height / 2 + style_.footer_scroll_arrow_offset_y;
        // Left-pointing asset; counter-clockwise 90° is downward (SDL angle is clockwise, so use -90°).
        drawArrowRotated(renderer, arrow_tex_, grid_mid_x, scroll_cy, -90.0, style_.arrow_mod_color);
    }
}

} // namespace pr
