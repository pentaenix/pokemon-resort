#include "gameplay/world3d/aquarium/AquariumSpeciesCatalog.hpp"
#include "gameplay/world3d/aquarium/AquariumExhibitPreset.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumHabitatValidator.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumStockingController.hpp"
#include "gameplay/world3d/aquarium/rendering/AquariumBoundedFog.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace aquarium = pr::gameplay::world3d::aquarium;
namespace construction = aquarium::construction;
namespace geo = pr::aquarium::geometry;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path writeCatalogue() {
    const auto path = std::filesystem::temp_directory_path() /
        "pokemon_resort_aquarium_stocking_catalogue.json";
    std::ofstream output(path, std::ios::trunc);
    output << R"JSON({
  "schema": "pokemon-resort-aquarium-species",
  "schemaVersion": 1,
  "catalogRevision": 8,
  "entries": [
    {
      "id": "0087:00", "dex": 87, "species": "dewgong", "displayName": "Dewgong", "form": "00",
      "review": {"status": "rejected"}
    },
    {
      "id": "0090:00", "dex": 90, "species": "shellder", "displayName": "Shellder", "form": "00",
      "review": {"status": "approved"},
      "model": {"path": "shellder.glbz"},
      "presentation": {"animation": "slot6_02", "pitchDegrees": 0, "yawDegrees": 0, "scaleMultiplier": 1},
      "habitat": {"verticalZone": "bottom", "surfaceBehavior": "submerged", "waterKinds": ["saltwater"]},
      "behavior": {"movementProfile": "bottom-swimmer", "travelDirection": "backward", "idleAnimation": "slot6_00", "minimumGroup": 1, "preferredGroup": 1},
      "capacity": {"mask": ["1"]}
    },
    {
      "id": "0382:00", "dex": 382, "species": "kyogre", "displayName": "Kyogre", "form": "00",
      "review": {"status": "approved"},
      "model": {"path": "kyogre.glbz"},
      "presentation": {"animation": "slot4_00", "pitchDegrees": 0, "yawDegrees": 0, "scaleMultiplier": 1},
      "habitat": {"verticalZone": "open-water", "surfaceBehavior": "submerged", "waterKinds": ["saltwater"]},
      "behavior": {"movementProfile": "large-cruiser", "minimumGroup": 1, "preferredGroup": 1},
      "capacity": {"mask": ["111", "111"]}
    }
  ]
})JSON";
    return path;
}

void catalogueExposesOnlyApprovedEntries() {
    const auto path = writeCatalogue();
    const auto loaded = aquarium::loadAquariumSpeciesCatalog(path);
    std::filesystem::remove(path);
    require(loaded.valid, "valid catalogue did not load");
    require(loaded.catalog.revision == 8, "catalogue revision changed");
    require(loaded.catalog.approved.size() == 2, "non-approved entry leaked into runtime catalogue");
    require(loaded.catalog.findApproved("0090:00") != nullptr, "approved species lookup failed");
    require(loaded.catalog.findApproved("0087:00") == nullptr, "unapproved species lookup succeeded");
}

void exhibitPresetsGradeTheWholeTankInterior() {
    const auto& river = aquarium::aquariumExhibitPreset("river");
    const auto& swamp = aquarium::aquariumExhibitPreset("swamp");
    const auto& depths = aquarium::aquariumExhibitPreset("depths");
    require(depths.attenuation_multiplier == river.attenuation_multiplier &&
            swamp.attenuation_multiplier == river.attenuation_multiplier &&
            depths.sand_brightness_multiplier == river.sand_brightness_multiplier &&
            depths.pokemon_brightness_multiplier == river.pokemon_brightness_multiplier,
        "color presets leaked into the independent brightness/murkiness controls");
    require(aquarium::aquariumExhibitDefaultBrightnessLevel("depths") == 0 &&
            aquarium::aquariumExhibitDefaultBrightnessLevel("river") ==
                aquarium::kAquariumDefaultBrightnessLevel,
        "Depths no longer begins at the dimmest player-adjustable light setting");
    require(swamp.sand_tint[1] > swamp.sand_tint[0] &&
            swamp.sand_tint[1] > swamp.sand_tint[2],
        "Swamp substrate lost its restrained green tint");
    require(aquarium::kAquariumSubstratePresets.size() == 4 &&
            aquarium::aquariumSubstratePreset("sand-flat").resort_tile_id == 103 &&
            aquarium::isAquariumSubstratePreset("gravel-flat") &&
            aquarium::isAquariumSubstratePreset("moss-flat") &&
            aquarium::isAquariumSubstratePreset("dirt-flat"),
        "Black 2 aquarium substrate catalogue changed unexpectedly");
    require(aquarium::aquariumMurkinessMultiplier(
                aquarium::kAquariumMurkinessLevelCount - 1) == 9.0f &&
            aquarium::kAquariumMurkinessControlLevelCount == 9 &&
            aquarium::aquariumMurkinessMultiplier(0) < 1.0f,
        "murkiness slider no longer spans nine ticks through former level 5");
}

