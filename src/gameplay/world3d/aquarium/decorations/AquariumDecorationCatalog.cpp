#include "gameplay/world3d/aquarium/decorations/AquariumDecoration.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
namespace pr::gameplay::world3d::aquarium::decorations {
Category decorationCategory(const std::string& id) {
    auto name=id;std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
    auto has=[&](const char* word){return name.find(word)!=std::string::npos;};
    if(has("coral")||has("anemone"))return Category::Corals;
    if(has("grass")||has("algae")||has("kelp")||has("plant")||has("lily")||has("tree")||has("stump")||has("fern"))return Category::Plants;
    if(has("rock")||has("boulder")||has("stone")||has("icicle"))return Category::Rocks;
    return Category::Other;
}
std::vector<std::size_t> Catalog::indices(Category category) const {
    std::vector<std::size_t> out;
    for(std::size_t i=0;i<assets_.size();++i)if(decorationCategory(assets_[i].id)==category)out.push_back(i);
    return out;
}
void Catalog::scan(const std::filesystem::path& root) {
    assets_.clear(); error_.clear();
    const auto folder=root/"assets/aquarium";
    std::error_code error;
    for (const auto& file : std::filesystem::directory_iterator(folder,error)) {
        if (!file.is_regular_file() || file.is_symlink() || !validAssetId(file.path().filename().string())) continue;
        Asset asset;
        asset.id=file.path().filename().string(); asset.path=file.path();
        asset.name=file.path().stem().string();
        const auto suffix=asset.name.find("_preview");
        if (suffix!=std::string::npos) asset.name.resize(suffix);
        assets_.push_back(std::move(asset));
    }
    std::sort(assets_.begin(),assets_.end(),[](const auto& a,const auto& b){return a.id<b.id;});
    if (error) error_=error.message();
}
const Asset* Catalog::resolve(const std::string& id) {
    auto found=std::find_if(assets_.begin(),assets_.end(),[&](const auto& a){return a.id==id;});
    if (found==assets_.end()) { error_="Decoration asset is unavailable";return nullptr; }
    if (!found->measured) {
        found->bounds=measureAquariumPokemon(found->path.string(),{},&error_);
        found->measured=true;
        const auto& b=found->bounds;
        const float span=std::max({b.max_x-b.min_x,b.max_y-b.min_y,b.max_z-b.min_z});
        if (!b.valid || !std::isfinite(span) || span<.00001f) return nullptr;
        found->base_scale=16.0f/span;
    }
    return found->bounds.valid ? &*found : nullptr;
}
}
