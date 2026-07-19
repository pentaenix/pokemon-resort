#include "ui/Overworld3DTestScreen.hpp"

#include "core/config/ConfigLoader.hpp"
#include "core/config/Json.hpp"
#include "core/input/InputBindings.hpp"
#include "gameplay/world3d/camera/Gen4CameraPreset.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"
#include "gameplay/world3d/camera/Gen4FollowCamera.hpp"
#include "gameplay/world3d/data/GlbModelLoader.hpp"
#include "gameplay/world3d/data/JsonOverworldLoader.hpp"
#include "gameplay/world3d/dialogue/OverworldTextboxConfig.hpp"
#include "gameplay/world3d/followers/NatureIdleConfig.hpp"
#include "gameplay/world3d/interactions/InteractionSequence.hpp"
#include "gameplay/world3d/interactions/InteractionText.hpp"
#include "gameplay/world3d/npc/NpcActorDriver.hpp"
#include "gameplay/world3d/rendering/FallbackTerrainRenderer.hpp"
#include "ui/overlay/OverlayCanvas.hpp"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <vector>

namespace pr {

namespace {

constexpr const char* kDefaultScenePath = "assets/overworld/maps/0.owmap";
namespace fs = std::filesystem;

gameplay::world3d::camera::Vec3 initialFreecamPosition(const gameplay::world3d::SceneConfig& scene,
                                                        const gameplay::world3d::camera::Vec3& player_pos) {
    gameplay::world3d::camera::Vec3 pos = player_pos;
    pos.x += scene.freecam_initial_offset_x;
    pos.y += scene.freecam_initial_offset_y;
    pos.z += scene.freecam_initial_offset_z;
    return pos;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool anyBindingPressed(const Uint8* keys, const std::vector<std::string>& bindings) {
    if (!keys) {
        return false;
    }
    for (const std::string& binding : bindings) {
        const SDL_Keycode keycode = keycodeFromBinding(binding);
        const SDL_Scancode scancode = SDL_GetScancodeFromKey(keycode);
        if (scancode != SDL_SCANCODE_UNKNOWN && keys[scancode]) {
            return true;
        }
    }
    return false;
}

std::optional<fs::path> findMapProjectPath(const fs::path& project_root) {
    const std::vector<fs::path> candidates{
        project_root / "config" / "gameplay" / "world3d" / "map_project.json",
        project_root / "assets" / "overworld" / "maps" / "map_project.json",
        project_root.parent_path() / "pokemon-resort-page" / "tools" / "admin" / "data" / "map-projects" / "default.json",
    };
    for (const fs::path& candidate : candidates) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

int jsonIntOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

std::string jsonStringOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

// Map screen-oriented input (up/down/left/right) to world grid steps using the Gen 4 camera basis on XZ.
std::pair<int, int> gridStepFromCameraInput(
    const gameplay::world3d::camera::Gen4FollowCamera& camera,
    int input_dx,
    int input_dy) {
    if (input_dx == 0 && input_dy == 0) {
        return {0, 0};
    }

    const auto pose = camera.pose();
    const float forward_x = pose.forward.x;
    const float forward_z = pose.forward.z;
    const float right_x = pose.right.x;
    const float right_z = pose.right.z;

    // Up on screen follows camera forward; strafe follows camera right (matches Gen 4 field camera).
    const float world_x = forward_x * static_cast<float>(-input_dy) + right_x * static_cast<float>(input_dx);
    const float world_z = forward_z * static_cast<float>(-input_dy) + right_z * static_cast<float>(input_dx);
    const float mag = std::sqrt(world_x * world_x + world_z * world_z);
    if (mag < 1e-4f) {
        return {0, 0};
    }

    // Pick the grid axis that best matches the camera-relative XZ direction.
    // CharacterController: dx -> world +X (East), dy -> world +Z (South).
    struct GridDir {
        int dx;
        int dy;
        float wx;
        float wz;
    };
    static constexpr GridDir kDirs[] = {
        {0, -1, 0.0f, -1.0f}, // North
        {0, 1, 0.0f, 1.0f},   // South
        {1, 0, 1.0f, 0.0f},   // East
        {-1, 0, -1.0f, 0.0f}, // West
    };
    int best_dx = 0;
    int best_dy = 0;
    float best_dot = -2.0f;
    const float nx = world_x / mag;
    const float nz = world_z / mag;
    for (const GridDir& dir : kDirs) {
        const float dot = nx * dir.wx + nz * dir.wz;
        if (dot > best_dot) {
            best_dot = dot;
            best_dx = dir.dx;
            best_dy = dir.dy;
        }
    }
    return {best_dx, best_dy};
}

std::pair<int, int> stepForFacing(gameplay::world3d::FacingDirection facing) {
    switch (facing) {
        case gameplay::world3d::FacingDirection::North: return {0, -1};
        case gameplay::world3d::FacingDirection::South: return {0, 1};
        case gameplay::world3d::FacingDirection::East: return {1, 0};
        case gameplay::world3d::FacingDirection::West: return {-1, 0};
    }
    return {0, 1};
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
    movement_config_ = gameplay::world3d::characters::loadCharacterMovementConfig(project_root_);
    character_ = gameplay::world3d::data::loadCharacterDefinition(project_root_, scene_.player.character_path);
    player_ = gameplay::world3d::characters::CharacterController(
        scene_,
        movement_config_.walkSpeed(),
        movement_config_.turnStepDelaySeconds());
    animator_.~SpriteSheetAnimator();
    new (&animator_) gameplay::world3d::characters::SpriteSheetAnimator(character_);
    map_ = gameplay::world3d::rendering::OverworldMapRenderer(scene_);
    follower_summon_config_ = gameplay::world3d::followers::loadFollowerSummonConfig(project_root_);
    follower_session_config_ = gameplay::world3d::followers::loadFollowerSessionConfig(project_root_);
    npc_actor_driver_ = std::make_unique<gameplay::world3d::npc::NpcActorDriver>(project_root_, scene_);
    const std::vector<gameplay::world3d::npc::ResortPokemonSpawnInfo> resort_pokemon =
        npc_actor_driver_->resortPokemonSpawnList();
    follower_session_config_.enabled = !resort_pokemon.empty();
    if (follower_session_config_.enabled) {
        follower_session_config_.pokemon_charbin_path = resort_pokemon.front().character_package_path;
        follower_session_config_.pokemon_species = !resort_pokemon.front().species_slug.empty()
            ? resort_pokemon.front().species_slug
            : resort_pokemon.front().species_name;
        follower_session_config_.pokemon_form_id = resort_pokemon.front().form_id;
        follower_session_config_.pokemon_shiny = resort_pokemon.front().shiny;
    } else {
        follower_session_config_.pokemon_charbin_path.clear();
        follower_session_config_.pokemon_species.clear();
        follower_session_config_.pokemon_form_id = "default";
        follower_session_config_.pokemon_shiny = false;
    }
    follower_controller_ = std::make_unique<gameplay::world3d::followers::FollowerController>(
        project_root_,
        scene_,
        follower_summon_config_,
        follower_session_config_);
    if (follower_controller_) {
        follower_controller_->initializeResources();
    }
    gameplay::world3d::followers::NatureIdleLandingDustConfig landing_dust_config =
        follower_controller_
            ? gameplay::world3d::followers::loadNatureIdleBehaviorConfig(project_root_).landing_dust
            : gameplay::world3d::followers::NatureIdleLandingDustConfig{};
    if (follower_summon_config_.landing_dust.override_idle_config) {
        landing_dust_config.sprite_scale = follower_summon_config_.landing_dust.sprite_scale;
        landing_dust_config.screen_offset_y_px = follower_summon_config_.landing_dust.screen_offset_y_px;
        landing_dust_config.world_offset_x = follower_summon_config_.landing_dust.world_offset_x;
        landing_dust_config.world_offset_y = follower_summon_config_.landing_dust.world_offset_y;
        landing_dust_config.world_offset_z = follower_summon_config_.landing_dust.world_offset_z;
    }
    landing_dust_system_ = std::make_unique<gameplay::world3d::effects::LandingDustSystem>(
        project_root_,
        landing_dust_config);
    if (landing_dust_system_) {
        landing_dust_system_->initializeResources();
    }
    reloadWorldTerrainQueries();
    npc_actor_driver_->initializeDefaultSceneActors(player_.position());
    textbox_config_ = gameplay::world3d::dialogue::loadOverworldTextboxConfig(project_root_);
    transition_config_ = transitions::loadOverworldTransitionConfig(project_root_);
    textbox_controller_.hide();
    textbox_renderer_ =
        std::make_unique<gameplay::world3d::dialogue::OverworldTextboxRenderer>(project_root_, textbox_config_);
    interaction_script_catalog_ = gameplay::world3d::scripts::loadScriptCatalog(project_root_);
    interaction_text_catalog_ =
        gameplay::world3d::interactions::loadInteractionTextCatalog(project_root_);
    active_interaction_target_ = {};
    interaction_sequence_.cancel();
    interaction_exit_requested_ = false;
    interaction_pokemon_session_started_ = false;
    interaction_time_seconds_ = 0.0;
    logLoadedWorldChunks();
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
    gameplay::world3d::rendering::applySceneCameraScaleToCameraPreset(preset, scene_);
    follow_camera_base_preset_ = preset;
    camera_ = gameplay::world3d::camera::Gen4FollowCamera(preset);
    reloadFollowCameraPresetConfig();
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
    freecam_mouse_dragging_ = false;
    run_toggle_active_ = false;
    blocked_movement_sfx_requested_ = false;
    blocked_movement_repeat_seconds_ = 0.0;
    freecam_yaw_deg_ = scene_.freecam_initial_yaw_deg;
    freecam_pitch_deg_ = scene_.freecam_initial_pitch_deg;
    return_to_title_requested_ = false;
    sprite_renderer_.reset();
    bgfx_renderer_.reset();
    bgfx_init_failed_ = false;
    aib_texture_ = TextureHandle{};
    cached_aib_label_.clear();
    debug_font_ = FontHandle{};
    initialized_renderer_ = false;
}

void Overworld3DTestScreen::shutdownBgfx() {
    if (bgfx_renderer_) {
        bgfx_renderer_->shutdown();
    }
    bgfx_renderer_.reset();
    bgfx_init_failed_ = false;
}

void Overworld3DTestScreen::resetForNextLaunch() {
    SDL_SetRelativeMouseMode(SDL_FALSE);
    shutdownBgfx();
    initializeSceneState();
}

void Overworld3DTestScreen::reloadFollowCameraPresetConfig() {
    const fs::path config_path =
        fs::path(project_root_) / "config" / "gameplay" / "camera" / "follow_camera_presets.json";
    try {
        const JsonValue root = parseJsonFile(config_path.string());
        if (!root.isObject()) {
            std::cerr << "[Overworld3D] Follow camera preset config must be an object: "
                      << config_path << '\n';
            return;
        }
        const JsonValue* selected = root.get("selectedPreset");
        int selected_preset = 0;
        if (selected && selected->isNumber()) {
            selected_preset = static_cast<int>(selected->asNumber());
        } else if (selected && selected->isString()) {
            try {
                selected_preset = std::stoi(selected->asString());
            } catch (const std::exception&) {
                selected_preset = -1;
            }
        }
        if (selected_preset < 0 || selected_preset > 15) {
            std::cerr << "[Overworld3D] Follow camera selectedPreset must be 0..15: "
                      << config_path << '\n';
            return;
        }

        const JsonValue* presets = root.get("presets");
        const JsonValue* preset_value = presets && presets->isObject()
            ? presets->get(std::to_string(selected_preset))
            : nullptr;
        if (!preset_value || !preset_value->isObject()) {
            std::cerr << "[Overworld3D] Follow camera preset " << selected_preset
                      << " missing in " << config_path << '\n';
            return;
        }
        const JsonValue* pitch = preset_value->get("pitchDeg");
        if (!pitch || !pitch->isNumber()) {
            std::cerr << "[Overworld3D] Follow camera preset " << selected_preset
                      << " needs pitchDeg in " << config_path << '\n';
            return;
        }

        gameplay::world3d::camera::Gen4CameraPreset preset = follow_camera_base_preset_;
        preset.pitch_deg = static_cast<float>(pitch->asNumber());
        camera_ = gameplay::world3d::camera::Gen4FollowCamera(preset);
        camera_.setTarget(player_.position());
        std::cerr << "[Overworld3D] Reloaded follow camera preset "
                  << selected_preset
                  << " pitchDeg=" << preset.pitch_deg
                  << " from " << config_path << '\n';
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D] Could not reload follow camera preset config: "
                  << ex.what() << '\n';
    }
}

std::vector<gameplay::world3d::characters::LoadedWorldChunk> Overworld3DTestScreen::buildLoadedWorldChunks() const {
    std::vector<gameplay::world3d::characters::LoadedWorldChunk> chunks;
    const auto project_path = findMapProjectPath(fs::path(project_root_));
    if (!project_path) {
        chunks.push_back(gameplay::world3d::characters::LoadedWorldChunk{
            scene_.id.empty() ? std::string{"testing"} : scene_.id,
            scene_,
            0,
            0});
        return chunks;
    }

    struct MapEntry {
        std::string id;
        std::string file;
        int grid_x = 0;
        int grid_y = 0;
        gameplay::world3d::SceneConfig scene;
    };

    std::vector<MapEntry> entries;
    try {
        const JsonValue root = parseJsonFile(project_path->string());
        const JsonValue* maps = root.get("maps");
        if (!maps || !maps->isArray()) {
            throw std::runtime_error("map project has no maps array");
        }
        const fs::path maps_dir = fs::path(project_root_) / "assets" / "overworld" / "maps";
        for (const JsonValue& item : maps->asArray()) {
            if (!item.isObject()) {
                continue;
            }
            MapEntry entry;
            entry.id = jsonStringOr(item.get("id"), "");
            entry.file = jsonStringOr(item.get("file"), "");
            entry.grid_x = jsonIntOr(item.get("gridX"), 0);
            entry.grid_y = jsonIntOr(item.get("gridY"), 0);
            if (entry.file.empty()) {
                continue;
            }
            const fs::path map_path = maps_dir / entry.file;
            if (!fs::exists(map_path)) {
                std::cerr << "[Overworld3D] Map project entry missing file: "
                          << map_path << '\n';
                continue;
            }
            entry.scene = gameplay::world3d::data::loadSceneConfig(project_root_, map_path.string());
            if (entry.id.empty()) {
                entry.id = entry.scene.id.empty() ? entry.file : entry.scene.id;
            }
            entries.push_back(std::move(entry));
        }
    } catch (const std::exception& ex) {
        std::cerr << "[Overworld3D] Could not load map project "
                  << *project_path << ": " << ex.what() << '\n';
        chunks.push_back(gameplay::world3d::characters::LoadedWorldChunk{
            scene_.id.empty() ? std::string{"testing"} : scene_.id,
            scene_,
            0,
            0});
        return chunks;
    }

    if (entries.empty()) {
        chunks.push_back(gameplay::world3d::characters::LoadedWorldChunk{
            scene_.id.empty() ? std::string{"testing"} : scene_.id,
            scene_,
            0,
            0});
        return chunks;
    }

    std::map<int, int> column_widths;
    std::map<int, int> row_heights;
    for (const MapEntry& entry : entries) {
        column_widths[entry.grid_x] = std::max(column_widths[entry.grid_x], std::max(1, entry.scene.grid.width));
        row_heights[entry.grid_y] = std::max(row_heights[entry.grid_y], std::max(1, entry.scene.grid.height));
    }

    const auto origin_for_axis = [](int grid_coord, const std::map<int, int>& spans) {
        int origin = 0;
        if (grid_coord > 0) {
            for (int cell = 0; cell < grid_coord; ++cell) {
                const auto it = spans.find(cell);
                origin += it == spans.end() ? 16 : std::max(1, it->second);
            }
        } else if (grid_coord < 0) {
            for (int cell = grid_coord; cell < 0; ++cell) {
                const auto it = spans.find(cell);
                origin -= it == spans.end() ? 16 : std::max(1, it->second);
            }
        }
        return origin;
    };

    for (MapEntry& entry : entries) {
        chunks.push_back(gameplay::world3d::characters::LoadedWorldChunk{
            entry.id,
            std::move(entry.scene),
            origin_for_axis(entry.grid_x, column_widths),
            origin_for_axis(entry.grid_y, row_heights)});
    }

    return chunks;
}

std::vector<gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer::StaticMapChunk>
Overworld3DTestScreen::buildStaticRenderChunks() const {
    std::vector<gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer::StaticMapChunk> out;
    const float tile_size = std::max(1.0f, scene_.grid.tile_size);
    for (const gameplay::world3d::characters::LoadedWorldChunk& chunk : buildLoadedWorldChunks()) {
        if (chunk.origin_tile_x == 0 && chunk.origin_tile_y == 0) {
            continue;
        }
        out.push_back({
            chunk.scene,
            static_cast<float>(chunk.origin_tile_x) * tile_size,
            0.0f,
            static_cast<float>(chunk.origin_tile_y) * tile_size});
    }
    return out;
}

void Overworld3DTestScreen::reloadWorldTerrainQueries() {
    std::shared_ptr<gameplay::world3d::characters::CharacterTerrainQuery> terrain_query =
        gameplay::world3d::characters::makeLoadedWorldCharacterTerrainQuery(buildLoadedWorldChunks());
    player_.setTerrainQuery(terrain_query);
    if (follower_controller_) {
        follower_controller_->setTerrainQuery(terrain_query);
    }
    if (npc_actor_driver_) {
        npc_actor_driver_->setTerrainQuery(terrain_query);
    }
}

void Overworld3DTestScreen::logLoadedWorldChunks() const {
    const std::vector<gameplay::world3d::characters::LoadedWorldChunk> chunks = buildLoadedWorldChunks();
    std::cerr << "[Overworld3D] Loaded " << chunks.size() << " world chunks\n";
    for (const gameplay::world3d::characters::LoadedWorldChunk& chunk : chunks) {
        std::cerr << "[Overworld3D] chunk id=" << chunk.id
                  << " originTile=(" << chunk.origin_tile_x << ',' << chunk.origin_tile_y << ')'
                  << " size=" << chunk.scene.grid.width << 'x' << chunk.scene.grid.height
                  << '\n';
    }
}


void Overworld3DTestScreen::update(double dt) {
    transition_.update(dt);
    interaction_wait_remaining_ = std::max(0.0, interaction_wait_remaining_ - dt);
    if (transition_.consumeClosed()) open_attend_requested_ = true;
    interaction_time_seconds_ += dt;
    if (blocked_movement_repeat_seconds_ > 0.0) {
        blocked_movement_repeat_seconds_ = std::max(0.0, blocked_movement_repeat_seconds_ - dt);
    }

    int keyboard_dx = 0;
    int keyboard_dy = 0;
    bool player_running = false;
    bool blocked_movement_attempt = false;
    if (!freecam_enabled_) {
        const Uint8* keys = SDL_GetKeyboardState(nullptr);
        if (!interactionActive()) {
            if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) keyboard_dx -= 1;
            if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) keyboard_dx += 1;
            if (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) keyboard_dy -= 1;
            if (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]) keyboard_dy += 1;
        }
        if (keyboard_dx != 0 || keyboard_dy != 0) {
            input_dx_ = keyboard_dx;
            input_dy_ = keyboard_dy;
        }

