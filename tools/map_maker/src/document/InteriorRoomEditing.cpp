#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <utility>

namespace pr::mapmaker {
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

JsonValue::Array* metadataArray(OwmapDocument& document, const char* key) {
    JsonValue& value = document.metadata()[key];
    if (value.isNull()) value.value() = JsonValue::Array{};
    return value.isArray() ? &value.asArray() : nullptr;
}

const JsonValue::Array* metadataArray(const OwmapDocument& document, const char* key) {
    const JsonValue* value = document.metadata().get(key);
    return value && value->isArray() ? &value->asArray() : nullptr;
}

void resizeJsonGrid(
    JsonValue& value,
    std::uint16_t width,
    std::uint16_t height,
    JsonValue fill) {
    JsonValue::Array previous = value.isArray() ? value.asArray() : JsonValue::Array{};
    JsonValue::Array rows;
    rows.reserve(height);
    for (std::uint16_t y = 0; y < height; ++y) {
        JsonValue::Array row;
        row.reserve(width);
        for (std::uint16_t x = 0; x < width; ++x) {
            if (y < previous.size() && previous[y].isArray() &&
                x < previous[y].asArray().size()) {
                row.push_back(previous[y].asArray()[x]);
            } else {
                row.push_back(fill);
            }
        }
        rows.emplace_back(std::move(row));
    }
    value = JsonValue(std::move(rows));
}

bool insideMap(const JsonValue& value, std::uint16_t width, std::uint16_t height) {
    const auto [x, y] = tilePair(value.get("tile"));
    return x >= 0 && y >= 0 && x < width && y < height;
}

} // namespace

InteriorRoomProjection projectInteriorRoom(const OwmapDocument& document) {
    InteriorRoomProjection result;
    if (stringOr(document.metadata().get("type"), "exterior") != "interior") return result;
    const JsonValue* interior = document.metadata().get("interior");
    if (!interior || !interior->isObject()) {
        result.default_room = true;
        return result;
    }
    const bool shell_empty = stringOr(interior->get("shellModelId")).empty();
    const JsonValue* room = interior->get("defaultRoom");
    const bool enabled = !room || !room->isObject() || boolOr(room->get("enabled"), true);
    result.default_room = shell_empty && enabled;
    if (room && room->isObject()) {
        result.wall_height_tiles = std::clamp(
            floatOr(room->get("wallHeightTiles"), result.wall_height_tiles), 0.0f, 16.0f);
        result.walkable_inset_tiles = std::clamp(
            intOr(room->get("walkableInsetTiles"), result.walkable_inset_tiles), 0, 8);
        result.wall_face_offset_tiles = std::clamp(
            floatOr(room->get("wallFaceOffsetTiles"), result.wall_face_offset_tiles),
            0.0f, 4.0f);
        result.entry_extension_depth_tiles = std::clamp(
            floatOr(room->get("entryExtensionDepthTiles"),
                result.entry_extension_depth_tiles),
            0.0f, 8.0f);
        result.black_top_cap = boolOr(room->get("blackTopCap"), result.black_top_cap);
    }
    if (const JsonValue* openings = interior->get("openings");
        openings && openings->isArray()) {
        for (const JsonValue& opening : openings->asArray()) {
            if (!opening.isObject()) continue;
            InteriorOpeningProjection item;
            item.edge = stringOr(opening.get("edge"));
            item.from = intOr(opening.get("from"));
            item.to = intOr(opening.get("to"), item.from);
            if (item.to < item.from) std::swap(item.from, item.to);
            result.openings.push_back(std::move(item));
        }
    }

    const auto opening_exists = [&](const std::string& edge, int along) {
        return std::any_of(result.openings.begin(), result.openings.end(),
            [&](const InteriorOpeningProjection& opening) {
                return opening.edge == edge && along >= opening.from && along <= opening.to;
            });
    };
    if (const JsonValue::Array* doors = metadataArray(document, "doorTriggers")) {
        for (const JsonValue& value : *doors) {
            if (!value.isObject()) continue;
            const auto [x, y] = tilePair(value.get("tile"));
            std::string edge;
            int along = 0;
            const int width = static_cast<int>(document.width());
            const int height = static_cast<int>(document.height());
            if (y < 0 && x >= 0 && x < width) {
                edge = "north";
                along = x;
            } else if (y >= height && x >= 0 && x < width) {
                edge = "south";
                along = x;
            } else if (x < 0 && y >= 0 && y < height) {
                edge = "west";
                along = y;
            } else if (x >= width && y >= 0 && y < height) {
                edge = "east";
                along = y;
            }
            if (!edge.empty() && !opening_exists(edge, along)) {
                result.openings.push_back({std::move(edge), along, along});
            }
        }
    }
    return result;
}

