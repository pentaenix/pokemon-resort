#include "mapmaker/project/MapProjectDocument.hpp"
#include "mapmaker/selection/SelectionModel.hpp"
#include "mapmaker/validation/ProjectValidator.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void expect(bool condition, const std::string& message) {
    if (!condition) throw TestFailure(message);
}

bool hasCode(
    const std::vector<pr::mapmaker::ValidationDiagnostic>& diagnostics,
    const std::string& code) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [&](const auto& diagnostic) {
        return diagnostic.code == code;
    });
}

void testSelectionModesAndPrimaryItem() {
    using namespace pr::mapmaker;
    SelectionModel selection;
    const SelectionItem grass{SelectionKind::TerrainCell, "outside", {}, 2, 3, 0};
    const SelectionItem door{SelectionKind::DoorTrigger, "outside", "front_door"};
    const SelectionItem tree{SelectionKind::Model, "outside", "tree"};

    expect(selection.select(grass), "replace selects first item");
    expect(selection.select(door, SelectionMode::Add), "add retains existing selection");
    expect(selection.items().size() == 2U && selection.primary() == door,
        "last added item is primary");
    expect(selection.select(grass, SelectionMode::Add), "reselect promotes existing item");
    expect(selection.items().size() == 2U && selection.primary() == grass,
        "promotion does not duplicate item");
    expect(selection.select(grass, SelectionMode::Toggle), "toggle removes selected item");
    expect(!selection.contains(grass) && selection.primary() == door, "remaining item becomes primary");
    expect(selection.select(tree, SelectionMode::Replace), "replace discards prior selection");
    expect(selection.items().size() == 1U && selection.primary() == tree, "replace keeps only target");
    expect(selection.clear() && selection.empty(), "clear empties selection");
}

pr::mapmaker::MapProjectDocument validProject() {
    return pr::mapmaker::MapProjectDocument::parse(R"json({
      "version":1,
      "id":"doors",
      "maps":[
        {"id":"outside","file":"outside.owmap","gridX":0,"gridY":0,"linked":true},
        {"id":"inside","file":"inside.owmap","gridX":0,"gridY":0,"linked":false},
        {"id":"inside_copy","file":"./inside.owmap","sourceMapId":"inside","gridX":4,"gridY":0,"linked":false}
      ],
      "tilePackages":[{"id":"tiles","file":"maptiles.rtpks"}],
      "defaultTilePackageId":"tiles",
      "future":{"preserved":true}
    })json");
}

std::vector<pr::mapmaker::MapValidationProjection> validMaps() {
    using namespace pr::mapmaker;
    MapValidationProjection outside;
    outside.source_file = "outside.owmap";
    outside.scene_id = "outside";
    outside.width = 16;
    outside.height = 16;
    outside.anchors.push_back({"outside_return", 8, 16, "north"});
    outside.links.push_back({"enter", "inside_copy", "inside_entry"});
    outside.doors.push_back({
        "front_door", 8, 15, {"north"}, "enter", "door_enter_default",
        DoorVisualProjection{"outside", "objects", 8, 15}});

    MapValidationProjection inside;
    inside.source_file = "inside.owmap";
    inside.scene_id = "inside";
    inside.width = 10;
    inside.height = 8;
    inside.anchors.push_back({"inside_entry", 5, 8, "north"});
    inside.links.push_back({"exit", "outside", "outside_return"});
    inside.doors.push_back({
        "inside_exit", 5, 8, {"south"}, "exit", "door_exit_default",
        DoorVisualProjection{"outside", "objects", 8, 15}});
    return {outside, inside};
}

void testValidDoorsLinksAnchorsAndReuseHaveNoErrors() {
    const auto diagnostics = pr::mapmaker::validateMapProject(validProject(), validMaps());
    if (pr::mapmaker::hasValidationErrors(diagnostics)) {
        std::string detail;
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.severity == pr::mapmaker::DiagnosticSeverity::Error) {
                detail += diagnostic.code + ": " + diagnostic.message + "; ";
            }
        }
        throw TestFailure("valid door project produced errors: " + detail);
    }
    expect(!hasCode(diagnostics, "map.source_not_loaded"),
        "one loaded shared source satisfies every reused instance");
}

