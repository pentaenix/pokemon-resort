#include "ui/TitleScreen.hpp"

#include <SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

#include "title_screen/TitleScreenInternal.hpp"

namespace pr {

using namespace title_screen;

TitleScreen::TitleScreen(TitleScreenConfig config, Assets assets)
    : config_(std::move(config)),
      assets_(std::move(assets)),
      main_menu_(assets_.menu_labels.size()) {
    UserSettings defaults;
    defaults.music_volume = config_.audio.music_volume;
    defaults.sfx_volume = config_.audio.sfx_volume;
    options_menu_.applyUserSettings(defaults);
}

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
                    emitEvent(TitleScreenEvent::OpenTransferRequested);
                } else {
                    section_screen_.commitPendingSection();
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
        if (main_menu_.navigate(delta)) {
            emitEvent(TitleScreenEvent::ButtonSfxRequested);
        }
    } else if (state_ == TitleState::OptionsIdle) {
        if (options_menu_.navigate(delta)) {
            emitEvent(TitleScreenEvent::ButtonSfxRequested);
        }
    }
}

void TitleScreen::onBackPressed() {
    if (state_ == TitleState::MainMenuIdle) {
        emitEvent(TitleScreenEvent::ButtonSfxRequested);
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
                main_menu_.selectIndex(static_cast<int>(i));
                activateMainMenuSelection();
                return true;
            }
        }
    } else if (state_ == TitleState::OptionsIdle) {
        const auto labels = optionLabels();
        for (std::size_t i = 0; i < labels.size(); ++i) {
            if (pointInRect(logical_x, logical_y, optionButtonRect(i))) {
                options_menu_.selectIndex(static_cast<int>(i));
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
    return options_menu_.musicVolumeScale();
}

float TitleScreen::sfxVolume() const {
    return options_menu_.sfxVolumeScale();
}

std::vector<TitleScreenEvent> TitleScreen::consumeEvents() {
    std::vector<TitleScreenEvent> events;
    events.swap(pending_events_);
    return events;
}

UserSettings TitleScreen::currentUserSettings() const {
    return options_menu_.currentUserSettings();
}

void TitleScreen::applyUserSettings(const UserSettings& settings) {
    options_menu_.applyUserSettings(settings);
    option_textures_dirty_ = true;
}

void TitleScreen::returnToMainMenuFromTransfer() {
    main_menu_.selectTransfer();
    changeState(TitleState::MainMenuIdle);
}

void TitleScreen::restartFromExternalScreen() {
    restartFromSplash();
}

#ifdef PR_ENABLE_TEST_HOOKS
TitleState TitleScreen::debugState() const {
    return state_;
}

int TitleScreen::debugMainMenuSelectedIndex() const {
    return main_menu_.selectedIndex();
}

int TitleScreen::debugOptionsSelectedIndex() const {
    return options_menu_.selectedIndex();
}

std::string TitleScreen::debugCurrentSectionTitle() const {
    return section_screen_.currentTitle();
}
#endif

void TitleScreen::changeState(TitleState next) {
    if (next == TitleState::TitleHold && state_ != TitleState::TitleHold) {
        title_scene_elapsed_ = 0.0;
    }

    if (next == TitleState::OptionsIntro) {
        options_menu_.resetSelection();
    } else if (next == TitleState::SectionScreen) {
        cached_section_title_.clear();
    } else if (next == TitleState::MainMenuIdle && state_ == TitleState::OptionsOutro) {
        main_menu_.selectOptions();
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
    emitEvent(TitleScreenEvent::ButtonSfxRequested);
    switch (main_menu_.activate()) {
        case MainMenuAction::OpenResort:
            pending_transfer_after_fade_ = false;
            section_screen_.queueSection(SectionKind::Resort);
            changeState(TitleState::MainMenuToSection);
            break;
        case MainMenuAction::OpenTransfer:
            pending_transfer_after_fade_ = true;
            changeState(TitleState::MainMenuToSection);
            break;
        case MainMenuAction::OpenTrade:
            pending_transfer_after_fade_ = false;
            section_screen_.queueSection(SectionKind::Trade);
            changeState(TitleState::MainMenuToSection);
            break;
        case MainMenuAction::OpenOptions:
            changeState(TitleState::OptionsIntro);
            break;
        case MainMenuAction::None:
            break;
        default:
            break;
    }
}

void TitleScreen::activateOptionSelection() {
    switch (options_menu_.activate()) {
        case OptionsMenuAction::ChangedSettings:
            emitEvent(TitleScreenEvent::ButtonSfxRequested);
            option_textures_dirty_ = true;
            emitEvent(TitleScreenEvent::UserSettingsSaveRequested);
            break;
        case OptionsMenuAction::CloseOptions:
            emitEvent(TitleScreenEvent::ButtonSfxRequested);
            changeState(TitleState::OptionsOutro);
            break;
        case OptionsMenuAction::None:
            break;
        default:
            break;
    }
}

void TitleScreen::returnToMainMenu() {
    if (state_ == TitleState::SectionScreen) {
        switch (section_screen_.currentSection()) {
            case SectionKind::Resort:
                main_menu_.reset();
                section_screen_.resetToResort();
                break;
            case SectionKind::Trade:
                main_menu_.selectTrade();
                section_screen_.selectTrade();
                break;
        }
    }
    changeState(TitleState::MainMenuIdle);
}

void TitleScreen::restartFromSplash() {
    emitEvent(TitleScreenEvent::ButtonSfxRequested);
    title_scene_elapsed_ = 0.0;
    main_menu_.reset();
    options_menu_.resetSelection();
    section_screen_.resetToResort();
    cached_section_title_.clear();
    option_textures_dirty_ = true;
    changeState(TitleState::SplashFadeIn);
}

void TitleScreen::emitEvent(TitleScreenEvent event) {
    pending_events_.push_back(event);
}

} // namespace pr
