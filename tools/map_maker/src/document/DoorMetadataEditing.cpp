#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>

namespace pr::mapmaker {
namespace {

std::string stringOr(const JsonValue* value) {
    return value && value->isString() ? value->asString() : std::string{};
}

int intOr(const JsonValue* value) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : 0;
}

std::optional<std::pair<int, int>> tilePair(const JsonValue* value) {
    if (!value || !value->isArray() || value->asArray().size() < 2U ||
        !value->asArray()[0].isNumber() || !value->asArray()[1].isNumber()) {
        return std::nullopt;
    }
    return std::pair{intOr(&value->asArray()[0]), intOr(&value->asArray()[1])};
}

JsonValue tileValue(int x, int y) {
    return JsonValue(JsonValue::Array{
        JsonValue(static_cast<double>(x)), JsonValue(static_cast<double>(y))});
}

JsonValue::Array* metadataArray(OwmapDocument& document, const char* key) {
    JsonValue& value = document.metadata()[key];
    if (value.isNull()) value.value() = JsonValue::Array{};
    return value.isArray() ? &value.asArray() : nullptr;
}

bool isInsideOrCardinalHalo(const OwmapDocument& document, int x, int y) {
    if (x >= 0 && x < document.width() && y >= 0 && y < document.height()) return true;
    return ((y == -1 || y == document.height()) && x >= 0 && x < document.width()) ||
        ((x == -1 || x == document.width()) && y >= 0 && y < document.height());
}

bool isLocalVisual(
    const OwmapDocument& document,
    const JsonValue& visual,
    const std::string& local_map_id) {
    const std::string map_id = stringOr(visual.get("mapId"));
    const std::string embedded_id = stringOr(document.metadata().get("id"));
    return map_id.empty() || (!embedded_id.empty() && map_id == embedded_id) ||
        (!local_map_id.empty() && map_id == local_map_id);
}

JsonValue* layerCell(
    OwmapDocument& document,
    const std::string& layer_id,
    int x,
    int y) {
    if (layer_id.empty() || x < 0 || y < 0 || x >= document.width() || y >= document.height()) {
        return nullptr;
    }
    JsonValue* tile_layers = document.metadata().get("tileLayers");
    JsonValue* layers = tile_layers && tile_layers->isObject() ? tile_layers->get("layers") : nullptr;
    if (!layers || !layers->isArray()) return nullptr;
    for (JsonValue& layer : layers->asArray()) {
        if (!layer.isObject() || stringOr(layer.get("id")) != layer_id) continue;
        JsonValue* rows = layer.get("cells");
        if (!rows || !rows->isArray() || static_cast<std::size_t>(y) >= rows->asArray().size()) {
            return nullptr;
        }
        JsonValue& row = rows->asArray()[static_cast<std::size_t>(y)];
        if (!row.isArray() || static_cast<std::size_t>(x) >= row.asArray().size()) return nullptr;
        return &row.asArray()[static_cast<std::size_t>(x)];
    }
    return nullptr;
}

bool sameLocalVisualCell(
    const OwmapDocument& document,
    const JsonValue& door,
    const std::string& local_map_id,
    const std::string& layer_id,
    int x,
    int y) {
    const JsonValue* visual = door.isObject() ? door.get("visual") : nullptr;
    if (!visual || !visual->isObject() || !isLocalVisual(document, *visual, local_map_id) ||
        stringOr(visual->get("layerId")) != layer_id) {
        return false;
    }
    const auto visual_tile = tilePair(visual->get("tile"));
    const auto trigger_tile = tilePair(door.get("tile"));
    return visual_tile.value_or(trigger_tile.value_or(std::pair{0, 0})) == std::pair{x, y};
}

} // namespace

bool moveDoorTrigger(OwmapDocument& document, const std::string& id, int x, int y) {
    return moveDoorWithVisual(document, id, x, y);
}

bool moveDoorWithVisual(
    OwmapDocument& document,
    const std::string& id,
    int x,
    int y,
    const std::string& local_map_id) {
    if (!isInsideOrCardinalHalo(document, x, y)) return false;
    JsonValue::Array* doors = metadataArray(document, "doorTriggers");
    if (!doors) return false;
    const auto found = std::find_if(doors->begin(), doors->end(), [&](const JsonValue& value) {
        return value.isObject() && stringOr(value.get("id")) == id;
    });
    if (found == doors->end()) return false;
    const auto old_trigger = tilePair(found->get("tile"));
    if (!old_trigger) return false;

    JsonValue* visual = found->get("visual");
    if (visual && visual->isObject() && isLocalVisual(document, *visual, local_map_id)) {
        const auto old_visual = tilePair(visual->get("tile")).value_or(*old_trigger);
        const std::pair next_visual{
            old_visual.first + x - old_trigger->first,
            old_visual.second + y - old_trigger->second};
        const std::string layer_id = stringOr(visual->get("layerId"));
        if (!layer_id.empty() && next_visual != old_visual) {
            JsonValue* source = layerCell(document, layer_id, old_visual.first, old_visual.second);
            JsonValue* destination = layerCell(document, layer_id, next_visual.first, next_visual.second);
            if (!source || !destination || !destination->isNull()) return false;
            *destination = *source;
            *source = JsonValue(nullptr);
        }
        (*visual)["tile"] = tileValue(next_visual.first, next_visual.second);
    }
    (*found)["tile"] = tileValue(x, y);
    return true;
}

