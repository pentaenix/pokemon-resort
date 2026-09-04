#include "ui/Overworld3DTestScreen.hpp"

#include "core/app/AppPaths.hpp"
#include "core/config/ConfigLoader.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionCamera.hpp"
#include "gameplay/world3d/rendering/PixelScale.hpp"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace pr {

namespace {

std::string aquariumProfileId(const PersistenceConfig& config) {
    std::string id = std::filesystem::path(
        config.resort_profile_file_name).stem().string();
    if (id.empty()) id = "default";
    for (char& c : id) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!safe) c = '_';
    }
    return id;
}

std::vector<pr::aquarium::geometry::GridCell> authoredObstacleCells(
    const gameplay::world3d::SceneConfig& scene) {
    std::vector<pr::aquarium::geometry::GridCell> obstacles;
    for (int row = 0; row < static_cast<int>(scene.terrain.collision.size()); ++row) {
        for (int column = 0;
             column < static_cast<int>(scene.terrain.collision[row].size()); ++column) {
            if (scene.terrain.collision[row][column] != 0) {
                obstacles.push_back({column, row});
            }
        }
    }
    return obstacles;
}

gameplay::world3d::FacingDirection constructionReturnFacing(
    const std::string& facing) {
    if (facing == "north") return gameplay::world3d::FacingDirection::North;
    if (facing == "east") return gameplay::world3d::FacingDirection::East;
    if (facing == "west") return gameplay::world3d::FacingDirection::West;
    return gameplay::world3d::FacingDirection::South;
}

const char* aquariumCommandName(
    gameplay::world3d::aquarium::construction::AquariumCommandKind kind) {
    using Kind = gameplay::world3d::aquarium::construction::AquariumCommandKind;
    switch (kind) {
    case Kind::CreateTank: return "create_tank";
    case Kind::EditTank: return "edit_tank";
    case Kind::DeleteTank: return "delete_tank";
    case Kind::EditTankSet: return "edit_tank_set";
    }
    return "unknown";
}

const char* aquariumHistoryActionName(
    gameplay::world3d::aquarium::construction::ConstructionHistoryAction action) {
    using Action = gameplay::world3d::aquarium::construction::ConstructionHistoryAction;
    switch (action) {
    case Action::RecordNew: return "record_new";
    case Action::Undo: return "undo";
    case Action::Redo: return "redo";
    }
    return "unknown";
}

const char* aquariumLoadStatusName(
    gameplay::world3d::aquarium::construction::AquariumStoreLoadStatus status) {
    using Status = gameplay::world3d::aquarium::construction::AquariumStoreLoadStatus;
    switch (status) {
    case Status::Missing: return "missing";
    case Status::Loaded: return "loaded";
    case Status::RecoveredBackup: return "recovered_backup";
    case Status::RecoveredPrevious: return "recovered_previous";
    case Status::RecoveredTemporary: return "recovered_temporary";
    case Status::NewerVersion: return "newer_version";
    case Status::Invalid: return "invalid";
    }
    return "unknown";
}

bool aquariumUsesComplexGeometry(
    const gameplay::world3d::aquarium::construction::AquariumDesignDocument& document) {
    return std::any_of(document.tanks.begin(), document.tanks.end(), [](const auto& tank) {
        return tank.corner_radius_steps > 0 || !tank.corner_radii.empty() ||
            !tank.tunnels.empty() ||
            tank.footprint.shape != pr::aquarium::geometry::FootprintShape::Rectangle ||
            !tank.footprint.subtracted_cells.empty();
    });
}

} // namespace

