#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterController.hpp"
#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"
#include "gameplay/world3d/followers/FollowerController.hpp"
#include "gameplay/world3d/effects/LandingDustSystem.hpp"
#include "gameplay/world3d/rendering/BillboardSpriteRenderer.hpp"
#include "gameplay/world3d/rendering/GlbModelRenderer.hpp"
#include "gameplay/world3d/rendering/OverworldMapRenderer.hpp"
#include "gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer.hpp"
#include "core/assets/Assets.hpp"
#include "core/assets/Font.hpp"
#include "core/Types.hpp"
#include "ui/Screen.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pr {

class Overworld3DTestScreen : public Screen {
public:
    explicit Overworld3DTestScreen(const std::string& project_root);

    void update(double dt) override;
    void render(SDL_Renderer* renderer) override;
    bool renderBgfx(
        SDL_Window* window,
        int framebuffer_w,
        int framebuffer_h,
        int logical_w,
        int logical_h,
        void* sdl_metal_view);
    bool wantsBgfxRenderer() const;
    bool isBgfxActive() const;
    void renderPresentationOverlay(SDL_Renderer* renderer);
    void queueBgfxScreenshot(const std::string& output_path);

    bool canNavigate2d() const override { return true; }
    bool capturesUnroutedKeyboardFocus() const override { return true; }
    bool handleUnroutedSdlEvent(const SDL_Event& event) override;
    void onNavigate2d(int dx, int dy) override;
    void onBackPressed() override;

    bool consumeReturnToTitleRequested();
    void shutdownBgfx();
    void resetForNextLaunch();

private:
    void initializeSceneState();

    std::string project_root_;
    gameplay::world3d::SceneConfig scene_;
    gameplay::world3d::CharacterSpriteDefinition character_;
    gameplay::world3d::camera::Gen4FollowCamera camera_;
    gameplay::world3d::characters::CharacterController player_;
    gameplay::world3d::characters::SpriteSheetAnimator animator_;
    gameplay::world3d::rendering::OverworldMapRenderer map_;
    std::vector<std::unique_ptr<gameplay::world3d::rendering::GlbModelRenderer>> placed_models_;
    std::unique_ptr<gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer> bgfx_renderer_;

    int input_dx_ = 0;
    int input_dy_ = 0;
    bool freecam_enabled_ = false;
    gameplay::world3d::camera::Vec3 freecam_pos_{};
    float freecam_yaw_deg_ = 0.0f;
    float freecam_pitch_deg_ = 0.0f;
    bool return_to_title_requested_ = false;
    std::unique_ptr<gameplay::world3d::rendering::BillboardSpriteRenderer> sprite_renderer_;
    gameplay::world3d::followers::FollowerSummonConfig follower_summon_config_{};
    gameplay::world3d::followers::FollowerSessionConfig follower_session_config_{};
    std::unique_ptr<gameplay::world3d::followers::FollowerController> follower_controller_;
    std::unique_ptr<gameplay::world3d::effects::LandingDustSystem> landing_dust_system_;

    bool map_loaded_ = false;
    bool initialized_renderer_ = false;
    bool bgfx_init_failed_ = false;
    std::string pending_bgfx_screenshot_;
    AppConfig app_config_{};
    FontHandle debug_font_{};
    TextureHandle aib_texture_{};
    std::string cached_aib_label_{};
};

} // namespace pr
