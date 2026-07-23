#include "gameplay/world3d/data/OwmapOverworldLoader.hpp"

#include "core/config/Json.hpp"
#include "gameplay/world3d/data/RtpksTilePackageLoader.hpp"
#include "gameplay/world3d/data/SceneMetadataParser.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <iostream>
#include <vector>

namespace pr::gameplay::world3d::data {

namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kOwmapMagic = 0x4F574D31u; // OWM1
constexpr std::uint16_t kOwmapVersion = 1u;
constexpr std::size_t kHeaderSize = 18u;

std::uint16_t readU16Le(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t readU32Le(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

float readF32Le(const std::uint8_t* p) {
    const std::uint32_t bits = readU32Le(p);
    float out = 0.0f;
    static_assert(sizeof(float) == sizeof(std::uint32_t), "float must be 32-bit");
    std::memcpy(&out, &bits, sizeof(float));
    return out;
}

std::vector<std::uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("Could not open OWMAP file: " + path);
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);
    if (size < 0) {
        throw std::runtime_error("Could not determine OWMAP file size: " + path);
    }
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        in.read(reinterpret_cast<char*>(bytes.data()), size);
    }
    return bytes;
}

void unpackByteGrid(
    const std::uint8_t* src,
    int width,
    int height,
    std::vector<std::vector<std::uint8_t>>& out) {
    out.assign(static_cast<std::size_t>(height), std::vector<std::uint8_t>(static_cast<std::size_t>(width), 0));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            out[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = src[i];
        }
    }
}

void unpackCollisionBits(
    const std::uint8_t* src,
    int width,
    int height,
    std::vector<std::vector<std::uint8_t>>& out) {
    out.assign(static_cast<std::size_t>(height), std::vector<std::uint8_t>(static_cast<std::size_t>(width), 0));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            const std::size_t byte_index = i >> 3U;
            const int bit_index = static_cast<int>(i & 7U);
            out[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>((src[byte_index] >> bit_index) & 0x01U);
        }
    }
}

bool hasTag(const RtpksTileMesh& tile, const char* tag) {
    return std::find(tile.tags.begin(), tile.tags.end(), tag) != tile.tags.end();
}

std::string tileSurface(const RtpksTileMesh& tile) {
    // Compatibility: imported coast meshes historically carried surface.water
    // as material metadata. terrain.beach identifies their actual walkable ramp.
    if (hasTag(tile, "terrain.beach")) return "beach";
    for (const std::string& tag : tile.tags) {
        constexpr const char* prefix = "surface.";
        if (tag.rfind(prefix, 0) == 0 && tag.size() > std::strlen(prefix)) {
            return tag.substr(std::strlen(prefix));
        }
    }
    if (hasTag(tile, "terrain.rock")) return "rock";
    if (hasTag(tile, "terrain.tall-grass") || hasTag(tile, "terrain.grass")) return "grass";
    if (hasTag(tile, "terrain.dry-grass")) return "dry_grass";
    if (hasTag(tile, "terrain.sand")) return "sand";
    if (hasTag(tile, "traversal.swim") || hasTag(tile, "traversal.surf")) return "water";
    return {};
}

float distanceToCellRect(float x, float y, int cell_x, int cell_y) {
    const float dx = std::max({static_cast<float>(cell_x) - x, 0.0f, x - static_cast<float>(cell_x + 1)});
    const float dy = std::max({static_cast<float>(cell_y) - y, 0.0f, y - static_cast<float>(cell_y + 1)});
    return std::sqrt((dx * dx) + (dy * dy));
}