void Overworld3DTestScreen::reloadAquariumConfig(bool force) {
    namespace fs = std::filesystem;
    const fs::path path = fs::path(project_root_) /
        "config/gameplay/world3d/aquariums.json";
    std::error_code time_error;
    const fs::file_time_type write_time = fs::last_write_time(path, time_error);
    if (!force && !time_error && aquarium_config_write_time_known_ &&
        write_time == aquarium_config_write_time_) return;
    if (!time_error) {
        aquarium_config_write_time_ = write_time;
        aquarium_config_write_time_known_ = true;
    }

    std::string error;
    auto catalog = gameplay::world3d::aquarium::loadAquariumCatalog(
        project_root_, &error);
    if (!error.empty()) {
        std::cerr << "[Aquarium] Config reload rejected; keeping the live setup: "
                  << error << '\n';
        return;
    }
    const std::string map_id = active_world_map_id_.empty()
        ? scene_.id : active_world_map_id_;
    const auto* map_config =
        gameplay::world3d::aquarium::aquariumMapConfig(catalog, map_id);
    applyAquariumBuildingPresentation(map_config);
    auto simulation = std::make_unique<gameplay::world3d::aquarium::AquariumSimulation>(
        project_root_, scene_,
        map_config);
    for (const std::string& warning : simulation->warnings()) {
        std::cerr << "[Aquarium] " << warning << '\n';
    }
    for (const auto& actor : simulation->actors()) {
        std::cerr << "[Aquarium] " << actor.id << " worldPosition=["
                  << actor.world_position[0] << ',' << actor.world_position[1]
                  << ',' << actor.world_position[2] << "]\n";
    }
    auto inspection =
        std::make_unique<gameplay::world3d::aquarium::AquariumInspectionCamera>(
            simulation->tanks());
    restoreAquariumInspectionFacing();
    aquarium_catalog_ = std::move(catalog);
    aquarium_simulation_ = std::move(simulation);
    aquarium_inspection_camera_ = std::move(inspection);
    configureAquariumConstruction(
        gameplay::world3d::aquarium::aquariumMapConfig(aquarium_catalog_, map_id));
    refreshAquariumRenderActors();
    std::cerr << "[Aquarium] Applied config for " << map_id << " with "
              << aquarium_simulation_->actors().size() << " actors\n";
}

void Overworld3DTestScreen::applyAquariumBuildingPresentation(
    const gameplay::world3d::aquarium::AquariumMapConfig* map_config) {
    namespace aquarium = gameplay::world3d::aquarium;
    follow_camera_base_preset_ = aquarium_camera_base_preset_;
    scene_.lighting_brightness = aquarium_base_lighting_brightness_;
    scene_.lighting_tint_r = aquarium_base_lighting_tint_[0];
    scene_.lighting_tint_g = aquarium_base_lighting_tint_[1];
    scene_.lighting_tint_b = aquarium_base_lighting_tint_[2];

    if (map_config) {
        follow_camera_base_preset_ = aquarium::aquariumBuildingCameraPreset(
            aquarium_camera_base_preset_, map_config->building_presentation.camera,
            scene_.grid.tile_size);
        const auto& lighting = map_config->building_presentation.lighting;
        if (lighting.enabled) {
            scene_.lighting_brightness = aquarium_base_lighting_brightness_ * lighting.brightness;
            scene_.lighting_tint_r = aquarium_base_lighting_tint_[0] * lighting.tint[0];
            scene_.lighting_tint_g = aquarium_base_lighting_tint_[1] * lighting.tint[1];
            scene_.lighting_tint_b = aquarium_base_lighting_tint_[2] * lighting.tint[2];
        }
    }

    camera_ = gameplay::world3d::camera::Gen4FollowCamera(follow_camera_base_preset_);
    camera_.setTarget(player_.position());
    if (bgfx_renderer_) {
        bgfx_renderer_->setSceneLighting(
            scene_.lighting_brightness,
            {scene_.lighting_tint_r, scene_.lighting_tint_g, scene_.lighting_tint_b});
    }
}

