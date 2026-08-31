#include "gameplay/world3d/aquarium/construction/AquariumDesign.hpp"

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
    tank.footprint.width_cells = 6;
    tank.footprint.depth_cells = 4;
    tank.height_steps = 8;
    tank.footprint.subtracted_cells = {{2, 0}};
    document.tanks.push_back(std::move(tank));
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
    return document;
}

void testCanonicalRoundTrip() {
    const construction::AquariumDesignDocument document = documentFixture();
    const std::string first = construction::serializeAquariumDesignCanonical(document);
    const construction::AquariumDesignLoadResult parsed = construction::parseAquariumDesign(first);
    require(parsed.status == construction::AquariumDesignLoadStatus::Loaded, "canonical design did not load");
    require(parsed.document.has_value(), "loaded design is missing document");
    require(construction::serializeAquariumDesignCanonical(*parsed.document) == first,
            "canonical round trip changed bytes");
}

void testNewerVersionIsReadOnly() {
    std::string text = construction::serializeAquariumDesignCanonical(documentFixture());
    const std::string needle = "\"schemaVersion\": 2";
    const std::size_t position = text.find(needle);
    require(position != std::string::npos, "schema version fixture missing");
    text.replace(position, needle.size(), "\"schemaVersion\": 3");
    const construction::AquariumDesignLoadResult result = construction::parseAquariumDesign(text);
    require(result.status == construction::AquariumDesignLoadStatus::NewerVersion,
            "newer design was not preserved as incompatible");
    require(!result.document.has_value(), "newer design exposed an editable document");
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

} // namespace

int main() {
    try {
        testCanonicalRoundTrip();
        testNewerVersionIsReadOnly();
        testDuplicateIdsAreRejected();
        testRevisionMustRemainExactlyRepresentable();
        std::cout << "aquarium_design_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "aquarium_design_tests: " << error.what() << '\n';
        return 1;
    }
}