void boundedFogUsesOnlyTheWaterSegment() {
    namespace fog = aquarium::rendering;
    constexpr float kTolerance = 1.0e-5f;
    const float expected[] = {
        0.0f, 0.2211992169f, 0.3934693403f,
        0.5276334473f, 0.6321205588f};
    for (int distance = 0; distance <= 4; ++distance) {
        const float actual = fog::aquariumFogCoverage(
            static_cast<float>(distance), 0.0f, 4.0f, 1.0f, 1.0f);
        require(std::abs(actual - expected[distance]) <= kTolerance,
            "bounded fog exponential extinction no longer matches its reference values");
    }
    require(fog::aquariumFogCoverage(8.0f, 0.0f, 4.0f, 1.0f, 1.0f) >
            expected[4] &&
            fog::aquariumFogCoverage(16.0f, 0.0f, 4.0f, 1.0f, 1.0f) >
            fog::aquariumFogCoverage(8.0f, 0.0f, 4.0f, 1.0f, 1.0f),
        "bounded fog must keep changing beyond the nominal visibility distance");

    const auto hit = fog::intersectFogRayBox(
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {-1.0f, -1.0f, 5.0f}, {1.0f, 1.0f, 9.0f});
    require(hit.hit && std::abs(hit.near_distance - 5.0f) <= kTolerance &&
            std::abs(hit.far_distance - 9.0f) <= kTolerance,
        "bounded fog ray/box interval changed");
    require(fog::aquariumFogWaterPath(hit, 4.0f) == 0.0f,
        "room air before a tank incorrectly contributed fog");
    require(std::abs(fog::aquariumFogWaterPath(hit, 7.0f) - 2.0f) <= kTolerance,
        "surface inside a tank did not use only its water path");
    require(std::abs(fog::aquariumFogWaterPath(hit, 12.0f) - 4.0f) <= kTolerance,
        "surface behind a tank did not clamp to the tank traversal");
    const float near_transmittance = fog::aquariumFogPathTransmittance(
        2.0f, 0.0f, 8.0f);
    const float far_transmittance = fog::aquariumFogPathTransmittance(
        7.0f, 0.0f, 8.0f);
    require(far_transmittance < near_transmittance,
        "sand seen through more water did not lose more transmitted light");
    require(fog::aquariumFogPathTransmittance(80.0f, 0.0f, 8.0f) <
            far_transmittance,
        "deep water absorption still flattened at a transmission clamp");
    const float one_tank_opacity = fog::aquariumFogCompositeOpacity(
        0.25f, far_transmittance);
    const float two_tank_transmittance =
        (1.0f - one_tank_opacity) * (1.0f - one_tank_opacity);
    require(two_tank_transmittance < 1.0f - one_tank_opacity,
        "a later tank pass replaced rather than accumulated water attenuation");

    const auto inside = fog::intersectFogRayBox(
        {0.0f, 0.0f, 6.0f}, {0.0f, 0.0f, 1.0f},
        {-1.0f, -1.0f, 5.0f}, {1.0f, 1.0f, 9.0f});
    require(std::abs(fog::aquariumFogWaterPath(inside, 2.0f) - 2.0f) <= kTolerance,
        "camera-inside fog did not begin at the camera");
    require(fog::aquariumFogProxyIsVisible(4.0f, 7.0f, true) &&
            !fog::aquariumFogProxyIsVisible(8.0f, 7.0f, true) &&
            fog::aquariumFogProxyIsVisible(8.0f, 7.0f, false),
        "bounded fog proxy no longer respects opaque floor occlusion");
    require(fog::aquariumFogVisibilityWorld(8) <
            fog::aquariumFogVisibilityWorld(0) &&
            fog::aquariumFogMaximumOpacity(8) >
            fog::aquariumFogMaximumOpacity(0),
        "murkiness ticks no longer monotonically strengthen bounded fog");
    require(std::abs(fog::aquariumFogVisibilityWorld(8) - 3.5f * 16.0f) <=
                kTolerance &&
            std::abs(fog::aquariumFogMaximumOpacity(8) - 0.75f) <= kTolerance &&
            std::abs(aquarium::aquariumMurkinessMultiplier(8) - 9.0f) <=
                kTolerance,
        "final resampled murkiness tick drifted from the former level 5");
}

