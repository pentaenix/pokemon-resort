#include "gameplay/world3d/aquarium/construction/AquariumConstructionSession.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumConstructionVisual.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumHabitatValidator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace aq = pr::gameplay::world3d::aquarium;
namespace construction = aq::construction;
namespace geo = pr::aquarium::geometry;

void runAquariumLargeRoomTests() {
    aq::AquariumConstructionConfig config;
    config.enabled = true;
    for (int row = 0; row < 64; ++row)
        for (int col = 0; col < 64; ++col) config.allowed_cells.push_back({col, row});
    construction::AquariumDesignDocument document;
    document.design_id = "large-room-test";
    document.map_id = "large-room";
    geo::TankDesign tank;
    tank.id = "large-tank";
    tank.footprint.origin_cell = {2, 2};
    tank.footprint.width_cells = tank.footprint.depth_cells = 60;
    document.tanks.push_back(tank);
    construction::AquariumConstructionSession session;
    session.configure(document.map_id, config, {{0, 0}}, document);
    if (!session.enter({1, 1})) throw std::runtime_error("large-room fixture unavailable");
    std::vector<double> times;
    for (int sample = 0; sample < 7; ++sample) {
        const auto start = std::chrono::steady_clock::now();
        int blocked = 0;
        for (const auto cell : config.allowed_cells)
            blocked += session.cellBlocked({cell.column, cell.row}) ? 1 : 0;
        times.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
        if (blocked != 3601) throw std::runtime_error("large-room occupancy omitted tank/authored cells");
    }
    std::sort(times.begin(), times.end());
    std::cout << "aquarium_large_room: room_cells=4096 tank_cells=3600 occupancy_p95_ms="
              << times.back() << '\n';
    construction::AquariumConstructionVisual visual;
    visual.visible = true;
    visual.state = construction::ConstructionState::Selected;
    visual.selected_cells = construction::tankFootprintCells(tank);
    for (auto cell : config.allowed_cells) visual.cells.push_back({{cell.column, cell.row}, 0, false});
    times.clear();
    for (int sample = 0; sample < 7; ++sample) {
        const auto start = std::chrono::steady_clock::now();
        const auto mesh = construction::buildAquariumConstructionWorldMesh(visual);
        times.push_back(std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
        for (auto index : mesh.indices) if (index >= mesh.vertices.size())
            throw std::runtime_error("large preview contains invalid mesh indices");
    }
    std::sort(times.begin(), times.end());
    std::cout << "aquarium_large_room: selected_preview_p95_ms=" << times.back() << '\n';
    session.pointAt({2, 2});
    if (!session.selectAtCursor() || !session.requestDeleteSelected())
        throw std::runtime_error("large-tank deletion selection failed");
    auto deletion = session.prepareDelete();
    if (!deletion || !session.publish(std::move(*deletion)) || session.cellBlocked({2, 2}) ||
        !session.cellBlocked({0, 0}))
        throw std::runtime_error("delete left stale tank cells or erased authored occupancy");
    auto undo = session.prepareUndo();
    if (!undo || !session.publish(std::move(*undo)) || !session.cellBlocked({2, 2}))
        throw std::runtime_error("undo failed to rebuild committed occupancy");
    auto redo = session.prepareRedo();
    if (!redo || !session.publish(std::move(*redo)) || session.cellBlocked({2, 2}))
        throw std::runtime_error("redo retained stale occupied cells");
    auto smaller_zone = config;
    smaller_zone.allowed_cells = {{0, 0}, {1, 1}};
    session.updateRoomBuildZone(smaller_zone);
    if (session.cellAllowed({2, 2}) || !session.cellAllowed({1, 1}))
        throw std::runtime_error("room resize retained stale build eligibility");
    document.tanks.clear();
    session.configure(document.map_id, config, {}, document);
    if (session.cellBlocked({2, 2}) || session.cellBlocked({0, 0}))
        throw std::runtime_error("room reconfigure retained stale occupied cells");

    const auto root = std::filesystem::path(__FILE__).parent_path().parent_path()
        .parent_path().parent_path().parent_path();
    const auto loaded = aq::loadAquariumSpeciesCatalog(
        root / "config/gameplay/world3d/aquarium_species.json");
    if (!loaded.valid) throw std::runtime_error("aquarium size catalogue failed to load");
    aq::AquariumNavigation nav;
    nav.valid = true; nav.export_units_per_meter = 16;
    nav.layers.push_back({"water", -40, 40, {{{{-40,-40},{40,-40},{40,40},{-40,40}}}}});
    for (const auto* id : {"0130:00", "0245:00", "0249:00", "0321:00", "0350:00",
                           "0382:00", "0484:00", "0781:00"}) {
        const auto* species = loaded.catalog.findApproved(id);
        if (!species || std::abs(species->scale_multiplier - .9f) > .00001f)
            throw std::runtime_error(std::string("massive aquarium scale mismatch: ") + id);
        auto original = *species;
        original.scale_multiplier = 1;
        const auto full_fit = construction::validateAquariumHabitat(original, nav, .17f);
        const auto small_fit = construction::validateAquariumHabitat(*species, nav, .17f);
        if (std::abs(small_fit.body_height_meters - full_fit.body_height_meters*.9f) > .0001f ||
            small_fit.body_radius_meters >= full_fit.body_radius_meters)
            throw std::runtime_error(std::string("massive model shrink did not propagate to physical fit: ") + id);
    }
}
