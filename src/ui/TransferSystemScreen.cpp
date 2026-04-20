#include "ui/TransferSystemScreen.hpp"

#include "core/Assets.hpp"
#include "core/Json.hpp"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace fs = std::filesystem;

namespace pr {

namespace {

constexpr int kLeftBoxColumnX = 40;
constexpr int kBoxViewportY = 100;

fs::path resolvePath(const std::string& root, const std::string& configured) {
    fs::path path(configured);
    return path.is_absolute() ? path : (fs::path(root) / path);
}

TextureHandle loadTexture(SDL_Renderer* renderer, const fs::path& path) {
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        throw std::runtime_error("Failed to load texture: " + path.string() + " | " + IMG_GetError());
    }

    TextureHandle texture;
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        throw std::runtime_error("Failed to query texture: " + path.string() + " | " + SDL_GetError());
    }
    return texture;
}

TextureHandle loadTextureOptional(SDL_Renderer* renderer, const fs::path& path) {
    TextureHandle texture;
    if (!fs::exists(path)) {
        return texture;
    }
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (!raw) {
        std::cerr << "Warning: failed to load texture: " << path.string() << " | " << IMG_GetError() << '\n';
        return texture;
    }
    texture.texture.reset(raw, SDL_DestroyTexture);
    if (SDL_QueryTexture(texture.texture.get(), nullptr, nullptr, &texture.width, &texture.height) != 0) {
        texture.texture.reset();
        return texture;
    }
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    return texture;
}

double doubleFromObjectOrDefault(const JsonValue& obj, const std::string& key, double fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asNumber() : fallback;
}

bool boolFromObjectOrDefault(const JsonValue& obj, const std::string& key, bool fallback) {
    const JsonValue* value = obj.get(key);
    return value ? value->asBool() : fallback;
}

