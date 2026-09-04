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

void testActorSpawnTileMetadata() {
    auto map = document();
    expect(pr::mapmaker::setSpawnTile(map, 2, 1, "npc_with_partner"),
        "actor spawn tile should accept a supported occupant kind");
    expect(map.specialAt(2, 1) == 14, "actor spawn tile should use terrain special 14");
    auto spawns = pr::mapmaker::projectSpawnTiles(map);
    expect(spawns.size() == 1 && spawns[0].tile_x == 2 && spawns[0].tile_y == 1 &&
        spawns[0].allows == "npc_with_partner", "actor spawn metadata should project losslessly");
    expect(pr::mapmaker::setSpawnTile(map, 2, 1, "npc_without_pokemon"),
        "actor spawn occupant kind should be editable");
    expect(pr::mapmaker::projectSpawnTiles(map)[0].allows == "npc_without_pokemon",
        "editing an actor spawn should not duplicate it");
    expect(!pr::mapmaker::setSpawnTile(map, 0, 0, "unsupported"),
        "unsupported actor spawn occupant kinds should be rejected");
    expect(pr::mapmaker::clearCell(map, 2, 1), "universal clear should remove actor spawn");
    expect(pr::mapmaker::projectSpawnTiles(map).empty() && map.specialAt(2, 1) == 0,
        "universal clear should remove actor spawn metadata and terrain marker");
}

void testSouthEntryAnchorsAreEasyAndNonDestructive() {
    auto map = pr::mapmaker::OwmapDocument::create(8, 6, 16.0f, pr::parseJsonText(R"({
        "id":"entry_room","anchors":[]
    })"));
    expect(pr::mapmaker::addSouthEntryAnchors(map),
        "south entry action should add a complete three-node entrance");
    auto anchors = pr::mapmaker::projectValidation(map, "entry_room.owmap").anchors;
    expect(anchors.size() == 3U &&
        anchors[0].id == "entry_left" && anchors[0].tile_x == 3 && anchors[0].tile_y == 5 &&
        anchors[1].id == "entry" && anchors[1].tile_x == 4 && anchors[1].tile_y == 5 &&
        anchors[2].id == "entry_right" && anchors[2].tile_x == 5 && anchors[2].tile_y == 5,
        "south entry nodes should be consecutive with entry in the middle");
    expect(pr::mapmaker::proposedDestinationAnchorId(anchors) == "entry",
        "door authoring should propose the middle entry anchor");

    expect(pr::mapmaker::moveAnchor(map, "entry", 2, 4) &&
        pr::mapmaker::setAnchorFacing(map, "entry", "east"),
        "the proposed center entry should remain fully adjustable");
    expect(!pr::mapmaker::setAnchorFacing(map, "entry", "diagonal"),
        "anchor facing should remain cardinal");
    expect(!pr::mapmaker::addSouthEntryAnchors(map),
        "adding the set again should not reset existing nodes");
    anchors = pr::mapmaker::projectValidation(map, "entry_room.owmap").anchors;
    const auto center = std::find_if(anchors.begin(), anchors.end(),
        [](const auto& anchor) { return anchor.id == "entry"; });
    expect(center != anchors.end() && center->tile_x == 2 && center->tile_y == 4 &&
        center->facing == "east",
        "re-adding missing entry nodes must preserve center position and facing adjustments");

    expect(pr::mapmaker::eraseAnchor(map, "entry_left") &&
        pr::mapmaker::addSouthEntryAnchors(map),
        "a removed entry node can be restored with one action");
    anchors = pr::mapmaker::projectValidation(map, "entry_room.owmap").anchors;
    expect(anchors.size() == 3U,
        "restoring the missing node should not duplicate the remaining entrance nodes");

    const std::vector<pr::mapmaker::AnchorProjection> legacy{{"legacy", 1, 1, "south"}};
    expect(pr::mapmaker::proposedDestinationAnchorId(legacy) == "legacy",
        "legacy maps with one anchor should still receive an automatic proposal");
}