void shippedCatalogueLoadsAndReferencesExistingModels() {
    const auto source_root = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path().parent_path().parent_path();
    const auto loaded = aquarium::loadAquariumSpeciesCatalog(
        source_root / "config/gameplay/world3d/aquarium_species.json");
    require(loaded.valid, "shipped aquarium catalogue did not load");
    require(!loaded.catalog.approved.empty(), "shipped catalogue has no approved species");
    for (const auto& species : loaded.catalog.approved) {
        require(std::filesystem::is_regular_file(source_root / species.model_path),
            "approved aquarium species references a missing model");
        require(species.physical_envelope.valid,
            "approved aquarium species has a missing or stale physical envelope");
    }
    const auto* kyogre = loaded.catalog.findApproved("0382:00");
    const auto* wishiwashi = loaded.catalog.findApproved("0746:00");
    const auto* pyukumuku = loaded.catalog.findApproved("0771:00");
    const auto* barboach = loaded.catalog.findApproved("0339:00");
    const auto* wailord = loaded.catalog.findApproved("0321:00");
    const auto* golisopod = loaded.catalog.findApproved("0768:00");
    const auto* corsola = loaded.catalog.findApproved("0222:00");
    const auto* tentacool = loaded.catalog.findApproved("0072:00");
    const auto* tentacruel = loaded.catalog.findApproved("0073:00");
    const auto* frillish = loaded.catalog.findApproved("0592:00");
    const auto* jellicent = loaded.catalog.findApproved("0593:00");
    const auto* shellder = loaded.catalog.findApproved("0090:00");
    const auto* cloyster = loaded.catalog.findApproved("0091:00");
    const auto* huntail = loaded.catalog.findApproved("0367:00");
    const auto* gorebyss = loaded.catalog.findApproved("0368:00");
    const auto* relicanth = loaded.catalog.findApproved("0369:00");
    const auto* clawitzer = loaded.catalog.findApproved("0693:00");
    require(kyogre && kyogre->capacity_mask ==
            std::vector<std::string>({"11111", "11111", "11111", "11111"}),
        "Kyogre capacity footprint did not receive large-species comfort clearance");
    require(golisopod && golisopod->capacity_mask ==
            std::vector<std::string>({"1111", "1111", "1111"}),
        "Golisopod capacity footprint still permits an undersized three-cell-wide tank");
    require(wishiwashi && wishiwashi->capacity_mask == std::vector<std::string>({"1"}) &&
            pyukumuku && pyukumuku->capacity_mask == std::vector<std::string>({"1"}) &&
            barboach && barboach->capacity_mask == std::vector<std::string>({"1"}),
        "small-species capacity-clearance exceptions were expanded");
    require(wailord && wailord->capacity_mask ==
            std::vector<std::string>({"111111", "111111", "111111", "111111"}),
        "maximum-size Wailord capacity footprint was expanded");
    require(corsola && corsola->movement_profile == "timid-reef" &&
            corsola->random_start && corsola->idle_seconds_minimum == 8.0f &&
            corsola->idle_seconds_maximum == 18.0f &&
            corsola->threat_species ==
                std::vector<std::string>({"mareanie", "toxapex"}),
        "Corsola timid reef behavior did not survive catalogue loading");
    require(tentacool && tentacruel && frillish && jellicent &&
            tentacool->movement_profile == "jelly-drift" &&
            tentacruel->movement_profile == "jelly-drift" &&
            frillish->movement_profile == "jelly-drift" &&
            jellicent->movement_profile == "jelly-drift",
        "approved jellyfish families lost their slow vertical-drift profile");
    require(shellder && cloyster &&
            shellder->activity.rest_seconds_minimum == 12.0f &&
            cloyster->activity.rest_seconds_minimum == 14.0f &&
            shellder->activity.move_seconds_maximum == 2.2f &&
            cloyster->activity.move_seconds_maximum == 2.0f &&
            shellder->activity.rest_at_bottom && cloyster->activity.rest_at_bottom,
        "Shellder and Cloyster lost their mostly-resting burst movement");
    require(pyukumuku && pyukumuku->activity.intermittent() &&
            pyukumuku->activity.rest_seconds_minimum == 5.0f &&
            pyukumuku->activity.rest_at_bottom,
        "Pyukumuku lost its intermittent bottom activity");
    require(huntail && gorebyss && relicanth && clawitzer &&
            huntail->movement_profile == "benthic-rest-swimmer" &&
            gorebyss->movement_profile == "benthic-rest-swimmer" &&
            relicanth->movement_profile == "benthic-rest-swimmer" &&
            clawitzer->movement_profile == "benthic-rest-swimmer" &&
            huntail->activity.roaming_height_meters == 0.9f &&
            huntail->activity.rest_at_bottom,
        "benthic swimmers lost their bottom-rest excursion profile");
}