int intFromObjectOrDefault(const JsonValue& obj, const std::string& key, int fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

std::string stringFromObjectOrDefault(const JsonValue& obj, const std::string& key, const std::string& fallback) {
    const JsonValue* value = obj.get(key);
    return value && value->isString() ? value->asString() : fallback;
}

Color parseHexColorString(const std::string& value, const Color& fallback) {
    if (value.size() != 7 || value[0] != '#') {
        return fallback;
    }
    try {
        const auto parse_component = [&](std::size_t offset) -> int {
            return std::stoi(value.substr(offset, 2), nullptr, 16);
        };
        return Color{parse_component(1), parse_component(3), parse_component(5), 255};
    } catch (...) {
        return fallback;
    }
}

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

void fillRoundedRingScanlines(
    SDL_Renderer* renderer,
    int x,
    int y,
    int w,
    int h,
    int outer_radius,
    int stroke,
    const Color& border,
    const Color& inner_fill) {
    if (w <= 0 || h <= 0 || stroke <= 0 || stroke * 2 >= w || stroke * 2 >= h) {
        return;
    }
    fillRoundedRectScanlines(renderer, x, y, w, h, outer_radius, border);
    const int inner_r = std::max(0, outer_radius - stroke);
    fillRoundedRectScanlines(renderer, x + stroke, y + stroke, w - 2 * stroke, h - 2 * stroke, inner_r, inner_fill);
}

/// Critically damped–style smoothing: approaches `target` faster when far, eases in at the end (inertial feel).
void approachExponential(double& v, double target, double dt, double lambda) {
    if (lambda <= 1e-9) {
        v = target;
        return;
    }
    const double alpha = 1.0 - std::exp(-lambda * dt);
    v += (target - v) * alpha;
    if (std::fabs(target - v) < 0.0005) {
        v = target;
    }
}

int carouselIconClipInset(const GameTransferToolCarouselStyle& st, int vw, int vh, int radius) {
    if (st.viewport_clip_inset > 0) {
        return std::clamp(st.viewport_clip_inset, 1, std::max(1, std::min(vw, vh) / 2 - 1));
    }
    return std::clamp(radius, 1, std::max(1, std::min(vw, vh) / 2 - 1));
}

void getPillTrackBounds(const GameTransferPillToggleStyle& st, int screen_w, int& tx, int& ty, int& tw, int& th) {
    const int right_col_x = screen_w - 40 - BoxViewport::kViewportWidth;
    tx = right_col_x + (BoxViewport::kViewportWidth - st.track_width) / 2;
    ty = kBoxViewportY - st.gap_above_boxes - st.track_height;
    tw = st.track_width;
    th = st.track_height;
}

struct BackgroundAnimLoaded {
    bool enabled = false;
    double scale = 1.0;
    double speed_x = 0.0;
    double speed_y = 0.0;
};

struct LoadedGameTransfer {
    double fade_in_seconds = 0;
    double fade_out_seconds = 0.12;
    BackgroundAnimLoaded background_animation{};
    GameTransferBoxViewportStyle box_viewport{};
    GameTransferPillToggleStyle pill_toggle{};
    GameTransferToolCarouselStyle tool_carousel{};
};

LoadedGameTransfer loadGameTransfer(const std::string& project_root) {
    LoadedGameTransfer out;
    const fs::path path = fs::path(project_root) / "config" / "game_transfer.json";
    if (!fs::exists(path)) {
        return out;
    }

    JsonValue root = parseJsonFile(path.string());
    if (!root.isObject()) {
        return out;
    }

    if (const JsonValue* fade = root.get("fade_in_seconds")) {
        if (fade->isNumber()) {
            out.fade_in_seconds = std::max(0.0, fade->asNumber());
        }
    }
    if (const JsonValue* fade = root.get("fade_out_seconds")) {
        if (fade->isNumber()) {
            out.fade_out_seconds = std::max(0.0, fade->asNumber());
        }
    }
    if (const JsonValue* fade = root.get("fade")) {
        if (fade->isObject()) {
            out.fade_in_seconds = std::max(0.0, doubleFromObjectOrDefault(*fade, "in_seconds", out.fade_in_seconds));
            out.fade_out_seconds = std::max(0.0, doubleFromObjectOrDefault(*fade, "out_seconds", out.fade_out_seconds));
        }
    }

    if (const JsonValue* background_animation = root.get("background_animation")) {
        if (background_animation->isObject()) {
            out.background_animation.enabled = boolFromObjectOrDefault(
                *background_animation,
                "enabled",
                out.background_animation.enabled);
            out.background_animation.scale = std::max(
                0.01,
                doubleFromObjectOrDefault(
                    *background_animation,
                    "scale",
                    out.background_animation.scale));
            out.background_animation.speed_x = doubleFromObjectOrDefault(
                *background_animation,
                "speed_x",
                out.background_animation.speed_x);
            out.background_animation.speed_y = doubleFromObjectOrDefault(
                *background_animation,
                "speed_y",
                out.background_animation.speed_y);
        }
    }

    if (const JsonValue* bv = root.get("box_viewport")) {
        if (bv->isObject()) {
            const JsonValue& o = *bv;
            out.box_viewport.arrow_texture =
                stringFromObjectOrDefault(o, "arrow_texture", out.box_viewport.arrow_texture);
            if (const JsonValue* c = o.get("arrow_mod_color")) {
                if (c->isString()) {
                    out.box_viewport.arrow_mod_color =
                        parseHexColorString(c->asString(), out.box_viewport.arrow_mod_color);
                }
            }
            out.box_viewport.box_name_font_pt =
                intFromObjectOrDefault(o, "box_name_font_pt", out.box_viewport.box_name_font_pt);
            if (const JsonValue* c = o.get("box_name_color")) {
                if (c->isString()) {
                    out.box_viewport.box_name_color =
                        parseHexColorString(c->asString(), out.box_viewport.box_name_color);
                }
            }
            out.box_viewport.box_space_font_pt =
                intFromObjectOrDefault(o, "box_space_font_pt", out.box_viewport.box_space_font_pt);
            if (const JsonValue* c = o.get("box_space_color")) {
                if (c->isString()) {
                    out.box_viewport.box_space_color =
                        parseHexColorString(c->asString(), out.box_viewport.box_space_color);
                }
            }
            out.box_viewport.footer_scroll_arrow_offset_y =
                intFromObjectOrDefault(o, "footer_scroll_arrow_offset_y", out.box_viewport.footer_scroll_arrow_offset_y);
        }
    }

    if (const JsonValue* pt = root.get("pill_toggle")) {
        if (pt->isObject()) {
            const JsonValue& o = *pt;
            out.pill_toggle.track_width = intFromObjectOrDefault(o, "track_width", out.pill_toggle.track_width);
            out.pill_toggle.track_height = intFromObjectOrDefault(o, "track_height", out.pill_toggle.track_height);
            out.pill_toggle.pill_width = intFromObjectOrDefault(o, "pill_width", out.pill_toggle.pill_width);
            out.pill_toggle.pill_height = intFromObjectOrDefault(o, "pill_height", out.pill_toggle.pill_height);
            out.pill_toggle.pill_inset = intFromObjectOrDefault(o, "pill_inset", out.pill_toggle.pill_inset);
            if (const JsonValue* gab = o.get("gap_above_boxes")) {
                if (gab->isNumber()) {
                    out.pill_toggle.gap_above_boxes = static_cast<int>(gab->asNumber());
                }
            } else {
                out.pill_toggle.gap_above_boxes =
                    intFromObjectOrDefault(o, "gap_above_left_box", out.pill_toggle.gap_above_boxes);
            }
            out.pill_toggle.font_pt = intFromObjectOrDefault(o, "font_pt", out.pill_toggle.font_pt);
            if (const JsonValue* c = o.get("track_color")) {
                if (c->isString()) {
                    out.pill_toggle.track_color = parseHexColorString(c->asString(), out.pill_toggle.track_color);
                }
            }
            if (const JsonValue* c = o.get("pill_color")) {
                if (c->isString()) {
                    out.pill_toggle.pill_color = parseHexColorString(c->asString(), out.pill_toggle.pill_color);
                }
            }
            out.pill_toggle.toggle_smoothing =
                doubleFromObjectOrDefault(o, "toggle_smoothing", out.pill_toggle.toggle_smoothing);
            out.pill_toggle.box_smoothing = doubleFromObjectOrDefault(o, "box_smoothing", out.pill_toggle.box_smoothing);
            if (!o.get("toggle_smoothing")) {
                const double legacy_dur =
                    doubleFromObjectOrDefault(o, "toggle_slide_duration_seconds", 0.18);
                if (legacy_dur > 1e-9) {
                    out.pill_toggle.toggle_smoothing = 5.0 / legacy_dur;
                }
            }
            if (!o.get("box_smoothing")) {
                const double legacy_dur =
                    doubleFromObjectOrDefault(o, "box_panel_slide_duration_seconds", 0.42);
                if (legacy_dur > 1e-9) {
                    out.pill_toggle.box_smoothing = 4.0 / legacy_dur;
                }
            }
        }
    }

    if (const JsonValue* tc = root.get("tool_carousel")) {
        if (tc->isObject()) {
            const JsonValue& o = *tc;
            out.tool_carousel.viewport_width =
                intFromObjectOrDefault(o, "viewport_width", out.tool_carousel.viewport_width);
            out.tool_carousel.viewport_height =
                intFromObjectOrDefault(o, "viewport_height", out.tool_carousel.viewport_height);
            out.tool_carousel.offset_from_left_wall =
                intFromObjectOrDefault(o, "offset_from_left_wall", out.tool_carousel.offset_from_left_wall);
            out.tool_carousel.rest_y = intFromObjectOrDefault(o, "rest_y", out.tool_carousel.rest_y);
            out.tool_carousel.hidden_y = intFromObjectOrDefault(o, "hidden_y", out.tool_carousel.hidden_y);
            out.tool_carousel.viewport_corner_radius =
                intFromObjectOrDefault(o, "viewport_corner_radius", out.tool_carousel.viewport_corner_radius);
            out.tool_carousel.viewport_clip_inset =
                intFromObjectOrDefault(o, "viewport_clip_inset", out.tool_carousel.viewport_clip_inset);
            if (const JsonValue* c = o.get("viewport_color")) {
                if (c->isString()) {
                    out.tool_carousel.viewport_color =
                        parseHexColorString(c->asString(), out.tool_carousel.viewport_color);
                }
            }
            out.tool_carousel.icon_size = intFromObjectOrDefault(o, "icon_size", out.tool_carousel.icon_size);
            out.tool_carousel.selection_frame_size =
                intFromObjectOrDefault(o, "selection_frame_size", out.tool_carousel.selection_frame_size);
            out.tool_carousel.selection_stroke =
                intFromObjectOrDefault(o, "selection_stroke", out.tool_carousel.selection_stroke);
            if (const JsonValue* s = o.get("selector_size")) {
                if (s->isNumber()) {
                    out.tool_carousel.selection_frame_size = static_cast<int>(s->asNumber());
                }
            }
            if (const JsonValue* s = o.get("selector_thickness")) {
                if (s->isNumber()) {
                    out.tool_carousel.selection_stroke = static_cast<int>(s->asNumber());
                }
            }
            out.tool_carousel.selector_corner_radius =
                intFromObjectOrDefault(o, "selector_corner_radius", out.tool_carousel.selector_corner_radius);
            out.tool_carousel.slide_span_pixels =
                intFromObjectOrDefault(o, "slide_span_pixels", out.tool_carousel.slide_span_pixels);
            out.tool_carousel.belt_spacing_pixels =
                intFromObjectOrDefault(o, "belt_spacing_pixels", out.tool_carousel.belt_spacing_pixels);
            out.tool_carousel.slide_smoothing = doubleFromObjectOrDefault(o, "slide_smoothing", out.tool_carousel.slide_smoothing);
            out.tool_carousel.slot_center_left =
                intFromObjectOrDefault(o, "slot_center_left", out.tool_carousel.slot_center_left);
            out.tool_carousel.slot_center_middle =
                intFromObjectOrDefault(o, "slot_center_middle", out.tool_carousel.slot_center_middle);
            out.tool_carousel.slot_center_right =
                intFromObjectOrDefault(o, "slot_center_right", out.tool_carousel.slot_center_right);
            out.tool_carousel.texture_multiple =
                stringFromObjectOrDefault(o, "texture_multiple", out.tool_carousel.texture_multiple);
            out.tool_carousel.texture_basic =
                stringFromObjectOrDefault(o, "texture_basic", out.tool_carousel.texture_basic);
            out.tool_carousel.texture_swap =
                stringFromObjectOrDefault(o, "texture_swap", out.tool_carousel.texture_swap);
            out.tool_carousel.texture_items =
                stringFromObjectOrDefault(o, "texture_items", out.tool_carousel.texture_items);
            if (const JsonValue* c = o.get("frame_multiple")) {
                if (c->isString()) {
                    out.tool_carousel.frame_multiple =
                        parseHexColorString(c->asString(), out.tool_carousel.frame_multiple);
                }
            }
            if (const JsonValue* c = o.get("frame_basic")) {
                if (c->isString()) {
                    out.tool_carousel.frame_basic = parseHexColorString(c->asString(), out.tool_carousel.frame_basic);
                }
            }
            if (const JsonValue* c = o.get("frame_swap")) {
                if (c->isString()) {
                    out.tool_carousel.frame_swap = parseHexColorString(c->asString(), out.tool_carousel.frame_swap);
                }
            }
            if (const JsonValue* c = o.get("frame_items")) {
                if (c->isString()) {
                    out.tool_carousel.frame_items = parseHexColorString(c->asString(), out.tool_carousel.frame_items);
                }
            }
        }
    }

    return out;
}

} // namespace