bool setDoorAllowedDirection(
    OwmapDocument& document, const std::string& id, const std::string& direction) {
    if (direction.empty()) return false;
    JsonValue::Array* doors = metadataArray(document, "doorTriggers");
    if (!doors) return false;
    for (JsonValue& value : *doors) {
        if (!value.isObject() || stringOr(value.get("id")) != id) continue;
        value["allowedDirections"] = JsonValue(JsonValue::Array{JsonValue(direction)});
        return true;
    }
    return false;
}

bool setDoorScript(
    OwmapDocument& document, const std::string& id, const std::string& script_id) {
    if (script_id.empty()) return false;
    JsonValue::Array* doors = metadataArray(document, "doorTriggers");
    if (!doors) return false;
    for (JsonValue& value : *doors) {
        if (!value.isObject() || stringOr(value.get("id")) != id) continue;
        value["scriptId"] = JsonValue(script_id);
        return true;
    }
    return false;
}

bool eraseDoorTrigger(OwmapDocument& document, const std::string& id) {
    return eraseDoorWithVisual(document, id);
}

bool eraseDoorWithVisual(
    OwmapDocument& document,
    const std::string& id,
    const std::string& local_map_id) {
    JsonValue::Array* doors = metadataArray(document, "doorTriggers");
    if (!doors) return false;
    const auto found = std::find_if(doors->begin(), doors->end(), [&](const JsonValue& value) {
        return value.isObject() && stringOr(value.get("id")) == id;
    });
    if (found == doors->end()) return false;

    const std::string link_id = stringOr(found->get("linkId"));
    const auto trigger_tile = tilePair(found->get("tile"));
    const JsonValue* visual = found->get("visual");
    std::string visual_layer;
    std::optional<std::pair<int, int>> visual_tile;
    if (visual && visual->isObject() && isLocalVisual(document, *visual, local_map_id)) {
        visual_layer = stringOr(visual->get("layerId"));
        visual_tile = tilePair(visual->get("tile"));
        if (!visual_tile) visual_tile = trigger_tile;
    }
    doors->erase(found);

    if (visual_tile && !visual_layer.empty()) {
        const bool still_referenced = std::any_of(doors->begin(), doors->end(), [&](const JsonValue& door) {
            return sameLocalVisualCell(document, door, local_map_id, visual_layer,
                visual_tile->first, visual_tile->second);
        });
        if (!still_referenced) {
            if (JsonValue* cell = layerCell(
                    document, visual_layer, visual_tile->first, visual_tile->second)) {
                *cell = JsonValue(nullptr);
            }
        }
    }

    const bool link_still_referenced = !link_id.empty() &&
        std::any_of(doors->begin(), doors->end(), [&](const JsonValue& door) {
            return door.isObject() && stringOr(door.get("linkId")) == link_id;
        });
    if (!link_id.empty() && !link_still_referenced) {
        if (JsonValue::Array* links = metadataArray(document, "links")) {
            links->erase(std::remove_if(links->begin(), links->end(), [&](const JsonValue& link) {
                return link.isObject() && stringOr(link.get("id")) == link_id;
            }), links->end());
        }
    }
    return true;
}

bool addDoorTrigger(
    OwmapDocument& document,
    const std::string& id,
    int x,
    int y,
    const std::string& allowed_direction,
    const std::string& link_id,
    const std::string& script_id,
    const std::string& visual_map_id,
    const std::string& visual_layer_id,
    std::optional<std::pair<int, int>> visual_tile) {
    if (id.empty() || !isInsideOrCardinalHalo(document, x, y)) return false;
    JsonValue::Array* doors = metadataArray(document, "doorTriggers");
    if (!doors) return false;
    JsonValue::Object door;
    door.emplace("id", JsonValue(id));
    door.emplace("kind", JsonValue(std::string("door")));
    door.emplace("tile", tileValue(x, y));
    door.emplace("activation", JsonValue(std::string("move_toward")));
    door.emplace("allowedDirections", JsonValue(JsonValue::Array{
        JsonValue(allowed_direction.empty() ? std::string("north") : allowed_direction)}));
    door.emplace("linkId", JsonValue(link_id));
    door.emplace("scriptId", JsonValue(script_id));
    if (visual_tile) {
        JsonValue::Object visual;
        visual.emplace("mapId", JsonValue(visual_map_id));
        visual.emplace("layerId", JsonValue(visual_layer_id));
        visual.emplace("tile", tileValue(visual_tile->first, visual_tile->second));
        door.emplace("visual", JsonValue(std::move(visual)));
    } else {
        door.emplace("visual", JsonValue(nullptr));
    }
    doors->emplace_back(std::move(door));
    return true;
}

} // namespace pr::mapmaker
