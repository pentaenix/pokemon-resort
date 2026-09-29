#include "ui/Overworld3DTestScreen.hpp"

#include "core/app/AppPaths.hpp"
#include "core/config/ConfigLoader.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace pr {
namespace fs = std::filesystem;
namespace world_decor = gameplay::world3d::decorations;

namespace {

std::string profileId(const PersistenceConfig& persistence) {
    std::string profile = fs::path(persistence.resort_profile_file_name).stem().string();
    if (profile.empty()) profile = "default";
    for (char& value : profile) {
        const bool safe = (value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
            value == '-' || value == '_';
        if (!safe) value = '_';
    }
    return profile;
}

} // namespace

void Overworld3DTestScreen::loadWorldDecorations() {
    world_decoration_document_ = {};
    world_decoration_read_only_ = false;
    if (!world_decoration_catalog_.load(project_root_, world_decoration_error_)) {
        world_decoration_read_only_ = true;
        std::cerr << "[WorldBuild] Catalog unavailable: " << world_decoration_error_ << '\n';
        return;
    }
    const PersistenceConfig persistence = loadConfigFromJson(
        (fs::path(project_root_) / "config/title_screen.json").string()).persistence;
    const fs::path path = resolveSaveDirectory(persistence, project_root_) /
        "world" / profileId(persistence) / "decorations.json";
    world_decoration_store_ = std::make_unique<world_decor::Store>(path);
    if (world_decoration_store_->exists()) {
        auto loaded = world_decoration_store_->load(world_decoration_error_);
        if (!loaded) {
            world_decoration_read_only_ = true;
            std::cerr << "[WorldBuild] Save is unreadable; build mode is read-only: "
                      << world_decoration_error_ << '\n';
            return;
        }
        world_decoration_document_ = std::move(*loaded);
    }
    applyWorldDecorationsToScene();
}

void Overworld3DTestScreen::applyWorldDecorationsToScene() {
    scene_.models.erase(std::remove_if(scene_.models.begin(), scene_.models.end(), [](const auto& model) {
        return model.id.rfind("player-decoration:", 0) == 0;
    }), scene_.models.end());
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    for (const world_decor::Placement& placement : world_decoration_document_.placements) {
        if (placement.map_id != map_id) continue;
        const world_decor::Asset* asset = world_decoration_catalog_.resolve(placement.asset_id);
        if (!asset) continue;
        gameplay::world3d::ModelPlacementConfig model;
        model.id = "player-decoration:" + placement.id;
        model.glb_path = asset->glb_path.string();
        model.x = (static_cast<float>(placement.cell_x) + 0.5f) * scene_.grid.tile_size;
        model.z = (static_cast<float>(placement.cell_y) + 0.5f) * scene_.grid.tile_size;
        model.y = gameplay::world3d::terrain::heightAtTileCenter(
            scene_, placement.cell_x, placement.cell_y);
        model.yaw_deg = static_cast<float>(placement.yaw_quarter_turns * 90);
        model.scale = asset->scale;
        scene_.models.push_back(std::move(model));
    }
}

std::vector<gameplay::world3d::ModelPlacementConfig>
Overworld3DTestScreen::worldDecorationModels() const {
    std::vector<world_decor::Placement> placements;
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    for (const auto& placement : world_decoration_document_.placements) {
        if (placement.map_id == map_id) placements.push_back(placement);
    }
    if (world_decoration_editor_.active() && world_decoration_editor_.mapId() == map_id) {
        placements = world_decoration_editor_.placements();
    }
    std::vector<gameplay::world3d::ModelPlacementConfig> models;
    models.reserve(placements.size());
    for (const auto& placement : placements) {
        const world_decor::Asset* asset = world_decoration_catalog_.resolve(placement.asset_id);
        if (!asset) continue;
        gameplay::world3d::ModelPlacementConfig model;
        model.id = "player-decoration:" + placement.id;
        model.glb_path = asset->glb_path.string();
        model.x = (placement.cell_x + 0.5f) * scene_.grid.tile_size;
        model.z = (placement.cell_y + 0.5f) * scene_.grid.tile_size;
        model.y = gameplay::world3d::terrain::heightAtTileCenter(
            scene_, placement.cell_x, placement.cell_y);
        model.yaw_deg = static_cast<float>(placement.yaw_quarter_turns * 90);
        model.scale = asset->scale;
        models.push_back(std::move(model));
    }
    return models;
}

void Overworld3DTestScreen::refreshWorldDecorationRenderModels() {
    if (bgfx_renderer_) bgfx_renderer_->setWorldDecorationModels(worldDecorationModels());
}

bool Overworld3DTestScreen::beginWorldBuildMode() {
    if (world_decoration_read_only_ || world_decoration_catalog_.entries().empty() ||
        freecam_enabled_ || interactionActive() || aquarium_construction_.active() ||
        decoration_editor_.active() || transition_.active()) return false;
    const auto chunk = std::find_if(loaded_world_chunks_.begin(), loaded_world_chunks_.end(),
        [&](const auto& candidate) {
            return (candidate.id.empty() ? candidate.scene.id : candidate.id) == active_world_map_id_;
        });
    if (chunk == loaded_world_chunks_.end() || !chunk->editable_land) {
        world_decoration_error_ = "This map is protected";
        std::cerr << "[WorldBuild] " << world_decoration_error_ << " map="
                  << active_world_map_id_ << '\n';
        return false;
    }
    std::vector<world_decor::Placement> placements;
    for (const auto& placement : world_decoration_document_.placements) {
        if (placement.map_id == active_world_map_id_) placements.push_back(placement);
    }
    world_decoration_editor_.open(
        active_world_map_id_, scene_, std::move(placements), world_decoration_catalog_,
        player_.tileX() - chunk->origin_tile_x,
        player_.tileY() - chunk->origin_tile_y);
    world_decoration_category_index_ = 2;
    world_decoration_page_ = 0;
    world_decoration_catalog_open_ = false;
    const auto nature = world_decoration_catalog_.indices("nature");
    if (!nature.empty()) world_decoration_editor_.selectAsset(static_cast<int>(nature.front()));
    player_.stop();
    animator_.setMoving(false);
    input_dx_ = 0;
    input_dy_ = 0;
    world_build_saved_camera_ = camera_;
    const auto pose = camera_.pose();
    world_build_camera_yaw_deg_ = std::atan2(pose.forward.x, pose.forward.z) *
        (180.0f / 3.1415926535f);
    world_build_camera_pitch_deg_ = std::asin(std::clamp(pose.forward.y, -1.0f, 1.0f)) *
        (180.0f / 3.1415926535f);
    world_build_camera_distance_ = std::clamp(
        pose.preset.distance, scene_.grid.tile_size * 6.0f, scene_.grid.tile_size * 60.0f);
    world_build_camera_pan_x_ = 0.0f;
    world_build_camera_pan_z_ = 0.0f;
    world_build_camera_dragging_ = false;
    world_build_camera_panning_ = false;
    applyWorldBuildCamera();
    std::cerr << "[WorldBuild] Entered map=" << active_world_map_id_ << '\n';
    return true;
}

bool Overworld3DTestScreen::finishWorldBuildMode() {
    if (!world_decoration_editor_.active() || !world_decoration_store_) return false;
    world_decor::Document candidate = world_decoration_document_;
    candidate.placements.erase(std::remove_if(candidate.placements.begin(), candidate.placements.end(),
        [&](const auto& placement) { return placement.map_id == world_decoration_editor_.mapId(); }),
        candidate.placements.end());
    candidate.placements.insert(candidate.placements.end(),
        world_decoration_editor_.placements().begin(), world_decoration_editor_.placements().end());
    ++candidate.revision;
    if (!world_decoration_store_->save(candidate, world_decoration_error_)) {
        std::cerr << "[WorldBuild] Save failed: " << world_decoration_error_ << '\n';
        return false;
    }
    world_decoration_document_ = std::move(candidate);
    world_decoration_editor_.close();
    if (world_build_saved_camera_) camera_ = *world_build_saved_camera_;
    world_build_saved_camera_.reset();
    world_build_camera_dragging_ = false;
    world_build_camera_panning_ = false;
    applyWorldDecorationsToScene();
    reloadWorldTerrainQueries();
    refreshWorldDecorationRenderModels();
    placed_models_.clear();
    placed_models_load_attempted_ = false;
    std::cerr << "[WorldBuild] Saved revision=" << world_decoration_document_.revision << '\n';
    return true;
}

void Overworld3DTestScreen::cancelWorldBuildMode() {
    if (!world_decoration_editor_.active()) return;
    world_decoration_editor_.close();
    refreshWorldDecorationRenderModels();
    world_decoration_error_.clear();
    if (world_build_saved_camera_) camera_ = *world_build_saved_camera_;
    else camera_.setTarget(player_.position());
    world_build_saved_camera_.reset();
    world_build_camera_dragging_ = false;
    world_build_camera_panning_ = false;
    std::cerr << "[WorldBuild] Cancelled\n";
}

void Overworld3DTestScreen::applyWorldBuildCamera() {
    if (!world_decoration_editor_.active()) return;
    constexpr float kRadians = 3.1415926535f / 180.0f;
    const float yaw = world_build_camera_yaw_deg_ * kRadians;
    const float pitch = world_build_camera_pitch_deg_ * kRadians;
    const gameplay::world3d::camera::Vec3 forward{
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)};
    const int cell_x = world_decoration_editor_.cursorX();
    const int cell_y = world_decoration_editor_.cursorY();
    gameplay::world3d::camera::Vec3 target{
        (cell_x + 0.5f) * scene_.grid.tile_size + world_build_camera_pan_x_,
        gameplay::world3d::terrain::heightAtTileCenter(scene_, cell_x, cell_y),
        (cell_y + 0.5f) * scene_.grid.tile_size + world_build_camera_pan_z_};
    const gameplay::world3d::camera::Vec3 position{
        target.x - forward.x * world_build_camera_distance_,
        target.y - forward.y * world_build_camera_distance_,
        target.z - forward.z * world_build_camera_distance_};
    camera_.setManualPose(position, world_build_camera_yaw_deg_, world_build_camera_pitch_deg_);
}

