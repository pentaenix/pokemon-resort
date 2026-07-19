#include "gameplay/world3d/data/RtpksTilePackageLoader.hpp"

#include "core/config/Json.hpp"

#include <miniz.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace pr::gameplay::world3d::data {

namespace {

void fail(std::string* error, const std::string& message) {
    if (error) *error = message;
}

std::string strOr(const JsonValue* value, const std::string& fallback) {
    return value && value->isString() ? value->asString() : fallback;
}

int intOr(const JsonValue* value, int fallback) {
    return value && value->isNumber() ? static_cast<int>(value->asNumber()) : fallback;
}

float floatOr(const JsonValue* value, float fallback) {
    return value && value->isNumber() ? static_cast<float>(value->asNumber()) : fallback;
}

std::vector<float> floatArray(const JsonValue* value) {
    std::vector<float> out;
    if (!value || !value->isArray()) return out;
    const auto& arr = value->asArray();
    out.reserve(arr.size());
    for (const JsonValue& item : arr) {
        out.push_back(item.isNumber() ? static_cast<float>(item.asNumber()) : 0.0f);
    }
    return out;
}

std::vector<std::array<float, 2>> vec2Array(const JsonValue* value) {
    std::vector<std::array<float, 2>> out;
    if (!value || !value->isArray()) return out;
    for (const JsonValue& item : value->asArray()) {
        if (!item.isArray() || item.asArray().size() < 2U) continue;
        const auto& values = item.asArray();
        out.push_back({
            values[0].isNumber() ? static_cast<float>(values[0].asNumber()) : 0.0f,
            values[1].isNumber() ? static_cast<float>(values[1].asNumber()) : 0.0f,
        });
    }
    return out;
}

std::vector<std::string> stringArray(const JsonValue* value) {
    std::vector<std::string> out;
    if (!value || !value->isArray()) return out;
    for (const JsonValue& item : value->asArray()) {
        if (item.isString() && !item.asString().empty()) out.push_back(item.asString());
    }
    return out;
}

bool boolOr(const JsonValue* value, bool fallback) {
    return value && value->isBool() ? value->asBool() : fallback;
}

std::vector<std::uint8_t> extractZipEntry(mz_zip_archive& zip, const std::string& path) {
    std::size_t size = 0;
    void* data = mz_zip_reader_extract_file_to_heap(&zip, path.c_str(), &size, 0);
    if (!data) return {};
    std::vector<std::uint8_t> out(static_cast<std::uint8_t*>(data), static_cast<std::uint8_t*>(data) + size);
    mz_free(data);
    return out;
}

JsonValue extractJson(mz_zip_archive& zip, const std::string& path) {
    const std::vector<std::uint8_t> bytes = extractZipEntry(zip, path);
    if (bytes.empty()) {
        throw std::runtime_error("RTPKS missing " + path);
    }
    return parseJsonText(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

RtpksTileMesh parseTileMesh(int resort_tile_id, const JsonValue& root, const JsonValue* definition) {
    RtpksTileMesh out;
    out.resort_tile_id = resort_tile_id;
    out.width = std::max(1, intOr(root.get("width"), 1));
    out.height = std::max(1, intOr(root.get("height"), 1));
    out.x_offset = floatOr(root.get("xOffset"), 0.0f);
    out.y_offset = floatOr(root.get("yOffset"), 0.0f);
    out.triangles = floatArray(root.get("triangles"));
    out.quads = floatArray(root.get("quads"));
    out.tex_coords_tri = floatArray(root.get("texCoordsTri"));
    out.tex_coords_quad = floatArray(root.get("texCoordsQuad"));
    out.colors_tri = floatArray(root.get("colorsTri"));
    out.colors_quad = floatArray(root.get("colorsQuad"));
    if (definition && definition->isObject()) {
        out.name = strOr(definition->get("name"), "");
        out.tags = stringArray(definition->get("tags"));
        if (const JsonValue* collision = definition->get("collision"); collision && collision->isObject()) {
            out.collision_mode = strOr(collision->get("mode"), "none");
            out.collision_auto_apply = boolOr(collision->get("autoApply"), false);
            out.collision_clear_on_erase = boolOr(collision->get("clearOnErase"), false);
            if (const JsonValue* rows = collision->get("mask"); rows && rows->isArray()) {
                for (const JsonValue& row_value : rows->asArray()) {
                    std::vector<bool> row;
                    if (row_value.isArray()) {
                        for (const JsonValue& cell : row_value.asArray()) row.push_back(cell.isBool() && cell.asBool());
                    }
                    out.collision_mask.push_back(std::move(row));
                }
            }
        }
    }

    if (const JsonValue* ranges = root.get("materialRanges"); ranges && ranges->isArray()) {
        for (const JsonValue& range_value : ranges->asArray()) {
            if (!range_value.isObject()) continue;
            RtpksMaterialRange range;
            range.material_id = intOr(range_value.get("materialId"), -1);
            range.tri_start = std::max(0, intOr(range_value.get("triStart"), 0));
            range.tri_count = std::max(0, intOr(range_value.get("triCount"), 0));
            range.quad_start = std::max(0, intOr(range_value.get("quadStart"), 0));
            range.quad_count = std::max(0, intOr(range_value.get("quadCount"), 0));
            if (range.material_id >= 0 && (range.tri_count > 0 || range.quad_count > 0)) {
                out.material_ranges.push_back(range);
            }
        }
    }
    if (out.material_ranges.empty()) {
        RtpksMaterialRange range;
        range.material_id = 0;
        range.tri_count = static_cast<int>(out.triangles.size() / 9U);
        range.quad_count = static_cast<int>(out.quads.size() / 12U);
        if (range.tri_count > 0 || range.quad_count > 0) out.material_ranges.push_back(range);
    }
    return out;
}

} // namespace

const RtpksMaterial* RtpksTilePackage::materialById(int material_id) const {
    auto it = std::find_if(materials.begin(), materials.end(), [material_id](const RtpksMaterial& mat) {
        return mat.material_id == material_id;
    });
    return it == materials.end() ? nullptr : &*it;
}

const RtpksTileMesh* RtpksTilePackage::tileById(int resort_tile_id) const {
    auto it = std::find_if(tiles.begin(), tiles.end(), [resort_tile_id](const RtpksTileMesh& tile) {
        return tile.resort_tile_id == resort_tile_id;
    });
    return it == tiles.end() ? nullptr : &*it;
}

RtpksTilePackage loadRtpksTilePackage(const std::string& path, std::string* error) {
    RtpksTilePackage out;
    out.path = path;

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        fail(error, "Could not open RTPKS package: " + path);
        return out;
    }

    try {
        const JsonValue manifest = extractJson(zip, "manifest.json");
        if (strOr(manifest.get("format"), "") != "pokemon_resort.rtpks" || intOr(manifest.get("version"), 0) != 2) {
            throw std::runtime_error("Unsupported RTPKS package. Re-export as RTPKS v2: " + path);
        }
        const JsonValue runtime = extractJson(zip, "runtime/manifest.json");
        if (strOr(runtime.get("format"), "") != "pokemon_resort.rpak") {
            throw std::runtime_error("RTPKS missing runtime manifest: " + path);
        }
        out.pack_id = strOr(runtime.get("packId"), strOr(manifest.get("packId"), ""));

        if (const JsonValue* materials = runtime.get("materials"); materials && materials->isArray()) {
            for (const JsonValue& material_value : materials->asArray()) {
                if (!material_value.isObject()) continue;
                RtpksMaterial material;
                material.material_id = intOr(material_value.get("materialId"), -1);
                material.name = strOr(material_value.get("name"), "");
                material.texture_name = strOr(material_value.get("textureName"), "");
                material.alpha = intOr(material_value.get("alpha"), 31);
                material.render_order = intOr(material_value.get("renderOrder"), 0);
                material.layer_role = strOr(material_value.get("layerRole"), "surface");
                if (const JsonValue* sampler = material_value.get("sampler"); sampler && sampler->isObject()) {
                    material.wrap_s = strOr(sampler->get("wrapS"), "repeat");
                    material.wrap_t = strOr(sampler->get("wrapT"), "repeat");
                    material.mag_filter = strOr(sampler->get("magFilter"), "nearest");
                    material.min_filter = strOr(sampler->get("minFilter"), "nearest");
                }
                if (const JsonValue* mapping = material_value.get("uvMapping"); mapping && mapping->isObject()) {
                    material.world_uv = strOr(mapping->get("mode"), "local") == "world";
                    // uPerTile/vPerTile are a single JSON vec2, not an array of vec2.
                    if (const JsonValue* value = mapping->get("uPerTile"); value && value->isArray() && value->asArray().size() >= 2U) {
                        material.u_per_tile = {
                            floatOr(&value->asArray()[0], 0.0f),
                            floatOr(&value->asArray()[1], 0.0f),
                        };
                    }
                    if (const JsonValue* value = mapping->get("vPerTile"); value && value->isArray() && value->asArray().size() >= 2U) {
                        material.v_per_tile = {
                            floatOr(&value->asArray()[0], 0.0f),
                            floatOr(&value->asArray()[1], 0.0f),
                        };
                    }
                }
                if (material.material_id < 0) continue;
                if (!material.texture_name.empty()) {
                    material.image_bytes = extractZipEntry(zip, "runtime/textures/" + material.texture_name);
                }
                if (const JsonValue* animation = material_value.get("animation"); animation && animation->isObject() &&
                    strOr(animation->get("type"), "") == "frames") {
                    material.animation_frame_time_ms = std::max(16, intOr(animation->get("frameDurationMs"), 180));
                    for (const std::string& frame_name : stringArray(animation->get("frames"))) {
                        std::vector<std::uint8_t> frame = extractZipEntry(zip, "runtime/textures/" + frame_name);
                        if (!frame.empty()) material.animation_frame_bytes.push_back(std::move(frame));
                    }
                } else if (animation && animation->isObject() &&
                    strOr(animation->get("type"), "") == "materialMotion") {
                    material.animation_frame_time_ms = std::max(16, intOr(animation->get("frameDurationMs"), 100));
                    material.animation_timebase_hz = std::max(
                        0.001f,
                        floatOr(
                            animation->get("timebaseHz"),
                            1000.0f / static_cast<float>(material.animation_frame_time_ms)));
                    material.animation_step = strOr(animation->get("interpolation"), "step") != "linear";
                    material.animation_frame_count = std::max(1, intOr(animation->get("frameCount"), 1));
                    material.animation_image_frame_count = std::max(
                        1, intOr(animation->get("imageFrameCount"), material.animation_frame_count));
                    material.animation_loop = boolOr(animation->get("loop"), true);
                    material.animation_uv_offsets = vec2Array(animation->get("offsets"));
                    if (const JsonValue* keyframes = animation->get("imageKeyframes"); keyframes && keyframes->isArray()) {
                        for (const JsonValue& keyframe_value : keyframes->asArray()) {
                            if (!keyframe_value.isObject()) continue;
                            const std::string texture_name = strOr(keyframe_value.get("textureName"), "");
                            if (texture_name.empty()) continue;
                            RtpksMaterialImageKeyframe keyframe;
                            keyframe.frame = std::max(0, intOr(keyframe_value.get("frame"), 0));
                            keyframe.image_bytes = extractZipEntry(zip, "runtime/textures/" + texture_name);
                            if (!keyframe.image_bytes.empty()) material.animation_image_keyframes.push_back(std::move(keyframe));
                        }
                        std::sort(
                            material.animation_image_keyframes.begin(),
                            material.animation_image_keyframes.end(),
                            [](const RtpksMaterialImageKeyframe& a, const RtpksMaterialImageKeyframe& b) {
                                return a.frame < b.frame;
                            });
                    }
                }
                out.materials.push_back(std::move(material));
            }
        }

        if (const JsonValue* tiles = runtime.get("tiles"); tiles && tiles->isArray()) {
            for (const JsonValue& tile_value : tiles->asArray()) {
                if (!tile_value.isObject()) continue;
                const int resort_tile_id = intOr(tile_value.get("resortTileId"), -1);
                if (resort_tile_id < 0) continue;
                const std::string mesh_path = "runtime/meshes/tile_" + std::to_string(resort_tile_id) + ".json";
                const std::vector<std::uint8_t> mesh_bytes = extractZipEntry(zip, mesh_path);
                if (mesh_bytes.empty()) continue;
                const JsonValue mesh_root = parseJsonText(
                    std::string(reinterpret_cast<const char*>(mesh_bytes.data()), mesh_bytes.size()));
                out.tiles.push_back(parseTileMesh(resort_tile_id, mesh_root, &tile_value));
            }
        }
        mz_zip_reader_end(&zip);
        return out;
    } catch (const std::exception& e) {
        mz_zip_reader_end(&zip);
        fail(error, e.what());
        return {};
    }
}

} // namespace pr::gameplay::world3d::data