void testInvalidProjectProducesActionableStableCodes() {
    using namespace pr::mapmaker;
    const auto project = MapProjectDocument::parse(R"json({
      "version":1,
      "id":"broken",
      "maps":[
        {"id":"outside","file":"../outside.txt","gridX":0,"gridY":0,"linked":true},
        {"id":"outside","file":"other.owmap","gridX":0,"gridY":0,"linked":true,"sourceMapId":"missing"}
      ],
      "defaultTilePackageId":"absent"
    })json");

    MapValidationProjection map;
    map.source_file = "../outside.txt";
    map.scene_id = "outside";
    map.width = 4;
    map.height = 4;
    map.anchors = {
        {"entry", 1, 9, "diagonal"},
        {"entry", 1, 1, "north"},
    };
    map.links = {{"bad_link", "nowhere", "none"}};
    map.doors = {{
        "bad_door", 8, 8, {"up"}, "missing_link", "", DoorVisualProjection{"nowhere", "", 0, 0}}};

    const auto diagnostics = validateMapProject(project, {map});
    expect(hasValidationErrors(diagnostics), "broken project reports errors");
    expect(hasCode(diagnostics, "map.id_duplicate"), "duplicate map id is diagnosed");
    expect(hasCode(diagnostics, "map.grid_overlap"), "linked grid collision is diagnosed");
    expect(hasCode(diagnostics, "map.file_escapes_root"), "escaping source path is diagnosed");
    expect(hasCode(diagnostics, "map.source_missing"), "missing reuse root is diagnosed");
    expect(hasCode(diagnostics, "tile_package.default_missing"), "missing default package is diagnosed");
    expect(hasCode(diagnostics, "anchor.id_duplicate"), "duplicate anchor is diagnosed");
    expect(hasCode(diagnostics, "anchor.tile_out_of_bounds"), "invalid anchor tile is diagnosed");
    expect(hasCode(diagnostics, "anchor.facing_invalid"), "invalid anchor facing is diagnosed");
    expect(hasCode(diagnostics, "link.destination_map_unknown"), "unknown destination map is diagnosed");
    expect(hasCode(diagnostics, "door.tile_out_of_bounds"), "invalid door tile is diagnosed");
    expect(hasCode(diagnostics, "door.link_unknown"), "unknown door link is diagnosed");
    expect(hasCode(diagnostics, "door.direction_invalid"), "invalid approach direction is diagnosed");
    expect(hasCode(diagnostics, "door.visual_map_unknown"), "unknown visual map is diagnosed");
}

void testOneCellCardinalHaloIsAcceptedButCornersAreNot() {
    using namespace pr::mapmaker;
    auto maps = validMaps();
    maps[1].anchors.push_back({"west_halo", -1, 3, "east"});
    maps[1].anchors.push_back({"bad_corner", -1, -1, "south"});
    const auto diagnostics = validateMapProject(validProject(), maps);
    const auto halo_error = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "anchor.tile_out_of_bounds" && item.object_id == "west_halo";
    });
    const auto corner_error = std::find_if(diagnostics.begin(), diagnostics.end(), [](const auto& item) {
        return item.code == "anchor.tile_out_of_bounds" && item.object_id == "bad_corner";
    });
    expect(halo_error == diagnostics.end(), "cardinal one-cell halo is valid for door travel");
    expect(corner_error != diagnostics.end(), "diagonal halo corner remains invalid");
}

void testSingleDoorIsAnAutomaticArrival() {
    using namespace pr::mapmaker;
    auto maps = validMaps();
    maps.front().links.front().destination_anchor_id.clear();
    maps.back().anchors.clear();
    const auto diagnostics = validateMapProject(validProject(), maps);
    expect(!hasCode(diagnostics, "link.destination_anchor_unknown"),
        "a destination with one door does not require a redundant anchor selection");

    maps.back().doors.push_back(maps.back().doors.front());
    const auto ambiguous = validateMapProject(validProject(), maps);
    expect(hasCode(ambiguous, "link.destination_anchor_unknown"),
        "multiple destination doors remain ambiguous without an explicit anchor");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, void (*)()>> tests{
        {"selection modes and primary item", testSelectionModesAndPrimaryItem},
        {"valid doors links anchors and reuse", testValidDoorsLinksAnchorsAndReuseHaveNoErrors},
        {"invalid project has actionable codes", testInvalidProjectProducesActionableStableCodes},
        {"cardinal halo accepted, corners rejected", testOneCellCardinalHaloIsAcceptedButCornersAreNot},
        {"single door automatic arrival", testSingleDoorIsAnAutomaticArrival},
    };
    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