TransferSystemScreen::TransferSystemScreen(
    SDL_Renderer* renderer,
    const WindowConfig& window_config,
    const std::string& font_path,
    const std::string& project_root)
    : window_config_(window_config),
      project_root_(project_root),
      font_path_(font_path),
      background_(loadTexture(
          renderer,
          resolvePath(project_root_, "assets/transfer_select_save/background.png"))) {
    const LoadedGameTransfer loaded = loadGameTransfer(project_root_);
    fade_in_seconds_ = loaded.fade_in_seconds;
    fade_out_seconds_ = loaded.fade_out_seconds;
    background_animation_.enabled = loaded.background_animation.enabled;
    background_animation_.scale = loaded.background_animation.scale;
    background_animation_.speed_x = loaded.background_animation.speed_x;
    background_animation_.speed_y = loaded.background_animation.speed_y;
    pill_style_ = loaded.pill_toggle;
    carousel_style_ = loaded.tool_carousel;
    pill_font_ = loadFont(font_path_, std::max(8, pill_style_.font_pt), project_root_);
    cachePillLabelTextures(renderer);

    const std::array<std::string, 4> tool_paths{
        carousel_style_.texture_multiple,
        carousel_style_.texture_basic,
        carousel_style_.texture_swap,
        carousel_style_.texture_items};
    for (std::size_t i = 0; i < tool_paths.size(); ++i) {
        tool_icons_[i] = loadTextureOptional(renderer, resolvePath(project_root_, tool_paths[i]));
    }

    constexpr int kBoxMarginX = 40;
    const int game_box_x = std::max(0, window_config_.virtual_width - kBoxMarginX - BoxViewport::kViewportWidth);
    resort_box_viewport_ = std::make_unique<BoxViewport>(
        renderer,
        project_root,
        font_path,
        loaded.box_viewport,
        BoxViewportRole::ResortStorage,
        kBoxMarginX,
        kBoxViewportY);
    game_save_box_viewport_ = std::make_unique<BoxViewport>(
        renderer,
        project_root,
        font_path,
        loaded.box_viewport,
        BoxViewportRole::ExternalGameSave,
        game_box_x,
        kBoxViewportY);
}

