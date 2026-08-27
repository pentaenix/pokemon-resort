#pragma once

#include "mapmaker/document/OwmapDocument.hpp"
#include "mapmaker/validation/ProjectValidator.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace pr::mapmaker {

struct TileLayerProjection {
    std::string id;
    std::string name;
    bool visible = true;
    std::vector<std::vector<int>> cells;
};

struct ModelPlacementProjection {
    std::size_t metadata_index = 0;
    std::string id;
    std::string glb;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw_deg = 0.0f;
    float scale = 1.0f;
};

struct SpawnTileProjection {
    std::string id;
    int tile_x = 0;
    int tile_y = 0;
    std::string allows;
};

struct InteriorOpeningProjection {
    std::string edge;
    int from = 0;
    int to = 0;
};

struct InteriorRoomProjection {
    bool default_room = false;
    float wall_height_tiles = 4.0f;
    int walkable_inset_tiles = 1;
    float wall_face_offset_tiles = 0.5f;
    float entry_extension_depth_tiles = 0.0f;
    bool black_top_cap = true;
    std::vector<InteriorOpeningProjection> openings;
};

std::string sceneId(const OwmapDocument& document);
std::vector<TileLayerProjection> projectTileLayers(const OwmapDocument& document);
std::vector<ModelPlacementProjection> projectModels(const OwmapDocument& document);
std::vector<SpawnTileProjection> projectSpawnTiles(const OwmapDocument& document);
InteriorRoomProjection projectInteriorRoom(const OwmapDocument& document);
bool interiorBoundaryCellBlocked(
    const InteriorRoomProjection& room,
    int width,
    int height,
    int x,
    int y);
MapValidationProjection projectValidation(
    const OwmapDocument& document,
    std::string source_file);
// Returns a readable id that does not collide with any model, door, link,
// anchor, or tile layer in this document. The associated `<id>_link` name is
// reserved too so callers can safely create a door and link as one operation.
std::string uniqueMapObjectId(const OwmapDocument& document, const std::string& prefix);

// Resize from the south-east corner. Terrain and metadata grids retain their
// north-west overlap; player/spawn metadata and interior openings are kept valid.
bool resizeMap(OwmapDocument& document, std::uint16_t width, std::uint16_t height);

// Patch-local edits preserve every unrelated metadata member and null value.
bool setTileLayerCell(
    OwmapDocument& document,
    std::size_t layer_index,
    int x,
    int y,
    std::optional<int> resort_tile_id);
bool addTileLayer(
    OwmapDocument& document,
    const std::string& id,
    const std::string& name);
bool renameTileLayer(OwmapDocument& document, std::size_t layer_index, const std::string& name);
bool setTileLayerVisible(OwmapDocument& document, std::size_t layer_index, bool visible);
bool moveTileLayer(OwmapDocument& document, std::size_t from_index, std::size_t to_index);
bool eraseTileLayer(OwmapDocument& document, std::size_t layer_index);
bool moveModel(
    OwmapDocument& document,
    std::size_t metadata_index,
    float world_x,
    float world_y,
    float world_z);
bool moveDoorTrigger(OwmapDocument& document, const std::string& id, int x, int y);
// Coherent door edit for the interactive editor. Local visuals retain their
// trigger-relative offset and their exact layer cell moves with them. Pass the
// active project map id when it may differ from the embedded scene id.
bool moveDoorWithVisual(
    OwmapDocument& document,
    const std::string& id,
    int x,
    int y,
    const std::string& local_map_id = {});
bool setDoorAllowedDirection(
    OwmapDocument& document, const std::string& id, const std::string& direction);
bool setDoorScript(OwmapDocument& document, const std::string& id, const std::string& script_id);
bool moveAnchor(OwmapDocument& document, const std::string& id, int x, int y);
bool addAnchor(
    OwmapDocument& document,
    const std::string& id,
    int x,
    int y,
    const std::string& facing = "south");
// Adds any missing entry_left / entry / entry_right anchors along the south
// row. Existing anchors are preserved so authored adjustments are never reset.
bool addSouthEntryAnchors(OwmapDocument& document);
bool setAnchorFacing(
    OwmapDocument& document, const std::string& id, const std::string& facing);
bool eraseAnchor(OwmapDocument& document, const std::string& id);
bool eraseDoorTrigger(OwmapDocument& document, const std::string& id);
// Deletes the trigger and its same-document visual cell. Its link is deleted
// only when no remaining trigger references it; remote and null visuals remain
// untouched.
bool eraseDoorWithVisual(
    OwmapDocument& document,
    const std::string& id,
    const std::string& local_map_id = {});
bool eraseModel(OwmapDocument& document, std::size_t metadata_index);
bool setSpawnTile(OwmapDocument& document, int x, int y, const std::string& allows);
bool eraseSpawnTile(OwmapDocument& document, int x, int y);

bool addModel(
    OwmapDocument& document,
    const std::string& id,
    const std::string& glb_path,
    float world_x,
    float world_y,
    float world_z,
    float yaw_deg,
    float scale);
bool addDoorTrigger(
    OwmapDocument& document,
    const std::string& id,
    int x,
    int y,
    const std::string& allowed_direction,
    const std::string& link_id,
    const std::string& script_id,
    const std::string& visual_map_id = {},
    const std::string& visual_layer_id = {},
    std::optional<std::pair<int, int>> visual_tile = std::nullopt);
bool addOrUpdateLink(
    OwmapDocument& document,
    const std::string& id,
    const std::string& destination_map_id,
    const std::string& destination_anchor_id);

// The conventional center entry wins when present. A legacy single anchor is
// still proposed automatically; ambiguous sets require an explicit choice.
std::string proposedDestinationAnchorId(const std::vector<AnchorProjection>& anchors);

// Universal clear semantics used by the editor's Delete action: reset terrain
// channels, clear every tile layer, and remove cell-bound objects/triggers.
bool clearCell(OwmapDocument& document, int x, int y);

} // namespace pr::mapmaker
