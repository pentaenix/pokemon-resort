#include "mapmaker/app/EditorController.hpp"

#include "mapmaker/document/MapMetadataEditing.hpp"

#include <algorithm>

namespace pr::mapmaker {
namespace {

std::string configuredLayerId(const OwmapDocument& document, std::size_t layer_index) {
    const auto layers = projectTileLayers(document);
    return layer_index < layers.size() ? layers[layer_index].id : std::string{};
}

std::string directionTowardMap(const OwmapDocument& document, int x, int y) {
    if (x < 0) return "east";
    if (x >= document.width()) return "west";
    if (y < 0) return "south";
    if (y >= document.height()) return "north";
    return "north";
}

} // namespace

void EditorController::commitPaint() {
    if (paint_.cells.empty()) {
        paint_ = {};
        return;
    }
    if (active_tool_ == EditorTool::EraseLayer) {
        const auto cells = paint_.cells;
        const std::size_t layer = active_layer_index_;
        executeMutation("Erase layer cells", [cells, layer](OwmapDocument& document) {
            for (const auto& [x, y] : cells) {
                (void)setTileLayerCell(document, layer, x, y, std::nullopt);
            }
        });
        paint_ = {};
        return;
    }
    if (active_tool_ == EditorTool::ClearCell) {
        const auto cells = paint_.cells;
        executeMutation("Clear cells", [cells](OwmapDocument& document) {
            for (const auto& [x, y] : cells) (void)clearCell(document, x, y);
        });
        paint_ = {};
        return;
    }
    if (active_tool_ == EditorTool::Height) {
        const auto cells = paint_.cells;
        const auto value = static_cast<std::uint8_t>(std::clamp(height_brush_value_, 0, 255));
        executeMutation("Paint height", [cells, value](OwmapDocument& document) {
            for (const auto& [x, y] : cells) {
                if (x >= 0 && y >= 0 && x < document.width() && y < document.height()) {
                    document.heightAt(static_cast<std::uint16_t>(x),
                        static_cast<std::uint16_t>(y)) = value;
                }
            }
        });
        paint_ = {};
        return;
    }
    if (active_tool_ == EditorTool::Collision) {
        const auto cells = paint_.cells;
        const std::uint8_t value = collision_brush_value_ ? 1U : 0U;
        executeMutation(collision_brush_value_ ? "Paint collision" : "Erase collision",
            [cells, value](OwmapDocument& document) {
                for (const auto& [x, y] : cells) {
                    if (x >= 0 && y >= 0 && x < document.width() && y < document.height()) {
                        document.collisionAt(static_cast<std::uint16_t>(x),
                            static_cast<std::uint16_t>(y)) = value;
                    }
                }
            });
        paint_ = {};
        return;
    }
    const AssetView* asset = activeAsset(assets());
    if (!asset) {
        paint_ = {};
        return;
    }
    if (asset->kind == AssetKind::Tile || asset->kind == AssetKind::Door) {
        const auto cells = paint_.cells;
        const int tile_id = asset->tile_id;
        const std::size_t layer = active_layer_index_;
        if (asset->kind == AssetKind::Door) {
            const auto [x, y] = cells.front();
            executeMutation("Place door", [=](OwmapDocument& document) {
                const bool visible = tile_id >= 0;
                if (visible) (void)setTileLayerCell(document, layer, x, y, tile_id);
                const std::string id = uniqueMapObjectId(document, "door");
                const std::string link = id + "_link";
                (void)addDoorTrigger(document, id, x, y, directionTowardMap(document, x, y), link,
                    "door_enter_default", visible ? sceneId(document) : std::string{},
                    visible ? configuredLayerId(document, layer) : std::string{},
                    visible ? std::optional{std::pair{x, y}} : std::nullopt);
                (void)addOrUpdateLink(document, link, {}, {});
            });
            selection_.select(selectionAt(x, y));
        } else {
            executeMutation("Paint " + asset->name, [cells, tile_id, layer](OwmapDocument& document) {
                for (const auto& [x, y] : cells) {
                    (void)setTileLayerCell(document, layer, x, y, tile_id);
                }
            });
        }
    } else if (asset->kind == AssetKind::SmartSet) {
        const auto found = std::find_if(tile_catalog_->smartSets().begin(),
            tile_catalog_->smartSets().end(), [&](const SmartTileSet& smart) {
                return smart.id == asset->id;
            });
        if (found != tile_catalog_->smartSets().end()) {
            const int origin_x = paint_.cells.front().first;
            const int origin_y = paint_.cells.front().second;
            const auto columns = found->columns;
            const std::size_t layer = active_layer_index_;
            executeMutation("Stamp " + found->name, [=](OwmapDocument& document) {
                for (std::size_t x = 0; x < columns.size(); ++x) {
                    for (std::size_t y = 0; y < columns[x].size(); ++y) {
                        if (columns[x][y] < 0) continue;
                        (void)setTileLayerCell(document, layer,
                            origin_x + static_cast<int>(x), origin_y + static_cast<int>(y),
                            columns[x][y]);
                    }
                }
            });
        }
    } else if (paint_.cells.size() == 1U) {
        placeActiveAsset(paint_.cells.front().first, paint_.cells.front().second);
    }
    paint_ = {};
}

void EditorController::handleLayerEvents(const EditorUiEvents& events) {
    OpenMapSource* source = workspace_->activeSource();
    if (!source) return;
    if (events.add_layer) {
        const auto existing = projectTileLayers(source->document);
        std::size_t number = existing.size() + 1U;
        while (std::any_of(existing.begin(), existing.end(), [&](const auto& layer) {
            return layer.id == "layer_" + std::to_string(number);
        })) ++number;
        const std::string id = "layer_" + std::to_string(number);
        executeMutation("Add layer", [id, number](OwmapDocument& document) {
            (void)addTileLayer(document, id, "Layer " + std::to_string(number));
        });
        active_layer_index_ = projectTileLayers(source->document).size() - 1U;
    }
    if (events.rename_layer) {
        const auto [index, name] = *events.rename_layer;
        executeMutation("Rename layer", [index, name](OwmapDocument& document) {
            (void)renameTileLayer(document, index, name);
        });
    }
    if (events.set_layer_visible) {
        const auto [index, visible] = *events.set_layer_visible;
        executeMutation(visible ? "Show layer" : "Hide layer",
            [index, visible](OwmapDocument& document) {
                (void)setTileLayerVisible(document, index, visible);
            });
    }
    const std::size_t count = projectTileLayers(source->document).size();
    if (events.move_layer_up && active_layer_index_ + 1U < count) {
        const std::size_t from = active_layer_index_;
        executeMutation("Move layer up", [from](OwmapDocument& document) {
            (void)moveTileLayer(document, from, from + 1U);
        });
        ++active_layer_index_;
    }
    if (events.move_layer_down && active_layer_index_ > 0U) {
        const std::size_t from = active_layer_index_;
        executeMutation("Move layer down", [from](OwmapDocument& document) {
            (void)moveTileLayer(document, from, from - 1U);
        });
        --active_layer_index_;
    }
    if (events.delete_layer && count > 1U) {
        const std::size_t removing = active_layer_index_;
        executeMutation("Delete layer", [removing](OwmapDocument& document) {
            (void)eraseTileLayer(document, removing);
        });
        active_layer_index_ = std::min(active_layer_index_, count - 2U);
    }
}

void EditorController::handleTerrainInspectorEvents(const EditorUiEvents& events) {
    const auto primary = selection_.primary();
    if (!primary || primary->kind != SelectionKind::TerrainCell) return;
    if (!events.set_cell_height && !events.set_cell_special && !events.set_cell_collision &&
        !events.set_spawn_tile_use) return;
    const int x = primary->tile_x;
    const int y = primary->tile_y;
    const auto height = events.set_cell_height;
    const auto special = events.set_cell_special;
    const auto collision = events.set_cell_collision;
    const auto spawn_tile_use = events.set_spawn_tile_use;
    executeMutation("Edit terrain cell", [=](OwmapDocument& document) {
        if (x < 0 || y < 0 || x >= document.width() || y >= document.height()) return;
        const auto tx = static_cast<std::uint16_t>(x);
        const auto ty = static_cast<std::uint16_t>(y);
        if (height) document.heightAt(tx, ty) = static_cast<std::uint8_t>(std::clamp(*height, 0, 255));
        if (special) {
            if (*special == 14) {
                (void)setSpawnTile(document, x, y, "pokemon_random_from_boxes");
            } else {
                (void)eraseSpawnTile(document, x, y);
                document.specialAt(tx, ty) = static_cast<std::uint8_t>(std::clamp(*special, 0, 255));
            }
        }
        if (spawn_tile_use) (void)setSpawnTile(document, x, y, *spawn_tile_use);
        if (collision) document.collisionAt(tx, ty) = *collision ? 1U : 0U;
    });
}

} // namespace pr::mapmaker