void TransferSystemScreen::cachePillLabelTextures(SDL_Renderer* renderer) {
    const Color black{0, 0, 0, 255};
    const Color white{255, 255, 255, 255};
    pill_label_pokemon_black_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", black);
    pill_label_items_black_ = renderTextTexture(renderer, pill_font_.get(), "Items", black);
    pill_label_pokemon_white_ = renderTextTexture(renderer, pill_font_.get(), "Pokemon", white);
    pill_label_items_white_ = renderTextTexture(renderer, pill_font_.get(), "Items", white);
}

void TransferSystemScreen::enter(const TransferSaveSelection& selection, SDL_Renderer* renderer) {
    (void)renderer;
    return_to_ticket_list_requested_ = false;
    play_button_sfx_requested_ = false;
    elapsed_seconds_ = 0.0;
    slider_t_ = 0.0;
    slider_target_ = 0.0;
    panels_reveal_ = 0.0;
    panels_target_ = 1.0;
    selected_tool_index_ = 1;
    carousel_slide_offset_x_ = 0.0;
    carousel_slide_target_x_ = 0.0;
    ui_enter_ = 0.0;
    ui_enter_target_ = 1.0;
    bottom_banner_reveal_ = 0.0;
    bottom_banner_target_ = 1.0;
    exit_in_progress_ = false;
    exit_fade_seconds_ = 0.0;

    if (resort_box_viewport_) {
        resort_box_viewport_->setModel(BoxViewportModel{});
        resort_box_viewport_->reloadResortIcon(renderer);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setModel(BoxViewportModel{});
        game_save_box_viewport_->reloadGameIcon(renderer, selection.game_key);
    }
    syncBoxViewportPositions();
}

void TransferSystemScreen::updateAnimations(double dt) {
    approachExponential(slider_t_, slider_target_, dt, pill_style_.toggle_smoothing);
    approachExponential(panels_reveal_, panels_target_, dt, pill_style_.box_smoothing);
}