aquarium::AquariumNavigation squareNavigation(float width, float depth, float height) {
    aquarium::AquariumNavigation navigation;
    aquarium::SwimVolumeLayer layer;
    layer.y_bottom = 0.0f;
    layer.y_top = height;
    layer.polygons = {{{{0.0f, 0.0f}, {width, 0.0f},
                         {width, depth}, {0.0f, depth}}}};
    navigation.layers.push_back(std::move(layer));
    navigation.valid = true;
    return navigation;
}

void physicalEnvelopePreventsImpossibleStocking() {
    aquarium::AquariumSpeciesEntry species;
    species.id = "large";
    species.species = "large";
    species.movement_profile = "large-cruiser";
    species.scale_multiplier = 1.0f;
    species.capacity_mask = {"1"};
    species.physical_envelope = {
        1, "fixture", -30.0f, 30.0f, -8.0f, 8.0f, -30.0f, 30.0f, 12, true};
    const auto compact = squareNavigation(3.0f, 3.0f, 3.0f);
    const auto rejected = construction::validateAquariumHabitat(species, compact, 1.0f);
    require(rejected.reason == construction::AquariumHabitatFitReason::HorizontalClearance,
        "large animated envelope was accepted by an undersized tank");
    aquarium::AquariumSpeciesCatalog catalog;
    catalog.approved.push_back(species);
    geo::TankDesign tank;
    tank.id = "compact";
    tank.footprint.width_cells = 3;
    tank.footprint.depth_cells = 3;
    construction::AquariumStockingController controller;
    controller.configure(&catalog);
    require(controller.open(tank, nullptr, &compact, 1.0f),
        "physical-fit controller fixture did not open");
    require(!controller.speciesCanBeAdded(0) && !controller.residentsWithFocusedAdded(),
        "stocking controller ignored the model-aware habitat rejection");

    species.movement_profile = "stationary";
    species.physical_envelope = {
        1, "fixture", -2.0f, 2.0f, -20.0f, 20.0f, -2.0f, 2.0f, 12, true};
    const auto shallow = squareNavigation(3.0f, 3.0f, 1.0f);
    const auto vertical = construction::validateAquariumHabitat(species, shallow, 1.0f);
    require(vertical.reason == construction::AquariumHabitatFitReason::VerticalClearance,
        "tall animated envelope was accepted without vertical clearance");
}