void testInteriorProjectionAndResizePreserveAuthoringGrids() {
    auto map = pr::mapmaker::OwmapDocument::create(3, 2, 16.0f, pr::parseJsonText(R"({
        "id":"room","type":"interior",
        "grid":{"width":3,"height":2,"tileSize":16},
        "player":{"spawnTile":[2,1]},
        "interior":{"shellModelId":"","defaultRoom":{"enabled":true,
            "wallHeightTiles":4,"openingHeightTiles":3,"walkableInsetTiles":1,"wallFaceOffsetTiles":0.75,
            "entryExtensionDepthTiles":0.5,
            "lowerFacadeDepthTiles":8,
            "blackTopCap":true},
            "openings":[{"edge":"south","from":1,"to":2}]},
        "tileLayers":{"version":1,"layers":[{"id":"base","name":"Base","visible":true,
            "cells":[[null,null,null],[null,null,77]]}]},
        "pathLayer":{"version":1,"cells":[[0,0,0],[0,0,1]]},
        "spawnTiles":[{"id":"edge_spawn","tile":[2,1],"allows":"npc_without_pokemon"}]
    })"));
    auto room = pr::mapmaker::projectInteriorRoom(map);
    expect(room.default_room && room.wall_height_tiles == 4.0f &&
            room.opening_height_tiles == 3.0f &&
            room.walkable_inset_tiles == 1 && room.wall_face_offset_tiles == 0.75f &&
            room.entry_extension_depth_tiles == 0.5f &&
            room.lower_facade_depth_tiles == 8.0f &&
            room.black_top_cap &&
            room.openings.size() == 1 && room.openings[0].to == 2,
        "shell-less interior default room and openings should project");
    expect(pr::mapmaker::interiorBoundaryCellBlocked(room, 3, 2, 0, 0) &&
            !pr::mapmaker::interiorBoundaryCellBlocked(room, 3, 2, 1, 1),
        "editor projection should expose automatic wall collision and doorway gaps");

    auto halo_room = pr::mapmaker::OwmapDocument::create(4, 4, 16.0f,
        pr::parseJsonText(R"({
            "id":"halo_room","type":"interior",
            "interior":{"shellModelId":"","defaultRoom":{"enabled":true},"openings":[]},
            "doorTriggers":[{"id":"exit","tile":[2,4]}]
        })"));
    const auto halo_projection = pr::mapmaker::projectInteriorRoom(halo_room);
    expect(halo_projection.walkable_inset_tiles == 1 &&
            halo_projection.wall_face_offset_tiles == 0.5f &&
            halo_projection.entry_extension_depth_tiles == 0.0f &&
            halo_projection.openings.size() == 1U &&
            halo_projection.openings[0].edge == "south" &&
            halo_projection.openings[0].from == 2 &&
            !pr::mapmaker::interiorBoundaryCellBlocked(halo_projection, 4, 4, 2, 3),
        "editor should infer a visible and walkable opening from a cardinal-halo door");

    expect(pr::mapmaker::resizeMap(map, 4, 3), "map resize should report a change");
    auto layers = pr::mapmaker::projectTileLayers(map);
    expect(map.width() == 4 && map.height() == 3 && layers[0].cells[1][2] == 77 &&
        layers[0].cells[2][3] == -1,
        "growing should preserve north-west tiles and initialize new cells empty");
    expect(map.metadata().get("pathLayer")->get("cells")->asArray().size() == 3,
        "path grid should grow with the map");

    expect(pr::mapmaker::resizeMap(map, 2, 1), "shrinking should report a change");
    layers = pr::mapmaker::projectTileLayers(map);
    expect(layers[0].cells.size() == 1 && layers[0].cells[0].size() == 2,
        "shrinking should trim tile-layer rows and columns");
    const auto* spawn = map.metadata().get("player")->get("spawnTile");
    expect(spawn->asArray()[0].asNumber() == 1 && spawn->asArray()[1].asNumber() == 0,
        "player spawn should clamp into resized bounds");
    expect(pr::mapmaker::projectSpawnTiles(map).empty(),
        "spawn markers outside shrunken bounds should be removed");
    room = pr::mapmaker::projectInteriorRoom(map);
    expect(room.openings.size() == 1 && room.openings[0].from == 1 && room.openings[0].to == 1,
        "interior doorway ranges should clip to the resized edge");
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
        testActorSpawnTileMetadata();
        testSouthEntryAnchorsAreEasyAndNonDestructive();
        testInteriorProjectionAndResizePreserveAuthoringGrids();
        std::cout << "map_metadata_editing_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "map_metadata_editing_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
