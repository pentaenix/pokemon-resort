#include "ui/Overworld3DTestScreen.hpp"

#include "core/config/ConfigLoader.hpp"
#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"
#include "gameplay/world3d/data/GlbModelLoader.hpp"
#include "gameplay/world3d/data/JsonOverworldLoader.hpp"
#include "gameplay/world3d/followers/NatureIdleConfig.hpp"
#include "gameplay/world3d/rendering/FallbackTerrainRenderer.hpp"

#include <SDL.h>
#include <algorithm>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <vector>

namespace pr {

namespace {

constexpr const char* kDefaultScenePath = "assets/overworld/maps/testing.owmap";
namespace fs = std::filesystem;

gameplay::world3d::camera::Vec3 initialFreecamPosition(const gameplay::world3d::SceneConfig& scene,
                                                        const gameplay::world3d::camera::Vec3& player_pos) {
    gameplay::world3d::camera::Vec3 pos = player_pos;
    pos.x += scene.freecam_initial_offset_x;
    pos.y += scene.freecam_initial_offset_y;
    pos.z += scene.freecam_initial_offset_z;
    return pos;
}

} // namespace

Overworld3DTestScreen::Overworld3DTestScreen(const std::string& project_root)
    : project_root_(project_root),
      scene_(gameplay::world3d::data::loadSceneConfig(project_root, kDefaultScenePath)),
      character_(gameplay::world3d::data::loadCharacterDefinition(project_root, scene_.player.character_path)),
      camera_(gameplay::world3d::camera::loadGen4PresetById(scene_.camera_preset.c_str())),
      player_(scene_),
      animator_(character_),
      map_(scene_) {
    initializeSceneState();
}

void Overworld3DTestScreen::initializeSceneState() {
    app_config_ = loadAppConfigFromJson((fs::path(project_root_) / "config" / "app.json").string());
    scene_ = gameplay::world3d::data::loadSceneConfig(project_root_, kDefaultScenePath);
    character_ = gameplay::world3d::data::loadCharacterDefinition(project_root_, scene_.player.character_path);
    player_ = gameplay::world3d::characters::CharacterController(scene_);
    animator_.~SpriteSheetAnimator();
    new (&animator_) gameplay::world3d::characters::SpriteSheetAnimator(character_);
    map_ = gameplay::world3d::rendering::OverworldMapRenderer(scene_);
    follower_summon_config_ = gameplay::world3d::followers::loadFollowerSummonConfig(project_root_);
    follower_session_config_ = gameplay::world3d::followers::loadFollowerSessionConfig(project_root_);
    follower_controller_ = std::make_unique<gameplay::world3d::followers::FollowerController>(
        project_root_,
        scene_,
        follower_summon_config_,
        follower_session_config_);
    landing_dust_system_ = std::make_unique<gameplay::world3d::effects::LandingDustSystem>(
        project_root_,
        follower_controller_
            ? gameplay::world3d::followers::loadNatureIdleBehaviorConfig(project_root_).landing_dust
            : gameplay::world3d::followers::NatureIdleLandingDustConfig{});
    gameplay::world3d::camera::Gen4CameraPreset preset =
        gameplay::world3d::camera::loadGen4PresetById(scene_.camera_preset.c_str());
    if (scene_.camera_distance > 0.0f) {
        preset.distance = scene_.camera_distance;
    }
    preset.pitch_deg = scene_.camera_pitch_deg;
    preset.yaw_deg = scene_.camera_yaw_deg;
    preset.roll_deg = scene_.camera_roll_deg;
    preset.near_clip = scene_.camera_near_clip;
    preset.far_clip = scene_.camera_far_clip;
    preset.aspect_width = scene_.camera_aspect_width;
    preset.aspect_height = scene_.camera_aspect_height;
    preset.fov_y_deg = scene_.camera_fov_y_deg;
    camera_ = gameplay::world3d::camera::Gen4FollowCamera(preset);
    map_loaded_ = map_.load();
    placed_models_.clear();
    for (const auto& model : scene_.models) {
        if (model.glb_path.empty()) {
            continue;
        }
        std::string load_error;
        gameplay::world3d::data::GlbMesh mesh =
            gameplay::world3d::data::loadGlbModel(model.glb_path, &load_error);
        if (!mesh.valid) {
            std::cerr << "[Overworld3D] Skipping model '" << model.id << "': " << load_error << std::endl;
            continue;
        }
        placed_models_.push_back(std::make_unique<gameplay::world3d::rendering::GlbModelRenderer>(
            std::move(mesh), model.x, model.y, model.z, model.yaw_deg, model.scale));
    }
    freecam_pos_ = initialFreecamPosition(scene_, player_.position());
    input_dx_ = 0;
    input_dy_ = 0;
    freecam_enabled_ = false;
    freecam_yaw_deg_ = scene_.freecam_initial_yaw_deg;
    freecam_pitch_deg_ = scene_.freecam_initial_pitch_deg;
    return_to_title_requested_ = false;
    sprite_renderer_.reset();
    aib_texture_ = TextureHandle{};
    cached_aib_label_.clear();
    debug_font_ = FontHandle{};
    initialized_renderer_ = false;
}

void Overworld3DTestScreen::resetForNextLaunch() {
    SDL_SetRelativeMouseMode(SDL_FALSE);
    initializeSceneState();
}

void Overworld3DTestScreen::update(double dt) {
    int keyboard_dx = 0;
    int keyboard_dy = 0;
    if (!freecam_enabled_) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) keyboard_dx -= 1;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) keyboard_dx += 1;
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) keyboard_dy -= 1;
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) keyboard_dy += 1;
        if (keyboard_dx != 0 || keyboard_dy != 0) {
            input_dx_ = keyboard_dx;
            input_dy_ = keyboard_dy;
        }

        player_.moveInput(input_dx_, input_dy_, dt);
        if (input_dx_ == 0 && input_dy_ == 0) {
            player_.stop();
        }

        animator_.setFacing(player_.facing());
        animator_.setMoving(player_.moving());
        animator_.update(dt);

        camera_.setTarget(player_.position());
    } else {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        const float speed = static_cast<float>(dt) * scene_.freecam_move_speed;
        const float yaw = freecam_yaw_deg_ * (3.1415926535f / 180.0f);
        const gameplay::world3d::camera::Vec3 forward{std::sin(yaw), 0.0f, std::cos(yaw)};
        const gameplay::world3d::camera::Vec3 right{forward.z, 0.0f, -forward.x};
        if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) {
            freecam_pos_.x += forward.x * speed;
            freecam_pos_.z += forward.z * speed;
        }
        if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) {
            freecam_pos_.x -= forward.x * speed;
            freecam_pos_.z -= forward.z * speed;
        }
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) {
            freecam_pos_.x += right.x * speed;
            freecam_pos_.z += right.z * speed;
        }
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) {
            freecam_pos_.x -= right.x * speed;
            freecam_pos_.z -= right.z * speed;
        }
        camera_.setManualPose(freecam_pos_, freecam_yaw_deg_, freecam_pitch_deg_);
    }
    input_dx_ = 0;
    input_dy_ = 0;
    if (follower_controller_) {
        const bool player_idle = !player_.moving() && !freecam_enabled_;
        const bool player_activity = !freecam_enabled_ && (keyboard_dx != 0 || keyboard_dy != 0);
        follower_controller_->update(dt, player_.position(), player_.facing(), player_idle, player_activity);
        if (landing_dust_system_) {
            if (const auto spawn = follower_controller_->consumeLandingDustSpawn()) {
                landing_dust_system_->spawn(*spawn);
            }
        }
    }
    if (landing_dust_system_) {
        landing_dust_system_->update(dt);
    }
}