void Overworld3DTestScreen::configureAquariumConstruction(
    const gameplay::world3d::aquarium::AquariumMapConfig* map_config) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    const auto configure_started = std::chrono::steady_clock::now();
    exitAquariumConstruction();
    aquarium_construction_return_cell_.reset();
    aquarium_construction_return_facing_ = gameplay::world3d::FacingDirection::South;
    aquarium_construction_controller_id_ = -1;
    aquarium_design_store_.reset();
    aquarium_population_policy_.reset();
    player_aquarium_runtime_ = {};
    aquarium_construction_read_only_ = false;
    if (!map_config || !map_config->construction.enabled) {
        aquarium_construction_.configure({}, {}, {}, {});
        if (aquarium_collision_overlay_) aquarium_collision_overlay_->setBlockedCells({});
        if (bgfx_renderer_) {
            std::string ignored;
            bgfx_renderer_->replacePlayerAquariumTanks({}, &ignored);
        }
        return;
    }
    if (map_config->construction.has_room_trim_color) {
        const auto& color = map_config->construction.room_trim_color;
        scene_.interior.default_room.trim_color = {
            color[0], color[1], color[2], color[3]};
    }
    aquarium_construction_return_cell_ = pr::aquarium::geometry::GridCell{
        map_config->construction.return_cell.column,
        map_config->construction.return_cell.row,
    };
    aquarium_construction_return_facing_ = constructionReturnFacing(
        map_config->construction.return_facing);

    const PersistenceConfig persistence = loadConfigFromJson(
        (std::filesystem::path(project_root_) / "config/title_screen.json").string()).persistence;
    const std::filesystem::path save_root = resolveSaveDirectory(persistence, project_root_);
    const std::filesystem::path design_path = save_root / "aquariums" /
        aquariumProfileId(persistence) /
        (map_config->map_id + ".aquarium.json");
    aquarium_design_store_ = std::make_unique<aqc::AquariumDesignStore>(design_path);
    aqc::AquariumDesignDocument document;
    document.design_id = "aqd_" + aquariumProfileId(persistence) + "_" + map_config->map_id;
    document.map_id = map_config->map_id;
    bool loaded_document_rejected = false;
    const auto document_load_started = std::chrono::steady_clock::now();
    const aqc::AquariumStoreLoadResult loaded = aquarium_design_store_->load();
    const auto document_load_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - document_load_started).count();
    if (loaded.document) {
        if (loaded.document->map_id == map_config->map_id) {
            document = *loaded.document;
        } else {
            loaded_document_rejected = true;
            std::cerr << "[AquariumConstruction] event=save_rejected map="
                      << map_config->map_id << " reason=map_id_mismatch\n";
        }
        if (loaded.status == aqc::AquariumStoreLoadStatus::RecoveredBackup ||
            loaded.status == aqc::AquariumStoreLoadStatus::RecoveredPrevious ||
            loaded.status == aqc::AquariumStoreLoadStatus::RecoveredTemporary) {
            const char* source = loaded.status == aqc::AquariumStoreLoadStatus::RecoveredBackup
                ? "backup"
                : loaded.status == aqc::AquariumStoreLoadStatus::RecoveredPrevious
                    ? "previous"
                    : "temporary";
            std::cerr << "[AquariumConstruction] event=save_recovered map="
                      << map_config->map_id << " source=" << source << '\n';
        }
    } else if (loaded.status == aqc::AquariumStoreLoadStatus::NewerVersion ||
               loaded.status == aqc::AquariumStoreLoadStatus::Invalid) {
        aquarium_construction_read_only_ = true;
        std::cerr << "[AquariumConstruction] event=save_rejected map="
                  << map_config->map_id << " reason=" << loaded.diagnostic << '\n';
    }

    std::vector<pr::aquarium::geometry::GridCell> authored_obstacles =
        authoredObstacleCells(scene_);
    if (!aquarium_construction_read_only_) {
        const std::vector<std::string> placement_diagnostics =
            aqc::validateAquariumPlacement(
                document, map_config->construction, authored_obstacles);
        loaded_document_rejected = loaded_document_rejected || !placement_diagnostics.empty();
        if (loaded_document_rejected) {
            const aqc::AquariumStoreLoadResult backup = aquarium_design_store_->loadBackup();
            if (backup.document && backup.document->map_id == map_config->map_id &&
                aqc::validateAquariumPlacement(
                    *backup.document, map_config->construction, authored_obstacles).empty()) {
                document = *backup.document;
                loaded_document_rejected = false;
                std::cerr << "[AquariumConstruction] event=save_recovered map="
                          << map_config->map_id << " source=placement_validated_backup\n";
            } else {
                aquarium_construction_read_only_ = true;
                document = {};
                document.design_id = "aqd_" + aquariumProfileId(persistence) + "_" + map_config->map_id;
                document.map_id = map_config->map_id;
                std::cerr << "[AquariumConstruction] event=save_rejected map="
                          << map_config->map_id << " reason=invalid_placement\n";
            }
        }
    }
    auto construction_config = map_config->construction;
    if (aquarium_construction_read_only_) construction_config.enabled = false;
    aquarium_construction_.configure(
        map_config->map_id, construction_config,
        std::move(authored_obstacles), std::move(document));
    aquarium_construction_overlay_.configure(map_config->construction, project_root_);
    aquarium_population_policy_ = aqc::makePlaceholderWishiwashiPolicy();
    refreshPlayerAquariumRuntime();
    const auto load_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - configure_started).count();
    std::cerr << "[AquariumConstruction] event=document_loaded map=" << map_config->map_id
              << " status=" << aquariumLoadStatusName(loaded.status)
              << " schema=" << pr::aquarium::geometry::kDesignSchemaVersion
              << " kernelAbi=" << pr::aquarium::geometry::kKernelAbiVersion
              << " revision=" << aquarium_construction_.committedDesign().revision
              << " tanks=" << aquarium_construction_.committedDesign().tanks.size()
              << " documentLoadUs=" << document_load_microseconds
              << " configureUs=" << load_microseconds << '\n';
}