        const bool run_held = anyBindingPressed(keys, app_config_.input.run_keys);
        player_running = character_.has_run && (run_held || run_toggle_active_);
        player_.setMoveSpeedUnitsPerSecond(player_running
            ? movement_config_.runSpeed()
            : movement_config_.walkSpeed());

        auto [grid_dx, grid_dy] = interactionActive()
            ? std::pair<int, int>{0, 0}
            : gridStepFromCameraInput(camera_, input_dx_, input_dy_);
        const auto move_result = player_.moveInput(
            grid_dx,
            grid_dy,
            dt,
            [this](int from_tx, int from_ty, int to_tx, int to_ty) {
                return !npc_actor_driver_ ||
                       npc_actor_driver_->canPlayerEnterTile(from_tx, from_ty, to_tx, to_ty);
            });
        blocked_movement_attempt = move_result.blocked;
        if (input_dx_ == 0 && input_dy_ == 0) {
            player_.stop();
        }

        const bool player_run_animation_active =
            player_running && player_.moving() && !blocked_movement_attempt;
        animator_.setFacing(player_.facing());
        animator_.setRunning(player_run_animation_active);
        if (blocked_movement_attempt && blocked_movement_repeat_seconds_ <= 0.0) {
            blocked_movement_sfx_requested_ = true;
            blocked_movement_repeat_seconds_ = movement_config_.blockedStepSfxRepeatSeconds();
        } else if (!blocked_movement_attempt) {
            blocked_movement_repeat_seconds_ = 0.0;
        }

