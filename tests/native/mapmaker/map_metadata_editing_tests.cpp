#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

pr::mapmaker::OwmapDocument document() {
    return pr::mapmaker::OwmapDocument::create(3, 2, 16.0f, pr::parseJsonText(R"({
        "id":"test","unknown":{"keep":null},
        "tileLayers":{"version":1,"layers":[{"id":"base","name":"Base","visible":true,
            "cells":[[null,null,null],[null,null,null]]}]},
        "models":[{"id":"house","glb":"house.glb","position":[8,0,8],"yawDeg":90,"scale":1}],
        "anchors":[{"id":"entry","tile":[1,2],"facing":"north"}],
        "links":[{"id":"out","destinationMapId":"outside","destinationAnchorId":"return"}],
        "doorTriggers":[{"id":"door","tile":[1,2],"allowedDirections":["south"],
            "linkId":"out","scriptId":"door_exit_default","visual":null}]
    })"));
}

void testProjectionAndPathLocalPatches() {
    auto map = document();
    expect(pr::mapmaker::sceneId(map) == "test", "scene id should project");
    expect(pr::mapmaker::projectModels(map).front().yaw_deg == 90.0f, "model placement should project");
    expect(pr::mapmaker::projectTileLayers(map).front().cells[0][0] == -1, "null tile must project empty");
    expect(pr::mapmaker::setTileLayerCell(map, 0, 2, 1, 42), "tile patch should succeed");
    expect(pr::mapmaker::moveModel(map, 0, 40, 8, 24), "model move should succeed");
    expect(pr::mapmaker::moveDoorTrigger(map, "door", 2, 2), "halo door move should succeed");
    expect(map.metadata().get("unknown")->get("keep")->isNull(),
        "patch-local edits must preserve unknown nested nulls");
    const auto layers = pr::mapmaker::projectTileLayers(map);
    expect(layers[0].cells[1][2] == 42, "tile ID should persist at y,x");
    const auto models = pr::mapmaker::projectModels(map);
    expect(models[0].x == 40 && models[0].y == 8 && models[0].z == 24, "model world position should persist");
    const auto validation = pr::mapmaker::projectValidation(map, "test.owmap");
    expect(validation.doors[0].tile_x == 2 && validation.doors[0].tile_y == 2,
        "door projection should preserve cardinal halo coordinates");
    expect(!validation.doors[0].visual.has_value(), "explicit null visual must remain invisible");
}

void testUniversalErase() {
    auto map = document();
    expect(pr::mapmaker::eraseDoorTrigger(map, "door"), "door delete should work without layer focus");
    expect(pr::mapmaker::eraseModel(map, 0), "model delete should work without tool switching");
    expect(pr::mapmaker::projectValidation(map, "test.owmap").doors.empty(), "door must be gone");
    expect(pr::mapmaker::projectModels(map).empty(), "model must be gone");
}

void testCoherentDoorMovePreservesVisualOffsetAndLayerCell() {
    auto map = document();
    expect(pr::mapmaker::setTileLayerCell(map, 0, 0, 0, 3337),
        "visual tile fixture should be placeable");
    expect(pr::mapmaker::addDoorTrigger(map, "offset_door", 1, 1, "north",
        "offset_link", "door_enter_default", "test_instance", "base", std::pair{0, 0}),
        "door with project-instance visual should be placeable");
    expect(pr::mapmaker::moveDoorWithVisual(map, "offset_door", 2, 1, "test_instance"),
        "coherent door move should accept a valid destination");

    const auto projected = pr::mapmaker::projectValidation(map, "test.owmap");
    const auto door = std::find_if(projected.doors.begin(), projected.doors.end(),
        [](const auto& value) { return value.id == "offset_door"; });
    expect(door != projected.doors.end() && door->tile_x == 2 && door->tile_y == 1,
        "coherent move must update trigger tile");
    expect(door->visual && door->visual->tile_x == 1 && door->visual->tile_y == 0,
        "coherent move must retain the visual's trigger-relative offset");
    const auto layer = pr::mapmaker::projectTileLayers(map).front();
    expect(layer.cells[0][0] == -1 && layer.cells[0][1] == 3337,
        "coherent move must transfer the exact local visual layer cell");

    expect(pr::mapmaker::setTileLayerCell(map, 0, 2, 0, 99), "occupied target fixture should set");
    const auto before = map.serialize();
    expect(!pr::mapmaker::moveDoorWithVisual(map, "offset_door", 3, 1, "test_instance"),
        "coherent move must reject a visual destination outside the layer grid");
    expect(map.serialize() == before, "rejected coherent move must not partially mutate metadata");
}

