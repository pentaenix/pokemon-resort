#include "ui/TitleScreen.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace pr {

namespace {

constexpr int kSectionTitleY = 125;
constexpr int kSectionBackButtonY = 708;
constexpr int kMainMenuResortIndex = 0;
constexpr int kMainMenuTransferIndex = 1;
constexpr int kMainMenuTradeIndex = 2;
constexpr int kMainMenuOptionsIndex = 3;

double clamp01(double v) { return std::max(0.0, std::min(1.0, v)); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double easeOutCubic(double t) { t = clamp01(t); double inv = 1.0 - t; return 1.0 - inv * inv * inv; }
double easeInOutQuad(double t) { t = clamp01(t); return t < 0.5 ? 2.0 * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 2.0) / 2.0; }
double easeOutBack(double t, double overshoot) {
    t = clamp01(t);
    const double c1 = std::max(0.0, overshoot);
    const double c3 = c1 + 1.0;
    const double shifted = t - 1.0;
    return 1.0 + c3 * shifted * shifted * shifted + c1 * shifted * shifted;
}

unsigned char triangleBlinkAlpha(double elapsed, double cycle) {
    if (cycle <= 0.0) {
        return 255;
    }
    const double t = std::fmod(elapsed, cycle) / cycle;
    const double wave = 1.0 - std::fabs(2.0 * t - 1.0);
    return static_cast<unsigned char>(std::round(lerp(0.0, 255.0, wave)));
}

std::uint32_t packRGBA(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    return (static_cast<std::uint32_t>(r) << 24) |
           (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) |
           static_cast<std::uint32_t>(a);
}

int wrapIndex(int value, int size) {
    if (size <= 0) {
        return 0;
    }
    value %= size;
    return value < 0 ? value + size : value;
}

int clampVolume(int value) {
    return std::max(0, std::min(10, value));
}

} // namespace

TitleScreen::TitleScreen(TitleScreenConfig config, Assets assets)
    : config_(std::move(config)),
      assets_(std::move(assets)),
      music_volume_(clampVolume(config_.audio.music_volume)),
      sfx_volume_(clampVolume(config_.audio.sfx_volume)) {}

void TitleScreen::update(double dt) {
    state_time_ += dt;

    if (state_ == TitleState::TitleHold || state_ == TitleState::WaitingForStart) {
        title_scene_elapsed_ += dt;
    }

    switch (state_) {
        case TitleState::SplashFadeIn:
            if (state_time_ >= config_.timings.splash_fade_in) changeState(TitleState::SplashHold);
            break;
        case TitleState::SplashHold:
            if (state_time_ >= config_.timings.splash_hold) changeState(TitleState::SplashFadeOut);
            break;
        case TitleState::SplashFadeOut:
            if (state_time_ >= config_.timings.splash_fade_out) changeState(TitleState::MainLogoOnBlack);
            break;
        case TitleState::MainLogoOnBlack:
            if (state_time_ >= config_.timings.main_logo_on_black) changeState(TitleState::WhiteFlash);
            break;
        case TitleState::WhiteFlash:
            if (state_time_ >= config_.timings.white_flash) changeState(TitleState::TitleHold);
            break;
        case TitleState::TitleHold:
            if (state_time_ >= config_.timings.title_hold_before_prompt) changeState(TitleState::WaitingForStart);
            break;
        case TitleState::WaitingForStart:
            break;
        case TitleState::StartTransition:
            if (state_time_ >= config_.timings.start_transition) changeState(TitleState::MainMenuIntro);
            break;
        case TitleState::MainMenuIntro:
            if (state_time_ >= config_.menu.animation.intro_duration) changeState(TitleState::MainMenuIdle);
            break;
        case TitleState::MainMenuToSection:
            if (state_time_ >= config_.menu.section_transition.button_out_duration) changeState(TitleState::MainMenuSectionFade);
            break;
        case TitleState::MainMenuSectionFade:
            if (state_time_ >= config_.menu.section_transition.fade_duration) {
                if (pending_transfer_after_fade_) {
                    pending_transfer_after_fade_ = false;
                    open_transfer_requested_ = true;
                } else {
                    current_section_ = pending_section_;
                    changeState(TitleState::SectionScreen);
                }
            }
            break;
        case TitleState::OptionsIntro:
            if (state_time_ >= config_.menu.animation.intro_duration) changeState(TitleState::OptionsIdle);
            break;
        case TitleState::OptionsOutro:
            if (state_time_ >= config_.menu.animation.outro_duration) changeState(TitleState::MainMenuIdle);
            break;
        case TitleState::MainMenuIdle:
        case TitleState::OptionsIdle:
        case TitleState::SectionScreen:
            break;
    }
}