        animator_.setMoving(player_.moving() || blocked_movement_attempt);
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
        if (keys[SDL_SCANCODE_SPACE]) {
            freecam_pos_.y += speed;
        }
        if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
            freecam_pos_.y -= speed;
        }
        camera_.setManualPose(freecam_pos_, freecam_yaw_deg_, freecam_pitch_deg_);
    }
    input_dx_ = 0;
    input_dy_ = 0;
    if (follower_controller_) {
        const bool player_idle = !player_.moving() && !freecam_enabled_;
        const bool player_activity =
            !interactionActive() && !freecam_enabled_ && (keyboard_dx != 0 || keyboard_dy != 0);
        follower_controller_->update(
            dt,
            player_.position(),
            player_.facing(),
            player_.movementSegment(),
            player_idle,
            player_activity,
            player_running);
        if (landing_dust_system_) {
            if (const auto spawn = follower_controller_->consumeLandingDustSpawn()) {
                landing_dust_system_->spawn(*spawn);
            }
        }
    }
    if (landing_dust_system_) {
        landing_dust_system_->update(dt);
    }
    if (npc_actor_driver_) {
        std::vector<std::pair<int, int>> reserved_tiles;
        reserved_tiles.push_back({player_.tileX(), player_.tileY()});
        const auto player_segment = player_.movementSegment();
        if (player_segment.active) {
            reserved_tiles.push_back({player_segment.to_x, player_segment.to_y});
        }
        const std::size_t player_reserved_tile_count = reserved_tiles.size();
        if (follower_controller_) {
            const std::vector<std::pair<int, int>> follower_tiles = follower_controller_->reservedTiles();
            reserved_tiles.insert(reserved_tiles.end(), follower_tiles.begin(), follower_tiles.end());
        }
        npc_actor_driver_->setReservedTiles(std::move(reserved_tiles), player_reserved_tile_count);
        npc_actor_driver_->update(dt);
    }
    updateInteractionSequence();
}