void testCoherentDoorMoveKeepsRemoteAndInvisibleVisualsValid() {
    auto map = document();
    expect(pr::mapmaker::addDoorTrigger(map, "remote", 0, 0, "north", "remote_link",
        "door_enter_default", "other_map", "base", std::pair{2, 1}),
        "remote visual door should be placeable");
    expect(pr::mapmaker::moveDoorWithVisual(map, "remote", 1, 0, "test"),
        "remote visual must not prevent trigger movement");
    auto projected = pr::mapmaker::projectValidation(map, "test.owmap");
    auto remote = std::find_if(projected.doors.begin(), projected.doors.end(),
        [](const auto& value) { return value.id == "remote"; });
    expect(remote != projected.doors.end() && remote->tile_x == 1 && remote->visual &&
        remote->visual->tile_x == 2 && remote->visual->tile_y == 1,
        "remote visual address must remain fixed when its trigger moves");

    expect(pr::mapmaker::addDoorTrigger(map, "invisible", 1, 0, "south", "invisible_link",
        "door_exit_default"), "invisible trigger should be placeable");
    expect(pr::mapmaker::moveDoorWithVisual(map, "invisible", 1, 2),
        "invisible trigger should move into a valid cardinal halo");
    projected = pr::mapmaker::projectValidation(map, "test.owmap");
    const auto invisible = std::find_if(projected.doors.begin(), projected.doors.end(),
        [](const auto& value) { return value.id == "invisible"; });
    expect(invisible != projected.doors.end() && invisible->tile_y == 2 && !invisible->visual,
        "moving an invisible door must not materialize a visual object");
    expect(!pr::mapmaker::moveDoorWithVisual(map, "invisible", -1, -1),
        "corner halo is not a valid trigger destination");
}

void testCoherentDoorDeleteClearsVisualAndOnlyUnreferencedLink() {
    auto map = document();
    expect(pr::mapmaker::setTileLayerCell(map, 0, 0, 0, 3337), "first visual should set");
    expect(pr::mapmaker::setTileLayerCell(map, 0, 2, 0, 3338), "second visual should set");
    expect(pr::mapmaker::addDoorTrigger(map, "shared_a", 0, 0, "north", "shared_link",
        "door_enter_default", "test", "base", std::pair{0, 0}), "first shared door should add");
    expect(pr::mapmaker::addDoorTrigger(map, "shared_b", 2, 0, "north", "shared_link",
        "door_enter_default", "test", "base", std::pair{2, 0}), "second shared door should add");
    expect(pr::mapmaker::addOrUpdateLink(map, "shared_link", "inside", "entry"),
        "shared link should add");

    expect(pr::mapmaker::eraseDoorWithVisual(map, "shared_a", "test"),
        "coherent delete should remove first door");
    auto projected = pr::mapmaker::projectValidation(map, "test.owmap");
    expect(std::any_of(projected.links.begin(), projected.links.end(),
        [](const auto& link) { return link.id == "shared_link"; }),
        "link must survive while another door references it");
    auto layer = pr::mapmaker::projectTileLayers(map).front();
    expect(layer.cells[0][0] == -1 && layer.cells[0][2] == 3338,
        "delete should clear only its same-document visual cell");

    expect(pr::mapmaker::eraseDoorWithVisual(map, "shared_b", "test"),
        "coherent delete should remove second door");
    projected = pr::mapmaker::projectValidation(map, "test.owmap");
    expect(std::none_of(projected.links.begin(), projected.links.end(),
        [](const auto& link) { return link.id == "shared_link"; }),
        "link must be removed once no door references it");
    layer = pr::mapmaker::projectTileLayers(map).front();
    expect(layer.cells[0][2] == -1, "last local door delete should clear its visual tile");

    expect(pr::mapmaker::eraseDoorWithVisual(map, "door"),
        "existing invisible door must remain deletable");
}

void testDoorCreationAcceptsOnlyRuntimeCardinalHalo() {
    auto map = document();
    expect(pr::mapmaker::addDoorTrigger(map, "south_exit", 1, 2, "south", "return",
        "door_exit_default"), "one-tile south halo should be authorable");
    expect(!pr::mapmaker::addDoorTrigger(map, "corner", -1, -1, "north", "bad",
        "door_exit_default"), "corner halo must remain invalid");
    expect(!pr::mapmaker::addDoorTrigger(map, "too_far", 1, 3, "north", "bad",
        "door_exit_default"), "two tiles outside the map must remain invalid");
}

