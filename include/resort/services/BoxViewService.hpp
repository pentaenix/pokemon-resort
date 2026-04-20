#pragma once

#include "resort/domain/ResortTypes.hpp"
#include "resort/persistence/BoxRepository.hpp"

#include <string>
#include <vector>

namespace pr::resort {

class BoxViewService {
public:
    explicit BoxViewService(BoxRepository& boxes);

    std::vector<PokemonSlotView> getBoxSlotViews(const std::string& profile_id, int box_id) const;

private:
    BoxRepository& boxes_;
};

} // namespace pr::resort
