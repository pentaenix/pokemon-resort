#pragma once

#include "mapmaker/assets/EditorAssetCatalog.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace pr::mapmaker {

// UI-only cache. Preview PNGs remain compressed in the RTPKS sidecar until a
// tile card actually becomes visible in the browser.
class BgfxThumbnailCache {
public:
    explicit BgfxThumbnailCache(const RtpksEditorCatalog& catalog);
    BgfxThumbnailCache(const BgfxThumbnailCache&) = delete;
    BgfxThumbnailCache& operator=(const BgfxThumbnailCache&) = delete;
    ~BgfxThumbnailCache();

    // Starts a UI frame and caps synchronous ZIP/decode/GPU work. Visible
    // cards that miss the budget request their image again on the next frame.
    void beginFrame(std::size_t upload_budget = 4);
    std::uint16_t textureForTile(int resort_tile_id);
    void clear();
    std::size_t size() const { return textures_.size(); }

private:
    const RtpksEditorCatalog& catalog_;
    std::unordered_map<int, std::uint16_t> textures_;
    std::unordered_set<int> failed_tiles_;
    std::size_t upload_budget_ = 4;
    std::size_t uploads_this_frame_ = 0;
};

} // namespace pr::mapmaker
