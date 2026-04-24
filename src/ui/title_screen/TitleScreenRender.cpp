#include "ui/TitleScreen.hpp"
#include "TitleScreenInternal.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pr {

using namespace title_screen;

void TitleScreen::render(SDL_Renderer* renderer) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);

    switch (state_) {
        case TitleState::SplashFadeIn:
        case TitleState::SplashHold:
        case TitleState::SplashFadeOut:
            renderSplash(renderer);
            break;
        case TitleState::MainLogoOnBlack:
            renderMainLogoOnBlack(renderer);
            break;
        case TitleState::WhiteFlash:
            renderTitleScene(renderer, false, 0.0);
            renderWhiteFlash(renderer);
            break;
        case TitleState::TitleHold:
            renderTitleScene(renderer, false, 0.0);
            break;
        case TitleState::WaitingForStart:
            renderTitleScene(renderer, true, 0.0);
            break;
        case TitleState::StartTransition:
            renderTitleScene(renderer, true, transitionProgress());
            break;
        case TitleState::MainMenuIntro:
            renderMainMenu(renderer, menuIntroProgress());
            break;
        case TitleState::MainMenuIdle:
            renderMainMenu(renderer, 1.0);
            break;
        case TitleState::MainMenuToSection:
        case TitleState::MainMenuSectionFade:
            renderMenuToSectionTransition(renderer);
            break;
        case TitleState::OptionsIntro:
            renderOptions(renderer, optionsTransitionProgress(), true);
            break;
        case TitleState::OptionsIdle:
            renderOptions(renderer, 1.0, true);
            break;
        case TitleState::OptionsOutro:
            renderOptions(renderer, optionsTransitionProgress(), false);
            break;
        case TitleState::SectionScreen:
            renderSection(renderer);
            break;
    }
}

void TitleScreen::renderSplash(SDL_Renderer* renderer) const {
    unsigned char alpha = 255;
    if (state_ == TitleState::SplashFadeIn) {
        alpha = static_cast<unsigned char>(std::round(255.0 * clamp01(state_time_ / config_.timings.splash_fade_in)));
    } else if (state_ == TitleState::SplashFadeOut) {
        alpha = static_cast<unsigned char>(std::round(255.0 * (1.0 - clamp01(state_time_ / config_.timings.splash_fade_out))));
    }
    drawTextureCentered(renderer, assets_.logo_splash, config_.layout.splash_logo_center.x, config_.layout.splash_logo_center.y, alpha);
}

void TitleScreen::renderMainLogoOnBlack(SDL_Renderer* renderer) const {
    renderMainLogo(renderer, config_.layout.main_logo_center.y);
}

void TitleScreen::renderWhiteFlash(SDL_Renderer* renderer) const {
    const double t = clamp01(state_time_ / config_.timings.white_flash);
    const unsigned char alpha = static_cast<unsigned char>(std::round(255.0 * (1.0 - t)));
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, alpha);

    SDL_Rect rect{0, 0, config_.window.virtual_width, config_.window.virtual_height};
    SDL_RenderFillRect(renderer, &rect);
}

void TitleScreen::renderTitleScene(SDL_Renderer* renderer, bool show_prompt, double transition_t) const {
    const double bg_ease = easeInOutQuad(
        std::min(1.0, transition_t * config_.transition.background_a_speed_scale));
    const double logo_ease = easeOutCubic(
        std::min(1.0, transition_t * config_.transition.main_logo_speed_scale));

    const int bg_a_y = static_cast<int>(std::round(lerp(
        static_cast<double>(config_.layout.background_a_top_left.y),
        static_cast<double>(config_.transition.background_a_end_y),
        bg_ease)));

    const int logo_y = static_cast<int>(std::round(lerp(
        static_cast<double>(config_.layout.main_logo_center.y),
        static_cast<double>(config_.transition.main_logo_end_y),
        logo_ease)));

    drawTextureTopLeft(renderer, assets_.background_b, config_.layout.background_b_top_left.x, config_.layout.background_b_top_left.y, 255);
    drawTextureTopLeft(renderer, assets_.background_a, config_.layout.background_a_top_left.x, bg_a_y, 255);
    renderMainLogo(renderer, logo_y);

    if (show_prompt) {
        unsigned char alpha = triangleBlinkAlpha(state_time_, config_.prompt.blink_cycle_seconds);
        if (state_ == TitleState::StartTransition && config_.transition.fade_prompt_out) {
            const double fade = 1.0 - clamp01(transition_t * 1.8);
            alpha = static_cast<unsigned char>(std::round(alpha * fade));
        }
        drawPressStart(renderer, alpha);
    }
}

