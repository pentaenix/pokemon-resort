#include "mapmaker/assets/EditorAssetCatalog.hpp"

#include "core/config/Json.hpp"
#include "core/crypto/Sha256.hpp"

#include <miniz.h>

#include <algorithm>
#include <fstream>
#include <unordered_map>

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

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) return {};
    const std::streamsize length = stream.tellg();
    if (length < 0) return {};
    stream.seekg(0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    if (length > 0) stream.read(reinterpret_cast<char*>(bytes.data()), length);
    return stream ? bytes : std::vector<std::uint8_t>{};
}

std::vector<std::uint8_t> zipEntry(mz_zip_archive& zip, const std::string& path) {
    std::size_t size = 0;
    void* raw = mz_zip_reader_extract_file_to_heap(&zip, path.c_str(), &size, 0);
    if (!raw) return {};
    auto* first = static_cast<std::uint8_t*>(raw);
    std::vector<std::uint8_t> result(first, first + size);
    mz_free(raw);
    return result;
}

JsonValue zipJson(mz_zip_archive& zip, const std::string& path) {
    const auto bytes = zipEntry(zip, path);
    if (bytes.empty()) throw std::runtime_error("Missing " + path);
    return parseJsonText(std::string(
        reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::vector<std::string> strings(const JsonValue* value) {
    std::vector<std::string> result;
    if (!value || !value->isArray()) return result;
    for (const JsonValue& entry : value->asArray()) {
        if (entry.isString()) result.push_back(entry.asString());
    }
    return result;
}

std::vector<int> integers(const JsonValue* value) {
    std::vector<int> result;
    if (!value || !value->isArray()) return result;
    result.reserve(value->asArray().size());
    for (const JsonValue& entry : value->asArray()) {
        result.push_back(entry.isNumber() ? static_cast<int>(entry.asNumber()) : -1);
    }
    return result;
}

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

} // namespace

class RtpksEditorCatalog::Impl {
public:
    ~Impl() {
        if (zip_open) mz_zip_reader_end(&zip);
    }

    mz_zip_archive zip{};
    bool zip_open = false;
    bool valid = false;
    std::string pack_id;
    std::vector<TileAssetTab> tabs;
    std::vector<TileAsset> tiles;
    std::vector<SmartTileSet> smart_sets;
    std::unordered_map<int, std::size_t> tile_index;
};

RtpksEditorCatalog::RtpksEditorCatalog() = default;
RtpksEditorCatalog::RtpksEditorCatalog(RtpksEditorCatalog&&) noexcept = default;
RtpksEditorCatalog& RtpksEditorCatalog::operator=(RtpksEditorCatalog&&) noexcept = default;
RtpksEditorCatalog::~RtpksEditorCatalog() = default;

RtpksEditorCatalog RtpksEditorCatalog::load(
    const std::filesystem::path& meta_path,
    std::string* error) {
    RtpksEditorCatalog result;
    result.impl_ = std::make_unique<Impl>();
    auto& impl = *result.impl_;
    if (!mz_zip_reader_init_file(&impl.zip, meta_path.string().c_str(), 0)) {
        if (error) *error = "Could not open RTPKS editor sidecar: " + meta_path.string();
        return result;
    }
    impl.zip_open = true;
    try {
        const JsonValue manifest = zipJson(impl.zip, "manifest.json");
        if (stringOr(manifest.get("format")) != "pokemon_resort.rtpks.meta" ||
            intOr(manifest.get("version")) != 1) {
            throw std::runtime_error("Unsupported RTPKS editor sidecar format");
        }
        impl.pack_id = stringOr(manifest.get("packId"));
        const std::filesystem::path source_path =
            meta_path.parent_path() / stringOr(manifest.get("sourceRtpks"));
        const auto source_bytes = readFile(source_path);
        if (source_bytes.empty()) {
            throw std::runtime_error("Missing source RTPKS: " + source_path.string());
        }
        const std::string expected_hash = stringOr(manifest.get("sourceSha256"));
        const std::string actual_hash = sha256HexLowercase(source_bytes);
        if (!expected_hash.empty() && actual_hash != expected_hash) {
            throw std::runtime_error("RTPKS editor sidecar does not match " + source_path.filename().string());
        }

        const JsonValue metadata = zipJson(
            impl.zip, stringOr(manifest.get("tileMetadata"), "editor/tile_metadata.json"));
        if (const JsonValue* tabs = metadata.get("tabs"); tabs && tabs->isArray()) {
            for (const JsonValue& value : tabs->asArray()) {
                if (!value.isObject()) continue;
                TileAssetTab tab;
                tab.id = stringOr(value.get("id"));
                tab.name = stringOr(value.get("name"), tab.id);
                tab.order = intOr(value.get("order"));
                tab.tile_ids = integers(value.get("tileIds"));
                if (!tab.id.empty()) impl.tabs.push_back(std::move(tab));
            }
        }
        std::sort(impl.tabs.begin(), impl.tabs.end(), [](const auto& a, const auto& b) {
            return a.order == b.order ? a.name < b.name : a.order < b.order;
        });

        if (const JsonValue* tiles = metadata.get("tiles"); tiles && tiles->isArray()) {
            impl.tiles.reserve(tiles->asArray().size());
            for (const JsonValue& value : tiles->asArray()) {
                if (!value.isObject()) continue;
                TileAsset tile;
                tile.resort_tile_id = intOr(value.get("resortTileId"), -1);
                tile.key = stringOr(value.get("key"));
                tile.name = stringOr(value.get("name"), tile.key);
                tile.tab_id = stringOr(value.get("tabId"), "default");
                tile.width = std::max(1, intOr(value.get("width"), 1));
                tile.height = std::max(1, intOr(value.get("height"), 1));
                tile.tags = strings(value.get("tags"));
                tile.door = contains(tile.tags, "interaction.door");
                if (const JsonValue* properties = value.get("properties");
                    properties && properties->isObject()) {
                    tile.door = tile.door || stringOr(properties->get("interaction.kind")) == "door";
                    tile.interior_role = stringOr(properties->get("interior.role"));
                }
                if (tile.interior_role.empty()) {
                    const auto tagged = std::find_if(tile.tags.begin(), tile.tags.end(),
                        [](const std::string& tag) { return tag.starts_with("interior."); });
                    if (tagged != tile.tags.end()) tile.interior_role = tagged->substr(9);
                }
                if (const JsonValue* preview = value.get("preview"); preview && preview->isObject()) {
                    tile.preview_path = stringOr(preview->get("image"));
                    tile.preview_width = intOr(preview->get("width"));
                    tile.preview_height = intOr(preview->get("height"));
                }
                if (tile.resort_tile_id < 0) continue;
                impl.tile_index[tile.resort_tile_id] = impl.tiles.size();
                impl.tiles.push_back(std::move(tile));
            }
        }

        if (const JsonValue* sets = metadata.get("smartSets"); sets && sets->isArray()) {
            for (const JsonValue& value : sets->asArray()) {
                if (!value.isObject()) continue;
                SmartTileSet set;
                set.id = stringOr(value.get("id"));
                set.name = stringOr(value.get("name"), set.id);
                set.width = intOr(value.get("width"));
                set.height = intOr(value.get("height"));
                if (const JsonValue* grid = value.get("grid"); grid && grid->isArray()) {
                    for (const JsonValue& column : grid->asArray()) {
                        set.columns.push_back(integers(&column));
                    }
                }
                if (!set.id.empty()) impl.smart_sets.push_back(std::move(set));
            }
        }
        impl.valid = !impl.pack_id.empty() && !impl.tiles.empty();
    } catch (const std::exception& exception) {
        if (error) *error = exception.what();
    }
    return result;
}

bool RtpksEditorCatalog::valid() const { return impl_ && impl_->valid; }

const std::string& RtpksEditorCatalog::packId() const {
    static const std::string empty;
    return impl_ ? impl_->pack_id : empty;
}

const std::vector<TileAssetTab>& RtpksEditorCatalog::tabs() const {
    static const std::vector<TileAssetTab> empty;
    return impl_ ? impl_->tabs : empty;
}

const std::vector<TileAsset>& RtpksEditorCatalog::tiles() const {
    static const std::vector<TileAsset> empty;
    return impl_ ? impl_->tiles : empty;
}

const std::vector<SmartTileSet>& RtpksEditorCatalog::smartSets() const {
    static const std::vector<SmartTileSet> empty;
    return impl_ ? impl_->smart_sets : empty;
}

const TileAsset* RtpksEditorCatalog::findTile(int resort_tile_id) const {
    if (!impl_) return nullptr;
    const auto found = impl_->tile_index.find(resort_tile_id);
    return found == impl_->tile_index.end() ? nullptr : &impl_->tiles[found->second];
}

std::vector<std::uint8_t> RtpksEditorCatalog::previewPng(int resort_tile_id) const {
    const TileAsset* tile = findTile(resort_tile_id);
    if (!impl_ || !tile || tile->preview_path.empty()) return {};
    return zipEntry(impl_->zip, tile->preview_path);
}

std::vector<ModelAsset> loadModelAssetCatalog(
    const std::filesystem::path& model_root,
    std::vector<std::string>* warnings) {
    std::vector<ModelAsset> result;
    if (!std::filesystem::exists(model_root)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(model_root)) {
        if (!entry.is_directory()) continue;
        const std::filesystem::path manifest_path = entry.path() / "model.json";
        if (!std::filesystem::exists(manifest_path)) continue;
        try {
            const JsonValue root = parseJsonFile(manifest_path.string());
            ModelAsset asset;
            asset.id = stringOr(root.get("id"), entry.path().filename().string());
            asset.display_name = stringOr(root.get("displayName"), asset.id);
            asset.manifest_path = manifest_path;
            asset.glb_path = entry.path() / stringOr(
                root.get("glbFile"), stringOr(root.get("modelFile")));
            asset.default_yaw_deg = floatOr(root.get("defaultYawDeg"));
            asset.default_scale = floatOr(root.get("defaultScale"), 1.0f);
            if (const JsonValue* footprint = root.get("footprintTiles");
                footprint && footprint->isObject()) {
                asset.footprint_width = std::max(1, intOr(footprint->get("w"), 1));
                asset.footprint_depth = std::max(1, intOr(footprint->get("d"), 1));
                asset.footprint_height = std::max(1, intOr(footprint->get("h"), 1));
            }
            if (!asset.id.empty() && std::filesystem::exists(asset.glb_path)) {
                result.push_back(std::move(asset));
            } else if (warnings) {
                warnings->push_back("Model manifest has no usable GLB: " + manifest_path.string());
            }
        } catch (const std::exception& exception) {
            if (warnings) warnings->push_back(manifest_path.string() + ": " + exception.what());
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.display_name < b.display_name;
    });
    return result;
}

} // namespace pr::mapmaker
