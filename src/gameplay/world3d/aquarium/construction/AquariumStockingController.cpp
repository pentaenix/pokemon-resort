#include "gameplay/world3d/aquarium/construction/AquariumStockingController.hpp"

#include "aquarium_geometry/Kernel.hpp"
#include "gameplay/world3d/aquarium/AquariumExhibitPreset.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace pr::gameplay::world3d::aquarium::construction {

void AquariumStockingController::configure(const AquariumSpeciesCatalog* catalog) {
    catalog_ = catalog;
    close();
}

bool AquariumStockingController::open(
    const pr::aquarium::geometry::TankDesign& tank,
    const AquariumTankPopulation* population,
    const AquariumNavigation* navigation,
    float pokemon_model_scale) {
    if (!catalog_ || catalog_->approved.empty() || tank.id.empty()) return false;
    tank_id_ = tank.id;
    residents_ = population ? population->residents : std::vector<AquariumResidentSelection>{};
    focused_index_ = 0;
    box_index_ = 0;
    tab_ = Tab::Pokemon;
    focus_area_ = FocusArea::Catalogue;
    held_species_index_.reset();
    pointer_active_ = false;
    capacity_first_visible_row_ = 0;
    setCurrentExhibitStyle(tank);
    const int floor_cells = static_cast<int>(
        pr::aquarium::geometry::footprintCells(tank.footprint).size());
    const int vertical_steps = std::max(0, tank.height_steps + tank.depth_steps - 4);
    const int raw_capacity = std::max(1,
        floor_cells + floor_cells * vertical_steps / 16);
    capacity_columns_ = std::clamp(
        pr::aquarium::geometry::occupiedWidthCells(tank.footprint), 3, 10);
    const int capacity_rows = std::max(1,
        (raw_capacity + capacity_columns_ - 1) / capacity_columns_);
    // The board is an abstract stocking budget, so round it to a complete,
    // readable rectangle instead of reproducing concave tank geometry or a
    // ragged final row.
    capacity_cells_ = capacity_columns_ * capacity_rows;
    habitat_fits_.clear();
    habitat_fits_.reserve(catalog_->approved.size());
    for (const auto& species : catalog_->approved) {
        habitat_fits_.push_back(navigation
            ? validateAquariumHabitat(species, *navigation, pokemon_model_scale)
            : AquariumHabitatFit{AquariumHabitatFitReason::Fits});
    }
    active_ = true;
    revealLastPlacement();
    return true;
}

void AquariumStockingController::close() {
    active_ = false;
    tank_id_.clear();
    residents_.clear();
    focused_index_ = 0;
    box_index_ = 0;
    tab_ = Tab::Pokemon;
    focus_area_ = FocusArea::Catalogue;
    held_species_index_.reset();
    pointer_active_ = false;
    capacity_cells_ = 0;
    capacity_first_visible_row_ = 0;
    focused_exhibit_preset_index_ = 0;
    current_exhibit_preset_id_ = "river";
    exhibit_control_ = ExhibitControl::Color;
    focused_brightness_level_ = kAquariumDefaultBrightnessLevel;
    focused_murkiness_level_ = kAquariumDefaultMurkinessLevel;
    focused_substrate_index_ = 0;
    habitat_fits_.clear();
}

void AquariumStockingController::scrollCapacityRows(int delta) {
    if (!active_ || delta == 0) return;
    capacity_first_visible_row_ = std::clamp(
        capacity_first_visible_row_ + delta, 0, maxCapacityFirstVisibleRow());
}

