#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <unordered_set>
#include <cmath>

namespace pr::mapmaker {

std::string uniqueMapObjectId(const OwmapDocument& document, const std::string& prefix) {
    std::unordered_set<std::string> used;
    for (const TileLayerProjection& layer : projectTileLayers(document)) used.insert(layer.id);
    for (const ModelPlacementProjection& model : projectModels(document)) used.insert(model.id);
    const MapValidationProjection projected = projectValidation(document, {});
    for (const DoorProjection& door : projected.doors) used.insert(door.id);
    for (const LinkProjection& link : projected.links) used.insert(link.id);
    for (const AnchorProjection& anchor : projected.anchors) used.insert(anchor.id);

    const std::string base = prefix.empty() ? "object" : prefix;
    const auto available = [&](const std::string& id) {
        return !used.contains(id) && !used.contains(id + "_link");
    };
    if (available(base)) return base;
    for (std::size_t suffix = 2; ; ++suffix) {
        const std::string candidate = base + "_" + std::to_string(suffix);
        if (available(candidate)) return candidate;
    }
}
namespace {

std::string stringOr(const JsonValue* value, std::string fallback = {}) {
    return value && value->isString() ? value->asString() : std::move(fallback);
}

int intOr(const JsonValue* value, int fallback = 0) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

float floatOr(const JsonValue* value, float fallback = 0.0f) {
    return value && value->isNumber() ? static_cast<float>(value->asNumber()) : fallback;
}

bool boolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

std::pair<int, int> tilePair(const JsonValue* value) {
    if (!value || !value->isArray() || value->asArray().size() < 2U) return {};
    return {intOr(&value->asArray()[0]), intOr(&value->asArray()[1])};
}

JsonValue tileValue(int x, int y) {
    return JsonValue(JsonValue::Array{
        JsonValue(static_cast<double>(x)), JsonValue(static_cast<double>(y))});
}

std::vector<std::string> stringArray(const JsonValue* value) {
    std::vector<std::string> result;
    if (!value || !value->isArray()) return result;
    for (const JsonValue& item : value->asArray()) {
        if (item.isString()) result.push_back(item.asString());
    }
    return result;
}

JsonValue::Array* metadataArray(OwmapDocument& document, const char* key) {
    JsonValue& value = document.metadata()[key];
    if (value.isNull()) value.value() = JsonValue::Array{};
    return value.isArray() ? &value.asArray() : nullptr;
}

const JsonValue::Array* metadataArray(const OwmapDocument& document, const char* key) {
    const JsonValue* value = document.metadata().get(key);
    return value && value->isArray() ? &value->asArray() : nullptr;
}

bool moveNamedTile(OwmapDocument& document, const char* key, const std::string& id, int x, int y) {
    JsonValue::Array* values = metadataArray(document, key);
    if (!values) return false;
    for (JsonValue& value : *values) {
        if (value.isObject() && stringOr(value.get("id")) == id) {
            value["tile"] = tileValue(x, y);
            return true;
        }
    }
    return false;
}

JsonValue* namedObject(OwmapDocument& document, const char* key, const std::string& id) {
    JsonValue::Array* values = metadataArray(document, key);
    if (!values) return nullptr;
    for (JsonValue& value : *values) {
        if (value.isObject() && stringOr(value.get("id")) == id) return &value;
    }
    return nullptr;
}

} // namespace

std::string sceneId(const OwmapDocument& document) {
    return stringOr(document.metadata().get("id"));
}

std::vector<TileLayerProjection> projectTileLayers(const OwmapDocument& document) {
    std::vector<TileLayerProjection> result;
    const JsonValue* tile_layers = document.metadata().get("tileLayers");
    const JsonValue* layers = tile_layers && tile_layers->isObject() ? tile_layers->get("layers") : nullptr;
    if (!layers || !layers->isArray()) return result;
    for (const JsonValue& value : layers->asArray()) {
        if (!value.isObject()) continue;
        TileLayerProjection layer;
        layer.id = stringOr(value.get("id"));
        layer.name = stringOr(value.get("name"), layer.id);
        layer.visible = boolOr(value.get("visible"), true);
        if (const JsonValue* rows = value.get("cells"); rows && rows->isArray()) {
            for (const JsonValue& row_value : rows->asArray()) {
                std::vector<int> row;
                if (row_value.isArray()) {
                    row.reserve(row_value.asArray().size());
                    for (const JsonValue& cell : row_value.asArray()) {
                        row.push_back(cell.isNumber() ? static_cast<int>(cell.asNumber()) : -1);
                    }
                }
                layer.cells.push_back(std::move(row));
            }
        }
        result.push_back(std::move(layer));
    }
    return result;
}

std::vector<ModelPlacementProjection> projectModels(const OwmapDocument& document) {
    std::vector<ModelPlacementProjection> result;
    const JsonValue::Array* values = metadataArray(document, "models");
    if (!values) return result;
    for (std::size_t index = 0; index < values->size(); ++index) {
        const JsonValue& value = (*values)[index];
        if (!value.isObject()) continue;
        ModelPlacementProjection model;
        model.metadata_index = index;
        model.id = stringOr(value.get("id"));
        model.glb = stringOr(value.get("glb"));
        if (const JsonValue* position = value.get("position");
            position && position->isArray() && position->asArray().size() >= 3U) {
            model.x = floatOr(&position->asArray()[0]);
            model.y = floatOr(&position->asArray()[1]);
            model.z = floatOr(&position->asArray()[2]);
        }
        model.yaw_deg = floatOr(value.get("yawDeg"));
        model.scale = floatOr(value.get("scale"), 1.0f);
        result.push_back(std::move(model));
    }
    return result;
}

std::vector<SpawnTileProjection> projectSpawnTiles(const OwmapDocument& document) {
    std::vector<SpawnTileProjection> result;
    const JsonValue::Array* values = metadataArray(document, "spawnTiles");
    if (!values) return result;
    for (const JsonValue& value : *values) {
        if (!value.isObject()) continue;
        SpawnTileProjection spawn;
        spawn.id = stringOr(value.get("id"));
        std::tie(spawn.tile_x, spawn.tile_y) = tilePair(value.get("tile"));
        spawn.allows = stringOr(value.get("allows"));
        result.push_back(std::move(spawn));
    }
    return result;
}

MapValidationProjection projectValidation(
    const OwmapDocument& document,
    std::string source_file) {
    MapValidationProjection projection;
    projection.source_file = std::move(source_file);
    projection.scene_id = sceneId(document);
    projection.width = document.width();
    projection.height = document.height();
    const JsonValue* visual = document.metadata().get("visual");
    projection.has_runtime_visual = visual && visual->isObject();

    if (const JsonValue::Array* anchors = metadataArray(document, "anchors")) {
        for (const JsonValue& value : *anchors) {
            if (!value.isObject()) continue;
            const auto [x, y] = tilePair(value.get("tile"));
            projection.anchors.push_back({
                stringOr(value.get("id")), x, y, stringOr(value.get("facing"), "south")});
        }
    }
    if (const JsonValue::Array* links = metadataArray(document, "links")) {
        for (const JsonValue& value : *links) {
            if (!value.isObject()) continue;
            projection.links.push_back({
                stringOr(value.get("id")),
                stringOr(value.get("destinationMapId")),
                stringOr(value.get("destinationAnchorId"))});
        }
    }
    if (const JsonValue::Array* doors = metadataArray(document, "doorTriggers")) {
        for (const JsonValue& value : *doors) {
            if (!value.isObject()) continue;
            DoorProjection door;
            door.id = stringOr(value.get("id"));
            std::tie(door.tile_x, door.tile_y) = tilePair(value.get("tile"));
            door.allowed_directions = stringArray(value.get("allowedDirections"));
            door.link_id = stringOr(value.get("linkId"));
            door.script_id = stringOr(value.get("scriptId"));
            if (const JsonValue* visual = value.get("visual"); visual && visual->isObject()) {
                DoorVisualProjection projected;
                projected.map_id = stringOr(visual->get("mapId"));
                projected.layer_id = stringOr(visual->get("layerId"));
                std::tie(projected.tile_x, projected.tile_y) = tilePair(visual->get("tile"));
                door.visual = std::move(projected);
            }
            projection.doors.push_back(std::move(door));
        }
    }
    return projection;
}

bool setTileLayerCell(
    OwmapDocument& document,
    std::size_t layer_index,
    int x,
    int y,
    std::optional<int> resort_tile_id) {
    if (x < 0 || y < 0 || x >= document.width() || y >= document.height()) return false;
    JsonValue* tile_layers = document.metadata().get("tileLayers");
    JsonValue* layers = tile_layers && tile_layers->isObject() ? tile_layers->get("layers") : nullptr;
    if (!layers || !layers->isArray() || layer_index >= layers->asArray().size()) return false;
    JsonValue& layer = layers->asArray()[layer_index];
    JsonValue* rows = layer.isObject() ? layer.get("cells") : nullptr;
    if (!rows || !rows->isArray() || static_cast<std::size_t>(y) >= rows->asArray().size()) return false;
    JsonValue& row = rows->asArray()[static_cast<std::size_t>(y)];
    if (!row.isArray() || static_cast<std::size_t>(x) >= row.asArray().size()) return false;
    row.asArray()[static_cast<std::size_t>(x)] = resort_tile_id
        ? JsonValue(static_cast<double>(*resort_tile_id)) : JsonValue(nullptr);
    return true;
}

bool moveModel(
    OwmapDocument& document,
    std::size_t metadata_index,
    float world_x,
    float world_y,
    float world_z) {
    JsonValue::Array* models = metadataArray(document, "models");
    if (!models || metadata_index >= models->size()) return false;
    JsonValue& model = (*models)[metadata_index];
    if (!model.isObject()) return false;
    model["position"] = JsonValue(JsonValue::Array{
        JsonValue(static_cast<double>(world_x)), JsonValue(static_cast<double>(world_y)),
        JsonValue(static_cast<double>(world_z))});
    return true;
}

bool moveAnchor(OwmapDocument& document, const std::string& id, int x, int y) {
    return moveNamedTile(document, "anchors", id, x, y);
}

bool addAnchor(
    OwmapDocument& document,
    const std::string& id,
    int x,
    int y,
    const std::string& facing) {
    if (id.empty() || namedObject(document, "anchors", id)) return false;
    JsonValue::Array* anchors = metadataArray(document, "anchors");
    if (!anchors) return false;
    JsonValue::Object anchor;
    anchor.emplace("id", JsonValue(id));
    anchor.emplace("tile", tileValue(x, y));
    anchor.emplace("facing", JsonValue(facing.empty() ? std::string("south") : facing));
    anchors->emplace_back(std::move(anchor));
    return true;
}

bool addSouthEntryAnchors(OwmapDocument& document) {
    const int center_x = static_cast<int>(document.width()) / 2;
    const int south_y = std::max(0, static_cast<int>(document.height()) - 1);
    bool changed = false;
    changed = addAnchor(document, "entry_left", center_x - 1, south_y, "north") || changed;
    changed = addAnchor(document, "entry", center_x, south_y, "north") || changed;
    changed = addAnchor(document, "entry_right", center_x + 1, south_y, "north") || changed;
    return changed;
}

bool setAnchorFacing(
    OwmapDocument& document, const std::string& id, const std::string& facing) {
    if (facing != "north" && facing != "east" &&
        facing != "south" && facing != "west") {
        return false;
    }
    JsonValue* anchor = namedObject(document, "anchors", id);
    if (!anchor) return false;
    (*anchor)["facing"] = JsonValue(facing);
    return true;
}

bool eraseAnchor(OwmapDocument& document, const std::string& id) {
    JsonValue::Array* anchors = metadataArray(document, "anchors");
    if (!anchors) return false;
    const auto found = std::find_if(anchors->begin(), anchors->end(), [&](const JsonValue& value) {
        return value.isObject() && stringOr(value.get("id")) == id;
    });
    if (found == anchors->end()) return false;
    anchors->erase(found);
    return true;
}

bool eraseModel(OwmapDocument& document, std::size_t metadata_index) {
    JsonValue::Array* models = metadataArray(document, "models");
    if (!models || metadata_index >= models->size()) return false;
    models->erase(models->begin() + static_cast<std::ptrdiff_t>(metadata_index));
    return true;
}

bool setSpawnTile(OwmapDocument& document, int x, int y, const std::string& allows) {
    static const std::unordered_set<std::string> supported{
        "pokemon_random_from_boxes", "npc_with_partner", "npc_without_pokemon"};
    if (x < 0 || y < 0 || x >= document.width() || y >= document.height() ||
        !supported.contains(allows)) return false;
    JsonValue::Array* values = metadataArray(document, "spawnTiles");
    if (!values) return false;
    for (JsonValue& value : *values) {
        if (!value.isObject() || tilePair(value.get("tile")) != std::pair{x, y}) continue;
        value["allows"] = JsonValue(allows);
        document.specialAt(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)) = 14U;
        return true;
    }
    JsonValue::Object spawn;
    spawn.emplace("id", JsonValue("actor_spawn_" + std::to_string(x) + "_" + std::to_string(y)));
    spawn.emplace("tile", tileValue(x, y));
    spawn.emplace("allows", JsonValue(allows));
    values->emplace_back(std::move(spawn));
    document.specialAt(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y)) = 14U;
    return true;
}