void TransferSystemScreen::updateEnterExit(double dt) {
    // Use the same smoothing family as the rest of the screen.
    constexpr double kEnterSmoothing = 14.0;
    constexpr double kBannerSmoothing = 12.0;
    approachExponential(ui_enter_, ui_enter_target_, dt, kEnterSmoothing);
    approachExponential(bottom_banner_reveal_, bottom_banner_target_, dt, kBannerSmoothing);

    if (exit_in_progress_) {
        exit_fade_seconds_ += dt;
        const bool ui_gone = ui_enter_ < 0.02 && bottom_banner_reveal_ < 0.02 && panels_reveal_ < 0.02;
        const bool fade_done =
            (fade_out_seconds_ <= 1e-6) || (exit_fade_seconds_ >= fade_out_seconds_);
        if (ui_gone && fade_done && !return_to_ticket_list_requested_) {
            requestReturnToTicketList();
        }
    }
}

void TransferSystemScreen::updateCarouselSlide(double dt) {
    if (std::fabs(carousel_slide_target_x_) < 1e-9) {
        if (std::fabs(carousel_slide_offset_x_) < 1e-4) {
            carousel_slide_offset_x_ = 0.0;
        }
        return;
    }
    const double lambda = std::max(1.0, carousel_style_.slide_smoothing);
    approachExponential(carousel_slide_offset_x_, carousel_slide_target_x_, dt, lambda);
    const double tgt = carousel_slide_target_x_;
    if (std::fabs(tgt) < 1e-9) {
        return;
    }
    if (std::fabs(carousel_slide_offset_x_ - tgt) < 0.75) {
        if (tgt < 0.0) {
            selected_tool_index_ = (selected_tool_index_ + 1) % 4;
        } else {
            selected_tool_index_ = (selected_tool_index_ + 3) % 4;
        }
        carousel_slide_offset_x_ = 0.0;
        carousel_slide_target_x_ = 0.0;
    }
}

void TransferSystemScreen::syncBoxViewportPositions() {
    const int screen_w = window_config_.virtual_width;
    const int resort_hidden_x = -BoxViewport::kViewportWidth;
    const int game_hidden_x = screen_w;
    const int resort_rest_x = kLeftBoxColumnX;
    const int game_rest_x = screen_w - 40 - BoxViewport::kViewportWidth;

    const int resort_x =
        static_cast<int>(std::round(resort_hidden_x + (resort_rest_x - resort_hidden_x) * panels_reveal_));
    const int game_x =
        static_cast<int>(std::round(game_hidden_x + (game_rest_x - game_hidden_x) * panels_reveal_));

    if (resort_box_viewport_) {
        resort_box_viewport_->setViewportOrigin(resort_x, kBoxViewportY);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->setViewportOrigin(game_x, kBoxViewportY);
    }
}

void TransferSystemScreen::update(double dt) {
    elapsed_seconds_ += dt;
    updateAnimations(dt);
    updateEnterExit(dt);
    updateCarouselSlide(dt);
    syncBoxViewportPositions();
}

void TransferSystemScreen::togglePillTarget() {
    if (slider_target_ < 0.5) {
        slider_target_ = 1.0;
        panels_target_ = 0.0;
    } else {
        slider_target_ = 0.0;
        panels_target_ = 1.0;
    }
    play_button_sfx_requested_ = true;
}

bool TransferSystemScreen::hitTestPillTrack(int logical_x, int logical_y) const {
    int tx = 0;
    int ty = 0;
    int tw = 0;
    int th = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, tx, ty, tw, th);
    // Pill enters from above on first open / exit only.
    const int enter_off = static_cast<int>(std::lround((1.0 - ui_enter_) * static_cast<double>(-(th + 24))));
    ty += enter_off;
    return logical_x >= tx && logical_x < tx + tw && logical_y >= ty && logical_y < ty + th;
}

int TransferSystemScreen::carouselScreenY() const {
    const double t = panels_reveal_;
    const double y = static_cast<double>(carousel_style_.rest_y) +
        (1.0 - t) * static_cast<double>(carousel_style_.hidden_y - carousel_style_.rest_y);
    return static_cast<int>(std::round(y));
}

Color TransferSystemScreen::carouselFrameColorForIndex(int tool_index) const {
    switch (tool_index) {
        case 0:
            return carousel_style_.frame_multiple;
        case 1:
            return carousel_style_.frame_basic;
        case 2:
            return carousel_style_.frame_swap;
        case 3:
            return carousel_style_.frame_items;
        default:
            return carousel_style_.frame_basic;
    }
}

bool TransferSystemScreen::carouselSlideAnimating() const {
    return std::fabs(carousel_slide_target_x_) > 1e-4 || std::fabs(carousel_slide_offset_x_) > 1e-3;
}

void TransferSystemScreen::cycleToolCarousel(int dir) {
    if (carouselSlideAnimating()) {
        return;
    }
    int span = 0;
    if (dir > 0) {
        if (carousel_style_.slide_span_pixels > 0) {
            span = carousel_style_.slide_span_pixels;
        } else if (carousel_style_.belt_spacing_pixels > 0) {
            span = carousel_style_.belt_spacing_pixels;
        } else {
            span = carousel_style_.slot_center_right - carousel_style_.slot_center_middle;
        }
        if (span <= 0) {
            return;
        }
        carousel_slide_target_x_ = -static_cast<double>(span);
    } else {
        if (carousel_style_.slide_span_pixels > 0) {
            span = carousel_style_.slide_span_pixels;
        } else if (carousel_style_.belt_spacing_pixels > 0) {
            span = carousel_style_.belt_spacing_pixels;
        } else {
            span = carousel_style_.slot_center_middle - carousel_style_.slot_center_left;
        }
        if (span <= 0) {
            return;
        }
        carousel_slide_target_x_ = static_cast<double>(span);
    }
}