bool Overworld3DTestScreen::consumeBlockedMovementSfxRequested() {
    const bool requested = blocked_movement_sfx_requested_;
    blocked_movement_sfx_requested_ = false;
    return requested;
}

void Overworld3DTestScreen::render(SDL_Renderer* renderer) {
    if (wantsBgfxRenderer()) {
        return;
    }
    if (!initialized_renderer_) {
        sprite_renderer_ = std::make_unique<gameplay::world3d::rendering::BillboardSpriteRenderer>(
            renderer,
            scene_,
            character_,
            scene_.sprite_shadow);
        if (app_config_.enable_active_idle_behavior_debug) {
            const TitleScreenConfig title_config =
                loadConfigFromJson((fs::path(project_root_) / "config" / "title_screen.json").string());
            debug_font_ = loadFontPreferringUnicode(title_config.assets.font, 20, project_root_);
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
                player_.terrainBinding(),
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
        // Follower billboards render on the bgfx path only.
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
        int tiebreak; // models < follower < player when depths match, for stable results
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
    if (fd) drawables.push_back({*fd, 1, draw_follower});
    if (pd) drawables.push_back({*pd, 2, draw_player});

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
            scene_,
            camera_,
            w,
            h,
            scene_.lighting_tint_r,
            scene_.lighting_tint_g,
            scene_.lighting_tint_b,
            scene_.lighting_brightness);
    }

}

bool Overworld3DTestScreen::wantsBgfxRenderer() const {
    const std::string backend = lower(app_config_.renderer.world3d_backend);
    return !bgfx_init_failed_ && (backend == "bgfx" || backend == "auto");
}

bool Overworld3DTestScreen::isBgfxActive() const {
    return bgfx_renderer_ != nullptr && bgfx_renderer_->valid();
}

