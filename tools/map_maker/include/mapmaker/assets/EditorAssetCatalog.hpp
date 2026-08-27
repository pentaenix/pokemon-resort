#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pr::mapmaker {

struct TileAsset {
    int resort_tile_id = -1;
    std::string key;
    std::string name;
    std::string tab_id;
    int width = 1;
    int height = 1;
    std::string preview_path;
    int preview_width = 0;
    int preview_height = 0;
    std::vector<std::string> tags;
    std::string interior_role;
    bool door = false;
};

struct TileAssetTab {
    std::string id;
    std::string name;
    int order = 0;
    std::vector<int> tile_ids;
};

struct SmartTileSet {
    std::string id;
    std::string name;
    int width = 0;
    int height = 0;
    // The source format is intentionally [x][y]. Do not transpose it here.
    std::vector<std::vector<int>> columns;
};

class RtpksEditorCatalog {
public:
    RtpksEditorCatalog();
    RtpksEditorCatalog(RtpksEditorCatalog&&) noexcept;
    RtpksEditorCatalog& operator=(RtpksEditorCatalog&&) noexcept;
    RtpksEditorCatalog(const RtpksEditorCatalog&) = delete;
    RtpksEditorCatalog& operator=(const RtpksEditorCatalog&) = delete;
    ~RtpksEditorCatalog();

    static RtpksEditorCatalog load(
        const std::filesystem::path& meta_path,
        std::string* error = nullptr);

    bool valid() const;
    const std::string& packId() const;
    const std::vector<TileAssetTab>& tabs() const;
    const std::vector<TileAsset>& tiles() const;
    const std::vector<SmartTileSet>& smartSets() const;
    const TileAsset* findTile(int resort_tile_id) const;

    // Extracts only the requested thumbnail. The catalog never eagerly decodes
    // thousands of preview PNGs during startup.
    std::vector<std::uint8_t> previewPng(int resort_tile_id) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

struct ModelAsset {
    std::string id;
    std::string display_name;
    std::filesystem::path manifest_path;
    std::filesystem::path glb_path;
    float default_yaw_deg = 0.0f;
    float default_scale = 1.0f;
    int footprint_width = 1;
    int footprint_depth = 1;
    int footprint_height = 1;
};

std::vector<ModelAsset> loadModelAssetCatalog(
    const std::filesystem::path& model_root,
    std::vector<std::string>* warnings = nullptr);

} // namespace pr::mapmaker