bool TransferSystemScreen::hitTestToolCarousel(int logical_x, int logical_y) const {
    const int vx = carousel_style_.offset_from_left_wall;
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    return logical_x >= vx && logical_x < vx + vw && logical_y >= vy && logical_y < vy + vh;
}

void TransferSystemScreen::drawToolCarousel(SDL_Renderer* renderer) const {
    const int vx = carousel_style_.offset_from_left_wall;
    const int vy = carouselScreenY();
    const int vw = carousel_style_.viewport_width;
    const int vh = carousel_style_.viewport_height;
    if (vw <= 0 || vh <= 0) {
        return;
    }

    const int radius =
        std::clamp(carousel_style_.viewport_corner_radius, 0, std::min(vw, vh) / 2);

    const int sel_i = selected_tool_index_;
    const int cy = vy + vh / 2;
    const int icon = std::max(1, carousel_style_.icon_size);

    /// Horizontal scroll: one belt with extra off-screen slots (clipped), no duplicate focal layer.
    const int scroll = static_cast<int>(std::lround(carousel_slide_offset_x_));
    const int focus_cx = vx + carousel_style_.slot_center_middle;
    const int pitch_l = carousel_style_.slot_center_middle - carousel_style_.slot_center_left;
    const int pitch_r = carousel_style_.slot_center_right - carousel_style_.slot_center_middle;

    auto strip_center_x_at_k = [&](int k) -> int {
        if (carousel_style_.belt_spacing_pixels > 0) {
            return focus_cx + k * carousel_style_.belt_spacing_pixels + scroll;
        }
        if (carousel_style_.slide_span_pixels > 0) {
            return focus_cx + k * carousel_style_.slide_span_pixels + scroll;
        }
        if (k == 0) {
            return focus_cx + scroll;
        }
        if (k < 0) {
            return focus_cx + k * pitch_l + scroll;
        }
        return focus_cx + k * pitch_r + scroll;
    };

    auto strip_tool_at_k = [&](int k) -> int {
        return ((sel_i + k) % 4 + 4) % 4;
    };

    // Fixed window panel (does not scroll with the strip).
    fillRoundedRectScanlines(renderer, vx, vy, vw, vh, radius, carousel_style_.viewport_color);

    const int clip_inset = carouselIconClipInset(carousel_style_, vw, vh, radius);
    const SDL_Rect viewport_clip{vx, vy, vw, vh};
    SDL_Rect inner_clip{vx + clip_inset, vy + clip_inset, vw - 2 * clip_inset, vh - 2 * clip_inset};
    if (inner_clip.w < icon * 2 || inner_clip.h < icon) {
        inner_clip = viewport_clip;
    }

    const int fs = std::max(carousel_style_.selection_frame_size, icon + 2);
    const int stroke = std::clamp(carousel_style_.selection_stroke, 1, fs / 2);
    int fr = carousel_style_.selector_corner_radius;
    if (fr <= 0) {
        fr = std::clamp(radius, 0, fs / 2);
    } else {
        fr = std::clamp(fr, 0, fs / 2);
    }

    auto draw_icon = [&](int tool_i, int center_x) {
        const TextureHandle& tex = tool_icons_[static_cast<std::size_t>(tool_i)];
        if (!tex.texture || tex.width <= 0) {
            return;
        }
        SDL_SetTextureColorMod(tex.texture.get(), 255, 255, 255);
        SDL_SetTextureAlphaMod(tex.texture.get(), 255);
        const int half = icon / 2;
        SDL_Rect dst{center_x - half, cy - half, icon, icon};
        SDL_RenderCopy(renderer, tex.texture.get(), nullptr, &dst);
    };

    // Five-slot belt: k=-2..2 already exists off-screen; clipping hides it until scroll brings it in.
    SDL_RenderSetClipRect(renderer, &inner_clip);
    for (int k = -2; k <= 2; ++k) {
        draw_icon(strip_tool_at_k(k), strip_center_x_at_k(k));
    }

    // Ring on top of the strip; inner fill clears the aperture — redraw only the belt slot nearest focus.
    SDL_RenderSetClipRect(renderer, &viewport_clip);
    const int fx = focus_cx - fs / 2;
    const int fy = cy - fs / 2;
    fillRoundedRingScanlines(
        renderer,
        fx,
        fy,
        fs,
        fs,
        fr,
        stroke,
        carouselFrameColorForIndex(sel_i),
        carousel_style_.viewport_color);

    int punch_cx = strip_center_x_at_k(0);
    int punch_tool = strip_tool_at_k(0);
    int best_d = std::abs(punch_cx - focus_cx);
    for (int k = -2; k <= 2; ++k) {
        const int cx = strip_center_x_at_k(k);
        const int d = std::abs(cx - focus_cx);
        if (d < best_d) {
            best_d = d;
            punch_cx = cx;
            punch_tool = strip_tool_at_k(k);
        }
    }
    SDL_RenderSetClipRect(renderer, &inner_clip);
    draw_icon(punch_tool, punch_cx);

    SDL_RenderSetClipRect(renderer, nullptr);
}