bool Overworld3DTestScreen::renderBgfx(
    SDL_Window* window,
    int framebuffer_w,
    int framebuffer_h,
    int logical_w,
    int logical_h,
    void* sdl_metal_view,
    const std::string& debug_frame_counter_label) {
    SDL_GetWindowSize(window, &pointer_window_w_, &pointer_window_h_);
    if (!wantsBgfxRenderer()) {
        return false;
    }
    if (!bgfx_renderer_) {
#if defined(__APPLE__)
        if (!sdl_metal_view) {
            std::cerr << "[Overworld3D] Refusing bgfx::init on macOS without SDL_MetalView. "
                      << "Create SDL_Metal_CreateView after releasing SDL_Renderer." << std::endl;
            return false;
        }
#endif
        bgfx_renderer_ =
            std::make_unique<gameplay::world3d::rendering::bgfx_backend::OverworldBgfxRenderer>(
                project_root_,
                scene_,
                character_);
        bgfx_renderer_->setStaticMapChunks(buildStaticRenderChunks());
        if (!bgfx_renderer_->initialize(
                window,
                framebuffer_w,
                framebuffer_h,
                app_config_.renderer.bgfx_preference,
                sdl_metal_view)) {
            std::cerr << "[Overworld3D] bgfx renderer initialization failed: "
                      << bgfx_renderer_->lastError()
                      << ". Falling back to SDL 3D renderer." << std::endl;
            bgfx_renderer_.reset();
            bgfx_init_failed_ = true;
            return false;
        }
    }
    if (!pending_bgfx_screenshot_.empty()) {
        bgfx_renderer_->queueScreenshot(pending_bgfx_screenshot_);
        pending_bgfx_screenshot_.clear();
    }
    std::vector<gameplay::world3d::rendering::CharacterBillboardDraw> character_draws;
    const int world_view_w = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseWidth(scene_)
        : logical_w;
    const int world_view_h = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseHeight(scene_)
        : logical_h;
    if (follower_controller_) {
        follower_controller_->collectBillboardDraws(camera_, world_view_w, world_view_h, character_draws);
    }
    if (npc_actor_driver_) {
        npc_actor_driver_->collectBillboardDraws(camera_, world_view_w, world_view_h, character_draws);
    }
    std::vector<gameplay::world3d::rendering::TextureBillboardDraw> texture_draws;
    if (landing_dust_system_) {
        landing_dust_system_->collectTextureBillboardDraws(
            scene_, camera_, world_view_w, world_view_h, texture_draws);
    }
    bgfx_renderer_->setTextboxOverlay(
        textbox_config_, textbox_controller_.active(), textbox_controller_.text());
    bgfx_renderer_->setAttendButtonOverlay(
        textbox_config_.attend_button_icon_path,
        attendButtonRect(),
        attendAvailable());
    float transition_x = static_cast<float>(logical_w) * 0.5f;
    float transition_y = static_cast<float>(logical_h) * 0.5f;
    float transition_depth = 0.0f;
    camera_.worldToScreen(player_.position(), logical_w, logical_h,
        transition_x, transition_y, transition_depth);
    bgfx_renderer_->setBlackIrisTransition(
        transition_x, transition_y,
        static_cast<float>(transition_.closedAmount()),
        transition_.active(),
        transition_.style().circle_segments,
        static_cast<float>(transition_.style().max_radius_scale));
    bgfx_renderer_->render(
        camera_,
        player_.position(),
        animator_.sourceRect(),
        animator_.activeActivityId(),
        animator_.running(),
        player_.terrainBinding(),
        logical_w,
        logical_h,
        framebuffer_w,
        framebuffer_h,
        character_draws,
        texture_draws,
        debug_frame_counter_label);
    return true;
}

void Overworld3DTestScreen::queueBgfxScreenshot(const std::string& output_path) {
    pending_bgfx_screenshot_ = output_path;
}

void Overworld3DTestScreen::renderPresentationOverlay(SDL_Renderer* renderer) {
    int logical_w = 0;
    int logical_h = 0;
    SDL_RenderGetLogicalSize(renderer, &logical_w, &logical_h);
    if (logical_w <= 0 || logical_h <= 0) {
        SDL_GetRendererOutputSize(renderer, &logical_w, &logical_h);
    }

    if (textbox_controller_.active() && textbox_renderer_ && !isBgfxActive()) {
        const SDL_Rect world_view = visibleWorldViewportRect(logical_w, logical_h);
        textbox_renderer_->render(
            renderer,
            world_view,
            gameplay::world3d::rendering::worldViewportBaseWidth(scene_),
            gameplay::world3d::rendering::worldViewportBaseHeight(scene_),
            textbox_controller_.text());
    }

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
    if (freecam_enabled_ || interactionActive()) {
        return;
    }
    input_dx_ = dx;
    input_dy_ = dy;
}

void Overworld3DTestScreen::onAdvancePressed() {
    if (freecam_enabled_) {
        return;
    }
    if (textbox_controller_.active()) {
        closeInteractionText();
        return;
    }
    if (interactionActive()) {
        return;
    }

    const auto target = findInteractionTarget();
    if (target.kind == gameplay::world3d::dialogue::OverworldTextboxController::TargetKind::None) {
        return;
    }
    if (!lockInteractionTarget(target)) {
        return;
    }
    beginInteraction(target);
}

bool Overworld3DTestScreen::handlePointerPressed(int logical_x, int logical_y) {
    const SDL_Point mapped = mapPointerToLogical(logical_x, logical_y);
    logical_x = mapped.x;
    logical_y = mapped.y;
    const SDL_Point point{logical_x, logical_y};
    const SDL_Rect attend_rect = attendButtonRect();
    if (attendAvailable() && SDL_PointInRect(&point, &attend_rect)) {
        attend_button_pressed_ = true;
        return true;
    }
    if (!freecam_enabled_) {
        return false;
    }
    freecam_mouse_dragging_ = true;
    return true;
}

bool Overworld3DTestScreen::handlePointerReleased(int logical_x, int logical_y) {
    const SDL_Point mapped = mapPointerToLogical(logical_x, logical_y);
    logical_x = mapped.x;
    logical_y = mapped.y;
    if (attend_button_pressed_) {
        attend_button_pressed_ = false;
        const SDL_Point point{logical_x, logical_y};
        const SDL_Rect rect = attendButtonRect();
        if (SDL_PointInRect(&point, &rect)) onAttendPressed();
        return true;
    }
    if (!freecam_enabled_) {
        return false;
    }
    freecam_mouse_dragging_ = false;
    return true;
}

void Overworld3DTestScreen::onAttendPressed() {
    if (attendAvailable() && !transition_.active()) {
        transition_.startClosing(transition_config_.attend);
    }
}