void Overworld3DTestScreen::panWorldBuildCamera(int dx, int dy) {
    const float amount = std::max(1.0f, scene_.grid.tile_size);
    world_build_camera_pan_x_ += dx * amount;
    world_build_camera_pan_z_ += dy * amount;
    applyWorldBuildCamera();
}

void Overworld3DTestScreen::rotateWorldBuildCamera(float degrees) {
    world_build_camera_yaw_deg_ += degrees;
    while (world_build_camera_yaw_deg_ > 180.0f) world_build_camera_yaw_deg_ -= 360.0f;
    while (world_build_camera_yaw_deg_ < -180.0f) world_build_camera_yaw_deg_ += 360.0f;
    applyWorldBuildCamera();
}

void Overworld3DTestScreen::zoomWorldBuildCamera(float steps) {
    const float factor = std::pow(0.88f, steps);
    world_build_camera_distance_ = std::clamp(world_build_camera_distance_ * factor,
        scene_.grid.tile_size * 6.0f, scene_.grid.tile_size * 60.0f);
    applyWorldBuildCamera();
}

void Overworld3DTestScreen::onWorldBuildModePressed() {
    if (aquarium_door_loading_active_) return;
    if (world_decoration_editor_.active()) {
        finishWorldBuildMode();
    } else {
        beginWorldBuildMode();
    }
}