void Overworld3DTestScreen::render(SDL_Renderer* renderer) {
    if (!initialized_renderer_) {
        sprite_renderer_ = std::make_unique<gameplay::world3d::rendering::BillboardSpriteRenderer>(
            renderer,
            character_,
            scene_.sprite_shadow);
        if (app_config_.enable_active_idle_behavior_debug) {
            const TitleScreenConfig title_config =
                loadConfigFromJson((fs::path(project_root_) / "config" / "title_screen.json").string());
            debug_font_ = loadFontPreferringUnicode(title_config.assets.font, 20, project_root_);
        }
        if (follower_controller_) {
            follower_controller_->initialize(renderer);
        }
        if (landing_dust_system_) {
            landing_dust_system_->initialize(renderer);
        }
        initialized_renderer_ = true;
    }

    int w = 0;
    int h = 0;
    SDL_RenderGetLogicalSize(renderer, &w, &h);
    if (w <= 0 || h <= 0) {
        SDL_GetRendererOutputSize(renderer, &w, &h);
    }

    SDL_SetRenderDrawColor(renderer, 145, 180, 205, 255);
    SDL_RenderClear(renderer);

    SDL_Rect sky{0, 0, w, h / 2};
    SDL_SetRenderDrawColor(renderer, 150, 191, 224, 255);
    SDL_RenderFillRect(renderer, &sky);

    if (map_loaded_) {
        map_.render(renderer, camera_, w, h);
    } else {
        gameplay::world3d::rendering::renderFallbackTerrain(
            renderer,
            camera_,
            scene_,
            scene_.grid.tile_size,
            w,
            h);
    }

    const auto draw_player = [&]() {
        if (sprite_renderer_ && sprite_renderer_->valid()) {
            sprite_renderer_->render(
                renderer,
                camera_,
                player_.position(),
                animator_.sourceRect(),
                w,
                h,
                scene_.lighting_tint_r,
                scene_.lighting_tint_g,
                scene_.lighting_tint_b,
                scene_.lighting_brightness);
        }
    };
    const auto draw_follower = [&]() {
        if (follower_controller_) {
            follower_controller_->render(
                renderer,
                camera_,
                w,
                h,
                scene_.lighting_tint_r,
                scene_.lighting_tint_g,
                scene_.lighting_tint_b,
                scene_.lighting_brightness);
        }
    };
    const auto player_depth = [&]() -> std::optional<float> {
        float sx = 0.0f, sy = 0.0f, d = 0.0f;
        if (!camera_.worldToScreen(player_.position(), w, h, sx, sy, d)) return std::nullopt;
        return d;
    };
    const std::optional<float> fd = follower_controller_ ? follower_controller_->renderDepth(camera_, w, h) : std::nullopt;
    const std::optional<float> pd = player_depth();

    // Unified back-to-front (painter) ordering of every dynamic occluder in the scene:
    // placed 3D models and the character billboards are sorted together by their world
    // anchor's camera depth, then drawn far → near. This is what stops the player from
    // always drawing on top of a building — when the player walks behind a prop, the prop's
    // nearer anchor makes it draw last and occlude the sprite. Objects don't interpenetrate
    // (the player can't stand inside a building), so per-object ordering is correct here.
    struct DepthDrawable {
        float depth;
        int tiebreak; // models < player < follower when depths match, for stable results
        std::function<void()> draw;
    };
    std::vector<DepthDrawable> drawables;
    drawables.reserve(placed_models_.size() + 2);
    // Bias each model's occlusion anchor toward the camera by N tiles (config-driven) so a
    // building starts hiding a character before they reach its center — e.g. a character
    // stepping into a doorway is occluded by the building instead of drawing over its roof.
    const float model_depth_bias = scene_.model_behind_bias_tiles * scene_.grid.tile_size;
    for (const auto& model_renderer : placed_models_) {
        gameplay::world3d::rendering::GlbModelRenderer* mr = model_renderer.get();
        const std::optional<float> md = mr->anchorDepth(camera_, w, h);
        // Anchor behind the near plane → treat as farthest so it draws first.
        const float depth = md ? (*md - model_depth_bias) : std::numeric_limits<float>::max();
        drawables.push_back({depth, 0, [this, mr, renderer, w, h]() {
            mr->render(renderer, camera_, w, h,
                scene_.lighting_tint_r, scene_.lighting_tint_g, scene_.lighting_tint_b,
                scene_.lighting_brightness);
        }});
    }
    if (pd) drawables.push_back({*pd, 1, draw_player});
    if (fd) drawables.push_back({*fd, 2, draw_follower});

    std::stable_sort(drawables.begin(), drawables.end(),
        [](const DepthDrawable& a, const DepthDrawable& b) {
            if (a.depth != b.depth) return a.depth > b.depth; // far first
            return a.tiebreak < b.tiebreak;
        });
    for (const auto& d : drawables) d.draw();

    // Characters whose anchor is behind the camera have no depth key; draw them last so
    // they remain visible rather than vanishing.
    if (!pd) draw_player();
    if (!fd) draw_follower();

    if (landing_dust_system_) {
        landing_dust_system_->render(
            renderer,
            camera_,
            w,
            h,
            scene_.lighting_tint_r,
            scene_.lighting_tint_g,
            scene_.lighting_tint_b,
            scene_.lighting_brightness);
    }

}