void Overworld3DTestScreen::refreshPlayerAquariumRuntime() {
    namespace aq = gameplay::world3d::aquarium;
    const auto rebuild_started = std::chrono::steady_clock::now();
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    const aq::AquariumMapConfig* map_config = aq::aquariumMapConfig(aquarium_catalog_, map_id);
    if (!map_config || !aquarium_population_policy_) {
        player_aquarium_runtime_ = {};
        if (aquarium_simulation_) {
            aquarium_simulation_->replacePlayerTanks({});
            aquarium_inspection_camera_ =
                std::make_unique<aq::AquariumInspectionCamera>(aquarium_simulation_->tanks());
        }
        return;
    }
    std::vector<std::string> diagnostics;
    player_aquarium_runtime_ = aq::construction::buildPlayerAquariumRuntime(
        aquarium_construction_.committedDesign(), scene_, *map_config,
        project_root_, *aquarium_population_policy_, &diagnostics);
    if (aquarium_simulation_) {
        aquarium_simulation_->replacePlayerTanks(
            player_aquarium_runtime_.simulation_tanks);
        aquarium_inspection_camera_ =
            std::make_unique<aq::AquariumInspectionCamera>(aquarium_simulation_->tanks());
    }
    for (const std::string& diagnostic : diagnostics) {
        std::cerr << "[AquariumConstruction] event=runtime_warning message="
                  << diagnostic << '\n';
    }
    if (aquarium_collision_overlay_) {
        aquarium_collision_overlay_->setBlockedCells(
            player_aquarium_runtime_.collision_cells);
    }
    if (bgfx_renderer_) {
        std::string upload_error;
        if (!bgfx_renderer_->replacePlayerAquariumTanks(
                player_aquarium_runtime_.tanks, &upload_error)) {
            std::cerr << "[AquariumConstruction] event=gpu_replace_failed reason="
                      << upload_error << '\n';
        }
    }
    refreshAquariumRenderActors();
    const auto rebuild_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - rebuild_started).count();
    std::size_t mesh_count = 0;
    std::size_t vertex_count = 0;
    for (const auto& tank : player_aquarium_runtime_.tanks) {
        mesh_count += tank.build.meshes.meshes.size();
        vertex_count += tank.build.statistics.vertex_count;
    }
    std::cerr << "[AquariumConstruction] event=runtime_rebuilt map=" << map_id
              << " revision=" << player_aquarium_runtime_.revision
              << " tanks=" << player_aquarium_runtime_.tanks.size()
              << " meshes=" << mesh_count
              << " vertices=" << vertex_count
              << " resources=" << (bgfx_renderer_ ? bgfx_renderer_->playerAquariumResourceCount() : 0U)
              << " rebuildUs=" << rebuild_microseconds << '\n';
}

void Overworld3DTestScreen::refreshAquariumRenderActors() {
    if (!bgfx_renderer_) return;
    std::vector<gameplay::world3d::aquarium::AquariumPokemonActor> actors;
    std::vector<gameplay::world3d::aquarium::AquariumTankRuntime> tanks;
    if (aquarium_simulation_) {
        actors = aquarium_simulation_->actors();
        tanks = aquarium_simulation_->tanks();
    }
    const std::string map_id = active_world_map_id_.empty()
        ? scene_.id : active_world_map_id_;
    const auto* map_config = gameplay::world3d::aquarium::aquariumMapConfig(
        aquarium_catalog_, map_id);
    bgfx_renderer_->setAquariumTankLights(
        std::move(tanks),
        map_config
            ? map_config->building_presentation.tank_lighting
            : gameplay::world3d::aquarium::AquariumTankLightingConfig{});
    bgfx_renderer_->setAquariumPokemonActors(std::move(actors));
}

bool Overworld3DTestScreen::commitAquariumConstruction() {
    namespace aqc = gameplay::world3d::aquarium::construction;
    if (aquarium_construction_.finishNoOpDraft()) return true;
    auto candidate = aquarium_construction_.state() == aqc::ConstructionState::DeleteConfirm
        ? aquarium_construction_.prepareDelete()
        : aquarium_construction_.prepareCommit();
    return beginAquariumConstructionCommit(std::move(candidate));
}