bool Overworld3DTestScreen::handleWorldBuildPointer(int x, int y) {
    if (!world_decoration_editor_.active()) return false;
    const auto controls = gameplay::world3d::aquarium::decorations::worldBuildHudLayout(
        app_config_.window.virtual_width, app_config_.window.virtual_height);
    const SDL_Point point{x, y};
    const auto hit = [&](const SDL_Rect& rect) { return SDL_PointInRect(&point, &rect); };
    if (hit(controls.catalog)) { world_decoration_catalog_open_ = !world_decoration_catalog_open_; return true; }
    if (hit(controls.save)) { finishWorldBuildMode(); return true; }
    if (hit(controls.cancel)) { cancelWorldBuildMode(); return true; }
    if (hit(controls.place)) {
        if (!world_decoration_editor_.place()) requestAquariumConstructionErrorFeedback();
        else refreshWorldDecorationRenderModels();
        return true;
    }
    if (hit(controls.rotate)) { world_decoration_editor_.rotate(); return true; }
    if (hit(controls.erase)) {
        if (!world_decoration_editor_.erase()) requestAquariumConstructionErrorFeedback();
        else refreshWorldDecorationRenderModels();
        return true;
    }
    if (hit(controls.camera_pan)) {
        world_build_camera_panning_ = true;
        world_build_camera_pointer_ = point;
        world_build_camera_pan_origin_x_ = world_build_camera_pan_x_;
        world_build_camera_pan_origin_z_ = world_build_camera_pan_z_;
        return true;
    }
    if (hit(controls.zoom_in)) { zoomWorldBuildCamera(1.0f); return true; }
    if (hit(controls.zoom_out)) { zoomWorldBuildCamera(-1.0f); return true; }
    if (!world_decoration_catalog_open_) return true;
    const auto layout = gameplay::world3d::aquarium::decorations::roomAssetTrayLayout(
        app_config_.window.virtual_width, app_config_.window.virtual_height);
    for (int category = 0; category < 4; ++category) {
        if (!SDL_PointInRect(&point, &layout.categories[category])) continue;
        world_decoration_category_index_ = category;
        world_decoration_page_ = 0;
        const auto entries = world_decoration_catalog_.indices(world_decor::kCategories[category]);
        if (!entries.empty()) world_decoration_editor_.selectAsset(static_cast<int>(entries.front()));
        return true;
    }
    const auto entries = world_decoration_catalog_.indices(
        world_decor::kCategories[world_decoration_category_index_]);
    const int pages = std::max(1, (static_cast<int>(entries.size()) + 5) / 6);
    if (SDL_PointInRect(&point, &layout.previous)) {
        world_decoration_page_ = (world_decoration_page_ + pages - 1) % pages;
        return true;
    }
    if (SDL_PointInRect(&point, &layout.next)) {
        world_decoration_page_ = (world_decoration_page_ + 1) % pages;
        return true;
    }
    for (int slot = 0; slot < 6; ++slot) {
        if (!SDL_PointInRect(&point, &layout.assets[slot])) continue;
        const int visible_index = world_decoration_page_ * 6 + slot;
        if (visible_index < static_cast<int>(entries.size())) {
            world_decoration_editor_.selectAsset(static_cast<int>(entries[visible_index]));
        }
        return true;
    }
    return y >= layout.panel.y;
}

std::vector<pr::aquarium::geometry::GridCell>
Overworld3DTestScreen::worldDecorationCollisionCells() const {
    std::vector<pr::aquarium::geometry::GridCell> result;
    for (const auto& placement : world_decoration_document_.placements) {
        const auto chunk = std::find_if(active_world_chunks_.begin(), active_world_chunks_.end(),
            [&](const auto& candidate) {
                return (candidate.id.empty() ? candidate.scene.id : candidate.id) == placement.map_id;
            });
        if (chunk != active_world_chunks_.end()) {
            result.push_back({chunk->origin_tile_x + placement.cell_x,
                chunk->origin_tile_y + placement.cell_y});
        }
    }
    return result;
}

} // namespace pr