void TitleScreen::renderMainMenu(SDL_Renderer* renderer, double transition_t) const {
    drawTextureTopLeft(renderer, assets_.background_b, config_.layout.background_b_top_left.x, config_.layout.background_b_top_left.y, 255);

    for (std::size_t i = 0; i < assets_.menu_labels.size(); ++i) {
        drawMainMenuButton(renderer, i, transition_t, true);
    }
}

void TitleScreen::renderMenuToSectionTransition(SDL_Renderer* renderer) const {
    drawTextureTopLeft(renderer, assets_.background_b, config_.layout.background_b_top_left.x, config_.layout.background_b_top_left.y, 255);

    if (state_ == TitleState::MainMenuToSection) {
        const double menu_out_t = sectionButtonOutProgress();
        for (std::size_t i = 0; i < assets_.menu_labels.size(); ++i) {
            drawMainMenuButton(renderer, i, menu_out_t, false);
        }
        return;
    }

    renderFadeOverlay(renderer, sectionFadeProgress());
}

void TitleScreen::renderOptions(SDL_Renderer* renderer, double transition_t, bool transitioning_in) const {
    drawTextureTopLeft(renderer, assets_.background_b, config_.layout.background_b_top_left.x, config_.layout.background_b_top_left.y, 255);

    if (state_ == TitleState::OptionsIntro) {
        if (transition_t < 0.5) {
            const double menu_out_t = transition_t * 2.0;
            for (std::size_t i = 0; i < assets_.menu_labels.size(); ++i) {
                drawMainMenuButton(renderer, i, menu_out_t, false);
            }
        } else {
            const double options_in_t = (transition_t - 0.5) * 2.0;
            const auto labels = optionLabels();
            for (std::size_t i = 0; i < labels.size(); ++i) {
                drawOptionButton(renderer, i, options_in_t, true);
            }
        }
    } else if (state_ == TitleState::OptionsOutro) {
        if (transition_t < 0.5) {
            const double options_out_t = transition_t * 2.0;
            const auto labels = optionLabels();
            for (std::size_t i = 0; i < labels.size(); ++i) {
                drawOptionButton(renderer, i, options_out_t, false);
            }
        } else {
            const double menu_in_t = (transition_t - 0.5) * 2.0;
            for (std::size_t i = 0; i < assets_.menu_labels.size(); ++i) {
                drawMainMenuButton(renderer, i, menu_in_t, true);
            }
        }
    } else if (state_ == TitleState::OptionsIdle || transitioning_in) {
        const auto labels = optionLabels();
        for (std::size_t i = 0; i < labels.size(); ++i) {
            drawOptionButton(renderer, i, 1.0, true);
        }
    }
}

void TitleScreen::renderSection(SDL_Renderer* renderer) const {
    SDL_SetRenderDrawColor(renderer, 96, 96, 96, 255);
    SDL_Rect bg{0, 0, config_.window.virtual_width, config_.window.virtual_height};
    SDL_RenderFillRect(renderer, &bg);

    SDL_SetRenderDrawColor(renderer, 72, 72, 72, 255);
    SDL_Rect header{0, 0, config_.window.virtual_width, sy(140)};
    SDL_RenderFillRect(renderer, &header);

    ensureSectionTextures(renderer);
    drawTextureCentered(renderer, section_title_texture_, config_.window.design_width / 2, kSectionTitleY, 255);
    drawSectionBackButton(renderer);
}

