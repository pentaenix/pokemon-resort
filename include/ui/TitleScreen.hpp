#pragma once

#include "core/Assets.hpp"
#include "core/Types.hpp"
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

enum class SectionKind {
    Resort,
    Transfer
};

class TitleScreen {
public:
    TitleScreen(TitleScreenConfig config, Assets assets);

    void update(double dt);
    void render(SDL_Renderer* renderer) const;

    void onAdvancePressed();
    void onNavigate(int delta);
    void onBackPressed();
    bool handlePointerPressed(int logical_x, int logical_y);

    bool acceptsAdvanceInput() const;
    bool wantsMenuMusic() const;
    float musicVolume() const;
    float sfxVolume() const;
    bool consumeButtonSfxRequest();
    bool consumeUserSettingsSaveRequest();
    UserSettings currentUserSettings() const;
    void applyUserSettings(const UserSettings& settings);

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
    void requestButtonSfx();

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
    TitleState state_ = TitleState::SplashFadeIn;
    SectionKind current_section_ = SectionKind::Resort;
    SectionKind pending_section_ = SectionKind::Resort;
    double state_time_ = 0.0;
    double title_scene_elapsed_ = 0.0;
    int selected_main_menu_index_ = 0;
    int selected_option_index_ = 0;
    int text_speed_index_ = 2;
    int music_volume_ = 7;
    int sfx_volume_ = 8;
    bool play_button_sfx_requested_ = false;
    bool user_settings_save_requested_ = false;

    mutable bool option_textures_dirty_ = true;
    mutable std::vector<TextureHandle> option_textures_;
    mutable std::string cached_section_title_;
    mutable TextureHandle section_title_texture_;
    mutable TextureHandle section_back_texture_;
    mutable std::shared_ptr<SDL_Texture> shine_texture_;
    mutable std::vector<std::uint32_t> shine_pixels_;
};

} // namespace pr