bool Overworld3DTestScreen::undoAquariumConstruction() {
    return beginAquariumConstructionCommit(aquarium_construction_.prepareUndo());
}

bool Overworld3DTestScreen::redoAquariumConstruction() {
    return beginAquariumConstructionCommit(aquarium_construction_.prepareRedo());
}

bool Overworld3DTestScreen::beginAquariumConstructionCommit(
    std::optional<gameplay::world3d::aquarium::construction::ConstructionCommitCandidate>
        candidate) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    if (aquarium_commit_future_.valid()) return false;
    if (!candidate || !aquarium_design_store_ || !aquarium_population_policy_) {
        requestAquariumConstructionErrorFeedback();
        return false;
    }
    if (!bgfx_renderer_ || !bgfx_renderer_->valid()) {
        aquarium_construction_.rejectCommit("Aquarium renderer is unavailable");
        requestAquariumConstructionErrorFeedback();
        return false;
    }
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    const auto* map_config = gameplay::world3d::aquarium::aquariumMapConfig(
        aquarium_catalog_, map_id);
    if (!map_config) {
        aquarium_construction_.rejectCommit("Aquarium map configuration is unavailable");
        requestAquariumConstructionErrorFeedback();
        return false;
    }
    aquarium_commit_cancelled_ = false;
    const auto scene = scene_;
    const auto config = *map_config;
    const auto project_root = std::filesystem::path(project_root_);
    const std::uint64_t operation_token = candidate->operation_token;
    const char* command_kind = aquariumCommandName(candidate->command.kind);
    const char* history_action = aquariumHistoryActionName(candidate->history_action);
    aquarium_commit_future_ = std::async(std::launch::async,
        [candidate = std::move(*candidate), scene, config, project_root]() mutable {
            const auto started = std::chrono::steady_clock::now();
            AquariumGeneratedCommit generated;
            generated.candidate = std::move(candidate);
            const auto placement_diagnostics = aqc::validateAquariumPlacement(
                generated.candidate.document, config.construction,
                authoredObstacleCells(scene));
            generated.validation_valid = placement_diagnostics.empty();
            generated.diagnostics.insert(generated.diagnostics.end(),
                placement_diagnostics.begin(), placement_diagnostics.end());
            if (!generated.validation_valid) {
                generated.generation_microseconds =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - started).count();
                return generated;
            }
            auto policy = aqc::makePlaceholderWishiwashiPolicy();
            generated.runtime = aqc::buildPlayerAquariumRuntime(
                generated.candidate.document, scene, config, project_root,
                *policy, &generated.diagnostics);
            generated.generation_microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - started).count();
            return generated;
        });
    std::cerr << "[AquariumConstruction] event=generation_started map=" << map_id
              << " revision=" << aquarium_construction_.committedDesign().revision + 1
              << " token=" << operation_token
              << " command=" << command_kind
              << " history=" << history_action << '\n';
    return true;
}