void TransferSystemScreen::drawPillToggle(SDL_Renderer* renderer) const {
    int track_x = 0;
    int track_y = 0;
    int track_w = 0;
    int track_h = 0;
    getPillTrackBounds(pill_style_, window_config_.virtual_width, track_x, track_y, track_w, track_h);
    const int enter_off = static_cast<int>(std::lround((1.0 - ui_enter_) * static_cast<double>(-(track_h + 24))));
    track_y += enter_off;

    const int pad = std::max(0, pill_style_.pill_inset);
    const int inner_x = track_x + pad;
    const int inner_y = track_y + pad;
    const int inner_w = std::max(0, track_w - 2 * pad);
    const int inner_h = std::max(0, track_h - 2 * pad);

    const int track_radius = std::min(track_h / 2, track_w / 2);
    fillRoundedRectScanlines(renderer, track_x, track_y, track_w, track_h, track_radius, pill_style_.track_color);

    const int pill_w = std::min(pill_style_.pill_width, inner_w);
    const int pill_h = std::min(pill_style_.pill_height, inner_h);
    const int max_travel = std::max(0, inner_w - pill_w);
    const int pill_x = inner_x + static_cast<int>(std::round(slider_t_ * static_cast<double>(max_travel)));
    const int pill_y = inner_y + (inner_h - pill_h) / 2;
    const int pill_radius = std::max(4, std::min(pill_h / 2, pill_w / 2));

    const int mid_x = inner_x + inner_w / 2;
    const int pokemon_cx = inner_x + inner_w / 4;
    const int items_cx = inner_x + (3 * inner_w) / 4;
    const int label_cy = inner_y + inner_h / 2;

    if (pill_label_pokemon_white_.texture) {
        SDL_Rect dr{
            pokemon_cx - pill_label_pokemon_white_.width / 2,
            label_cy - pill_label_pokemon_white_.height / 2,
            pill_label_pokemon_white_.width,
            pill_label_pokemon_white_.height};
        SDL_RenderCopy(renderer, pill_label_pokemon_white_.texture.get(), nullptr, &dr);
    }
    if (pill_label_items_white_.texture) {
        SDL_Rect dr{
            items_cx - pill_label_items_white_.width / 2,
            label_cy - pill_label_items_white_.height / 2,
            pill_label_items_white_.width,
            pill_label_items_white_.height};
        SDL_RenderCopy(renderer, pill_label_items_white_.texture.get(), nullptr, &dr);
    }

    fillRoundedRectScanlines(renderer, pill_x, pill_y, pill_w, pill_h, pill_radius, pill_style_.pill_color);

    const int pill_cx = pill_x + pill_w / 2;
    const bool pokemon_selected = pill_cx < mid_x;
    if (pokemon_selected && pill_label_pokemon_black_.texture) {
        SDL_Rect dr{
            pokemon_cx - pill_label_pokemon_black_.width / 2,
            label_cy - pill_label_pokemon_black_.height / 2,
            pill_label_pokemon_black_.width,
            pill_label_pokemon_black_.height};
        SDL_RenderCopy(renderer, pill_label_pokemon_black_.texture.get(), nullptr, &dr);
    } else if (!pokemon_selected && pill_label_items_black_.texture) {
        SDL_Rect dr{
            items_cx - pill_label_items_black_.width / 2,
            label_cy - pill_label_items_black_.height / 2,
            pill_label_items_black_.width,
            pill_label_items_black_.height};
        SDL_RenderCopy(renderer, pill_label_items_black_.texture.get(), nullptr, &dr);
    }
}

void TransferSystemScreen::render(SDL_Renderer* renderer) const {
    SDL_RenderSetClipRect(renderer, nullptr);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    drawBackground(renderer);
    if (resort_box_viewport_) {
        resort_box_viewport_->render(renderer);
    }
    if (game_save_box_viewport_) {
        game_save_box_viewport_->render(renderer);
    }
    drawToolCarousel(renderer);
    drawPillToggle(renderer);
    drawBottomBanner(renderer);

    if (!exit_in_progress_ && fade_in_seconds_ > 1e-6) {
        const double t = std::clamp(elapsed_seconds_ / fade_in_seconds_, 0.0, 1.0);
        const int a = static_cast<int>(std::lround(255.0 * (1.0 - t)));
        if (a > 0) {
            setDrawColor(renderer, Color{0, 0, 0, a});
            SDL_Rect r{0, 0, window_config_.virtual_width, window_config_.virtual_height};
            SDL_RenderFillRect(renderer, &r);
        }
    }

    if (exit_in_progress_ && fade_out_seconds_ > 1e-6) {
        const double t = std::clamp(exit_fade_seconds_ / fade_out_seconds_, 0.0, 1.0);
        const int a = static_cast<int>(std::lround(255.0 * t));
        setDrawColor(renderer, Color{0, 0, 0, a});
        SDL_Rect r{0, 0, window_config_.virtual_width, window_config_.virtual_height};
        SDL_RenderFillRect(renderer, &r);
    }
}