void Overworld3DTestScreen::onBackPressed() {
    if (interactionActive()) {
        if (textbox_controller_.active()) {
            textbox_controller_.hide();
        }
        requestInteractionExit();
        return;
    }
    if (freecam_enabled_) {
        freecam_enabled_ = false;
        freecam_mouse_dragging_ = false;
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
    return_to_title_requested_ = true;
}

bool Overworld3DTestScreen::consumeReturnToTitleRequested() {
    const bool value = return_to_title_requested_;
    return_to_title_requested_ = false;
    return value;
}

bool Overworld3DTestScreen::consumeOpenAttendRequested() {
    const bool value = open_attend_requested_;
    open_attend_requested_ = false;
    return value;
}

void Overworld3DTestScreen::resumeFromAttend() {
    const auto target = active_interaction_target_;
    transition_.startOpening(transition_config_.attend);
    if (interactionTargetKind(target) != gameplay::world3d::interactions::InteractionTargetKind::Pokemon) {
        finishInteraction();
        return;
    }
    // Preserve the player's interaction stay pose until the returned script reaches
    // its authored EXIT_INTERACTION step.
    textbox_controller_.hide();
    interaction_sequence_.cancel();
    if (lockInteractionTarget(target)) {
        after_pokemon_attend_context_ = true;
        beginInteraction(target, interaction_pokemon_session_started_);
        after_pokemon_attend_context_ = false;
    }
}

SDL_Rect Overworld3DTestScreen::attendButtonRect() const {
    const int logical_w = std::max(1, app_config_.window.virtual_width);
    return SDL_Rect{
        logical_w - textbox_config_.attend_button_right_px - textbox_config_.attend_button_width_px,
        textbox_config_.attend_button_top_px,
        textbox_config_.attend_button_width_px,
        textbox_config_.attend_button_height_px};
}

bool Overworld3DTestScreen::attendAvailable() const {
    return attend_enabled_for_interaction_ && textbox_config_.attend_button_enabled && textbox_controller_.active() &&
        active_interaction_target_.kind != gameplay::world3d::dialogue::OverworldTextboxController::TargetKind::None &&
        interactionTargetKind(active_interaction_target_) ==
            gameplay::world3d::interactions::InteractionTargetKind::Pokemon;
}

SDL_Point Overworld3DTestScreen::mapPointerToLogical(int x, int y) const {
    return mapOverlayPointerToLogical(
        x, y, pointer_window_w_, pointer_window_h_,
        app_config_.window.virtual_width, app_config_.window.virtual_height);
}

bool Overworld3DTestScreen::handleUnroutedSdlEvent(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && event.key.repeat == 0) {
        if (event.key.keysym.sym == SDLK_q) {
            freecam_enabled_ = !freecam_enabled_;
            freecam_mouse_dragging_ = false;
            if (!freecam_enabled_) {
                SDL_SetRelativeMouseMode(SDL_FALSE);
                camera_.setTarget(player_.position());
            } else {
                SDL_SetRelativeMouseMode(SDL_FALSE);
                freecam_pos_ = initialFreecamPosition(scene_, player_.position());
                camera_.setManualPose(freecam_pos_, freecam_yaw_deg_, freecam_pitch_deg_);
            }
            return true;
        }
        if (!freecam_enabled_ && event.key.keysym.sym == SDLK_1) {
            reloadFollowCameraPresetConfig();
            return true;
        }
        if (!freecam_enabled_ &&
            character_.has_run &&
            matchesBinding(event.key.keysym.sym, app_config_.input.run_toggle_keys)) {
            run_toggle_active_ = !run_toggle_active_;
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

    if (freecam_enabled_ && event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        freecam_mouse_dragging_ = true;
        return true;
    }
    if (freecam_enabled_ && event.type == SDL_MOUSEBUTTONUP && event.button.button == SDL_BUTTON_LEFT) {
        freecam_mouse_dragging_ = false;
        return true;
    }
    if (freecam_enabled_ && event.type == SDL_MOUSEMOTION && freecam_mouse_dragging_) {
        if ((event.motion.state & SDL_BUTTON_LMASK) == 0) {
            freecam_mouse_dragging_ = false;
            return true;
        }
        freecam_yaw_deg_ += static_cast<float>(event.motion.xrel) * scene_.freecam_mouse_sensitivity;
        freecam_pitch_deg_ -= static_cast<float>(event.motion.yrel) * scene_.freecam_mouse_sensitivity;
        if (freecam_pitch_deg_ > scene_.freecam_pitch_max_deg) freecam_pitch_deg_ = scene_.freecam_pitch_max_deg;
        if (freecam_pitch_deg_ < scene_.freecam_pitch_min_deg) freecam_pitch_deg_ = scene_.freecam_pitch_min_deg;
        return true;
    }
    return false;
}

gameplay::world3d::dialogue::OverworldTextboxController::Target
Overworld3DTestScreen::findInteractionTarget() const {
    using Target = gameplay::world3d::dialogue::OverworldTextboxController::Target;
    using TargetKind = gameplay::world3d::dialogue::OverworldTextboxController::TargetKind;
    const auto [dx, dy] = stepForFacing(player_.facing());
    const int tx = player_.tileX() + dx;
    const int ty = player_.tileY() + dy;

    if (npc_actor_driver_) {
        if (const auto actor_id = npc_actor_driver_->interactableActorIdAtTile(tx, ty)) {
            return Target{TargetKind::NpcActor, *actor_id};
        }
    }
    if (follower_controller_) {
        if (const auto follower_id = follower_controller_->interactionTargetIdAtTile(tx, ty)) {
            return Target{TargetKind::FollowerPokemon, *follower_id};
        }
    }
    return Target{};
}

bool Overworld3DTestScreen::lockInteractionTarget(
    const gameplay::world3d::dialogue::OverworldTextboxController::Target& target) {
    using TargetKind = gameplay::world3d::dialogue::OverworldTextboxController::TargetKind;
    if (npc_actor_driver_) {
        npc_actor_driver_->clearInteractionLockedActor();
    }
    if (follower_controller_) {
        follower_controller_->setInteractionLocked(false);
    }

    if (target.kind == TargetKind::NpcActor && npc_actor_driver_) {
        return npc_actor_driver_->setInteractionLockedActor(target.id);
    }
    if (target.kind == TargetKind::FollowerPokemon && follower_controller_) {
        return follower_controller_->setInteractionLocked(true);
    }
    return false;
}

bool Overworld3DTestScreen::interactionActive() const {
    return textbox_controller_.active() ||
        interaction_sequence_.active() ||
        interaction_exit_requested_;
}

gameplay::world3d::interactions::InteractionTargetKind
Overworld3DTestScreen::interactionTargetKind(
    const gameplay::world3d::dialogue::OverworldTextboxController::Target& target) const {
    using TargetKind = gameplay::world3d::dialogue::OverworldTextboxController::TargetKind;
    using InteractionTargetKind = gameplay::world3d::interactions::InteractionTargetKind;
    if (target.kind == TargetKind::FollowerPokemon) {
        return InteractionTargetKind::Pokemon;
    }
    if (target.kind == TargetKind::NpcActor && npc_actor_driver_) {
        const auto info = npc_actor_driver_->interactionActorInfo(target.id);
        if (info && info->kind == gameplay::world3d::npc::NpcActorKind::Pokemon) {
            return InteractionTargetKind::Pokemon;
        }
    }
    return InteractionTargetKind::Character;
}

void Overworld3DTestScreen::beginInteraction(
    const gameplay::world3d::dialogue::OverworldTextboxController::Target& target,
    bool preserve_player_interaction_session) {
    namespace interactions = gameplay::world3d::interactions;
    using TargetKind = gameplay::world3d::dialogue::OverworldTextboxController::TargetKind;
    active_interaction_target_ = target;
    interaction_exit_requested_ = false;
    interaction_pokemon_session_started_ = preserve_player_interaction_session;
    if (!preserve_player_interaction_session) {
        attend_enabled_for_interaction_ = true;
    }
    interactions::InteractionSourceRequest request{};
    request.target_kind = interactionTargetKind(target);
    const interactions::InteractionTextContext text_context = buildInteractionTextContext(target);
    request.script_context.tags.insert(text_context.tags.begin(), text_context.tags.end());
    if (after_pokemon_attend_context_) request.script_context.tags.insert("AFTER_POKEMON_ATTEND");
    request.script_context.nearest_tag_distance_tiles["PLAYER"] = 1;
    if (target.kind == TargetKind::NpcActor && npc_actor_driver_) {
        if (const auto info = npc_actor_driver_->interactionActorInfo(target.id)) {
            request.npc_mode = interactions::npcInteractionModeFromMetadata(info->npc_interaction_mode);
            request.runtime_script_id = info->runtime_interaction_script_id;
        }
    }
    const interactions::InteractionSourceResolution source = interactions::resolveInteractionSource(
        interaction_script_catalog_, request, interaction_script_cooldowns_, interaction_time_seconds_, interaction_rng_);
    interaction_sequence_.begin(request.target_kind, source.behavior);
    updateInteractionSequence();
}

gameplay::world3d::interactions::InteractionTextContext
Overworld3DTestScreen::buildInteractionTextContext(
    const gameplay::world3d::dialogue::OverworldTextboxController::Target& target) const {
    namespace interactions = gameplay::world3d::interactions;
    using TargetKind = gameplay::world3d::dialogue::OverworldTextboxController::TargetKind;
    interactions::InteractionTextContext context{};
    const auto add_tag = [&context](const std::string& tag) {
        context.tags.insert(interactions::normalizeInteractionTag(tag));
    };
    add_tag(interactionTargetKind(target) == interactions::InteractionTargetKind::Pokemon
        ? "POKEMON"
        : "CHARACTER");
    add_tag("TILE_GROUND");
    context.variables["PLAYER_NAME"] = "Player";
    context.variables["ROLE"] = interactionTargetKind(target) == interactions::InteractionTargetKind::Pokemon
        ? "Pokemon"
        : "Character";
    context.variables["TILE"] = "ground";
    context.variables["TYPE_PRIMARY"] = "";
    context.variables["TYPE_SECONDARY"] = "";
    context.variables["NATURE"] = "";
    context.variables["FORM"] = "";
    context.variables["COMPANION_NAME"] = "";
    context.variables["NEARBY_POKEMON_NAME"] = "";
    if (target.kind == TargetKind::FollowerPokemon && follower_controller_) {
        if (const auto info = follower_controller_->interactionInfo()) {
            add_tag("POKEMON");
            context.variables["NAME"] = info->display_name;
            context.variables["SPECIES"] = info->species_name;
            if (!info->species_name.empty()) add_tag("SPECIES_" + info->species_name);
            if (!info->pokemon_types.empty()) context.variables["TYPE_PRIMARY"] = info->pokemon_types.front();
            if (info->pokemon_types.size() > 1) context.variables["TYPE_SECONDARY"] = info->pokemon_types[1];
            for (const std::string& type : info->pokemon_types) {
                add_tag("TYPE_" + type);
            }
        }
    } else if (target.kind == TargetKind::NpcActor && npc_actor_driver_) {
        if (const auto info = npc_actor_driver_->interactionActorInfo(target.id)) {
            if (info->kind == gameplay::world3d::npc::NpcActorKind::Pokemon) {
                add_tag("POKEMON");
            }
            context.variables["NAME"] = info->display_name;
            context.variables["SPECIES"] = info->species_name;
            if (!info->species_name.empty()) add_tag("SPECIES_" + info->species_name);
            if (!info->pokemon_types.empty()) context.variables["TYPE_PRIMARY"] = info->pokemon_types.front();
            if (info->pokemon_types.size() > 1) context.variables["TYPE_SECONDARY"] = info->pokemon_types[1];
            for (const std::string& type : info->pokemon_types) {
                add_tag("TYPE_" + type);
            }
        }
    }
    return context;
}

std::string Overworld3DTestScreen::characterDialogueForTarget(
    const gameplay::world3d::dialogue::OverworldTextboxController::Target& target) {
    using TargetKind = gameplay::world3d::dialogue::OverworldTextboxController::TargetKind;
    if (target.kind != TargetKind::NpcActor || !npc_actor_driver_) return {};
    const auto info = npc_actor_driver_->interactionActorInfo(target.id);
    if (!info || info->kind != gameplay::world3d::npc::NpcActorKind::Human || info->dialogue_lines.empty()) return {};
    std::uniform_int_distribution<std::size_t> pick(0, info->dialogue_lines.size() - 1U);
    return gameplay::world3d::interactions::renderInteractionTextTemplate(info->dialogue_lines[pick(interaction_rng_)],
        buildInteractionTextContext(target).variables);
}

void Overworld3DTestScreen::updateInteractionSequence() {
    namespace interactions = gameplay::world3d::interactions;
    using TargetKind = gameplay::world3d::dialogue::OverworldTextboxController::TargetKind;
    if (interaction_wait_remaining_ > 0.0) return;
    if (interaction_exit_requested_) {
        if (animator_.activityFinished()) {
            finishInteraction();
        }
        return;
    }
    if (textbox_controller_.active() || !interaction_sequence_.active()) {
        if (!textbox_controller_.active() && !interaction_sequence_.active() &&
            active_interaction_target_.kind != TargetKind::None) {
            requestInteractionExit();
        }
        return;
    }

    while (const interactions::InteractionAction* action = interaction_sequence_.currentAction()) {
        switch (action->kind) {
            case interactions::InteractionActionKind::Wait:
                if (interaction_wait_remaining_ <= 0.0) {
                    interaction_wait_remaining_ = action->duration_seconds;
                    interaction_sequence_.advance();
                }
                return;
            case interactions::InteractionActionKind::Jump:
                if (active_interaction_target_.kind == TargetKind::FollowerPokemon && follower_controller_) {
                    follower_controller_->triggerInteractionJump(action->height_pixels);
                } else if (active_interaction_target_.kind == TargetKind::NpcActor && npc_actor_driver_) {
                    npc_actor_driver_->triggerInteractionJump(action->height_pixels);
                }
                interaction_sequence_.advance();
                break;
            case interactions::InteractionActionKind::DisableAttend:
                attend_enabled_for_interaction_ = false;
                interaction_sequence_.advance();
                break;
            case interactions::InteractionActionKind::EnableAttend:
                attend_enabled_for_interaction_ = true;
                interaction_sequence_.advance();
                break;
            case interactions::InteractionActionKind::ExitInteraction:
                requestInteractionExit();
                return;
            case interactions::InteractionActionKind::FacePlayer:
                if (active_interaction_target_.kind == TargetKind::FollowerPokemon && follower_controller_) {
                    follower_controller_->faceInteractionLockedTowardTile(player_.tileX(), player_.tileY());
                } else if (active_interaction_target_.kind == TargetKind::NpcActor && npc_actor_driver_) {
                    npc_actor_driver_->faceInteractionLockedActorTowardTile(player_.tileX(), player_.tileY());
                }
                interaction_sequence_.advance();
                break;
            case interactions::InteractionActionKind::FaceDirection:
                if (active_interaction_target_.kind == TargetKind::FollowerPokemon && follower_controller_) {
                    follower_controller_->faceInteractionLocked(action->direction);
                } else if (active_interaction_target_.kind == TargetKind::NpcActor && npc_actor_driver_) {
                    npc_actor_driver_->faceInteractionLockedActor(action->direction);
                }
                interaction_sequence_.advance();
                break;
            case interactions::InteractionActionKind::FaceAway: {
                const auto away = interactions::oppositeFacing(interactions::facingTowardTiles(
                    player_.tileX(),
                    player_.tileY(),
                    player_.tileX(),
                    player_.tileY(),
                    player_.facing()));
                if (active_interaction_target_.kind == TargetKind::FollowerPokemon && follower_controller_) {
                    follower_controller_->faceInteractionLocked(away);
                } else if (active_interaction_target_.kind == TargetKind::NpcActor && npc_actor_driver_) {
                    npc_actor_driver_->faceInteractionLockedActor(away);
                }
                interaction_sequence_.advance();
                break;
            }
            case interactions::InteractionActionKind::PokemonInteractionSession:
                if (interactionTargetKind(active_interaction_target_) != interactions::InteractionTargetKind::Pokemon) {
                    interaction_sequence_.advance();
                    break;
                }
                if (!interaction_pokemon_session_started_) {
                    std::string pokemon_size = "small";
                    if (active_interaction_target_.kind == TargetKind::FollowerPokemon && follower_controller_) {
                        if (const auto info = follower_controller_->interactionInfo()) {
                            pokemon_size = info->pokemon_size;
                        }
                    } else if (active_interaction_target_.kind == TargetKind::NpcActor && npc_actor_driver_) {
                        if (const auto info = npc_actor_driver_->interactionActorInfo(active_interaction_target_.id)) {
                            pokemon_size = info->pokemon_size;
                        }
                    }
                    const std::optional<std::string> activity_id =
                        interactions::interactionActivityIdForPokemonSize(pokemon_size);
                    if (!activity_id) {
                        interaction_sequence_.advance();
                        break;
                    }
                    interaction_pokemon_session_started_ = animator_.startActivitySession(*activity_id);
                    if (!interaction_pokemon_session_started_) {
                        interaction_sequence_.advance();
                        break;
                    }
                }
                if (animator_.activityStayActive()) {
                    interaction_sequence_.advance();
                } else {
                    return;
                }
                break;
            case interactions::InteractionActionKind::TextLiteral:
            case interactions::InteractionActionKind::TextFree: {
                if (!gameplay::world3d::dialogue::overworldTextboxEnabled(textbox_config_)) {
                    interaction_sequence_.advance();
                    break;
                }
                std::string text = action->value;
                if (action->kind == interactions::InteractionActionKind::TextLiteral) {
                    text = interactions::renderInteractionTextTemplate(
                        text, buildInteractionTextContext(active_interaction_target_).variables);
                }
                if (action->kind == interactions::InteractionActionKind::TextFree) {
                    if (interactionTargetKind(active_interaction_target_) == interactions::InteractionTargetKind::Character) {
                        text = characterDialogueForTarget(active_interaction_target_);
                    } else {
                        interactions::InteractionTextContext context = buildInteractionTextContext(active_interaction_target_);
                        const interactions::InteractionTextSelection selection = interactions::selectInteractionText(
                            interaction_text_catalog_,
                            context,
                            interaction_text_cooldowns_,
                            interaction_time_seconds_,
                            interaction_rng_);
                        text = selection.found ? selection.body : std::string{};
                    }
                }
                textbox_controller_.show(active_interaction_target_, std::move(text));
                return;
            }
            case interactions::InteractionActionKind::Cry:
            case interactions::InteractionActionKind::Emoticon:
                interaction_sequence_.advance();
                break;
        }
    }
}

void Overworld3DTestScreen::closeInteractionText() {
    textbox_controller_.hide();
    interaction_sequence_.advance();
    updateInteractionSequence();
}

void Overworld3DTestScreen::requestInteractionExit() {
    if (active_interaction_target_.kind == gameplay::world3d::dialogue::OverworldTextboxController::TargetKind::None) {
        return;
    }
    interaction_sequence_.cancel();
    if (interaction_pokemon_session_started_) {
        animator_.requestActivityExit();
        interaction_exit_requested_ = true;
        return;
    }
    finishInteraction();
}

void Overworld3DTestScreen::finishInteraction() {
    active_interaction_target_ = {};
    interaction_sequence_.cancel();
    interaction_exit_requested_ = false;
    interaction_pokemon_session_started_ = false;
    attend_enabled_for_interaction_ = false;
    clearInteractionTextBox();
}

void Overworld3DTestScreen::clearInteractionTextBox() {
    textbox_controller_.hide();
    if (npc_actor_driver_) {
        npc_actor_driver_->clearInteractionLockedActor();
    }
    if (follower_controller_) {
        follower_controller_->setInteractionLocked(false);
    }
}

SDL_Rect Overworld3DTestScreen::visibleWorldViewportRect(int logical_w, int logical_h) const {
    logical_w = std::max(1, logical_w);
    logical_h = std::max(1, logical_h);
    if (!scene_.world_viewport.enabled) {
        return SDL_Rect{0, 0, logical_w, logical_h};
    }

    const int base_w = gameplay::world3d::rendering::worldViewportBaseWidth(scene_);
    const int base_h = gameplay::world3d::rendering::worldViewportBaseHeight(scene_);
    const double sx = static_cast<double>(logical_w) / static_cast<double>(std::max(1, base_w));
    const double sy = static_cast<double>(logical_h) / static_cast<double>(std::max(1, base_h));
    const double scale = std::min(sx, sy);
    const int dest_w = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_w) * scale)));
    const int dest_h = std::max(1, static_cast<int>(std::lround(static_cast<double>(base_h) * scale)));
    return SDL_Rect{(logical_w - dest_w) / 2, (logical_h - dest_h) / 2, dest_w, dest_h};
}

} // namespace pr