void TitleScreen::renderFadeOverlay(SDL_Renderer* renderer, double alpha01) const {
    const unsigned char alpha = static_cast<unsigned char>(std::round(255.0 * clamp01(alpha01)));
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
    SDL_Rect rect{0, 0, config_.window.virtual_width, config_.window.virtual_height};
    SDL_RenderFillRect(renderer, &rect);
}

void TitleScreen::renderMainLogo(SDL_Renderer* renderer, int center_y) const {
    drawTextureCentered(renderer, assets_.logo_main, config_.layout.main_logo_center.x, center_y, 255);
    renderLogoShine(renderer, center_y);
}

void TitleScreen::renderLogoShine(SDL_Renderer* renderer, int center_y) const {
    if (!config_.shine.enabled ||
        assets_.logo_main_mask.alpha.empty() ||
        state_ == TitleState::SplashFadeIn ||
        state_ == TitleState::SplashHold ||
        state_ == TitleState::SplashFadeOut) {
        return;
    }

    const int dst_w = std::max(1, sx(assets_.logo_main_mask.width));
    const int dst_h = std::max(1, sy(assets_.logo_main_mask.height));

    const bool needs_rebuild =
        !shine_texture_ ||
        static_cast<int>(shine_pixels_.size()) != (dst_w * dst_h);

    if (needs_rebuild) {
        SDL_Texture* raw = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STREAMING,
            dst_w,
            dst_h);

        if (!raw) {
            return;
        }

        shine_texture_.reset(raw, SDL_DestroyTexture);
        SDL_SetTextureBlendMode(
            shine_texture_.get(),
            config_.shine.use_additive_blend ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);

        shine_pixels_.assign(static_cast<std::size_t>(dst_w) * static_cast<std::size_t>(dst_h), 0);
    }

    updateShinePixels();

    SDL_UpdateTexture(
        shine_texture_.get(),
        nullptr,
        shine_pixels_.data(),
        dst_w * static_cast<int>(sizeof(std::uint32_t)));

    SDL_Rect dst{
        sx(config_.layout.main_logo_center.x) - dst_w / 2,
        sy(center_y) - dst_h / 2,
        dst_w,
        dst_h
    };

    SDL_RenderCopy(renderer, shine_texture_.get(), nullptr, &dst);
}