void controllerAddsAndRemovesWithinTankCapacity() {
    const auto path = writeCatalogue();
    const auto loaded = aquarium::loadAquariumSpeciesCatalog(path);
    std::filesystem::remove(path);
    construction::AquariumStockingController controller;
    controller.configure(&loaded.catalog);
    geo::TankDesign tank;
    tank.id = "tank_stocking";
    tank.footprint.width_cells = 3;
    tank.footprint.depth_cells = 3;
    tank.height_steps = 4;
    require(controller.open(tank, nullptr), "stocking controller did not open");
    require(controller.capacityCells() == 9, "flat tank capacity must begin with floor area");
    require(controller.focusIndex(1), "large species could not be focused");
    require(controller.pickUpFocused() && controller.holdingSpecies() &&
            controller.heldSpecies() && controller.heldSpecies()->id == "0382:00",
        "red-tool pickup did not retain the selected catalogue species");
    for (int step = 0; step < construction::AquariumStockingController::kColumns; ++step) {
        controller.navigate(1, 0);
    }
    require(controller.focusArea() == construction::AquariumStockingController::FocusArea::Tank,
        "held catalogue species could not navigate onto the tank target");
    const auto added = controller.residentsWithFocusedAdded();
    require(added && added->size() == 1 && added->front().species_id == "0382:00",
        "focused approved species was not added");
    controller.setResidents(*added);
    controller.cancelHeld();
    require(controller.usedCapacityCells() == 6, "capacity mask did not consume six cells");
    const auto placements = controller.capacityPlacements();
    require(placements.size() == 1 && placements.front().cell_indices.size() == 6,
        "stocked Pokemon did not retain its authored capacity-mask shape");
    require(!controller.residentsWithFocusedAdded(), "over-capacity resident was accepted");
    const auto removed = controller.residentsWithFocusedRemoved();
    require(removed && removed->empty(), "resident removal did not empty the roster");

    aquarium::AquariumSpeciesCatalog scrolling_catalog;
    for (int index = 0; index < 31; ++index) {
        auto entry = loaded.catalog.approved.front();
        entry.id = "scroll:" + std::to_string(index);
        scrolling_catalog.approved.push_back(std::move(entry));
    }
    controller.configure(&scrolling_catalog);
    require(controller.open(tank, nullptr), "scrolling catalogue did not open");
    require(controller.boxCount() == 2, "31 approved species did not create two box pages");
    require(controller.changeBox(1), "catalogue could not move to its second box");
    require(controller.pageStart() == construction::AquariumStockingController::kBoxSize &&
            controller.focusedIndex() >= controller.pageStart() &&
            controller.focusedIndex() < controller.pageStart() +
                construction::AquariumStockingController::kBoxSize,
        "box navigation did not keep focus on the current 30-slot page");
    require(controller.changeBox(1) && controller.boxIndex() == 0,
        "last catalogue box did not wrap back to the first");
    controller.setTab(construction::AquariumStockingController::Tab::Exhibit);
    require(controller.focusedSpecies() == nullptr &&
            !controller.residentsWithFocusedAdded(),
        "empty exhibit tab leaked Pokemon stocking actions");
    require(controller.focusExhibitPreset(3) &&
            controller.focusedExhibitPresetId() == "depths",
        "exhibit preset grid could not focus Depths");
    controller.setCurrentExhibitPreset("depths");
    require(controller.currentExhibitPresetId() == "depths",
        "saved exhibit preset did not synchronize into the stocking UI");
    controller.navigate(-1, 0);
    require(controller.focusedExhibitPresetId() == "open-ocean",
        "controller could not navigate the compact exhibit color row");
    controller.navigate(0, 1);
    controller.navigate(-1, 0);
    require(controller.focusedExhibitControl() ==
                construction::AquariumStockingController::ExhibitControl::Brightness &&
            controller.focusedBrightnessLevel() ==
                aquarium::aquariumExhibitDefaultBrightnessLevel("open-ocean") - 1,
        "controller could not focus and adjust tank brightness");
    controller.navigate(0, 1);
    controller.navigate(1, 0);
    require(controller.focusedExhibitControl() ==
                construction::AquariumStockingController::ExhibitControl::Murkiness &&
            controller.focusedMurkinessLevel() ==
                aquarium::kAquariumDefaultMurkinessLevel + 1,
        "controller could not focus and adjust water murkiness");
    require(controller.focusExhibitMurkiness(
                aquarium::kAquariumMurkinessControlLevelCount - 1) &&
            !controller.focusExhibitMurkiness(
                aquarium::kAquariumMurkinessControlLevelCount),
        "controller did not expose exactly nine resampled murkiness ticks");
    controller.navigate(0, 1);
    controller.navigate(1, 0);
    require(controller.focusedExhibitControl() ==
                construction::AquariumStockingController::ExhibitControl::Substrate &&
            controller.focusedSubstrateKind() == "gravel-flat",
        "controller could not focus and change substrate");
    controller.setTab(construction::AquariumStockingController::Tab::Pokemon);

    tank.footprint.width_cells = 10;
    tank.footprint.depth_cells = 20;
    controller.configure(&scrolling_catalog);
    require(controller.open(tank, nullptr), "large rectangular capacity board did not open");
    require(controller.capacityCells() ==
            controller.capacityColumns() * controller.capacityRows(),
        "capacity board retained a ragged non-rectangular final row");
    controller.scrollCapacityRows(5);
    require(controller.capacityFirstVisibleRow() == 5,
        "large capacity board could not scroll independently");
}

} // namespace

int main() {
    try {
        catalogueExposesOnlyApprovedEntries();
        exhibitPresetsGradeTheWholeTankInterior();
        boundedFogUsesOnlyTheWaterSegment();
        shippedCatalogueLoadsAndReferencesExistingModels();
        physicalEnvelopePreventsImpossibleStocking();
        controllerAddsAndRemovesWithinTankCapacity();
        std::cout << "aquarium_stocking_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_stocking_tests: " << error.what() << '\n';
        return 1;
    }
}