void TitleScreen::render(SDL_Renderer* renderer) const {
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

void TitleScreen::onAdvancePressed() {
    switch (state_) {
        case TitleState::WaitingForStart:
            changeState(TitleState::StartTransition);
            return;
        case TitleState::MainMenuIdle:
            activateMainMenuSelection();
            return;
        case TitleState::OptionsIdle:
            activateOptionSelection();
            return;
        case TitleState::SectionScreen:
            restartFromSplash();
            return;
        default:
            if (canSkipCurrentState()) {
                changeState(skipTargetState());
            }
            return;
    }
}

bool TitleScreen::canNavigate() const {
    return state_ == TitleState::MainMenuIdle || state_ == TitleState::OptionsIdle;
}

void TitleScreen::onNavigate(int delta) {
    if (state_ == TitleState::MainMenuIdle) {
        const int next = wrapIndex(selected_main_menu_index_ + delta, static_cast<int>(assets_.menu_labels.size()));
        if (next != selected_main_menu_index_) {
            selected_main_menu_index_ = next;
            requestButtonSfx();
        }
    } else if (state_ == TitleState::OptionsIdle) {
        const int next = wrapIndex(selected_option_index_ + delta, static_cast<int>(optionLabels().size()));
        if (next != selected_option_index_) {
            selected_option_index_ = next;
            requestButtonSfx();
        }
    }
}

void TitleScreen::onBackPressed() {
    if (state_ == TitleState::MainMenuIdle) {
        requestButtonSfx();
        title_scene_elapsed_ = 0.0;
        changeState(TitleState::WaitingForStart);
    } else if (state_ == TitleState::OptionsIdle) {
        changeState(TitleState::OptionsOutro);
    } else if (state_ == TitleState::SectionScreen) {
        restartFromSplash();
    }
}

bool TitleScreen::handlePointerPressed(int logical_x, int logical_y) {
    if (state_ == TitleState::MainMenuIdle) {
        for (std::size_t i = 0; i < assets_.menu_labels.size(); ++i) {
            if (pointInRect(logical_x, logical_y, mainMenuButtonRect(i))) {
                selected_main_menu_index_ = static_cast<int>(i);
                activateMainMenuSelection();
                return true;
            }
        }
    } else if (state_ == TitleState::OptionsIdle) {
        const auto labels = optionLabels();
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (pointInRect(logical_x, logical_y, optionButtonRect(i))) {
                selected_option_index_ = static_cast<int>(i);
                activateOptionSelection();
                return true;
            }
        }
    } else if (state_ == TitleState::SectionScreen) {
        if (pointInRect(logical_x, logical_y, sectionBackButtonRect())) {
            restartFromSplash();
            return true;
        }
    } else if (acceptsAdvanceInput()) {
        onAdvancePressed();
        return true;
    }

    return false;
}

bool TitleScreen::acceptsAdvanceInput() const {
    return state_ == TitleState::WaitingForStart ||
           state_ == TitleState::MainMenuIdle ||
           state_ == TitleState::OptionsIdle ||
           state_ == TitleState::SectionScreen ||
           canSkipCurrentState();
}

bool TitleScreen::wantsMenuMusic() const {
    switch (state_) {
        case TitleState::SplashFadeIn:
        case TitleState::SplashHold:
        case TitleState::SplashFadeOut:
        case TitleState::SectionScreen:
            return false;
        default:
            return true;
    }
}

float TitleScreen::musicVolume() const {
    return static_cast<float>(music_volume_) / 10.0f;
}

float TitleScreen::sfxVolume() const {
    return static_cast<float>(sfx_volume_) / 10.0f;
}

bool TitleScreen::consumeButtonSfxRequest() {
    const bool requested = play_button_sfx_requested_;
    play_button_sfx_requested_ = false;
    return requested;
}

bool TitleScreen::consumeUserSettingsSaveRequest() {
    const bool requested = user_settings_save_requested_;
    user_settings_save_requested_ = false;
    return requested;
}