void AquariumStockingController::navigate(int dx, int dy) {
    if (!active_) return;
    pointer_active_ = false;
    if (tab_ == Tab::Exhibit) {
        int control = static_cast<int>(exhibit_control_);
        if (dy != 0) {
            control = std::clamp(control + (dy > 0 ? 1 : -1), 0, 3);
            exhibit_control_ = static_cast<ExhibitControl>(control);
        }
        if (dx == 0) return;
        const int direction = dx > 0 ? 1 : -1;
        switch (exhibit_control_) {
        case ExhibitControl::Color:
            focused_exhibit_preset_index_ = static_cast<std::size_t>(std::clamp(
                static_cast<int>(focused_exhibit_preset_index_) + direction,
                0, static_cast<int>(kAquariumExhibitPresets.size()) - 1));
            focused_brightness_level_ = aquariumExhibitDefaultBrightnessLevel(
                kAquariumExhibitPresets[focused_exhibit_preset_index_].id);
            break;
        case ExhibitControl::Brightness:
            focused_brightness_level_ = std::clamp(
                focused_brightness_level_ + direction, 0,
                kAquariumBrightnessLevelCount - 1);
            break;
        case ExhibitControl::Murkiness:
            focused_murkiness_level_ = std::clamp(
                focused_murkiness_level_ + direction, 0,
                kAquariumMurkinessControlLevelCount - 1);
            break;
        case ExhibitControl::Substrate:
            focused_substrate_index_ = static_cast<std::size_t>(std::clamp(
                static_cast<int>(focused_substrate_index_) + direction,
                0, static_cast<int>(kAquariumSubstratePresets.size()) - 1));
            break;
        }
        return;
    }
    if (!catalog_ || catalog_->approved.empty()) return;
    if (focus_area_ == FocusArea::Tank) {
        if (dy != 0) scrollCapacityRows(dy);
        if (dx < 0) focus_area_ = FocusArea::Catalogue;
        return;
    }
    const int count = static_cast<int>(catalog_->approved.size());
    const int local = focused_index_ - pageStart();
    const int page_end = std::min(count, pageStart() + kBoxSize);
    // Transfer's red tool can cross straight into the destination from the
    // right-most occupied slot.  A partially filled final row must not trap
    // controller focus in invisible empty slots.
    if (dx > 0 && (local % kColumns == kColumns - 1 ||
                   focused_index_ + 1 >= page_end)) {
        focus_area_ = FocusArea::Tank;
        return;
    }
    const int row = std::clamp(local / kColumns + dy, 0, kRows - 1);
    const int column = std::clamp(local % kColumns + dx, 0, kColumns - 1);
    focused_index_ = std::clamp(pageStart() + row * kColumns + column,
        pageStart(), count - 1);
}

bool AquariumStockingController::changeBox(int delta) {
    if (!active_ || tab_ != Tab::Pokemon || !catalog_ ||
        catalog_->approved.empty() || delta == 0) return false;
    const int previous = box_index_;
    const int local = std::max(0, focused_index_ - pageStart());
    const int count = boxCount();
    if (count <= 1) return false;
    box_index_ = (box_index_ + (delta > 0 ? 1 : -1) + count) % count;
    if (box_index_ == previous) return false;
    focused_index_ = std::min(
        static_cast<int>(catalog_->approved.size()) - 1, pageStart() + local);
    return true;
}

void AquariumStockingController::setTab(Tab tab) {
    if (!active_) return;
    tab_ = tab;
    focus_area_ = FocusArea::Catalogue;
    held_species_index_.reset();
}

void AquariumStockingController::toggleTab() {
    setTab(tab_ == Tab::Pokemon ? Tab::Exhibit : Tab::Pokemon);
}

bool AquariumStockingController::pickUpFocused() {
    if (!focusedSpecies() || focused_index_ < 0 ||
        !speciesCanBeAdded(static_cast<std::size_t>(focused_index_))) return false;
    held_species_index_ = focused_index_;
    focus_area_ = FocusArea::Catalogue;
    return true;
}

void AquariumStockingController::cancelHeld() {
    held_species_index_.reset();
    focus_area_ = FocusArea::Catalogue;
}

const AquariumSpeciesEntry* AquariumStockingController::heldSpecies() const {
    if (!active_ || !catalog_ || !held_species_index_ || *held_species_index_ < 0 ||
        *held_species_index_ >= static_cast<int>(catalog_->approved.size())) return nullptr;
    return &catalog_->approved[static_cast<std::size_t>(*held_species_index_)];
}

