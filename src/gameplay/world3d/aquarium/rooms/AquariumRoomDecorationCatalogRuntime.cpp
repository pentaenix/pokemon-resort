#include "gameplay/world3d/aquarium/rooms/AquariumRoomDecorationCatalog.hpp"

#include <algorithm>
#include <cmath>

namespace pr::gameplay::world3d::aquarium::rooms {

const RoomDecorationAsset* RoomDecorationCatalog::resolve(const std::string& id) const {
    auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& asset){return asset.id==id;});
    if(found==entries_.end()) {error_="Room decoration asset is unavailable";return nullptr;}
    if(!found->measured) {
        found->bounds=measureAquariumPokemon(found->path.string(),{},&error_);
        found->measured=true;
        const auto& b=found->bounds;
        const float span=std::max({b.max_x-b.min_x,b.max_y-b.min_y,b.max_z-b.min_z});
        if(!b.valid||!std::isfinite(span)||span<.00001f)return nullptr;
        // Room props are authored in Resort world units. Unlike tank dressing,
        // they must retain that physical scale instead of being normalized to
        // fit inside one aquarium cell.
        found->base_scale=1.0f;
    }
    return found->bounds.valid?&*found:nullptr;
}

} // namespace pr::gameplay::world3d::aquarium::rooms