bool TitleScreen::consumeOpenTransferRequest() {
    const bool requested = open_transfer_requested_;
    open_transfer_requested_ = false;
    return requested;
}

UserSettings TitleScreen::currentUserSettings() const {
    UserSettings settings;
    settings.text_speed_index = wrapIndex(text_speed_index_, 3);
    settings.music_volume = clampVolume(music_volume_);
    settings.sfx_volume = clampVolume(sfx_volume_);
    return settings;
}

void TitleScreen::applyUserSettings(const UserSettings& settings) {
    text_speed_index_ = wrapIndex(settings.text_speed_index, 3);
    music_volume_ = clampVolume(settings.music_volume);
    sfx_volume_ = clampVolume(settings.sfx_volume);
    option_textures_dirty_ = true;
}

void TitleScreen::returnToMainMenuFromTransfer() {
    selected_main_menu_index_ = kMainMenuTransferIndex;
    changeState(TitleState::MainMenuIdle);
}

void TitleScreen::restartFromExternalScreen() {
    restartFromSplash();
}

void TitleScreen::changeState(TitleState next) {
    if (next == TitleState::TitleHold && state_ != TitleState::TitleHold) {
        title_scene_elapsed_ = 0.0;
    }

    if (next == TitleState::OptionsIntro) {
        selected_option_index_ = 0;
    } else if (next == TitleState::SectionScreen) {
        cached_section_title_.clear();
    } else if (next == TitleState::MainMenuIdle && state_ == TitleState::OptionsOutro) {
        selected_main_menu_index_ = kMainMenuOptionsIndex;
    }

    state_ = next;
    state_time_ = 0.0;
}

double TitleScreen::transitionProgress() const {
    if (config_.timings.start_transition <= 0.0) {
        return 1.0;
    }
    return clamp01(state_time_ / config_.timings.start_transition);
}

double TitleScreen::menuIntroProgress() const {
    if (config_.menu.animation.intro_duration <= 0.0) {
        return 1.0;
    }
    return clamp01(state_time_ / config_.menu.animation.intro_duration);
}

double TitleScreen::sectionButtonOutProgress() const {
    if (config_.menu.section_transition.button_out_duration <= 0.0) {
        return 1.0;
    }
    return clamp01(state_time_ / config_.menu.section_transition.button_out_duration);
}

double TitleScreen::sectionFadeProgress() const {
    if (config_.menu.section_transition.fade_duration <= 0.0) {
        return 1.0;
    }
    return clamp01(state_time_ / config_.menu.section_transition.fade_duration);
}

double TitleScreen::optionsTransitionProgress() const {
    const double duration =
        state_ == TitleState::OptionsOutro
            ? config_.menu.animation.outro_duration
            : config_.menu.animation.intro_duration;

    if (duration <= 0.0) {
        return 1.0;
    }
    return clamp01(state_time_ / duration);
}

bool TitleScreen::canSkipCurrentState() const {
    switch (state_) {
        case TitleState::SplashFadeIn:
            return config_.skip.splash_fade_in;
        case TitleState::SplashHold:
            return config_.skip.splash_hold;
        case TitleState::SplashFadeOut:
            return config_.skip.splash_fade_out;
        case TitleState::MainLogoOnBlack:
            return config_.skip.main_logo_on_black;
        case TitleState::WhiteFlash:
            return config_.skip.white_flash;
        case TitleState::TitleHold:
            return config_.skip.title_hold;
        case TitleState::WaitingForStart:
            return config_.skip.waiting_for_start;
        case TitleState::StartTransition:
            return config_.skip.start_transition;
        case TitleState::MainMenuIntro:
            return config_.skip.main_menu_intro;
        case TitleState::MainMenuToSection:
        case TitleState::MainMenuSectionFade:
        case TitleState::MainMenuIdle:
        case TitleState::OptionsIntro:
        case TitleState::OptionsIdle:
        case TitleState::OptionsOutro:
        case TitleState::SectionScreen:
            return false;
    }
    return false;
}

