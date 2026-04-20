#include "resort/services/BoxViewService.hpp"

namespace pr::resort {

BoxViewService::BoxViewService(BoxRepository& boxes)
    : boxes_(boxes) {}

std::vector<PokemonSlotView> BoxViewService::getBoxSlotViews(
    const std::string& profile_id,
    int box_id) const {
    return boxes_.getBoxSlotViews(profile_id, box_id);
}

} // namespace pr::resort
