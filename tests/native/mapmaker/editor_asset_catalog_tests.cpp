#include "mapmaker/assets/EditorAssetCatalog.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::filesystem::path root() {
#ifdef PR_SOURCE_DIR
    return PR_SOURCE_DIR;
#else
    return std::filesystem::current_path();
#endif
}

void testCatalogAndLazyPreview() {
    std::string error;
    auto catalog = pr::mapmaker::RtpksEditorCatalog::load(
        root() / "assets/overworld/tilepacks/maptiles.rtpks.meta", &error);
    expect(catalog.valid(), "live RTPKS editor catalog should load: " + error);
    expect(catalog.packId() == "map_tiles", "catalog pack ID should match runtime package");
    expect(catalog.tabs().size() >= 3, "catalog should expose authored tabs");
    expect(catalog.tiles().size() > 3000, "catalog should expose stable tile IDs without eager images");
    const auto* tile = catalog.findTile(0);
    expect(tile && tile->width == 2 && tile->height == 2, "tile footprint metadata should load");
    const auto png = catalog.previewPng(0);
    expect(png.size() > 8 && png[0] == 0x89 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G',
        "requested tile preview should be lazily extracted as PNG");
    expect(!catalog.smartSets().empty(), "future smart-object seeds should remain available");
}

void testModelCatalog() {
    std::vector<std::string> warnings;
    const auto models = pr::mapmaker::loadModelAssetCatalog(
        root() / "assets/overworld/models", &warnings);
    expect(models.size() >= 10, "model browser should discover usable model manifests");
    expect(std::filesystem::exists(models.front().glb_path), "catalog model GLB should exist");
}

} // namespace

int main() {
    try {
        testCatalogAndLazyPreview();
        testModelCatalog();
        std::cout << "editor_asset_catalog_tests: PASS\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "editor_asset_catalog_tests: FAIL: " << exception.what() << '\n';
        return 1;
    }
}