bool interiorBoundaryCellBlocked(
    const InteriorRoomProjection& room,
    int width,
    int height,
    int x,
    int y) {
    if (!room.default_room || x < 0 || y < 0 || x >= width || y >= height) return false;
    const int inset = std::clamp(room.walkable_inset_tiles, 0, std::max(width, height));
    if (inset == 0) return false;
    const auto opening_covers = [&](const char* edge, int along) {
        return std::any_of(room.openings.begin(), room.openings.end(),
            [&](const InteriorOpeningProjection& opening) {
                return opening.edge == edge && along >= opening.from && along <= opening.to;
            });
    };
    if (y < inset && !opening_covers("north", x)) return true;
    if (y >= height - inset && !opening_covers("south", x)) return true;
    if (x < inset && !opening_covers("west", y)) return true;
    if (x >= width - inset && !opening_covers("east", y)) return true;
    return false;
}

bool resizeMap(OwmapDocument& document, std::uint16_t width, std::uint16_t height) {
    if (width == document.width() && height == document.height()) return false;
    document.resize(width, height);

    if (JsonValue* tile_layers = document.metadata().get("tileLayers");
        tile_layers && tile_layers->isObject()) {
        if (JsonValue* layers = tile_layers->get("layers"); layers && layers->isArray()) {
            for (JsonValue& layer : layers->asArray()) {
                if (!layer.isObject()) continue;
                resizeJsonGrid(layer["cells"], width, height, JsonValue(nullptr));
            }
        }
    }
    if (JsonValue* path_layer = document.metadata().get("pathLayer");
        path_layer && path_layer->isObject()) {
        resizeJsonGrid((*path_layer)["cells"], width, height, JsonValue(0.0));
    }
    if (JsonValue* player = document.metadata().get("player"); player && player->isObject()) {
        const auto [x, y] = tilePair(player->get("spawnTile"));
        (*player)["spawnTile"] = tileValue(
            std::clamp(x, 0, static_cast<int>(width) - 1),
            std::clamp(y, 0, static_cast<int>(height) - 1));
    }
    if (JsonValue::Array* spawns = metadataArray(document, "spawnTiles")) {
        spawns->erase(std::remove_if(spawns->begin(), spawns->end(), [&](const JsonValue& value) {
            return !value.isObject() || !insideMap(value, width, height);
        }), spawns->end());
    }
    if (JsonValue* interior = document.metadata().get("interior");
        interior && interior->isObject()) {
        if (JsonValue* openings = interior->get("openings"); openings && openings->isArray()) {
            openings->asArray().erase(std::remove_if(
                openings->asArray().begin(), openings->asArray().end(),
                [&](JsonValue& opening) {
                    if (!opening.isObject()) return true;
                    const std::string edge = stringOr(opening.get("edge"));
                    const int limit = (edge == "north" || edge == "south")
                        ? static_cast<int>(width) : static_cast<int>(height);
                    int from = intOr(opening.get("from"));
                    int to = intOr(opening.get("to"), from);
                    if (to < from) std::swap(from, to);
                    if (to < 0 || from >= limit) return true;
                    opening["from"] = JsonValue(static_cast<double>(std::max(0, from)));
                    opening["to"] = JsonValue(static_cast<double>(std::min(limit - 1, to)));
                    return false;
                }), openings->asArray().end());
        }
    }
    return true;
}

} // namespace pr::mapmaker
