#include "ui/Overworld3DTestScreen.hpp"

#include "core/app/AppPaths.hpp"
#include "core/config/ConfigLoader.hpp"

#include <chrono>
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

void Overworld3DTestScreen::configureAquariumConstruction(
    const gameplay::world3d::aquarium::AquariumMapConfig* map_config) {
    namespace aqc = gameplay::world3d::aquarium::construction;
    const auto configure_started = std::chrono::steady_clock::now();
    exitAquariumConstruction();
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
    const aqc::AquariumStoreLoadResult loaded = aquarium_design_store_->load();
    if (loaded.document) {
        if (loaded.document->map_id == map_config->map_id) {
            document = *loaded.document;
        } else {
            loaded_document_rejected = true;
            std::cerr << "[AquariumConstruction] event=save_rejected map="
                      << map_config->map_id << " reason=map_id_mismatch\n";
        }
        if (loaded.status == aqc::AquariumStoreLoadStatus::RecoveredBackup) {
            std::cerr << "[AquariumConstruction] event=save_recovered map="
                      << map_config->map_id << " source=backup\n";
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
    aquarium_construction_overlay_.configure(map_config->construction);
    aquarium_population_policy_ = aqc::makePlaceholderWishiwashiPolicy();
    refreshPlayerAquariumRuntime();
    const auto load_microseconds = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - configure_started).count();
    std::cerr << "[AquariumConstruction] event=document_loaded map=" << map_config->map_id
              << " revision=" << aquarium_construction_.committedDesign().revision
              << " tanks=" << aquarium_construction_.committedDesign().tanks.size()
              << " loadUs=" << load_microseconds << '\n';
}

void Overworld3DTestScreen::refreshPlayerAquariumRuntime() {
    namespace aq = gameplay::world3d::aquarium;
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    const aq::AquariumMapConfig* map_config = aq::aquariumMapConfig(aquarium_catalog_, map_id);
    if (!map_config || !aquarium_population_policy_) {
        player_aquarium_runtime_ = {};
        return;
    }
    std::vector<std::string> diagnostics;
    player_aquarium_runtime_ = aq::construction::buildPlayerAquariumRuntime(
        aquarium_construction_.committedDesign(), scene_, *map_config,
        project_root_, *aquarium_population_policy_, &diagnostics);
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
}

void Overworld3DTestScreen::refreshAquariumRenderActors() {
    if (!bgfx_renderer_) return;
    std::vector<gameplay::world3d::aquarium::AquariumPokemonActor> actors;
    if (aquarium_simulation_) actors = aquarium_simulation_->actors();
    actors.insert(actors.end(), player_aquarium_runtime_.actors.begin(),
                  player_aquarium_runtime_.actors.end());
    bgfx_renderer_->setAquariumPokemonActors(std::move(actors));
}

bool Overworld3DTestScreen::commitAquariumConstruction() {
    namespace aqc = gameplay::world3d::aquarium::construction;
    if (aquarium_commit_future_.valid()) return false;
    auto candidate = aquarium_construction_.prepareCommit();
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
              << " revision=" << aquarium_construction_.committedDesign().revision + 1 << '\n';
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
        aquarium_construction_.state() != aqc::ConstructionState::Building) {
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
    if (aquarium_collision_overlay_) {
        aquarium_collision_overlay_->setBlockedCells(player_aquarium_runtime_.collision_cells);
    }
    const std::uint64_t revision = generated.candidate.document.revision;
    aquarium_construction_.publish(std::move(generated.candidate));
    aquarium_construction_save_sfx_requested_ = true;
    refreshAquariumRenderActors();
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    std::size_t mesh_count = 0;
    std::size_t vertex_count = 0;
    std::size_t triangle_count = 0;
    for (const auto& tank : player_aquarium_runtime_.tanks) {
        mesh_count += tank.build.meshes.meshes.size();
        vertex_count += tank.build.statistics.vertex_count;
        triangle_count += tank.build.statistics.triangle_count;
    }
    std::cerr << "[AquariumConstruction] event=commit map=" << map_id
              << " revision=" << revision
              << " tanks=" << player_aquarium_runtime_.tanks.size()
              << " meshes=" << mesh_count
              << " vertices=" << vertex_count
              << " triangles=" << triangle_count
              << " resources=" << (bgfx_renderer_ ? bgfx_renderer_->playerAquariumResourceCount() : 0U)
              << " generationUs=" << generated.generation_microseconds
              << " uploadUs=" << upload_microseconds << '\n';
}

void Overworld3DTestScreen::applyAquariumConstructionCamera() {
    if (!aquarium_construction_.active()) return;
    camera_.setManualPose({248.0f, 240.0f, 330.0f}, 180.0f, -48.0f);
}

void Overworld3DTestScreen::requestAquariumConstructionErrorFeedback() {
    aquarium_construction_error_sfx_requested_ = true;
#if SDL_VERSION_ATLEAST(2, 0, 9)
    if (aquarium_construction_controller_id_ >= 0) {
        if (SDL_GameController* controller = SDL_GameControllerFromInstanceID(
                aquarium_construction_controller_id_)) {
            SDL_GameControllerRumble(controller, 0x5000, 0x2800, 110);
        }
    }
#endif
}

void Overworld3DTestScreen::exitAquariumConstruction() {
    if (!aquarium_construction_.active()) return;
    aquarium_construction_.exit();
    player_.stop();
    animator_.setMoving(false);
    camera_.setTarget(player_.position());
    SDL_SetRelativeMouseMode(SDL_FALSE);
    aquarium_construction_controller_id_ = -1;
    std::cerr << "[AquariumConstruction] event=exit\n";
}

} // namespace pr
