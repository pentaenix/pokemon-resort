#pragma once
#include "gameplay/world3d/aquarium/rooms/AquariumBuildingLayout.hpp"
#include <filesystem>

namespace pr::gameplay::world3d::aquarium::rooms {
class AquariumRoomStore {
public:
    explicit AquariumRoomStore(std::filesystem::path path):path_(std::move(path)) {}
    bool exists() const;
    LayoutLoadResult load() const;
    bool save(const BuildingLayout&,std::string& error) const;
private:
    std::filesystem::path path_;
};
}
