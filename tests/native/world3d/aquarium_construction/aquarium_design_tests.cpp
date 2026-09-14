#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace construction = pr::gameplay::world3d::aquarium::construction;
namespace geometry = pr::aquarium::geometry;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

construction::AquariumDesignDocument documentFixture() {
    construction::AquariumDesignDocument document;
    document.design_id = "aqd_golden";
    document.map_id = "aquarium12";
    document.revision = 12;
    geometry::TankDesign tank;
    tank.id = "tank_golden_rectangle_even";
    tank.footprint.origin_cell = {7, 6};
    tank.footprint.width_cells = 7;
    tank.footprint.depth_cells = 4;
    tank.height_steps = 8;
    tank.exhibit_preset = "swamp";
    tank.substrate_kind = "moss-flat";
    tank.brightness_level = 6;
    tank.murkiness_level = 7;
    tank.footprint.subtracted_cells = {{3, 0}};
    tank.corner_radii = {{{0, 0}, 1}, {{7, 4}, 2}};
    document.tanks.push_back(std::move(tank));
    document.tank_populations.push_back({
        "tank_golden_rectangle_even", {{"0087:00", 2}, {"0223:00", 4}}});
    geometry::TankDesign shaped;
    shaped.id = "tank_golden_u_rotated";
    shaped.footprint.shape = geometry::FootprintShape::U;
    shaped.footprint.origin_cell = {2, 3};
    shaped.footprint.width_cells = 7;
    shaped.footprint.depth_cells = 5;
    shaped.footprint.rotation_quarter_turns = 3;
    shaped.footprint.notch_width_cells = 3;
    shaped.footprint.notch_depth_cells = 3;
    shaped.height_steps = 12;
    shaped.corner_radius_steps = 2;
    document.tanks.push_back(std::move(shaped));
    geometry::TankDesign tunnel;
    tunnel.id = "tank_golden_tunnel";
    tunnel.footprint.origin_cell = {20, 20};
    tunnel.footprint.width_cells = 6;
    tunnel.footprint.depth_cells = 4;
    tunnel.tunnels.push_back({"tunnel_straight", geometry::TunnelRoute::Straight,
        {{20, 21}, {21, 21}, {22, 21}, {23, 21}, {24, 21}, {25, 21}, {26, 21}}});
    document.tanks.push_back(std::move(tunnel));
    return document;
}

void testCanonicalRoundTrip() {
    const construction::AquariumDesignDocument document = documentFixture();
    const std::string first = construction::serializeAquariumDesignCanonical(document);
    const construction::AquariumDesignLoadResult parsed = construction::parseAquariumDesign(first);
    require(parsed.status == construction::AquariumDesignLoadStatus::Loaded, "canonical design did not load");
    require(parsed.document.has_value(), "loaded design is missing document");
    require(parsed.document->tanks.front().corner_radii.size() == 2 &&
            parsed.document->tanks.front().corner_radii.back().radius_steps == 2,
        "per-corner radii did not survive serialization");
    require(parsed.document->tanks.front().exhibit_preset == "swamp",
        "per-tank exhibit preset did not survive serialization");
    require(parsed.document->tanks.front().substrate_kind == "moss-flat" &&
            parsed.document->tanks.front().brightness_level == 6 &&
            parsed.document->tanks.front().murkiness_level == 7,
        "per-tank substrate or water controls did not survive serialization");
    require(parsed.document->tanks.back().tunnels.size() == 1 &&
            parsed.document->tanks.back().tunnels.front().centreline_cells.size() == 7,
        "ordered tunnel centreline did not survive serialization");
    const auto* population = construction::aquariumTankPopulation(
        *parsed.document, "tank_golden_rectangle_even");
    require(population && population->residents.size() == 2 &&
            population->residents.front().species_id == "0087:00" &&
            population->residents.front().count == 2,
        "tank population did not survive serialization");
    require(construction::serializeAquariumDesignCanonical(*parsed.document) == first,
            "canonical round trip changed bytes");
}

void testNewerVersionIsReadOnly() {
    std::string text = construction::serializeAquariumDesignCanonical(documentFixture());
    const std::string needle = "\"schemaVersion\": 5";
    const std::size_t position = text.find(needle);
    require(position != std::string::npos, "schema version fixture missing");
    text.replace(position, needle.size(), "\"schemaVersion\": 8");
    const construction::AquariumDesignLoadResult result = construction::parseAquariumDesign(text);
    require(result.status == construction::AquariumDesignLoadStatus::NewerVersion,
            "newer design was not preserved as incompatible");
    require(!result.document.has_value(), "newer design exposed an editable document");
}

