#pragma once

#include "aquarium_geometry/Types.hpp"
#include "gameplay/world3d/aquarium/AquariumSpeciesCatalog.hpp"
#include "gameplay/world3d/aquarium/AquariumSubstratePreset.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumHabitatValidator.hpp"
#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::construction {

struct AquariumResidentCapacityPlacement {
    std::string species_id;
    int instance_index = 0;
    std::vector<int> cell_indices;
};

class AquariumStockingController {
public:
    static constexpr int kColumns = 6;
    static constexpr int kRows = 5;
    static constexpr int kBoxSize = kColumns * kRows;
    static constexpr int kPageSize = kBoxSize;
    static constexpr int kCapacityVisibleRows = 7;
    static constexpr int kExhibitPresetColumns = 2;

    enum class Tab {
        Pokemon,
        Exhibit,
    };
    enum class FocusArea {
        Catalogue,
        Tank,
    };
    enum class ExhibitControl {
        Color,
        Brightness,
        Murkiness,
        Substrate,
    };

    void configure(const AquariumSpeciesCatalog* catalog);
    bool open(
        const pr::aquarium::geometry::TankDesign& tank,
        const AquariumTankPopulation* population,
        const AquariumNavigation* navigation = nullptr,
        float pokemon_model_scale = 0.0f);
    void close();
    bool active() const { return active_; }

    void navigate(int dx, int dy);
    bool changeBox(int delta);
    void scrollRows(int delta) { changeBox(delta); }
    void scrollCapacityRows(int delta);
    void setTab(Tab tab);
    void toggleTab();
    Tab tab() const { return tab_; }
    bool pickUpFocused();
    void cancelHeld();
    bool holdingSpecies() const { return held_species_index_.has_value(); }
    const AquariumSpeciesEntry* heldSpecies() const;
    FocusArea focusArea() const { return focus_area_; }
    void focusTank();
    void focusCatalogue();
    void setPointerPosition(int x, int y);
    void useControllerPointer();
    bool pointerActive() const { return pointer_active_; }
    int pointerX() const { return pointer_x_; }
    int pointerY() const { return pointer_y_; }
    bool focusIndex(std::size_t index);
    bool focusExhibitPreset(std::size_t index);
    bool focusExhibitBrightness(int level);
    bool focusExhibitMurkiness(int level);
    bool focusExhibitSubstrate(std::size_t index);
    void focusExhibitControl(ExhibitControl control) { exhibit_control_ = control; }
    ExhibitControl focusedExhibitControl() const { return exhibit_control_; }
    std::size_t focusedExhibitPresetIndex() const { return focused_exhibit_preset_index_; }
    const std::string& currentExhibitPresetId() const { return current_exhibit_preset_id_; }
    std::string focusedExhibitPresetId() const;
    void setCurrentExhibitPreset(std::string preset_id);
    void setCurrentExhibitStyle(const pr::aquarium::geometry::TankDesign& tank);
    int focusedBrightnessLevel() const { return focused_brightness_level_; }
    int focusedMurkinessLevel() const { return focused_murkiness_level_; }
    std::size_t focusedSubstrateIndex() const { return focused_substrate_index_; }
    std::string focusedSubstrateKind() const;
    int focusedIndex() const { return focused_index_; }
    int pageStart() const;
    int boxIndex() const { return box_index_; }
    int boxCount() const;
    int capacityRows() const;
    int capacityFirstVisibleRow() const { return capacity_first_visible_row_; }
    int maxCapacityFirstVisibleRow() const;
    const AquariumSpeciesEntry* focusedSpecies() const;

    int capacityCells() const { return capacity_cells_; }
    int capacityColumns() const { return capacity_columns_; }
    int usedCapacityCells() const;
    std::vector<AquariumResidentCapacityPlacement> capacityPlacements() const;
    std::vector<AquariumResidentCapacityPlacement> previewCapacityPlacements() const;
    std::optional<AquariumResidentCapacityPlacement> focusedPreviewPlacement() const;
    const AquariumHabitatFit& habitatFit(std::size_t species_index) const;
    bool speciesCanBeAdded(std::size_t species_index) const;
    int residentCount(const std::string& species_id) const;
    const std::string& tankId() const { return tank_id_; }
    const std::vector<AquariumResidentSelection>& residents() const { return residents_; }
    void setResidents(std::vector<AquariumResidentSelection> residents);

    std::optional<std::vector<AquariumResidentSelection>> residentsWithFocusedAdded() const;
    std::optional<std::vector<AquariumResidentSelection>> residentsWithFocusedRemoved() const;

private:
    const AquariumSpeciesCatalog* catalog_ = nullptr;
    bool active_ = false;
    std::string tank_id_;
    int focused_index_ = 0;
    int box_index_ = 0;
    Tab tab_ = Tab::Pokemon;
    FocusArea focus_area_ = FocusArea::Catalogue;
    std::optional<int> held_species_index_;
    bool pointer_active_ = false;
    int pointer_x_ = 0;
    int pointer_y_ = 0;
    int capacity_cells_ = 0;
    int capacity_columns_ = 6;
    int capacity_first_visible_row_ = 0;
    std::size_t focused_exhibit_preset_index_ = 0;
    std::string current_exhibit_preset_id_ = "river";
    ExhibitControl exhibit_control_ = ExhibitControl::Color;
    int focused_brightness_level_ = kAquariumDefaultBrightnessLevel;
    int focused_murkiness_level_ = kAquariumDefaultMurkinessLevel;
    std::size_t focused_substrate_index_ = 0;
    std::vector<AquariumResidentSelection> residents_;
    std::vector<AquariumHabitatFit> habitat_fits_;

    void revealLastPlacement();
};

} // namespace pr::gameplay::world3d::aquarium::construction
