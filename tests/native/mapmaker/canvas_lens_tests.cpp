#include "mapmaker/ui/CanvasLens.hpp"

#include <iostream>
#include <stdexcept>

namespace {

int channel(std::uint32_t color, int shift) {
    return static_cast<int>((color >> shift) & 0xffU);
}

} // namespace

int main() {
    try {
        const auto low = pr::mapmaker::heightHeatColor(0U);
        const auto middle = pr::mapmaker::heightHeatColor(16U);
        const auto high = pr::mapmaker::heightHeatColor(31U);
        if (channel(low, 16) <= channel(low, 0)) {
            throw std::runtime_error("height zero should be visibly cool/blue");
        }
        if (channel(high, 0) <= channel(high, 16)) {
            throw std::runtime_error("height 31 should be visibly warm/red");
        }
        if (low == middle || middle == high) {
            throw std::runtime_error("height lens must distinguish authored elevations");
        }
        if (pr::mapmaker::heightHeatColor(31U) !=
            pr::mapmaker::heightHeatColor(255U)) {
            throw std::runtime_error("heights above the authored legend must saturate");
        }
        if (pr::mapmaker::collisionLensColor(true) ==
            pr::mapmaker::collisionLensColor(false)) {
            throw std::runtime_error("blocked and walkable cells need different colors");
        }
        std::cout << "canvas_lens_tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "canvas_lens_tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
