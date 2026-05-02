#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"
#include "ui/Screen.hpp"
#include "ui/title_screen/MainMenuController.hpp"
#include "ui/title_screen/OptionsMenuController.hpp"
#include "ui/title_screen/SectionScreenController.hpp"
#include <SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pr {

enum class TitleState {
    SplashFadeIn,
    SplashHold,
    SplashFadeOut,
    MainLogoOnBlack,
    WhiteFlash,
    TitleHold,
    WaitingForStart,
    StartTransition,
    MainMenuIntro,
    MainMenuIdle,
    MainMenuToSection,
    MainMenuSectionFade,
    OptionsIntro,
    OptionsIdle,
    OptionsOutro,
    SectionScreen
};

enum class TitleScreenEvent {
    ButtonSfxRequested,
    UserSettingsSaveRequested,
    OpenResortLoadingRequested,
    OpenTradeLoadingRequested,
    OpenTransferRequested
};

class TitleScreen : public Screen {
public:
    TitleScreen(TitleScreenConfig config, Assets assets);

    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;

    bool canNavigate() const override;
    void onAdvancePressed() override;
    void onNavigate(int delta) override;
    void onBackPressed() override;
    bool handlePointerPressed(int logical_x, int logical_y) override;

    bool acceptsAdvanceInput() const override;
    bool wantsMenuMusic() const;
    float musicVolume() const;
    float sfxVolume() const;
    std::vector<TitleScreenEvent> consumeEvents();
    UserSettings currentUserSettings() const;
    void applyUserSettings(const UserSettings& settings);
    void returnToMainMenuFromResort();
    void returnToMainMenuFromTradeLoading();
    void returnToMainMenuFromTransfer();
    void restartFromExternalScreen();

#ifdef PR_ENABLE_TEST_HOOKS
    TitleState debugState() const;
    int debugMainMenuSelectedIndex() const;
    int debugOptionsSelectedIndex() const;
    std::string debugCurrentSectionTitle() const;
#endif

private:
    void changeState(TitleState next);
    double transitionProgress() const;
    double menuIntroProgress() const;
    double optionsTransitionProgress() const;
    double sectionButtonOutProgress() const;
    double sectionFadeProgress() const;
    bool canSkipCurrentState() const;
    TitleState skipTargetState() const;

    void activateMainMenuSelection();
    void activateOptionSelection();
    void returnToMainMenu();
    void restartFromSplash();
    void emitEvent(TitleScreenEvent event);

    void renderSplash(SDL_Renderer* renderer) const;
    void renderMainLogoOnBlack(SDL_Renderer* renderer) const;
    void renderWhiteFlash(SDL_Renderer* renderer) const;
    void renderTitleScene(SDL_Renderer* renderer, bool show_prompt, double transition_t) const;
    void renderMainMenu(SDL_Renderer* renderer, double transition_t) const;
    void renderMenuToSectionTransition(SDL_Renderer* renderer) const;
    void renderOptions(SDL_Renderer* renderer, double transition_t, bool transitioning_in) const;
    void renderSection(SDL_Renderer* renderer) const;
    void renderFadeOverlay(SDL_Renderer* renderer, double alpha01) const;
    void renderMainLogo(SDL_Renderer* renderer, int center_y) const;
    void renderLogoShine(SDL_Renderer* renderer, int center_y) const;
    void updateShinePixels() const;

    void ensureOptionTextures(SDL_Renderer* renderer) const;
    void ensureSectionTextures(SDL_Renderer* renderer) const;

    void drawTextureCentered(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y, unsigned char alpha) const;
    void drawTextureCenteredScaled(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y, unsigned char alpha, double extra_scale) const;
    void drawTextureTopLeft(SDL_Renderer* renderer, const TextureHandle& texture, int x, int y, unsigned char alpha) const;
    void drawPressStart(SDL_Renderer* renderer, unsigned char alpha) const;
    void drawButtonWithLabel(SDL_Renderer* renderer, const TextureHandle& label, int center_x, int center_y, unsigned char alpha, bool selected) const;
    void drawMainMenuButton(SDL_Renderer* renderer, std::size_t index, double transition_t, bool entering) const;
    void drawOptionButton(SDL_Renderer* renderer, std::size_t index, double transition_t, bool entering) const;
    void drawSectionBackButton(SDL_Renderer* renderer) const;
    void drawSelectionHighlight(SDL_Renderer* renderer, int center_x, int center_y) const;
    double selectionBeat() const;

    int mainMenuButtonBaseY(std::size_t index) const;
    int optionButtonBaseY(std::size_t index) const;
    int buttonAnimatedCenterX(std::size_t index, double transition_t, bool entering) const;
    SDL_Rect buttonRect(int center_x, int center_y) const;
    SDL_Rect mainMenuButtonRect(std::size_t index) const;
    SDL_Rect optionButtonRect(std::size_t index) const;
    SDL_Rect sectionBackButtonRect() const;
    bool pointInRect(int x, int y, const SDL_Rect& rect) const;
    std::string currentSectionTitle() const;
    std::vector<std::string> optionLabels() const;

    double scaleX() const;
    double scaleY() const;
    int sx(int value) const;
    int sy(int value) const;

    TitleScreenConfig config_;
    Assets assets_;
    title_screen::MainMenuController main_menu_;
    title_screen::OptionsMenuController options_menu_;
    title_screen::SectionScreenController section_screen_;
    TitleState state_ = TitleState::SplashFadeIn;
    double state_time_ = 0.0;
    double title_scene_elapsed_ = 0.0;
    bool pending_transfer_after_fade_ = false;
    bool pending_resort_after_fade_ = false;
    bool pending_trade_after_fade_ = false;
    std::vector<TitleScreenEvent> pending_events_;

    mutable bool option_textures_dirty_ = true;
    mutable std::vector<TextureHandle> option_textures_;
    mutable std::string cached_section_title_;
    mutable TextureHandle section_title_texture_;
    mutable TextureHandle section_back_texture_;
    mutable std::shared_ptr<SDL_Texture> shine_texture_;
    mutable std::vector<std::uint32_t> shine_pixels_;
};

} // namespace pr