TitleState TitleScreen::skipTargetState() const {
    switch (state_) {
        case TitleState::SplashFadeIn:
        case TitleState::SplashHold:
        case TitleState::SplashFadeOut:
            return TitleState::MainLogoOnBlack;
        case TitleState::MainLogoOnBlack:
            return TitleState::WhiteFlash;
        case TitleState::WhiteFlash:
        case TitleState::TitleHold:
            return TitleState::WaitingForStart;
        case TitleState::WaitingForStart:
            return TitleState::StartTransition;
        case TitleState::StartTransition:
            return TitleState::MainMenuIntro;
        case TitleState::MainMenuIntro:
            return TitleState::MainMenuIdle;
        default:
            return state_;
    }
}

void TitleScreen::activateMainMenuSelection() {
    requestButtonSfx();
    switch (selected_main_menu_index_) {
        case kMainMenuResortIndex:
            pending_transfer_after_fade_ = false;
            pending_section_ = SectionKind::Resort;
            changeState(TitleState::MainMenuToSection);
            break;
        case kMainMenuTransferIndex:
            pending_transfer_after_fade_ = true;
            changeState(TitleState::MainMenuToSection);
            break;
        case kMainMenuTradeIndex:
            pending_transfer_after_fade_ = false;
            pending_section_ = SectionKind::Trade;
            changeState(TitleState::MainMenuToSection);
            break;
        case kMainMenuOptionsIndex:
            changeState(TitleState::OptionsIntro);
            break;
        default:
            break;
    }
}

void TitleScreen::activateOptionSelection() {
    switch (selected_option_index_) {
        case 0:
            requestButtonSfx();
            text_speed_index_ = wrapIndex(text_speed_index_ + 1, 3);
            option_textures_dirty_ = true;
            user_settings_save_requested_ = true;
            break;
        case 1:
            requestButtonSfx();
            music_volume_ = wrapIndex(music_volume_ + 1, 11);
            option_textures_dirty_ = true;
            user_settings_save_requested_ = true;
            break;
        case 2:
            requestButtonSfx();
            sfx_volume_ = wrapIndex(sfx_volume_ + 1, 11);
            option_textures_dirty_ = true;
            user_settings_save_requested_ = true;
            break;
        case 3:
            requestButtonSfx();
            changeState(TitleState::OptionsOutro);
            break;
        default:
            break;
    }
}

void TitleScreen::returnToMainMenu() {
    if (state_ == TitleState::SectionScreen) {
        switch (current_section_) {
            case SectionKind::Resort:
                selected_main_menu_index_ = kMainMenuResortIndex;
                break;
            case SectionKind::Trade:
                selected_main_menu_index_ = kMainMenuTradeIndex;
                break;
        }
    }
    changeState(TitleState::MainMenuIdle);
}

void TitleScreen::restartFromSplash() {
    requestButtonSfx();
    title_scene_elapsed_ = 0.0;
    selected_main_menu_index_ = kMainMenuResortIndex;
    selected_option_index_ = 0;
    cached_section_title_.clear();
    option_textures_dirty_ = true;
    changeState(TitleState::SplashFadeIn);
}

void TitleScreen::requestButtonSfx() {
    play_button_sfx_requested_ = true;
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
    const bool selected = selected_main_menu_index_ == static_cast<int>(index);
    const int center_x = buttonAnimatedCenterX(index, transition_t, entering);
    const int center_y = mainMenuButtonBaseY(index);
    const unsigned char alpha = selected ? 255 : 220;
    drawButtonWithLabel(renderer, assets_.menu_labels[index], center_x, center_y, alpha, selected);
}

void TitleScreen::drawOptionButton(SDL_Renderer* renderer, std::size_t index, double transition_t, bool entering) const {
    ensureOptionTextures(renderer);
    const bool selected = selected_option_index_ == static_cast<int>(index);
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
    switch (current_section_) {
        case SectionKind::Resort:
            return "RESORT";
        case SectionKind::Trade:
            return "TRADE";
    }
    return "SECTION";
}

std::vector<std::string> TitleScreen::optionLabels() const {
    static const std::array<const char*, 3> kTextSpeeds{"SLOW", "MID", "FAST"};

    return {
        "TEXT SPEED: " + std::string(kTextSpeeds[wrapIndex(text_speed_index_, static_cast<int>(kTextSpeeds.size()))]),
        "MUSIC VOLUME: " + std::to_string(music_volume_),
        "SFX VOLUME: " + std::to_string(sfx_volume_),
        "BACK"
    };
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