void TitleScreen::updateShinePixels() const {
    std::fill(shine_pixels_.begin(), shine_pixels_.end(), 0);

    if (!config_.shine.enabled ||
        assets_.logo_main_mask.alpha.empty() ||
        config_.shine.duration_seconds <= 0.0 ||
        config_.shine.repeat_count <= 0 ||
        !shine_texture_) {
        return;
    }

    const double shine_time = title_scene_elapsed_ - config_.shine.delay_seconds;
    if (shine_time < 0.0) {
        return;
    }

    const double cycle_duration =
        config_.shine.duration_seconds + std::max(0.0, config_.shine.gap_seconds);

    const int cycle_index =
        cycle_duration > 0.0 ? static_cast<int>(std::floor(shine_time / cycle_duration)) : 0;

    if (cycle_index < 0 || cycle_index >= config_.shine.repeat_count) {
        return;
    }

    const double cycle_time =
        cycle_duration > 0.0 ? shine_time - static_cast<double>(cycle_index) * cycle_duration : shine_time;

    if (cycle_time < 0.0 || cycle_time > config_.shine.duration_seconds) {
        return;
    }

    const double progress = clamp01(cycle_time / config_.shine.duration_seconds);

    const int src_w = assets_.logo_main_mask.width;
    const int src_h = assets_.logo_main_mask.height;

    const int dst_w = std::max(1, sx(src_w));
    const int dst_h = std::max(1, sy(src_h));

    const double scale_x = static_cast<double>(dst_w) / static_cast<double>(src_w);
    const double scale_y = static_cast<double>(dst_h) / static_cast<double>(src_h);

    const double scaled_band_width =
        std::max(1.0, static_cast<double>(config_.shine.band_width) * ((scale_x + scale_y) * 0.5));

    const double scaled_travel_padding =
        std::max(1.0, static_cast<double>(config_.shine.travel_padding) * ((scale_x + scale_y) * 0.5));

    const double start_center = -scaled_travel_padding;
    const double end_center = static_cast<double>(dst_w + dst_h) + scaled_travel_padding;
    const double center = lerp(start_center, end_center, progress);

    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            const int src_x = std::clamp(
                static_cast<int>(std::floor(static_cast<double>(x) / scale_x)),
                0,
                src_w - 1);

            const int src_y = std::clamp(
                static_cast<int>(std::floor(static_cast<double>(y) / scale_y)),
                0,
                src_h - 1);

            const std::size_t src_idx =
                static_cast<std::size_t>(src_y) * static_cast<std::size_t>(src_w) +
                static_cast<std::size_t>(src_x);

            const unsigned char mask_alpha = assets_.logo_main_mask.alpha[src_idx];
            if (mask_alpha == 0) {
                continue;
            }

            const double diagonal_pos = static_cast<double>(x + y);
            const double distance = std::fabs(diagonal_pos - center);
            if (distance > scaled_band_width) {
                continue;
            }

            const double edge = 1.0 - (distance / scaled_band_width);
            const double intensity = edge * edge;

            const double alpha01 =
                (static_cast<double>(mask_alpha) / 255.0) *
                intensity *
                (static_cast<double>(config_.shine.max_alpha) / 255.0);

            const unsigned char a =
                static_cast<unsigned char>(std::round(255.0 * clamp01(alpha01)));

            const std::size_t dst_idx =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_w) +
                static_cast<std::size_t>(x);

            shine_pixels_[dst_idx] = packRGBA(255, 255, 255, a);
        }
    }
}

void TitleScreen::ensureOptionTextures(SDL_Renderer* renderer) const {
    if (!option_textures_dirty_ && option_textures_.size() == optionLabels().size()) {
        return;
    }

    option_textures_.clear();
    for (const std::string& label : optionLabels()) {
        option_textures_.push_back(
            renderTextTexture(renderer, assets_.ui_font.get(), label, config_.menu.text_color));
    }
    option_textures_dirty_ = false;
}

void TitleScreen::ensureSectionTextures(SDL_Renderer* renderer) const {
    const std::string title = currentSectionTitle();
    if (cached_section_title_ != title || !section_title_texture_.texture) {
        cached_section_title_ = title;
        section_title_texture_ = renderTextTexture(renderer, assets_.ui_font.get(), cached_section_title_, config_.menu.text_color);
    }

    if (!section_back_texture_.texture) {
        section_back_texture_ = renderTextTexture(renderer, assets_.ui_font.get(), "BACK", config_.menu.text_color);
    }
}

void TitleScreen::drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y, unsigned char alpha) const {
    drawTextureCenteredScaled(renderer, texture, x, y, alpha, 1.0);
}

void TitleScreen::drawTextureCenteredScaled(
    SDL_Renderer* renderer,
    const TextureHandle& texture,
    int x,
    int y,
    unsigned char alpha,
    double extra_scale) const {
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), alpha);

    const double safe_scale = std::max(0.1, extra_scale);
    const int scaled_w = std::max(1, static_cast<int>(std::round(static_cast<double>(sx(texture.width)) * safe_scale)));
    const int scaled_h = std::max(1, static_cast<int>(std::round(static_cast<double>(sy(texture.height)) * safe_scale)));
    const int scaled_x = sx(x);
    const int scaled_y = sy(y);

    SDL_Rect dst{
        scaled_x - scaled_w / 2,
        scaled_y - scaled_h / 2,
        scaled_w,
        scaled_h
    };
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TitleScreen::drawTextureTopLeft(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y, unsigned char alpha) const {
    SDL_SetTextureBlendMode(texture.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture.texture.get(), alpha);

    SDL_Rect dst{
        sx(x),
        sy(y),
        sx(texture.width),
        sy(texture.height)
    };
    SDL_RenderCopy(renderer, texture.texture.get(), nullptr, &dst);
}