void Overworld3DTestScreen::renderPresentationOverlay(SDL_Renderer* renderer) {
    if (!app_config_.enable_active_idle_behavior_debug || !debug_font_ || !follower_controller_) {
        return;
    }

    const std::string label = "AIB: " + follower_controller_->debugActivityLabel();
    if (label != cached_aib_label_) {
        cached_aib_label_ = label;
        aib_texture_ = renderTextTexture(renderer, debug_font_.get(), cached_aib_label_, Color{245, 245, 245, 255});
    }
    if (!aib_texture_.texture) {
        return;
    }

    const int margin = 12;
    SDL_Rect dst{margin, margin, aib_texture_.width, aib_texture_.height};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
    SDL_Rect bg{std::max(0, dst.x - 6), std::max(0, dst.y - 4), dst.w + 12, dst.h + 8};
    SDL_RenderFillRect(renderer, &bg);
    SDL_RenderCopy(renderer, aib_texture_.texture.get(), nullptr, &dst);
}

void Overworld3DTestScreen::onNavigate2d(int dx, int dy) {
    if (freecam_enabled_) {
        return;
    }
    input_dx_ = dx;
    input_dy_ = dy;
}

void Overworld3DTestScreen::onBackPressed() {
    if (freecam_enabled_) {
        freecam_enabled_ = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
    return_to_title_requested_ = true;
}

bool Overworld3DTestScreen::consumeReturnToTitleRequested() {
    const bool value = return_to_title_requested_;
    return_to_title_requested_ = false;
    return value;
}

bool Overworld3DTestScreen::handleUnroutedSdlEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        if (event.key.keysym.sym == SDLK_q) {
            freecam_enabled_ = !freecam_enabled_;
            if (!freecam_enabled_) {
                SDL_SetRelativeMouseMode(SDL_FALSE);
                camera_.setTarget(player_.position());
            } else {
                SDL_SetRelativeMouseMode(SDL_TRUE);
                freecam_pos_ = initialFreecamPosition(scene_, player_.position());
                camera_.setManualPose(freecam_pos_, freecam_yaw_deg_, freecam_pitch_deg_);
            }
            return true;
        }
        if (!freecam_enabled_ && follower_controller_) {
            if (event.key.keysym.sym == SDLK_j) {
                return follower_controller_->triggerDebugJump();
            }
            if (event.key.keysym.sym == SDLK_p) {
                return follower_controller_->triggerDebugPoke();
            }
        }
    }

    if (freecam_enabled_ && event.type == SDL_MOUSEMOTION) {
        freecam_yaw_deg_ += static_cast<float>(event.motion.xrel) * scene_.freecam_mouse_sensitivity;
        freecam_pitch_deg_ -= static_cast<float>(event.motion.yrel) * scene_.freecam_mouse_sensitivity;
        if (freecam_pitch_deg_ > scene_.freecam_pitch_max_deg) freecam_pitch_deg_ = scene_.freecam_pitch_max_deg;
        if (freecam_pitch_deg_ < scene_.freecam_pitch_min_deg) freecam_pitch_deg_ = scene_.freecam_pitch_min_deg;
        return true;
    }
    return false;
}

} // namespace pr