void AquariumStockingController::focusTank() {
    if (active_ && tab_ == Tab::Pokemon) focus_area_ = FocusArea::Tank;
}

void AquariumStockingController::focusCatalogue() {
    if (active_ && tab_ == Tab::Pokemon) focus_area_ = FocusArea::Catalogue;
}

void AquariumStockingController::setPointerPosition(int x, int y) {
    pointer_x_ = x;
    pointer_y_ = y;
    pointer_active_ = true;
}

void AquariumStockingController::useControllerPointer() {
    pointer_active_ = false;
}

bool AquariumStockingController::focusIndex(std::size_t index) {
    if (!active_ || tab_ != Tab::Pokemon || !catalog_ ||
        index >= catalog_->approved.size()) return false;
    focused_index_ = static_cast<int>(index);
    box_index_ = focused_index_ / kBoxSize;
    focus_area_ = FocusArea::Catalogue;
    return true;
}

bool AquariumStockingController::focusExhibitPreset(std::size_t index) {
    if (!active_ || tab_ != Tab::Exhibit ||
        index >= kAquariumExhibitPresets.size()) return false;
    focused_exhibit_preset_index_ = index;
    focused_brightness_level_ = aquariumExhibitDefaultBrightnessLevel(
        kAquariumExhibitPresets[index].id);
    exhibit_control_ = ExhibitControl::Color;
    return true;
}

bool AquariumStockingController::focusExhibitBrightness(int level) {
    if (!active_ || tab_ != Tab::Exhibit || level < 0 ||
        level >= kAquariumBrightnessLevelCount) return false;
    focused_brightness_level_ = level;
    exhibit_control_ = ExhibitControl::Brightness;
    return true;
}

bool AquariumStockingController::focusExhibitMurkiness(int level) {
    if (!active_ || tab_ != Tab::Exhibit || level < 0 ||
        level >= kAquariumMurkinessControlLevelCount) return false;
    focused_murkiness_level_ = level;
    exhibit_control_ = ExhibitControl::Murkiness;
    return true;
}

bool AquariumStockingController::focusExhibitSubstrate(std::size_t index) {
    if (!active_ || tab_ != Tab::Exhibit ||
        index >= kAquariumSubstratePresets.size()) return false;
    focused_substrate_index_ = index;
    exhibit_control_ = ExhibitControl::Substrate;
    return true;
}

std::string AquariumStockingController::focusedExhibitPresetId() const {
    return std::string(kAquariumExhibitPresets[
        std::min(focused_exhibit_preset_index_,
            kAquariumExhibitPresets.size() - 1U)].id);
}

void AquariumStockingController::setCurrentExhibitPreset(std::string preset_id) {
    current_exhibit_preset_id_ = isAquariumExhibitPreset(preset_id)
        ? std::move(preset_id) : std::string("river");
    for (std::size_t index = 0; index < kAquariumExhibitPresets.size(); ++index) {
        if (kAquariumExhibitPresets[index].id == current_exhibit_preset_id_) {
            focused_exhibit_preset_index_ = index;
            break;
        }
    }
}

void AquariumStockingController::setCurrentExhibitStyle(
    const pr::aquarium::geometry::TankDesign& tank) {
    setCurrentExhibitPreset(tank.exhibit_preset);
    focused_brightness_level_ = std::clamp(
        tank.brightness_level, 0, kAquariumBrightnessLevelCount - 1);
    focused_murkiness_level_ = std::clamp(
        tank.murkiness_level, 0, kAquariumMurkinessControlLevelCount - 1);
    focused_substrate_index_ = aquariumSubstratePresetIndex(tank.substrate_kind);
}

std::string AquariumStockingController::focusedSubstrateKind() const {
    return std::string(kAquariumSubstratePresets[
        std::min(focused_substrate_index_,
            kAquariumSubstratePresets.size() - 1U)].kind);
}

int AquariumStockingController::pageStart() const {
    return box_index_ * kBoxSize;
}