void testSchemaFourTunnelGridMigratesToWalkingCells() {
    std::string text = construction::serializeAquariumDesignCanonical(documentFixture());
    const std::string version_five = "\"schemaVersion\": 5";
    const std::size_t version = text.find(version_five);
    require(version != std::string::npos, "migration fixture schema version missing");
    text.replace(version, version_five.size(), "\"schemaVersion\": 4");
    const std::size_t last_column = text.find("\"column\": 26");
    const std::size_t object_begin = text.rfind('{', last_column);
    const std::size_t separator = text.rfind(',', object_begin);
    const std::size_t object_end = text.find('}', last_column);
    require(last_column != std::string::npos && object_begin != std::string::npos &&
            separator != std::string::npos && object_end != std::string::npos,
        "legacy tunnel endpoint fixture missing");
    text.erase(separator, object_end - separator + 1U);
    const auto parsed = construction::parseAquariumDesign(text);
    require(parsed.status == construction::AquariumDesignLoadStatus::Loaded &&
            parsed.document &&
            parsed.document->tanks.back().tunnels.front().centreline_cells.back().column == 26,
        "schema-4 positive-edge portal did not migrate onto the walking grid");
}

void testDuplicateIdsAreRejected() {
    construction::AquariumDesignDocument document = documentFixture();
    document.tanks.push_back(document.tanks.front());
    const std::vector<std::string> diagnostics = construction::validateAquariumDesign(document);
    require(diagnostics.size() == 1, "unexpected duplicate ID diagnostic count");
    require(diagnostics.front() == "duplicate_tank_id:tank_golden_rectangle_even",
            "duplicate ID diagnostic changed");
}

void testRevisionMustRemainExactlyRepresentable() {
    construction::AquariumDesignDocument document = documentFixture();
    document.revision = 9007199254740992ULL;
    const std::vector<std::string> diagnostics = construction::validateAquariumDesign(document);
    require(!diagnostics.empty() && diagnostics.front() == "revision_exceeds_json_integer_range",
            "unsafe JSON revision was accepted");
}

void testPopulationMustReferenceAnExistingTank() {
    construction::AquariumDesignDocument document = documentFixture();
    document.tank_populations.push_back({"tank_missing", {{"0090:00", 1}}});
    const auto diagnostics = construction::validateAquariumDesign(document);
    require(std::find(diagnostics.begin(), diagnostics.end(),
                "population_unknown_tank:tank_missing") != diagnostics.end(),
        "population for a missing tank was accepted");
}

void testUnknownExhibitPresetIsRejected() {
    construction::AquariumDesignDocument document = documentFixture();
    document.tanks.front().exhibit_preset = "lava";
    const auto diagnostics = construction::validateAquariumDesign(document);
    require(std::find(diagnostics.begin(), diagnostics.end(),
                "unsupported_exhibit_preset:tank_golden_rectangle_even") != diagnostics.end(),
        "unknown exhibit presentation was accepted");
}

void testInvalidExhibitStyleIsRejected() {
    construction::AquariumDesignDocument document = documentFixture();
    document.tanks.front().substrate_kind = "plastic-grass";
    document.tanks.front().brightness_level = 99;
    document.tanks.front().murkiness_level = -1;
    const auto diagnostics = construction::validateAquariumDesign(document);
    require(std::find(diagnostics.begin(), diagnostics.end(),
                "unsupported_substrate:tank_golden_rectangle_even") != diagnostics.end() &&
            std::find(diagnostics.begin(), diagnostics.end(),
                "invalid_brightness_level:tank_golden_rectangle_even") != diagnostics.end() &&
            std::find(diagnostics.begin(), diagnostics.end(),
                "invalid_murkiness_level:tank_golden_rectangle_even") != diagnostics.end(),
        "invalid per-tank exhibit style was accepted");
}

} // namespace

int main() {
    try {
        auto framed=documentFixture();
        const auto original_column=framed.tanks.front().footprint.origin_cell.column;
        construction::rebaseAquariumDesign(framed,-4,-3);
        require(framed.tanks.front().footprint.origin_cell.column==original_column+4,
            "moving west wall must preserve stable tank position through rebasing");
        const auto saved=construction::serializeAquariumDesignCanonical(framed);
        const auto restored=construction::parseAquariumDesign(saved);
        require(restored.document && restored.document->room_frame &&
            restored.document->room_frame->column==-4,"room frame must round-trip in schema 6");
        framed=*restored.document;
        construction::rebaseAquariumDesign(framed,0,0);
        require(framed.tanks.front().footprint.origin_cell.column==original_column,
            "room frame round-trip drifted a tank");
        testCanonicalRoundTrip();
        testNewerVersionIsReadOnly();
        testSchemaFourTunnelGridMigratesToWalkingCells();
        testDuplicateIdsAreRejected();
        testRevisionMustRemainExactlyRepresentable();
        testPopulationMustReferenceAnExistingTank();
        testUnknownExhibitPresetIsRejected();
        testInvalidExhibitStyleIsRejected();
        std::cout << "aquarium_design_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_design_tests: " << error.what() << '\n';
        return 1;
    }
}