void buildWaterTerrainSemantics(SceneConfig& scene) {
    const int width = std::max(0, scene.grid.width);
    const int height = std::max(0, scene.grid.height);
    scene.tile_surfaces.cells.assign(
        static_cast<std::size_t>(height),
        std::vector<TileSurfaceInfo>(static_cast<std::size_t>(width)));
    scene.water_terrain.actual_water_cells.assign(
        static_cast<std::size_t>(height),
        std::vector<std::uint8_t>(static_cast<std::size_t>(width), 0));
    scene.water_terrain.shoreline_cells.assign(
        static_cast<std::size_t>(height),
        std::vector<std::uint8_t>(static_cast<std::size_t>(width), 0));
    scene.water_terrain.shoreline_corner_progress.assign(
        static_cast<std::size_t>(height + 1),
        std::vector<float>(static_cast<std::size_t>(width + 1), 0.0f));
    if (scene.tile_package.path.empty() || scene.tile_layers.layers.empty()) {
        return;
    }

    std::string error;
    const RtpksTilePackage package = loadRtpksTileSemantics(scene.tile_package.path, &error);
    if (package.tiles.empty()) {
        std::cerr << "[OWMAP] Water semantics unavailable: "
                  << (error.empty() ? scene.tile_package.path : error) << '\n';
        return;
    }

    const auto markFootprint = [width, height](
        std::vector<std::vector<std::uint8_t>>& cells,
        int origin_x,
        int origin_y,
        const RtpksTileMesh& tile) {
        for (int local_y = 0; local_y < tile.height; ++local_y) {
            const int y = origin_y + local_y;
            if (y < 0 || y >= height) continue;
            for (int local_x = 0; local_x < tile.width; ++local_x) {
                const int x = origin_x + local_x;
                if (x < 0 || x >= width) continue;
                cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 1;
            }
        }
    };

    const auto markSurfaceFootprint = [&scene, width, height](
        int origin_x,
        int origin_y,
        const RtpksTileMesh& tile,
        const std::string& surface) {
        if (surface.empty()) return;
        for (int local_y = 0; local_y < tile.height; ++local_y) {
            const int y = origin_y + local_y;
            if (y < 0 || y >= height) continue;
            for (int local_x = 0; local_x < tile.width; ++local_x) {
                const int x = origin_x + local_x;
                if (x < 0 || x >= width) continue;
                TileSurfaceInfo& info = scene.tile_surfaces.cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                info.surface = surface;
                info.resort_tile_id = tile.resort_tile_id;
                info.tile_name = tile.name;
                info.tags = tile.tags;
            }
        }
    };

    for (const TileLayerConfig& layer : scene.tile_layers.layers) {
        if (!layer.visible) continue;
        for (int y = 0; y < static_cast<int>(layer.cells.size()); ++y) {
            const auto& row = layer.cells[static_cast<std::size_t>(y)];
            for (int x = 0; x < static_cast<int>(row.size()); ++x) {
                const RtpksTileMesh* tile = package.tileById(row[static_cast<std::size_t>(x)]);
                if (!tile) continue;
                markSurfaceFootprint(x, y, *tile, tileSurface(*tile));
                if (scene.water_terrain.enabled && (hasTag(*tile, "traversal.swim") || hasTag(*tile, "traversal.surf"))) {
                    markFootprint(scene.water_terrain.actual_water_cells, x, y, *tile);
                }
                if (scene.water_terrain.enabled && hasTag(*tile, "terrain.beach")) {
                    markFootprint(scene.water_terrain.shoreline_cells, x, y, *tile);
                }
            }
        }
    }

    if (!scene.water_terrain.enabled) return;

    // A coast tile can intentionally sit over the open-water body layer. Coast
    // always wins: it is the walkable ramp, never a swimming cell.
    std::vector<std::pair<int, int>> water_cells;
    std::vector<std::pair<int, int>> land_cells;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool shoreline = scene.water_terrain.shoreline_cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0;
            if (shoreline) {
                scene.water_terrain.actual_water_cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] = 0;
            } else if (scene.water_terrain.actual_water_cells[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] != 0) {
                water_cells.emplace_back(x, y);
            } else {
                land_cells.emplace_back(x, y);
            }
        }
    }

    if (water_cells.empty() || land_cells.empty()) return;
    for (int vy = 0; vy <= height; ++vy) {
        for (int vx = 0; vx <= width; ++vx) {
            float distance_water = std::numeric_limits<float>::max();
            float distance_land = std::numeric_limits<float>::max();
            for (const auto& [x, y] : water_cells) {
                distance_water = std::min(distance_water, distanceToCellRect(
                    static_cast<float>(vx), static_cast<float>(vy), x, y));
            }
            for (const auto& [x, y] : land_cells) {
                distance_land = std::min(distance_land, distanceToCellRect(
                    static_cast<float>(vx), static_cast<float>(vy), x, y));
            }
            const float sum = distance_land + distance_water;
            scene.water_terrain.shoreline_corner_progress[static_cast<std::size_t>(vy)][static_cast<std::size_t>(vx)] =
                sum <= 0.00001f ? 0.5f : std::clamp(distance_land / sum, 0.0f, 1.0f);
        }
    }
}

} // namespace

