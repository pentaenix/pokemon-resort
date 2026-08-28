#include "ui/Overworld3DTestScreen.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace pr {

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
    auto simulation = std::make_unique<gameplay::world3d::aquarium::AquariumSimulation>(
        project_root_, scene_,
        gameplay::world3d::aquarium::aquariumMapConfig(catalog, map_id));
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
    if (bgfx_renderer_) {
        bgfx_renderer_->setAquariumPokemonActors(aquarium_simulation_->actors());
    }
    std::cerr << "[Aquarium] Applied config for " << map_id << " with "
              << aquarium_simulation_->actors().size() << " actors\n";
}

} // namespace pr