bool eraseSpawnTile(OwmapDocument& document, int x, int y) {
    JsonValue::Array* values = metadataArray(document, "spawnTiles");
    if (!values) return false;
    const auto before = values->size();
    values->erase(std::remove_if(values->begin(), values->end(), [&](const JsonValue& value) {
        return value.isObject() && tilePair(value.get("tile")) == std::pair{x, y};
    }), values->end());
    return values->size() != before;
}

bool addModel(
    OwmapDocument& document,
    const std::string& id,
    const std::string& glb_path,
    float world_x,
    float world_y,
    float world_z,
    float yaw_deg,
    float scale) {
    if (id.empty() || glb_path.empty()) return false;
    JsonValue::Array* models = metadataArray(document, "models");
    if (!models) return false;
    JsonValue::Object model;
    model.emplace("id", JsonValue(id));
    model.emplace("glb", JsonValue(glb_path));
    model.emplace("position", JsonValue(JsonValue::Array{
        JsonValue(static_cast<double>(world_x)), JsonValue(static_cast<double>(world_y)),
        JsonValue(static_cast<double>(world_z))}));
    model.emplace("yawDeg", JsonValue(static_cast<double>(yaw_deg)));
    model.emplace("scale", JsonValue(static_cast<double>(scale)));
    models->emplace_back(std::move(model));
    return true;
}