bool isOwmapFile(const std::string& path) {
    const fs::path p(path);
    if (p.extension() == ".owmap") {
        return true;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::uint8_t header[4]{};
    in.read(reinterpret_cast<char*>(header), 4);
    if (in.gcount() != 4) return false;
    return readU32Le(header) == kOwmapMagic;
}

SceneConfig loadOwmapScene(const std::string& project_root, const std::string& owmap_path) {
    const std::vector<std::uint8_t> file = readBinaryFile(owmap_path);
    if (file.size() < kHeaderSize) {
        throw std::runtime_error("OWMAP too small: " + owmap_path);
    }

    const std::uint32_t magic = readU32Le(file.data() + 0);
    const std::uint16_t version = readU16Le(file.data() + 4);
    const std::uint16_t width = readU16Le(file.data() + 6);
    const std::uint16_t height = readU16Le(file.data() + 8);
    const float tile_size = readF32Le(file.data() + 10);
    const std::uint32_t meta_len = readU32Le(file.data() + 14);

    if (magic != kOwmapMagic) {
        throw std::runtime_error("Bad OWMAP magic in: " + owmap_path);
    }
    if (version != kOwmapVersion) {
        throw std::runtime_error("Unsupported OWMAP version in: " + owmap_path);
    }
    if (width == 0 || height == 0 || width > 256 || height > 256) {
        throw std::runtime_error("Invalid OWMAP dimensions in: " + owmap_path);
    }

    const std::size_t cells = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t collision_bytes = (cells + 7U) / 8U;
    const std::size_t payload_start = kHeaderSize;
    const std::size_t heights_start = payload_start + static_cast<std::size_t>(meta_len);
    const std::size_t specials_start = heights_start + cells;
    const std::size_t collision_start = specials_start + cells;
    const std::size_t expected_size = collision_start + collision_bytes;
    if (expected_size > file.size()) {
        throw std::runtime_error("OWMAP appears truncated: " + owmap_path);
    }

    const std::string meta_json(
        reinterpret_cast<const char*>(file.data() + payload_start),
        static_cast<std::size_t>(meta_len));
    const JsonValue meta_root = parseJsonText(meta_json);
    SceneConfig scene = parseSceneMetadata(meta_root, project_root);

    scene.grid.width = static_cast<int>(width);
    scene.grid.height = static_cast<int>(height);
    scene.grid.tile_size = tile_size;

    unpackByteGrid(file.data() + heights_start, scene.grid.width, scene.grid.height, scene.terrain.heights);
    unpackByteGrid(file.data() + specials_start, scene.grid.width, scene.grid.height, scene.terrain.specials);
    unpackCollisionBits(file.data() + collision_start, scene.grid.width, scene.grid.height, scene.terrain.collision);
    buildWaterTerrainSemantics(scene);

    for (int y = 0; y < scene.grid.height; ++y) {
        for (int x = 0; x < scene.grid.width; ++x) {
            if (scene.terrain.specials[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == 1U) {
                std::ostringstream oss;
                oss << "unresolved editor-only special=1 found at tile " << x << "," << y
                    << ". Re-export this map from the map editor.";
#ifndef NDEBUG
                throw std::runtime_error(oss.str() + " Source: " + owmap_path);
#else
                std::cerr << "[OWMAP] ERROR: " << oss.str() << " Source: " << owmap_path << std::endl;
#endif
            }
        }
    }

    return scene;
}

} // namespace pr::gameplay::world3d::data
