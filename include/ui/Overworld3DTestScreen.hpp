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
#include "gameplay/world3d/aquarium/AquariumConfig.hpp"
#include "gameplay/world3d/aquarium/AquariumInspectionCamera.hpp"
#include "gameplay/world3d/aquarium/AquariumInspectionFacing.hpp"
#include "gameplay/world3d/aquarium/AquariumSimulation.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumCollisionOverlay.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionOverlay.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesignStore.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumPlayerRuntime.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"
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
#include <future>
#include <filesystem>
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
    bool consumeAquariumConstructionErrorSfxRequested();
    bool consumeAquariumConstructionSaveSfxRequested();
    bool consumeAquariumConstructionMoveSfxRequested();

    bool canNavigate2d() const override { return true; }
    bool acceptsControllerAxisNavigation() const override {
        return aquarium_construction_.active();
    }
    bool capturesUnroutedKeyboardFocus() const override { return true; }
    bool handleUnroutedSdlEvent(const SDL_Event& event) override;
    bool handlePointerPressed(int logical_x, int logical_y) override;
    bool handlePointerReleased(int logical_x, int logical_y) override;
    void handlePointerMoved(int logical_x, int logical_y) override;
    void onNavigate2d(int dx, int dy) override;
    void onAdvancePressed() override;
    void onBackPressed() override;
    void onAttendPressed() override;
    void onAquariumConstructionPressed(SDL_JoystickID controller_instance_id = -1) override;

    bool consumeReturnToTitleRequested();
    bool consumeOpenAttendRequested();
    std::optional<gameplay::attend::AttendLaunchContext> consumeAttendLaunchContext();
    void resumeFromAttend();
    void beginBackgroundPreload();
    void prepareForLaunch();
    void shutdownBgfx();
    void resetForNextLaunch();

private:
    void initializeSceneState(bool reload_primary_scene = true);
    void ensurePlacedModelsLoaded();
    void reloadFollowCameraPresetConfig();
    void captureFreeCameraPose();
    std::vector<gameplay::world3d::characters::LoadedWorldChunk> buildLoadedWorldChunks() const;
    void rebuildActiveWorldChunks();
    bool activateWorldMap(const gameplay::world3d::characters::LoadedWorldChunk& chunk);
    std::vector<gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer::StaticMapChunk>
    buildStaticRenderChunks() const;
    void reloadWorldTerrainQueries();
    void reloadAquariumConfig(bool force);
    void configureAquariumConstruction(const gameplay::world3d::aquarium::AquariumMapConfig* map_config);
    void refreshPlayerAquariumRuntime();
    void refreshAquariumRenderActors();
    bool commitAquariumConstruction();
    bool beginAquariumConstructionCommit(
        std::optional<gameplay::world3d::aquarium::construction::ConstructionCommitCandidate>
            candidate);
    bool undoAquariumConstruction();
    bool redoAquariumConstruction();
    void updateAquariumConstructionCommit();
    void exitAquariumConstruction();
    void applyAquariumConstructionCamera();
    void requestAquariumConstructionErrorFeedback();
    gameplay::world3d::aquarium::construction::AquariumConstructionVisual
        aquariumConstructionVisual() const;
    std::optional<pr::aquarium::geometry::GridCell> aquariumConstructionCellAt(
        int logical_x, int logical_y) const;
    std::optional<gameplay::world3d::aquarium::construction::ConstructionGizmoHit>
        aquariumConstructionGizmoAt(int logical_x, int logical_y) const;
    bool handleAquariumConstructionPointerPressed(int logical_x, int logical_y);
    gameplay::world3d::aquarium::construction::ConstructionHudAction
        aquariumConstructionHudActionAt(int logical_x, int logical_y) const;
    bool activateAquariumConstructionAction(
        gameplay::world3d::aquarium::construction::ConstructionHudAction action);
    void syncAquariumConstructionFocus();
    void cycleAquariumConstructionFocus(int direction);
    bool adjustAquariumConstructionProperty(int direction);
    void beginAquariumInspectionExit();
    void restoreAquariumInspectionFacing();
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
    bool placed_models_load_attempted_ = false;
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
    std::future<gameplay::world3d::SceneConfig> scene_preload_{};
    bool scene_initialized_ = false;
    std::unique_ptr<gameplay::world3d::npc::NpcActorDriver> npc_actor_driver_;
    gameplay::world3d::aquarium::AquariumCatalog aquarium_catalog_{};
    std::filesystem::file_time_type aquarium_config_write_time_{};
    bool aquarium_config_write_time_known_ = false;
    double aquarium_config_poll_seconds_ = 0.0;
    std::unique_ptr<gameplay::world3d::aquarium::AquariumSimulation> aquarium_simulation_;
    std::unique_ptr<gameplay::world3d::aquarium::AquariumInspectionCamera>
        aquarium_inspection_camera_;
    gameplay::world3d::aquarium::AquariumInspectionFacing aquarium_inspection_facing_;
    gameplay::world3d::aquarium::construction::AquariumConstructionSession
        aquarium_construction_;
    gameplay::world3d::aquarium::construction::AquariumConstructionOverlay
        aquarium_construction_overlay_;
    std::unique_ptr<gameplay::world3d::aquarium::construction::AquariumDesignStore>
        aquarium_design_store_;
    std::unique_ptr<gameplay::world3d::aquarium::construction::AquariumPopulationPolicy>
        aquarium_population_policy_;
    gameplay::world3d::aquarium::construction::PlayerAquariumRuntimeSet
        player_aquarium_runtime_;
    std::shared_ptr<gameplay::world3d::aquarium::construction::AquariumCollisionOverlay>
        aquarium_collision_overlay_;
    bool aquarium_construction_read_only_ = false;
    bool aquarium_construction_error_sfx_requested_ = false;
    bool aquarium_construction_save_sfx_requested_ = false;
    bool aquarium_construction_move_sfx_requested_ = false;
    SDL_JoystickID aquarium_construction_controller_id_ = -1;
    struct AquariumGeneratedCommit {
        gameplay::world3d::aquarium::construction::ConstructionCommitCandidate candidate;
        gameplay::world3d::aquarium::construction::PlayerAquariumRuntimeSet runtime;
        std::vector<std::string> diagnostics;
        bool validation_valid = false;
        std::int64_t generation_microseconds = 0;
    };
    std::future<AquariumGeneratedCommit> aquarium_commit_future_;
    bool aquarium_commit_cancelled_ = false;
    bool aquarium_pointer_down_ = false;
    bool aquarium_pointer_dragged_ = false;
    bool aquarium_pointer_second_click_ = false;
    bool aquarium_left_trigger_down_ = false;
    bool aquarium_right_trigger_down_ = false;
    gameplay::world3d::aquarium::construction::ConstructionHudAction
        aquarium_construction_focused_action_ =
            gameplay::world3d::aquarium::construction::ConstructionHudAction::None;
    gameplay::world3d::aquarium::construction::ConstructionState
        aquarium_construction_focus_state_ =
            gameplay::world3d::aquarium::construction::ConstructionState::Dormant;
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
    bool pending_post_attend_interaction_ = false;
    bool post_attend_open_frame_presented_ = false;
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