int AquariumStockingController::boxCount() const {
    if (!catalog_ || catalog_->approved.empty()) return 0;
    return (static_cast<int>(catalog_->approved.size()) + kBoxSize - 1) / kBoxSize;
}

int AquariumStockingController::capacityRows() const {
    return capacity_columns_ > 0
        ? (capacity_cells_ + capacity_columns_ - 1) / capacity_columns_ : 0;
}

int AquariumStockingController::maxCapacityFirstVisibleRow() const {
    return std::max(0, capacityRows() - kCapacityVisibleRows);
}

const AquariumHabitatFit& AquariumStockingController::habitatFit(
    std::size_t species_index) const {
    static const AquariumHabitatFit missing{};
    return species_index < habitat_fits_.size() ? habitat_fits_[species_index] : missing;
}

const AquariumSpeciesEntry* AquariumStockingController::focusedSpecies() const {
    if (!active_ || tab_ != Tab::Pokemon || !catalog_ || focused_index_ < 0 ||
        focused_index_ >= static_cast<int>(catalog_->approved.size())) return nullptr;
    return &catalog_->approved[static_cast<std::size_t>(focused_index_)];
}

int AquariumStockingController::residentCount(const std::string& species_id) const {
    const auto found = std::find_if(residents_.begin(), residents_.end(),
        [&](const auto& resident) { return resident.species_id == species_id; });
    return found == residents_.end() ? 0 : static_cast<int>(found->count);
}

int AquariumStockingController::usedCapacityCells() const {
    if (!catalog_) return 0;
    int used = 0;
    for (const AquariumResidentSelection& resident : residents_) {
        if (const AquariumSpeciesEntry* species = catalog_->findApproved(resident.species_id)) {
            used += species->capacityCellCount() * static_cast<int>(resident.count);
        }
    }
    return used;
}

std::vector<AquariumResidentCapacityPlacement>
AquariumStockingController::capacityPlacements() const {
    std::vector<AquariumResidentCapacityPlacement> placements;
    if (!catalog_ || capacity_cells_ <= 0 || capacity_columns_ <= 0) return placements;
    const int rows = (capacity_cells_ + capacity_columns_ - 1) / capacity_columns_;
    std::vector<bool> occupied(static_cast<std::size_t>(capacity_cells_), false);
    for (const AquariumResidentSelection& resident : residents_) {
        const AquariumSpeciesEntry* species = catalog_->findApproved(resident.species_id);
        if (!species || species->capacity_mask.empty()) continue;
        int mask_width = 0;
        for (const std::string& mask_row : species->capacity_mask) {
            mask_width = std::max(mask_width, static_cast<int>(mask_row.size()));
        }
        const int mask_height = static_cast<int>(species->capacity_mask.size());
        for (int instance = 0; instance < static_cast<int>(resident.count); ++instance) {
            AquariumResidentCapacityPlacement placement{species->id, instance, {}};
            bool placed = false;
            for (int row = 0; row + mask_height <= rows && !placed; ++row) {
                for (int column = 0; column + mask_width <= capacity_columns_ && !placed;
                     ++column) {
                    std::vector<int> candidate;
                    bool valid = true;
                    for (int mask_row = 0; mask_row < mask_height && valid; ++mask_row) {
                        for (int mask_column = 0;
                             mask_column < static_cast<int>(species->capacity_mask[mask_row].size());
                             ++mask_column) {
                            if (species->capacity_mask[mask_row][mask_column] != '1') continue;
                            const int cell = (row + mask_row) * capacity_columns_ +
                                column + mask_column;
                            if (cell < 0 || cell >= capacity_cells_ || occupied[cell]) {
                                valid = false;
                                break;
                            }
                            candidate.push_back(cell);
                        }
                    }
                    if (valid && !candidate.empty()) {
                        placement.cell_indices = std::move(candidate);
                        for (const int cell : placement.cell_indices) occupied[cell] = true;
                        placed = true;
                    }
                }
            }
            if (!placed) return placements;
            placements.push_back(std::move(placement));
        }
    }
    return placements;
}

