#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"
#include "gameplay/attend/AttendLaunchContext.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/characters/CharacterController.hpp"
#include "gameplay/world3d/characters/CharacterMovementConfig.hpp"
#include "gameplay/world3d/characters/SpriteSheetAnimator.hpp"
#include "gameplay/world3d/followers/FollowerController.hpp"
#include "gameplay/world3d/effects/LandingDustSystem.hpp"
#include "gameplay/world3d/effects/ProceduralWaterParticleSystem.hpp"
#include "gameplay/world3d/dialogue/OverworldTextboxConfig.hpp"
#include "gameplay/world3d/dialogue/OverworldTextboxController.hpp"
#include "gameplay/world3d/dialogue/OverworldTextboxRenderer.hpp"
#include "gameplay/world3d/doors/DoorTravel.hpp"
#include "gameplay/world3d/interactions/InteractionSequence.hpp"
#include "gameplay/world3d/interactions/InteractionText.hpp"
#include "gameplay/world3d/npc/NpcActorDriver.hpp"
#include "gameplay/world3d/rendering/BillboardSpriteRenderer.hpp"
#include "gameplay/world3d/rendering/GlbModelRenderer.hpp"
#include "gameplay/world3d/rendering/OverworldMapRenderer.hpp"
#include "gameplay/world3d/rendering/bgfx/OverworldBgfxRenderer.hpp"
#include "core/assets/Assets.hpp"
#include "core/assets/Font.hpp"
#include "core/Types.hpp"
#include "ui/Screen.hpp"
#include "ui/transitions/ScreenTransition.hpp"

#include <memory>
#include <optional>
#include <random>
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
        void* sdl_metal_view,
        const std::string& debug_frame_counter_label = {});
    bool wantsBgfxRenderer() const;
    bool isBgfxActive() const;
    void renderPresentationOverlay(SDL_Renderer* renderer);
    void queueBgfxScreenshot(const std::string& output_path);
    bool consumeBlockedMovementSfxRequested();

    bool canNavigate2d() const override { return true; }
    bool capturesUnroutedKeyboardFocus() const override { return true; }
    bool handleUnroutedSdlEvent(const SDL_Event& event) override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    bool handlePointerReleased(int logical_x, int logical_y) override;
    void onNavigate2d(int dx, int dy) override;
    void onAdvancePressed() override;
    void onBackPressed() override;
    void onAttendPressed() override;

    bool consumeReturnToTitleRequested();
    bool consumeOpenAttendRequested();
    std::optional<gameplay::attend::AttendLaunchContext> consumeAttendLaunchContext();
    void resumeFromAttend();
    void shutdownBgfx();
    void resetForNextLaunch();