void TransferSystemScreen::onAdvancePressed() {
}

void TransferSystemScreen::onBackPressed() {
    // Pull UI away before returning.
    exit_in_progress_ = true;
    ui_enter_target_ = 0.0;
    bottom_banner_target_ = 0.0;
    panels_target_ = 0.0;
}

bool TransferSystemScreen::handlePointerPressed(int logical_x, int logical_y) {
    if (panels_reveal_ > 0.02 && !carouselSlideAnimating() && hitTestToolCarousel(logical_x, logical_y)) {
        const int vx = carousel_style_.offset_from_left_wall;
        const int vw = carousel_style_.viewport_width;
        const int rel = logical_x - vx;
        if (rel * 2 < vw) {
            cycleToolCarousel(-1);
        } else {
            cycleToolCarousel(1);
        }
        play_button_sfx_requested_ = true;
        return true;
    }
    if (hitTestPillTrack(logical_x, logical_y)) {
        togglePillTarget();
        return true;
    }
    return false;
}

bool TransferSystemScreen::consumeButtonSfxRequest() {
    const bool requested = play_button_sfx_requested_;
    play_button_sfx_requested_ = false;
    return requested;
}

bool TransferSystemScreen::consumeReturnToTicketListRequest() {
    const bool requested = return_to_ticket_list_requested_;
    return_to_ticket_list_requested_ = false;
    return requested;
}

void TransferSystemScreen::requestReturnToTicketList() {
    return_to_ticket_list_requested_ = true;
}

void TransferSystemScreen::drawBottomBanner(SDL_Renderer* renderer) const {
    const int screen_w = window_config_.virtual_width;
    const int screen_h = window_config_.virtual_height;
    constexpr int help_h = 33;
    constexpr int stats_h = 75;
    constexpr int top_line_h = 4;
    const int total_h = help_h + stats_h + top_line_h;

    const Color c_help{191, 191, 191, 255};  // #BFBFBF
    const Color c_stats{224, 224, 224, 255}; // #E0E0E0
    const Color c_line{191, 191, 191, 255};  // #BFBFBF

    const int off = static_cast<int>(std::lround((1.0 - bottom_banner_reveal_) * static_cast<double>(total_h)));
    const int y0 = screen_h - total_h + off;

    // Thin line on top.
    SDL_Rect r_line{0, y0, screen_w, top_line_h};
    setDrawColor(renderer, c_line);
    SDL_RenderFillRect(renderer, &r_line);

    // Stats banner.
    SDL_Rect r_stats{0, y0 + top_line_h, screen_w, stats_h};
    setDrawColor(renderer, c_stats);
    SDL_RenderFillRect(renderer, &r_stats);

    // Button help bar at bottom.
    SDL_Rect r_help{0, y0 + top_line_h + stats_h, screen_w, help_h};
    setDrawColor(renderer, c_help);
    SDL_RenderFillRect(renderer, &r_help);
}

void TransferSystemScreen::drawBackground(SDL_Renderer* renderer) const {
    if (!background_.texture) {
        return;
    }

    SDL_SetTextureBlendMode(background_.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(background_.texture.get(), 255);
    SDL_SetTextureColorMod(background_.texture.get(), 255, 255, 255);

    const double safe_scale = std::max(0.01, background_animation_.scale);
    const int width = std::max(1, static_cast<int>(std::round(
        static_cast<double>(background_.width) * safe_scale)));
    const int height = std::max(1, static_cast<int>(std::round(
        static_cast<double>(background_.height) * safe_scale)));

    if (!background_animation_.enabled ||
        (background_animation_.speed_x == 0.0 && background_animation_.speed_y == 0.0)) {
        SDL_Rect dst{0, 0, width, height};
        SDL_RenderCopy(renderer, background_.texture.get(), nullptr, &dst);
        return;
    }

    const int screen_width = window_config_.virtual_width;
    const int screen_height = window_config_.virtual_height;
    const int offset_x = static_cast<int>(std::floor(background_animation_.speed_x * elapsed_seconds_)) % width;
    const int offset_y = static_cast<int>(std::floor(background_animation_.speed_y * elapsed_seconds_)) % height;
    const int start_x = offset_x > 0 ? offset_x - width : offset_x;
    const int start_y = offset_y > 0 ? offset_y - height : offset_y;

    for (int y = start_y; y < screen_height; y += height) {
        for (int x = start_x; x < screen_width; x += width) {
            SDL_Rect dst{x, y, width, height};
            SDL_RenderCopy(renderer, background_.texture.get(), nullptr, &dst);
        }
    }
}

} // namespace pr