void Overworld3DTestScreen::updateAquariumConstructionCommit() {
    namespace aqc = gameplay::world3d::aquarium::construction;
    if (!aquarium_commit_future_.valid() ||
        aquarium_commit_future_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }
    AquariumGeneratedCommit generated;
    try {
        generated = aquarium_commit_future_.get();
    } catch (const std::exception& error) {
        aquarium_construction_.rejectCommit("Aquarium generation failed");
        requestAquariumConstructionErrorFeedback();
        std::cerr << "[AquariumConstruction] event=commit_failed stage=generation reason="
                  << error.what() << '\n';
        return;
    }
    if (aquarium_commit_cancelled_ ||
        !aquarium_construction_.candidateCurrent(generated.candidate)) {
        aquarium_commit_cancelled_ = false;
        std::cerr << "[AquariumConstruction] event=generation_discarded reason=cancelled\n";
        return;
    }
    if (!generated.validation_valid) {
        const std::string reason = generated.diagnostics.empty()
            ? "Aquarium placement validation failed" : generated.diagnostics.front();
        aquarium_construction_.rejectCommit(reason);
        requestAquariumConstructionErrorFeedback();
        std::cerr << "[AquariumConstruction] event=commit_failed stage=validation reason="
                  << reason << '\n';
        return;
    }
    if (generated.runtime.tanks.size() != generated.candidate.document.tanks.size()) {
        aquarium_construction_.rejectCommit("Generated aquarium failed runtime validation");
        requestAquariumConstructionErrorFeedback();
        return;
    }
    const auto upload_started = std::chrono::steady_clock::now();
    std::string upload_error;
    if (!bgfx_renderer_ || !bgfx_renderer_->valid() ||
        !bgfx_renderer_->stagePlayerAquariumTanks(generated.runtime.tanks, &upload_error)) {
        aquarium_construction_.rejectCommit(
            upload_error.empty() ? "Aquarium renderer became unavailable" : upload_error);
        requestAquariumConstructionErrorFeedback();
        return;
    }
    const auto upload_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - upload_started).count();
    std::string save_error;
    if (!aquarium_design_store_ ||
        !aquarium_design_store_->saveTransactionally(generated.candidate.document, &save_error)) {
        bgfx_renderer_->discardStagedPlayerAquariumTanks();
        aquarium_construction_.rejectCommit(save_error);
        requestAquariumConstructionErrorFeedback();
        std::cerr << "[AquariumConstruction] event=commit_failed stage=save reason="
                  << save_error << '\n';
        return;
    }
    if (!bgfx_renderer_->publishStagedPlayerAquariumTanks()) {
        aquarium_construction_.rejectCommit("Staged aquarium resources could not be published");
        requestAquariumConstructionErrorFeedback();
        std::cerr << "[AquariumConstruction] event=commit_failed stage=publish\n";
        return;
    }
    player_aquarium_runtime_ = std::move(generated.runtime);
    if (aquarium_simulation_) {
        aquarium_simulation_->replacePlayerTanks(
            player_aquarium_runtime_.simulation_tanks);
        aquarium_inspection_camera_ =
            std::make_unique<gameplay::world3d::aquarium::AquariumInspectionCamera>(
                aquarium_simulation_->tanks());
    }
    if (aquarium_collision_overlay_) {
        aquarium_collision_overlay_->setBlockedCells(player_aquarium_runtime_.collision_cells);
    }
    const std::uint64_t revision = generated.candidate.document.revision;
    const std::uint64_t operation_token = generated.candidate.operation_token;
    const char* command_kind = aquariumCommandName(generated.candidate.command.kind);
    const char* history_action = aquariumHistoryActionName(generated.candidate.history_action);
    if (!aquarium_construction_.publish(std::move(generated.candidate))) {
        std::cerr << "[AquariumConstruction] event=commit_failed stage=publish reason=stale_token\n";
        return;
    }
    aquarium_construction_save_sfx_requested_ = true;
    refreshAquariumRenderActors();
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    std::size_t mesh_count = 0;
    std::size_t vertex_count = 0;
    std::size_t triangle_count = 0;
    std::uint64_t water_volume_litres = 0;
    for (const auto& tank : player_aquarium_runtime_.tanks) {
        mesh_count += tank.build.meshes.meshes.size();
        vertex_count += tank.build.statistics.vertex_count;
        triangle_count += tank.build.statistics.triangle_count;
        water_volume_litres += tank.water_volume_litres;
    }
    const std::int64_t generation_budget_us = aquariumUsesComplexGeometry(
        aquarium_construction_.committedDesign()) ? 100000 : 50000;
    constexpr std::int64_t kUploadBudgetUs = 4000;
    std::cerr << "[AquariumConstruction] event=commit map=" << map_id
              << " revision=" << revision
              << " token=" << operation_token
              << " command=" << command_kind
              << " history=" << history_action
              << " tanks=" << player_aquarium_runtime_.tanks.size()
              << " meshes=" << mesh_count
              << " vertices=" << vertex_count
              << " triangles=" << triangle_count
              << " waterVolumeLitres=" << water_volume_litres
              << " resources=" << (bgfx_renderer_ ? bgfx_renderer_->playerAquariumResourceCount() : 0U)
              << " generationUs=" << generated.generation_microseconds
              << " generationBudgetUs=" << generation_budget_us
              << " generationWithinBudget="
              << (generated.generation_microseconds <= generation_budget_us ? 1 : 0)
              << " uploadUs=" << upload_microseconds
              << " uploadBudgetUs=" << kUploadBudgetUs
              << " uploadWithinBudget=" << (upload_microseconds <= kUploadBudgetUs ? 1 : 0)
              << '\n';
}