bool addOrUpdateLink(
    OwmapDocument& document,
    const std::string& id,
    const std::string& destination_map_id,
    const std::string& destination_anchor_id) {
    if (id.empty()) return false;
    JsonValue::Array* links = metadataArray(document, "links");
    if (!links) return false;
    for (JsonValue& value : *links) {
        if (!value.isObject() || stringOr(value.get("id")) != id) continue;
        value["destinationMapId"] = JsonValue(destination_map_id);
        value["destinationAnchorId"] = JsonValue(destination_anchor_id);
        return true;
    }
    JsonValue::Object link;
    link.emplace("id", JsonValue(id));
    link.emplace("destinationMapId", JsonValue(destination_map_id));
    link.emplace("destinationAnchorId", JsonValue(destination_anchor_id));
    links->emplace_back(std::move(link));
    return true;
}

std::string proposedDestinationAnchorId(const std::vector<AnchorProjection>& anchors) {
    const auto center = std::find_if(anchors.begin(), anchors.end(),
        [](const AnchorProjection& anchor) { return anchor.id == "entry"; });
    if (center != anchors.end()) return center->id;
    return anchors.size() == 1U ? anchors.front().id : std::string{};
}

bool clearCell(OwmapDocument& document, int x, int y) {
    if (x < 0 || y < 0 || x >= document.width() || y >= document.height()) return false;
    bool changed = false;
    const auto index = document.cellIndex(
        static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
    changed = changed || document.heights()[index] != 0U;
    changed = changed || document.specials()[index] != 0U;
    changed = changed || document.collision()[index] != 0U;
    document.heights()[index] = 0U;
    document.specials()[index] = 0U;
    document.collision()[index] = 0U;
    changed = eraseSpawnTile(document, x, y) || changed;

    const auto layers = projectTileLayers(document);
    for (std::size_t layer = 0; layer < layers.size(); ++layer) {
        const auto& rows = layers[layer].cells;
        if (y >= static_cast<int>(rows.size()) || x >= static_cast<int>(rows[y].size())) continue;
        if (rows[y][x] >= 0) {
            changed = setTileLayerCell(document, layer, x, y, std::nullopt) || changed;
        }
    }

    std::vector<std::string> door_ids;
    if (const JsonValue::Array* doors = metadataArray(
            static_cast<const OwmapDocument&>(document), "doorTriggers")) {
        for (const JsonValue& value : *doors) {
            if (value.isObject() && tilePair(value.get("tile")) == std::pair{x, y}) {
                door_ids.push_back(stringOr(value.get("id")));
            }
        }
    }
    for (const std::string& door_id : door_ids) {
        changed = eraseDoorWithVisual(document, door_id) || changed;
    }
    if (JsonValue::Array* anchors = metadataArray(document, "anchors")) {
        const auto before = anchors->size();
        anchors->erase(std::remove_if(anchors->begin(), anchors->end(), [&](const JsonValue& value) {
            return value.isObject() && tilePair(value.get("tile")) == std::pair{x, y};
        }), anchors->end());
        changed = changed || anchors->size() != before;
    }
    if (JsonValue::Array* models = metadataArray(document, "models")) {
        const float tile_size = document.tileSize();
        const auto before = models->size();
        models->erase(std::remove_if(models->begin(), models->end(), [&](const JsonValue& value) {
            const JsonValue* position = value.isObject() ? value.get("position") : nullptr;
            if (!position || !position->isArray() || position->asArray().size() < 3U) return false;
            const int model_x = static_cast<int>(std::floor(
                floatOr(&position->asArray()[0]) / tile_size));
            const int model_y = static_cast<int>(std::floor(
                floatOr(&position->asArray()[2]) / tile_size));
            return model_x == x && model_y == y;
        }), models->end());
        changed = changed || models->size() != before;
    }
    return changed;
}

} // namespace pr::mapmaker
