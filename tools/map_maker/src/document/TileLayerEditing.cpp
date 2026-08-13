#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>
#include <utility>

namespace pr::mapmaker {
namespace {

JsonValue::Array* layers(OwmapDocument& document) {
    JsonValue* tile_layers = document.metadata().get("tileLayers");
    if (!tile_layers || !tile_layers->isObject()) return nullptr;
    JsonValue* value = tile_layers->get("layers");
    return value && value->isArray() ? &value->asArray() : nullptr;
}

bool hasLayerId(const JsonValue::Array& values, const std::string& id) {
    return std::any_of(values.begin(), values.end(), [&](const JsonValue& value) {
        const JsonValue* current = value.isObject() ? value.get("id") : nullptr;
        return current && current->isString() && current->asString() == id;
    });
}

JsonValue emptyCells(std::uint16_t width, std::uint16_t height) {
    JsonValue::Array rows;
    rows.reserve(height);
    for (std::uint16_t y = 0; y < height; ++y) {
        JsonValue::Array row;
        row.resize(width, JsonValue(nullptr));
        rows.emplace_back(std::move(row));
    }
    return JsonValue(std::move(rows));
}

void setActiveLayer(OwmapDocument& document, std::size_t index) {
    JsonValue* tile_layers = document.metadata().get("tileLayers");
    if (tile_layers && tile_layers->isObject()) {
        (*tile_layers)["activeLayer"] = JsonValue(static_cast<double>(index));
    }
}

std::size_t activeLayer(const OwmapDocument& document) {
    const JsonValue* tile_layers = document.metadata().get("tileLayers");
    const JsonValue* value = tile_layers && tile_layers->isObject()
        ? tile_layers->get("activeLayer") : nullptr;
    return value && value->isNumber()
        ? static_cast<std::size_t>(std::max(0.0, value->asNumber())) : 0U;
}

} // namespace

bool addTileLayer(
    OwmapDocument& document,
    const std::string& id,
    const std::string& name) {
    JsonValue::Array* values = layers(document);
    if (!values || id.empty() || hasLayerId(*values, id)) return false;
    JsonValue::Object layer;
    layer.emplace("id", JsonValue(id));
    layer.emplace("name", JsonValue(name.empty() ? id : name));
    layer.emplace("visible", JsonValue(true));
    layer.emplace("cells", emptyCells(document.width(), document.height()));
    values->emplace_back(std::move(layer));
    setActiveLayer(document, values->size() - 1U);
    return true;
}

bool renameTileLayer(
    OwmapDocument& document,
    std::size_t layer_index,
    const std::string& name) {
    JsonValue::Array* values = layers(document);
    if (!values || layer_index >= values->size() || name.empty()) return false;
    JsonValue& layer = (*values)[layer_index];
    if (!layer.isObject()) return false;
    layer["name"] = JsonValue(name);
    return true;
}

bool setTileLayerVisible(
    OwmapDocument& document,
    std::size_t layer_index,
    bool visible) {
    JsonValue::Array* values = layers(document);
    if (!values || layer_index >= values->size()) return false;
    JsonValue& layer = (*values)[layer_index];
    if (!layer.isObject()) return false;
    layer["visible"] = JsonValue(visible);
    return true;
}

bool moveTileLayer(
    OwmapDocument& document,
    std::size_t from_index,
    std::size_t to_index) {
    JsonValue::Array* values = layers(document);
    if (!values || from_index >= values->size() || to_index >= values->size()) return false;
    if (from_index == to_index) return true;
    const std::size_t selected = activeLayer(document);
    JsonValue moving = std::move((*values)[from_index]);
    values->erase(values->begin() + static_cast<std::ptrdiff_t>(from_index));
    values->insert(values->begin() + static_cast<std::ptrdiff_t>(to_index), std::move(moving));
    std::size_t adjusted = selected;
    if (selected == from_index) adjusted = to_index;
    else if (from_index < selected && selected <= to_index) --adjusted;
    else if (to_index <= selected && selected < from_index) ++adjusted;
    setActiveLayer(document, adjusted);
    return true;
}

bool eraseTileLayer(OwmapDocument& document, std::size_t layer_index) {
    JsonValue::Array* values = layers(document);
    if (!values || values->size() <= 1U || layer_index >= values->size()) return false;
    const std::size_t selected = activeLayer(document);
    values->erase(values->begin() + static_cast<std::ptrdiff_t>(layer_index));
    std::size_t adjusted = selected;
    if (selected > layer_index) --adjusted;
    else if (selected == layer_index) adjusted = std::min(layer_index, values->size() - 1U);
    setActiveLayer(document, adjusted);
    return true;
}

} // namespace pr::mapmaker