void Overworld3DTestScreen::applyAquariumConstructionCamera(double delta_seconds) {
    if (!aquarium_construction_.active()) return;
    if (aquarium_pointer_controls_cursor_ && aquarium_pointer_position_valid_ &&
        !aquariumConstructionUiAt(
            aquarium_pointer_position_.x, aquarium_pointer_position_.y)) {
        if (const auto cell = aquariumConstructionCellAt(
                aquarium_pointer_position_.x, aquarium_pointer_position_.y)) {
            aquarium_construction_.pointAt(*cell);
        }
    }
    const auto visual = aquariumConstructionVisual();
    const float floor_y = visual.cells.empty() ? 0.0f : visual.cells.front().floor_y;
    const int viewport_width = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseWidth(scene_)
        : std::max(1, app_config_.window.virtual_width);
    const int viewport_height = scene_.world_viewport.enabled
        ? gameplay::world3d::rendering::worldViewportBaseHeight(scene_)
        : std::max(1, app_config_.window.virtual_height);
    const auto shown_tank = visual.preview_tank ? visual.preview_tank : visual.selected_tank;
    const bool property_panel = gameplay::world3d::aquarium::construction::
        aquariumConstructionPropertyPanelVisible(visual.state, shown_tank.has_value());
    const auto focus = property_panel && shown_tank
        ? gameplay::world3d::aquarium::construction::tankCentreCell(*shown_tank)
        : aquarium_construction_.cursor();
    const auto overview = gameplay::world3d::aquarium::construction::
        trackAquariumConstructionCursor(
            aquarium_construction_camera_tracking_,
            scene_.grid.width, scene_.grid.height, scene_.grid.tile_size, floor_y,
            camera_.pose().preset.fov_y_deg,
            static_cast<float>(viewport_width) / static_cast<float>(viewport_height),
            follow_camera_base_preset_.pitch_deg, focus, property_panel, delta_seconds,
            visual.state == gameplay::world3d::aquarium::construction::
                    ConstructionState::TunnelRoute
                ? 0.0f : visual.placement_offset_world_units);
    camera_.setManualPose(
        overview.position, overview.yaw_degrees, overview.pitch_degrees);
}