void TitleScreen::drawPressStart(SDL_Renderer* renderer, unsigned char alpha) const {
    SDL_SetTextureBlendMode(assets_.press_start.texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(assets_.press_start.texture.get(), alpha);

    const int scaled_w = sx(assets_.press_start.width);
    const int scaled_h = sy(assets_.press_start.height);

    SDL_Rect dst{
        sx(config_.prompt.center_x) - scaled_w / 2,
        sy(config_.prompt.baseline_y) - scaled_h,
        scaled_w,
        scaled_h
    };
    SDL_RenderCopy(renderer, assets_.press_start.texture.get(), nullptr, &dst);
}

void TitleScreen::drawButtonWithLabel(
    SDL_Renderer* renderer,
    const TextureHandle& label,
    int center_x,
    int center_y,
    unsigned char alpha,
    bool selected) const {
    const double scale_boost = selected
        ? 1.0 + (std::max(0.0, config_.menu.selection.beat_magnitude) * selectionBeat())
        : 1.0;
    if (selected) {
        drawSelectionHighlight(renderer, center_x, center_y);
    }
    drawTextureCenteredScaled(renderer, assets_.button_main, center_x, center_y, alpha, scale_boost);
    drawTextureCenteredScaled(renderer, label, center_x, center_y, alpha, scale_boost);
}

void TitleScreen::drawMainMenuButton(SDL_Renderer* renderer, std::size_t index, double transition_t, bool entering) const {
    const bool selected = main_menu_.selectedIndex() == static_cast<int>(index);
    const int center_x = buttonAnimatedCenterX(index, transition_t, entering);
    const int center_y = mainMenuButtonBaseY(index);
    const unsigned char alpha = selected ? 255 : 220;
    drawButtonWithLabel(renderer, assets_.menu_labels[index], center_x, center_y, alpha, selected);
}

void TitleScreen::drawOptionButton(SDL_Renderer* renderer, std::size_t index, double transition_t, bool entering) const {
    ensureOptionTextures(renderer);
    const bool selected = options_menu_.selectedIndex() == static_cast<int>(index);
    const int center_x = buttonAnimatedCenterX(index, transition_t, entering);
    const int center_y = optionButtonBaseY(index);
    const unsigned char alpha = selected ? 255 : 220;
    drawButtonWithLabel(renderer, option_textures_[index], center_x, center_y, alpha, selected);
}

void TitleScreen::drawSectionBackButton(SDL_Renderer* renderer) const {
    ensureSectionTextures(renderer);
    drawButtonWithLabel(renderer, section_back_texture_, config_.window.design_width / 2, kSectionBackButtonY, 255, true);
}

void TitleScreen::drawSelectionHighlight(SDL_Renderer* renderer, int center_x, int center_y) const {
    const SDL_Rect rect = buttonRect(center_x, center_y);
    const double beat = selectionBeat();
    const double magnitude = std::max(0.0, config_.menu.selection.beat_magnitude);
    const double emphasis = beat * (1.0 + magnitude * 6.0);
    const int pulse = static_cast<int>(std::round(24.0 + 40.0 * std::min(1.0, emphasis)));
    const int expand_x = static_cast<int>(std::round(8.0 + 16.0 * emphasis));
    const int expand_y = static_cast<int>(std::round(5.0 + 10.0 * emphasis));
    const int marker_width = static_cast<int>(std::round(8.0 + 10.0 * emphasis));
    const int marker_height = static_cast<int>(std::round(10.0 + 8.0 * emphasis));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 230, 140, static_cast<Uint8>(pulse));

    SDL_Rect outer{
        rect.x - expand_x,
        rect.y - expand_y,
        rect.w + (expand_x * 2),
        rect.h + (expand_y * 2)
    };
    SDL_RenderDrawRect(renderer, &outer);

    SDL_Rect left_marker{
        rect.x - expand_x - marker_width - 2,
        rect.y + rect.h / 2 - marker_height / 2,
        marker_width,
        marker_height
    };
    SDL_Rect right_marker{
        rect.x + rect.w + expand_x + 2,
        rect.y + rect.h / 2 - marker_height / 2,
        marker_width,
        marker_height
    };
    SDL_RenderFillRect(renderer, &left_marker);
    SDL_RenderFillRect(renderer, &right_marker);
}

