#include "ui/Overworld3DTestScreen.hpp"

#include <algorithm>
#include <iostream>
#include <utility>

namespace pr {

bool Overworld3DTestScreen::beginAquariumStocking() {
    namespace aqc = gameplay::world3d::aquarium::construction;
    if (aquarium_construction_.state() != aqc::ConstructionState::Selected ||
        aquarium_commit_future_.valid()) return false;
    const auto* tank = aquarium_construction_.selectedTank();
    const auto runtime = tank ? std::find_if(
        player_aquarium_runtime_.simulation_tanks.begin(),
        player_aquarium_runtime_.simulation_tanks.end(),
        [&](const auto& candidate) { return candidate.tank_id == tank->id; })
        : player_aquarium_runtime_.simulation_tanks.end();
    const std::string map_id = active_world_map_id_.empty() ? scene_.id : active_world_map_id_;
    const auto* map_config = gameplay::world3d::aquarium::aquariumMapConfig(
        aquarium_catalog_, map_id);
    if (!tank || !aquarium_stocking_.open(
            *tank, aqc::aquariumTankPopulation(
                aquarium_construction_.committedDesign(), tank->id),
            runtime != player_aquarium_runtime_.simulation_tanks.end()
                ? &runtime->navigation : nullptr,
            map_config ? map_config->pokemon_scale : 0.0f)) {
        return false;
    }
    resetAquariumConstructionPointerOperation();
    std::cerr << "[AquariumStocking] event=open tank=" << tank->id
              << " capacity=" << aquarium_stocking_.capacityCells() << '\n';
    return true;
}

void Overworld3DTestScreen::closeAquariumStocking() {
    if (!aquarium_stocking_.active()) return;
    std::cerr << "[AquariumStocking] event=close tank="
              << aquarium_stocking_.tankId() << '\n';
    aquarium_stocking_.close();
    aquarium_stocking_slider_drag_.reset();
    aquarium_pointer_controls_cursor_ = false;
    syncAquariumConstructionFocus();
}

bool Overworld3DTestScreen::applyAquariumStockingChange(bool add) {
    if (!aquarium_stocking_.active() || aquarium_commit_future_.valid()) return false;
    const auto residents = add
        ? aquarium_stocking_.residentsWithFocusedAdded()
        : aquarium_stocking_.residentsWithFocusedRemoved();
    if (!residents) return false;
    const auto* species = add && aquarium_stocking_.heldSpecies()
        ? aquarium_stocking_.heldSpecies() : aquarium_stocking_.focusedSpecies();
    auto candidate = aquarium_construction_.preparePopulationChange(
        aquarium_stocking_.tankId(), *residents);
    if (!candidate || !beginAquariumConstructionCommit(std::move(candidate))) return false;
    std::cerr << "[AquariumStocking] event=change_started tank="
              << aquarium_stocking_.tankId() << " species="
              << (species ? species->id : std::string{})
              << " operation=" << (add ? "add" : "remove") << '\n';
    return true;
}

bool Overworld3DTestScreen::applyAquariumExhibitStyleChange() {
    if (!aquarium_stocking_.active() || aquarium_commit_future_.valid()) return false;
    const auto* tank = aquarium_construction_.selectedTank();
    if (!tank) return false;
    const std::string preset_id = aquarium_stocking_.focusedExhibitPresetId();
    const int brightness = aquarium_stocking_.focusedBrightnessLevel();
    const int murkiness = aquarium_stocking_.focusedMurkinessLevel();
    const std::string substrate = aquarium_stocking_.focusedSubstrateKind();
    if (tank->exhibit_preset == preset_id &&
        tank->brightness_level == brightness &&
        tank->murkiness_level == murkiness &&
        tank->substrate_kind == substrate) return true;
    auto candidate = aquarium_construction_.prepareExhibitStyleChange(
        aquarium_stocking_.tankId(), preset_id, brightness, murkiness, substrate);
    if (!candidate || !beginAquariumConstructionCommit(std::move(candidate))) return false;
    std::cerr << "[AquariumStocking] event=exhibit_started tank="
              << aquarium_stocking_.tankId() << " preset=" << preset_id
              << " brightness=" << brightness << " murkiness=" << murkiness
              << " substrate=" << substrate << '\n';
    return true;
}

void Overworld3DTestScreen::syncAquariumStockingResidents() {
    if (!aquarium_stocking_.active()) return;
    const auto* tank = aquarium_construction_.selectedTank();
    if (!tank || tank->id != aquarium_stocking_.tankId()) {
        closeAquariumStocking();
        return;
    }
    namespace aqc = gameplay::world3d::aquarium::construction;
    const auto* population = aqc::aquariumTankPopulation(
        aquarium_construction_.committedDesign(), tank->id);
    aquarium_stocking_.setResidents(
        population ? population->residents
                   : std::vector<aqc::AquariumResidentSelection>{});
    aquarium_stocking_.setCurrentExhibitStyle(*tank);
}

bool Overworld3DTestScreen::handleAquariumStockingPointerPressed(
    int logical_x, int logical_y, bool remove) {
    if (!aquarium_stocking_.active()) return false;
    const int width = std::max(1, app_config_.window.virtual_width);
    const int height = std::max(1, app_config_.window.virtual_height);
    if (aquarium_stocking_overlay_.closeAt(width, height, logical_x, logical_y)) {
        closeAquariumStocking();
        return true;
    }
    if (const auto tab = aquarium_stocking_overlay_.tabAt(
            width, height, logical_x, logical_y)) {
        aquarium_stocking_.setTab(*tab);
        return true;
    }
    if (aquarium_stocking_.tab() == gameplay::world3d::aquarium::construction::
            AquariumStockingController::Tab::Pokemon) {
        if (aquarium_stocking_overlay_.previousBoxAt(
                width, height, logical_x, logical_y)) {
            aquarium_stocking_.changeBox(-1);
            return true;
        }
        if (aquarium_stocking_overlay_.nextBoxAt(
                width, height, logical_x, logical_y)) {
            aquarium_stocking_.changeBox(1);
            return true;
        }
    }
    if (aquarium_stocking_.tab() == gameplay::world3d::aquarium::construction::
            AquariumStockingController::Tab::Exhibit) {
        if (remove) {
            aquarium_stocking_slider_drag_.reset();
            if (const auto* tank = aquarium_construction_.selectedTank()) {
                aquarium_stocking_.setCurrentExhibitStyle(*tank);
            }
            return true;
        }
        bool changed = false;
        if (const auto preset = aquarium_stocking_overlay_.exhibitPresetAt(
                width, height, logical_x, logical_y)) {
            changed = aquarium_stocking_.focusExhibitPreset(*preset);
        } else if (const auto brightness =
                aquarium_stocking_overlay_.exhibitBrightnessAt(
                    width, height, logical_x, logical_y)) {
            changed = aquarium_stocking_.focusExhibitBrightness(*brightness);
            aquarium_stocking_slider_drag_ = gameplay::world3d::aquarium::construction::
                AquariumStockingController::ExhibitControl::Brightness;
            return true;
        } else if (const auto murkiness =
                aquarium_stocking_overlay_.exhibitMurkinessAt(
                    width, height, logical_x, logical_y)) {
            changed = aquarium_stocking_.focusExhibitMurkiness(*murkiness);
            aquarium_stocking_slider_drag_ = gameplay::world3d::aquarium::construction::
                AquariumStockingController::ExhibitControl::Murkiness;
            return true;
        } else if (const auto substrate =
                aquarium_stocking_overlay_.exhibitSubstrateAt(
                    width, height, logical_x, logical_y)) {
            changed = aquarium_stocking_.focusExhibitSubstrate(*substrate);
        }
        if (changed && !applyAquariumExhibitStyleChange())
            requestAquariumConstructionErrorFeedback();
        return true;
    }
    aquarium_stocking_.setPointerPosition(logical_x, logical_y);
    if (remove && aquarium_stocking_.holdingSpecies()) {
        aquarium_stocking_.cancelHeld();
        return true;
    }
    if (aquarium_stocking_.holdingSpecies() &&
        aquarium_stocking_overlay_.capacityAt(
            width, height, logical_x, logical_y)) {
        if (applyAquariumStockingChange(true)) aquarium_stocking_.cancelHeld();
        else requestAquariumConstructionErrorFeedback();
        return true;
    }
    const auto index = aquarium_stocking_overlay_.speciesAt(
        width, height, logical_x, logical_y, aquarium_stocking_);
    if (!index || !aquarium_stocking_.focusIndex(*index)) return true;
    if (remove) {
        if (!applyAquariumStockingChange(false)) requestAquariumConstructionErrorFeedback();
    } else if (!aquarium_stocking_.pickUpFocused()) {
        requestAquariumConstructionErrorFeedback();
    }
    return true;
}

bool Overworld3DTestScreen::handleAquariumStockingPointerReleased(
    int logical_x, int logical_y) {
    if (!aquarium_stocking_.active()) return false;
    const int width = std::max(1, app_config_.window.virtual_width);
    const int height = std::max(1, app_config_.window.virtual_height);
    aquarium_stocking_.setPointerPosition(logical_x, logical_y);
    if (aquarium_stocking_.tab() ==
        gameplay::world3d::aquarium::construction::
            AquariumStockingController::Tab::Exhibit) {
        if (!aquarium_stocking_slider_drag_) return true;
        if (*aquarium_stocking_slider_drag_ == gameplay::world3d::aquarium::construction::
                AquariumStockingController::ExhibitControl::Brightness) {
            aquarium_stocking_.focusExhibitBrightness(
                aquarium_stocking_overlay_.exhibitBrightnessLevelAtX(width, logical_x));
        } else {
            aquarium_stocking_.focusExhibitMurkiness(
                aquarium_stocking_overlay_.exhibitMurkinessLevelAtX(width, logical_x));
        }
        aquarium_stocking_slider_drag_.reset();
        if (!applyAquariumExhibitStyleChange())
            requestAquariumConstructionErrorFeedback();
        return true;
    }
    if (aquarium_stocking_.holdingSpecies() &&
        aquarium_stocking_overlay_.capacityAt(
            width, height, logical_x, logical_y)) {
        if (applyAquariumStockingChange(true)) aquarium_stocking_.cancelHeld();
        else requestAquariumConstructionErrorFeedback();
    }
    return true;
}

} // namespace pr