std::vector<AquariumResidentCapacityPlacement>
AquariumStockingController::previewCapacityPlacements() const {
    const auto candidate_residents = residentsWithFocusedAdded();
    if (!candidate_residents) return capacityPlacements();
    AquariumStockingController candidate = *this;
    candidate.residents_ = *candidate_residents;
    return candidate.capacityPlacements();
}

std::optional<AquariumResidentCapacityPlacement>
AquariumStockingController::focusedPreviewPlacement() const {
    const AquariumSpeciesEntry* species = heldSpecies();
    if (!species) species = focusedSpecies();
    if (!species) return std::nullopt;
    const int new_instance = residentCount(species->id);
    const auto placements = previewCapacityPlacements();
    const auto found = std::find_if(placements.begin(), placements.end(),
        [&](const auto& placement) {
            return placement.species_id == species->id &&
                placement.instance_index == new_instance;
        });
    return found == placements.end() ? std::nullopt
                                     : std::optional<AquariumResidentCapacityPlacement>(*found);
}

bool AquariumStockingController::speciesCanBeAdded(std::size_t species_index) const {
    if (!active_ || tab_ != Tab::Pokemon || !catalog_ ||
        species_index >= catalog_->approved.size() ||
        !habitatFit(species_index).fits()) return false;
    AquariumStockingController candidate = *this;
    candidate.focused_index_ = static_cast<int>(species_index);
    candidate.held_species_index_.reset();
    return candidate.residentsWithFocusedAdded().has_value();
}

void AquariumStockingController::setResidents(
    std::vector<AquariumResidentSelection> residents) {
    residents_ = std::move(residents);
    revealLastPlacement();
}

void AquariumStockingController::revealLastPlacement() {
    const auto placements = capacityPlacements();
    if (placements.empty()) {
        capacity_first_visible_row_ = 0;
        return;
    }
    int last_row = 0;
    for (const int cell : placements.back().cell_indices) {
        last_row = std::max(last_row, cell / std::max(1, capacity_columns_));
    }
    if (last_row >= capacity_first_visible_row_ + kCapacityVisibleRows) {
        capacity_first_visible_row_ = last_row - kCapacityVisibleRows + 1;
    }
    capacity_first_visible_row_ = std::clamp(
        capacity_first_visible_row_, 0, maxCapacityFirstVisibleRow());
}

std::optional<std::vector<AquariumResidentSelection>>
AquariumStockingController::residentsWithFocusedAdded() const {
    const int species_index = held_species_index_.value_or(focused_index_);
    const AquariumSpeciesEntry* species = heldSpecies();
    if (!species) species = focusedSpecies();
    if (!species || species_index < 0 ||
        !habitatFit(static_cast<std::size_t>(species_index)).fits() ||
        usedCapacityCells() + species->capacityCellCount() > capacity_cells_) {
        return std::nullopt;
    }
    auto result = residents_;
    const auto found = std::find_if(result.begin(), result.end(),
        [&](const auto& resident) { return resident.species_id == species->id; });
    if (found == result.end()) result.push_back({species->id, 1});
    else ++found->count;
    AquariumStockingController candidate = *this;
    candidate.residents_ = result;
    std::size_t expected_instances = 0;
    for (const auto& resident : result) expected_instances += resident.count;
    if (candidate.capacityPlacements().size() != expected_instances) return std::nullopt;
    return result;
}

std::optional<std::vector<AquariumResidentSelection>>
AquariumStockingController::residentsWithFocusedRemoved() const {
    const AquariumSpeciesEntry* species = focusedSpecies();
    if (!species) return std::nullopt;
    auto result = residents_;
    const auto found = std::find_if(result.begin(), result.end(),
        [&](const auto& resident) { return resident.species_id == species->id; });
    if (found == result.end()) return std::nullopt;
    if (found->count > 1) --found->count;
    else result.erase(found);
    return result;
}

} // namespace pr::gameplay::world3d::aquarium::construction