private:
    void initializeSceneState();
    void reloadFollowCameraPresetConfig();
    std::vector<gameplay::world3d::characters::LoadedWorldChunk> buildLoadedWorldChunks() const;
    void rebuildActiveWorldChunks();
    bool activateWorldMap(const gameplay::world3d::characters::LoadedWorldChunk& chunk);
    std::vector<gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer::StaticMapChunk>
    buildStaticRenderChunks() const;
    void reloadWorldTerrainQueries();
    bool beginDoorSequenceForStep(int dx, int dy);
    void updateDoorSequence(double dt);
    void logLoadedWorldChunks() const;
    gameplay::world3d::dialogue::OverworldTextboxController::Target findInteractionTarget() const;
    bool lockInteractionTarget(const gameplay::world3d::dialogue::OverworldTextboxController::Target& target);
    bool interactionActive() const;
    void beginInteraction(
        const gameplay::world3d::dialogue::OverworldTextboxController::Target& target,
        bool preserve_player_interaction_session = false);
    void updateInteractionSequence();
    void closeInteractionText();
    void requestInteractionExit();
    void finishInteraction();
    gameplay::world3d::interactions::InteractionTargetKind interactionTargetKind(
        const gameplay::world3d::dialogue::OverworldTextboxController::Target& target) const;
    gameplay::world3d::interactions::InteractionTextContext buildInteractionTextContext(
        const gameplay::world3d::dialogue::OverworldTextboxController::Target& target) const;
    std::string characterDialogueForTarget(
        const gameplay::world3d::dialogue::OverworldTextboxController::Target& target);
    void clearInteractionTextBox();
    SDL_Rect visibleWorldViewportRect(int logical_w, int logical_h) const;
    SDL_Rect attendButtonRect() const;
    bool attendAvailable() const;
    std::optional<gameplay::attend::AttendLaunchContext> buildAttendLaunchContext() const;
    SDL_Point mapPointerToLogical(int x, int y) const;

    std::string project_root_;
    gameplay::world3d::SceneConfig scene_;
    gameplay::world3d::characters::CharacterMovementConfig movement_config_{};
    gameplay::world3d::CharacterSpriteDefinition character_;
    gameplay::world3d::camera::Gen4CameraPreset follow_camera_base_preset_{};
    gameplay::world3d::camera::Gen4FollowCamera camera_;
    gameplay::world3d::characters::CharacterController player_;
    gameplay::world3d::characters::SpriteSheetAnimator animator_;
    gameplay::world3d::rendering::OverworldMapRenderer map_;
    std::vector<std::unique_ptr<gameplay::world3d::rendering::GlbModelRenderer>> placed_models_;
    std::unique_ptr<gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer> bgfx_renderer_;

    int input_dx_ = 0;
    int input_dy_ = 0;
    bool freecam_enabled_ = false;
    bool freecam_mouse_dragging_ = false;
    bool run_toggle_active_ = false;
    bool blocked_movement_sfx_requested_ = false;
    double blocked_movement_repeat_seconds_ = 0.0;
    gameplay::world3d::camera::Vec3 freecam_pos_{};
    float freecam_yaw_deg_ = 0.0f;
    float freecam_pitch_deg_ = 0.0f;
    bool return_to_title_requested_ = false;
    bool open_attend_requested_ = false;
    bool attend_enabled_for_interaction_ = false;
    bool attend_button_pressed_ = false;
    transitions::OverworldTransitionConfig transition_config_{};
    transitions::ScreenTransition transition_{};
    int pointer_window_w_ = 800;
    int pointer_window_h_ = 500;
    std::unique_ptr<gameplay::world3d::rendering::BillboardSpriteRenderer> sprite_renderer_;
    gameplay::world3d::followers::FollowerSummonConfig follower_summon_config_{};
    gameplay::world3d::followers::FollowerSessionConfig follower_session_config_{};
    std::unique_ptr<gameplay::world3d::followers::FollowerController> follower_controller_;
    std::unique_ptr<gameplay::world3d::effects::LandingDustSystem> landing_dust_system_;
    std::unique_ptr<gameplay::world3d::effects::ProceduralWaterParticleSystem> water_particle_system_;
    std::unique_ptr<gameplay::world3d::npc::NpcActorDriver> npc_actor_driver_;
    gameplay::world3d::dialogue::OverworldTextboxConfig textbox_config_{};
    gameplay::world3d::dialogue::OverworldTextboxController textbox_controller_{};
    std::unique_ptr<gameplay::world3d::dialogue::OverworldTextboxRenderer> textbox_renderer_;
    gameplay::world3d::dialogue::OverworldTextboxController::Target active_interaction_target_{};
    std::optional<gameplay::attend::AttendLaunchContext> pending_attend_launch_context_;
    gameplay::world3d::scripts::ScriptCatalog interaction_script_catalog_{};
    std::vector<gameplay::world3d::characters::LoadedWorldChunk> loaded_world_chunks_{};
    std::vector<gameplay::world3d::characters::LoadedWorldChunk> active_world_chunks_{};
    std::string active_world_map_id_{};
    gameplay::world3d::doors::DoorSequenceController door_sequence_{};
    gameplay::world3d::doors::DoorTravelConfig door_travel_config_{};
    gameplay::world3d::doors::DoorDestinationTuning active_door_destination_tuning_{};
    float door_forced_move_speed_ = 64.0f;
    double door_animation_wait_seconds_ = 0.0;
    bool door_waiting_for_close_ = false;
    bool door_waiting_for_open_ = false;
    gameplay::world3d::doors::ForcedDoorMoveController door_forced_move_{};
    gameplay::world3d::scripts::ScriptCooldowns interaction_script_cooldowns_{};
    gameplay::world3d::interactions::InteractionSequenceController interaction_sequence_{};
    gameplay::world3d::interactions::InteractionTextCatalog interaction_text_catalog_{};
    gameplay::world3d::interactions::InteractionTextCooldowns interaction_text_cooldowns_{};
    std::mt19937 interaction_rng_{0x52534f52U};
    double interaction_time_seconds_ = 0.0;
    bool interaction_exit_requested_ = false;
    bool interaction_pokemon_session_started_ = false;
    bool after_pokemon_attend_context_ = false;
    double interaction_wait_remaining_ = 0.0;

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
