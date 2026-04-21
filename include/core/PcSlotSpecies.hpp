#pragma once

#include <string>

namespace pr {

/// One PC slot’s species line from the PKHeX bridge (box slots / box_1 summary).
struct PcSlotSpecies {
    std::string slug;
    /// National dex species id when present (e.g. 29 / 32 disambiguate Nidoran when slug is generic).
    int species_id = -1;
    /// PKHeX gender: 0 = male, 1 = female, 2 = genderless; -1 = unknown / not sent.
    int gender = -1;
};

} // namespace pr