gameplay::world3d::aquarium::construction::AquariumConstructionVisual
Overworld3DTestScreen::aquariumConstructionVisual() const {
    namespace aqc = gameplay::world3d::aquarium::construction;
    aqc::AquariumConstructionVisual visual;
    visual.visible = aquarium_construction_.active();
    visual.tile_world_units = scene_.grid.tile_size;
    visual.cursor = aquarium_construction_.cursor();
    visual.state = aquarium_construction_.state();
    visual.property_draft = aquarium_construction_.draftOperation() ==
        gameplay::world3d::aquarium::construction::ConstructionDraftOperation::Properties;
    visual.draft_valid = aquarium_construction_.draftValid();
    visual.draft_cells = aquarium_construction_.draftCells();
    visual.undo_available = aquarium_construction_.canUndo();
    visual.redo_available = aquarium_construction_.canRedo();
    visual.subtract_mode = aquarium_subtract_mode_;
    visual.focused_action = aquarium_construction_focused_action_;
    switch (visual.state) {
    case aqc::ConstructionState::Browse:
        visual.navigation_hint = "CHOOSE PLUS OR MINUS  CLICK A CELL";
        break;
    case aqc::ConstructionState::Selected:
        visual.navigation_hint = "CLICK HANDLE  ZR HEIGHT  ZL DEPTH";
        break;
    case aqc::ConstructionState::ResizeFootprint:
    case aqc::ConstructionState::MoveTank:
    case aqc::ConstructionState::ResizeTank:
        visual.navigation_hint = "MOVE HANDLE  EDGES PAN VIEW";
        break;
    case aqc::ConstructionState::SubtractFootprint:
        visual.navigation_hint = "A OR CLICK CUTS CELLS  X APPLIES";
        break;
    case aqc::ConstructionState::PaintFootprint:
        visual.navigation_hint = visual.property_draft
            ? "MOVE ACROSS CELLS  CLICK OR A TO APPLY"
            : aquarium_construction_.draftOperation() == aqc::ConstructionDraftOperation::Subtract
                ? "CHOOSE CUT AREA  CLICK OR A TO APPLY"
                : "MOVE ACROSS CELLS  CLICK OR A TO APPLY";
        break;
    case aqc::ConstructionState::TunnelRoute:
        visual.navigation_hint = "FOLLOW CELLS TO ANOTHER GLOWING PORTAL";
        break;
    case aqc::ConstructionState::DraftReview:
        visual.navigation_hint = visual.property_draft
            ? "MOVE UP DOWN  CLICK OR A TO APPLY"
            : "BUILD OR ADJUST FOOTPRINT";
        break;
    case aqc::ConstructionState::DeleteConfirm:
        visual.navigation_hint = "CONFIRM DELETE OR CANCEL";
        break;
    case aqc::ConstructionState::Building:
        visual.navigation_hint = "BUILDING";
        break;
    case aqc::ConstructionState::Dormant:
        break;
    }
    visual.status_hint = aqc::aquariumConstructionHintForValidation(
        aquarium_construction_.validationMessage());
    if (const auto* selected = aquarium_construction_.selectedTank()) {
        visual.selected_tank = *selected;
        if (aquarium_construction_.state() == aqc::ConstructionState::Selected &&
            aquarium_construction_focused_action_ == aqc::ConstructionHudAction::Resize) {
            visual.active_resize_handle = aquarium_construction_.preferredResizeHandle();
        }
        if (!aquarium_construction_.draft()) {
            visual.selected_cells = aqc::tankFootprintCells(*selected);
        }
        for (const auto& tunnel : selected->tunnels) {
            for (const auto point : tunnel.centreline_cells) {
                visual.existing_tunnel_cells.push_back({point.column, point.row});
            }
        }
    }
    if (aquarium_construction_.draft()) {
        visual.anchor = aquarium_construction_.draft()->anchor;
        visual.active_resize_handle = aquarium_construction_.draft()->resize_handle;
        visual.original_cells = aquarium_construction_.draftOriginalCells();
        visual.cut_cells = aquarium_construction_.draftCutCells();
    }
    visual.preview_tank = aquarium_construction_.previewTank();
    visual.tunnel_portal_cells = aquarium_construction_.tunnelPortalCells();
    visual.tunnel_route_cells = aquarium_construction_.tunnelRouteCells();
    if (!visual.preview_tank && visual.selected_tank) visual.preview_tank = visual.selected_tank;
    if (visual.state == aqc::ConstructionState::TunnelRoute) {
        // Tunnel authoring owns a tank-local point grid. Do not reuse the
        // room-wide construction cells or the full-cell draft silhouettes:
        // both imply room placement cells instead of the installed tank's
        // canonical half-cell-centred route points.
        visual.draft_cells.clear();
        visual.original_cells.clear();
    }
    if (visual.preview_tank && visual.cut_cells.empty()) {
        const auto& footprint = visual.preview_tank->footprint;
        for (const auto cell : footprint.subtracted_cells) {
            visual.cut_cells.push_back({
                footprint.origin_cell.column + cell.column,
                footprint.origin_cell.row + cell.row});
        }
    }
    const float height_step = scene_.terrain.height_per_floor > 0.0f
        ? scene_.terrain.height_per_floor : scene_.grid.tile_size;
    const auto route_cells = visual.state == aqc::ConstructionState::TunnelRoute
        ? aquarium_construction_.tunnelRoutingCells()
        : std::vector<pr::aquarium::geometry::GridCell>{};
    const auto& construction_cells = visual.state == aqc::ConstructionState::TunnelRoute
        ? route_cells : aquarium_construction_.allowedCells();
    for (const auto cell : construction_cells) {
        if (visual.state == aqc::ConstructionState::TunnelRoute &&
            std::any_of(visual.existing_tunnel_cells.begin(),
                visual.existing_tunnel_cells.end(), [&](const auto occupied) {
                    return occupied.column == cell.column && occupied.row == cell.row;
                })) {
            continue;
        }
        float floor_y = 0.0f;
        if (cell.row >= 0 && cell.column >= 0 &&
            cell.row < static_cast<int>(scene_.terrain.heights.size()) &&
            cell.column < static_cast<int>(scene_.terrain.heights[cell.row].size())) {
            floor_y = static_cast<float>(scene_.terrain.heights[cell.row][cell.column]) * height_step;
        }
        visual.cells.push_back({
            cell,
            floor_y,
            aquarium_construction_.cellBlocked(cell),
        });
    }
    if (visual.state != aqc::ConstructionState::TunnelRoute) {
        visual.locked_cells = aqc::aquariumConstructionContextLockedCells(
            aquarium_construction_.allowedCells(), authoredObstacleCells(scene_));
    }
    return visual;
}

} // namespace pr
