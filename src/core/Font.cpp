#include "core/Font.hpp"

#include <filesystem>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace pr {

FontHandle loadFont(const std::string& configured_path, int pt_size, const std::string& project_root) {
    std::vector<fs::path> candidates;
    fs::path configured(configured_path);
    if (configured.is_absolute()) candidates.push_back(configured);
    else candidates.push_back(fs::path(project_root) / configured);

#ifdef __APPLE__
    candidates.push_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    candidates.push_back("/Library/Fonts/Arial.ttf");
    candidates.push_back("/System/Library/Fonts/Supplemental/Helvetica.ttc");
#endif

    for (const auto& path : candidates) {
        if (!fs::exists(path)) continue;
        if (TTF_Font* raw = TTF_OpenFont(path.string().c_str(), pt_size)) {
            return FontHandle(raw, TTF_CloseFont);
        }
    }

    throw std::runtime_error("Could not load a font. Add a font at the configured path or adjust config.assets.font.");
}

} // namespace pr