double TitleScreen::selectionBeat() const {
    const double speed = std::max(0.1, config_.menu.selection.beat_speed);
    return 0.5 + 0.5 * std::sin(state_time_ * speed);
}

int TitleScreen::mainMenuButtonBaseY(std::size_t index) const {
    const int step = assets_.button_main.height + config_.menu.vertical_spacing;
    return config_.menu.top_y + assets_.button_main.height / 2 + static_cast<int>(index) * step;
}

int TitleScreen::optionButtonBaseY(std::size_t index) const {
    const int step = assets_.button_main.height + std::max(20, config_.menu.vertical_spacing / 2);
    return config_.menu.top_y + assets_.button_main.height / 2 + static_cast<int>(index) * step;
}

int TitleScreen::buttonAnimatedCenterX(std::size_t index, double transition_t, bool entering) const {
    const int direction = (index % 2) == 0 ? 1 : -1;
    const int minimum_slide_distance =
        (config_.window.design_width / 2) +
        (assets_.button_main.width / 2) +
        32;
    const int slide_distance = std::max(config_.menu.animation.slide_distance, minimum_slide_distance);
    const int offscreen_x = config_.menu.center_x + direction * slide_distance;

    if (entering) {
        const double eased_t = easeOutBack(transition_t, config_.menu.animation.bounce);
        return static_cast<int>(std::round(lerp(
            static_cast<double>(offscreen_x),
            static_cast<double>(config_.menu.center_x),
            eased_t)));
    }

    const double eased_t = easeInOutQuad(transition_t);
    return static_cast<int>(std::round(lerp(
        static_cast<double>(config_.menu.center_x),
        static_cast<double>(offscreen_x),
        eased_t)));
}

SDL_Rect TitleScreen::buttonRect(int center_x, int center_y) const {
    const int w = sx(assets_.button_main.width);
    const int h = sy(assets_.button_main.height);
    return SDL_Rect{sx(center_x) - w / 2, sy(center_y) - h / 2, w, h};
}

SDL_Rect TitleScreen::mainMenuButtonRect(std::size_t index) const {
    return buttonRect(config_.menu.center_x, mainMenuButtonBaseY(index));
}

SDL_Rect TitleScreen::optionButtonRect(std::size_t index) const {
    return buttonRect(config_.menu.center_x, optionButtonBaseY(index));
}

SDL_Rect TitleScreen::sectionBackButtonRect() const {
    return buttonRect(config_.window.design_width / 2, kSectionBackButtonY);
}

bool TitleScreen::pointInRect(int x, int y, const SDL_Rect& rect) const {
    return x >= rect.x &&
           x < rect.x + rect.w &&
           y >= rect.y &&
           y < rect.y + rect.h;
}

std::string TitleScreen::currentSectionTitle() const {
    return section_screen_.currentTitle();
}

std::vector<std::string> TitleScreen::optionLabels() const {
    return options_menu_.labels();
}

double TitleScreen::scaleX() const {
    return static_cast<double>(config_.window.virtual_width) /
           static_cast<double>(config_.window.design_width);
}

double TitleScreen::scaleY() const {
    return static_cast<double>(config_.window.virtual_height) /
           static_cast<double>(config_.window.design_height);
}

int TitleScreen::sx(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleX()));
}

int TitleScreen::sy(int value) const {
    return static_cast<int>(std::round(static_cast<double>(value) * scaleY()));
}

} // namespace pr
