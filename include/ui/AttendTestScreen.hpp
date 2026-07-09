#pragma once

#include "core/Types.hpp"
#include "gameplay/attend/AttendSceneConfig.hpp"
#include "gameplay/attend/rendering/AttendBgfxRenderer.hpp"
#include "ui/Screen.hpp"
#include "ui/attend/AttendOverlay.hpp"

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace pr {

class AttendTestScreen : public Screen {
public:
    explicit AttendTestScreen(std::string project_root, AppConfig app_config);
    ~AttendTestScreen() override;

    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;
    bool renderBgfx(
        SDL_Window* window,
        int framebuffer_w,
        int framebuffer_h,
        void* sdl_metal_view);
    void renderPresentationOverlay(SDL_Renderer* renderer);
    bool wantsBgfxRenderer() const;
    void queueBgfxScreenshot(const std::string& output_path);
    void shutdownBgfx();

    bool capturesUnroutedKeyboardFocus() const override;
    bool handleUnroutedSdlEvent(const SDL_Event& event) override;
    void onBackPressed() override;
    void handlePointerMoved(int logical_x, int logical_y) override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    bool handlePointerReleased(int logical_x, int logical_y) override;
    bool consumeReturnRequested();
    std::vector<std::string> consumeOneShotSfxRequests();

private:
    class HandCursor;
    struct PokemonModelOption {
        std::string id;
        std::string path;
    };
    struct OneShotSfxRequest {
        std::string path;
        double play_after_seconds = 0.0;
    };

    std::string project_root_;
    AppConfig app_config_{};
    gameplay::attend::AttendSceneConfig scene_config_{};
    std::unique_ptr<gameplay::attend::rendering::AttendBgfxRenderer> bgfx_renderer_;
    std::unique_ptr<HandCursor> hand_cursor_;
    bool bgfx_init_failed_ = false;
    double scene_time_seconds_ = 0.0;
    std::string pending_bgfx_screenshot_;
    bool return_requested_ = false;

    bool pointer_over_pokemon_ = false;
    bool pointer_pet_active_ = false;
    int pointer_space_w_ = 1280;
    int pointer_space_h_ = 800;
    int bgfx_window_w_ = 1280;
    int bgfx_window_h_ = 800;
    float pointer_face_x_ = 0.0f;
    float pointer_face_y_ = 0.0f;
    float pointer_pet_contact_bias_ = 0.0f;
    double pointer_pet_started_seconds_ = -1.0;
    float viewport_look_x_ = 0.0f;
    float viewport_look_y_ = 0.0f;
    int weather_index_ = 0;
    int texture_variant_index_ = -1;
    int form_variant_index_ = -1;
    bool face_view_ = false;
    bool freecam_enabled_ = false;
    bool freecam_mouse_dragging_ = false;
    gameplay::attend::rendering::AttendFreeCameraPose freecam_pose_{};
    bool debug_ui_visible_ = false;
    std::uint64_t debug_fps_frame_count_ = 0;
    double debug_fps_sample_start_seconds_ = -1.0;
    double debug_fps_ = 0.0;
    std::vector<PokemonModelOption> available_pokemon_;
    int pokemon_index_ = 0;
    std::mt19937 idle_rng_{0x41545444u};
    double last_pokemon_interaction_seconds_ = 0.0;
    double next_idle_emote_seconds_ = 30.0;
    double wake_pet_block_until_seconds_ = 0.0;
    bool idle_sleep_triggered_ = false;
    float corner_buttons_visibility_ = 1.0f;
    bool corner_buttons_waiting_return_ = false;
    double corner_buttons_return_after_seconds_ = -1.0;
    bool pointer_near_screen_edge_ = false;
    double pointer_press_seconds_ = -1.0;
    int pointer_press_x_ = 0;
    int pointer_press_y_ = 0;
    bool pointer_press_head_focus_candidate_ = false;
    bool pointer_press_face_exit_candidate_ = false;
    double last_head_click_seconds_ = -1.0;
    int last_head_click_x_ = 0;
    int last_head_click_y_ = 0;
    AttendOverlay overlay_;
    std::vector<OneShotSfxRequest> pending_one_shot_sfx_requests_;

    bool pointerOverPokemon(int logical_x, int logical_y) const;
    void mapPointerToLogical(int& x, int& y) const;
    bool pointerOverPokemonHead(int logical_x, int logical_y) const;
    bool consumePokemonHeadDoubleClick(int logical_x, int logical_y);
    void focusPokemonHead();
    bool environmentControlsEnabled() const;
    bool faceViewControlEnabled() const;
    bool pointerOverWeatherButton(int logical_x, int logical_y) const;
    bool pointerOverViewButton(int logical_x, int logical_y) const;
    bool pointerOverPokemonButton(int logical_x, int logical_y) const;
    bool pointerOverTextureVariantButton(int logical_x, int logical_y) const;
    bool pointerOverFormVariantButton(int logical_x, int logical_y) const;
    bool pointerOverSkyButton(int logical_x, int logical_y) const;
    bool pointerOverEmoteButton(int logical_x, int logical_y) const;
    bool pointerOverSleepButton(int logical_x, int logical_y) const;
    bool pointerOverCryButton(int logical_x, int logical_y) const;
    bool pointerOverOverlayButton(int logical_x, int logical_y) const;
    std::string currentWeatherLabel() const;
    std::string currentViewLabel() const;
    std::string currentPokemonLabel() const;
    std::string currentPokemonSpritePath() const;
    std::string currentPokemonCryPath() const;
    std::string currentFormVariantId() const;
    bool currentTextureVariantIsShiny() const;
    std::string currentTextureVariantLabel() const;
    std::string currentFormVariantLabel() const;
    std::string currentSkyLabel() const;
    void cycleWeatherMode();
    void cycleTextureVariant();
    void cycleFormVariant();
    void cycleSkyPreset();
    void triggerIdleEmote();
    void triggerIdleSleep();
    void requestActivePokemonCry(double delay_seconds = 0.0);
    void updateIdleBehavior();
    bool wakePokemonFromIdle();
    void resetPokemonInactivity();
    void scheduleNextIdleEmote();
    void toggleFreeCamera();
    void updateFreeCamera(double dt);
    void updateCornerButtonVisibility(double dt);
    void revealCornerButtons();
    void toggleViewMode();
    void cyclePokemonModel();
    void applyViewportLook();
    void applyWeatherMode();
    void applyTextureVariant();
    void applyFormVariant();
    void applyViewMode();
    void refreshAvailablePokemonModels();
    void applyPokemonModelByIndex();
    void updatePointerFaceTarget(int logical_x, int logical_y);
    void updatePointerEdgeLook(int logical_x, int logical_y);
    void updatePetContactBias(int logical_x, int logical_y);
    void updatePointerSpace(int w, int h);
    void restoreSystemCursor();
    void reloadSceneConfig();
};

} // namespace pr