void testPlacementAndClearCell() {
    auto map = document();
    expect(pr::mapmaker::setTileLayerCell(map, 0, 0, 0, 3337),
        "door visual tile should be placeable");
    expect(pr::mapmaker::addDoorTrigger(map, "door_2", 0, 0, "north",
        "door_2_link", "door_enter_default"), "door metadata should be placeable");
    expect(pr::mapmaker::addOrUpdateLink(map, "door_2_link", "inside", "entry"),
        "door link should be placeable");
    expect(pr::mapmaker::setDoorAllowedDirection(map, "door_2", "east"),
        "door direction should be editable from the inspector");
    expect(pr::mapmaker::setDoorScript(map, "door_2", "door_exit_default"),
        "door script should be editable from the inspector");
    expect(pr::mapmaker::addModel(map, "tree", "tree.glb", 24, 0, 8, 0, 1),
        "model should be placeable");
    map.heightAt(0, 0) = 3;
    map.specialAt(0, 0) = 2;
    map.collisionAt(0, 0) = 1;
    const auto before_clear = pr::mapmaker::projectValidation(map, "test.owmap");
    const auto added_door = std::find_if(before_clear.doors.begin(), before_clear.doors.end(),
        [](const pr::mapmaker::DoorProjection& value) { return value.id == "door_2"; });
    expect(added_door != before_clear.doors.end() &&
        added_door->allowed_directions == std::vector<std::string>{"east"} &&
        added_door->script_id == "door_exit_default",
        "door inspector edits should project back from lossless metadata");

    expect(pr::mapmaker::clearCell(map, 0, 0),
        "universal clear should report all cell-bound content removed");
    expect(map.heightAt(0, 0) == 0 && map.specialAt(0, 0) == 0 &&
        map.collisionAt(0, 0) == 0, "universal clear must reset terrain channels");
    expect(pr::mapmaker::projectTileLayers(map)[0].cells[0][0] == -1,
        "universal clear must clear every tile layer at the cell");
    const auto validation = pr::mapmaker::projectValidation(map, "test.owmap");
    expect(validation.doors.size() == 1 && validation.doors[0].id == "door",
        "universal clear must remove only the door anchored to its cell");
    expect(std::none_of(validation.links.begin(), validation.links.end(),
        [](const pr::mapmaker::LinkProjection& value) { return value.id == "door_2_link"; }),
        "universal clear must remove a deleted door's now-unreferenced link");
    expect(pr::mapmaker::projectModels(map).size() == 1,
        "universal clear must remove only the model centered on its cell");
}

void testLayerLifecyclePreservesCellsAndActiveIdentity() {
    auto map = document();
    expect(pr::mapmaker::addTileLayer(map, "details", "Details"),
        "a second authoring layer should be addable");
    auto layers = pr::mapmaker::projectTileLayers(map);
    expect(layers.size() == 2 && layers[1].cells.size() == map.height() &&
        layers[1].cells[0].size() == map.width(),
        "new layer cells must match the OWMAP grid");
    expect(pr::mapmaker::setTileLayerCell(map, 1, 1, 0, 99),
        "new layer should be immediately paintable");
    expect(pr::mapmaker::renameTileLayer(map, 1, "Roof"), "layer should be renameable");
    expect(pr::mapmaker::setTileLayerVisible(map, 1, false), "layer visibility should be editable");
    expect(pr::mapmaker::moveTileLayer(map, 1, 0), "layer should be reorderable");
    layers = pr::mapmaker::projectTileLayers(map);
    expect(layers[0].id == "details" && layers[0].name == "Roof" && !layers[0].visible &&
        layers[0].cells[0][1] == 99,
        "reordering must preserve the complete layer and painted cells");
    const auto* tile_layers = map.metadata().get("tileLayers");
    expect(tile_layers && tile_layers->get("activeLayer") &&
        static_cast<int>(tile_layers->get("activeLayer")->asNumber()) == 0,
        "active layer identity should follow a reordered layer");
    expect(pr::mapmaker::eraseTileLayer(map, 0), "a non-final layer should be deletable");
    expect(!pr::mapmaker::eraseTileLayer(map, 0), "the final layer must not be deletable");
    expect(!pr::mapmaker::addTileLayer(map, "base", "Duplicate"),
        "duplicate layer IDs must be rejected");
}

void testReadableObjectIdsReserveDoorLinks() {
    auto map = document();
    expect(pr::mapmaker::uniqueMapObjectId(map, "new_house") == "new_house",
        "an unused readable object id should not gain a random suffix");
    expect(pr::mapmaker::addModel(map, "new_house", "house.glb", 0, 0, 0, 0, 1),
        "model fixture should be addable");
    expect(pr::mapmaker::uniqueMapObjectId(map, "new_house") == "new_house_2",
        "existing object ids should receive deterministic numeric suffixes");
    expect(pr::mapmaker::addOrUpdateLink(map, "gate_link", "outside", "entry"),
        "link fixture should be addable");
    expect(pr::mapmaker::uniqueMapObjectId(map, "gate") == "gate_2",
        "a generated door id must reserve its automatically generated link id");
}

} // namespace

int main() {
    try {
        testProjectionAndPathLocalPatches();
        testUniversalErase();
        testPlacementAndClearCell();
        testCoherentDoorMovePreservesVisualOffsetAndLayerCell();
        testCoherentDoorMoveKeepsRemoteAndInvisibleVisualsValid();
        testCoherentDoorDeleteClearsVisualAndOnlyUnreferencedLink();
        testDoorCreationAcceptsOnlyRuntimeCardinalHalo();
        testLayerLifecyclePreservesCellsAndActiveIdentity();
        testReadableObjectIdsReserveDoorLinks();
        std::cout << "map_metadata_editing_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "map_metadata_editing_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
