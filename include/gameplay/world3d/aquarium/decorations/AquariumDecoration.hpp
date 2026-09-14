#pragma once

#include "core/config/Json.hpp"
#include "gameplay/world3d/aquarium/AquariumPokemonMetrics.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::aquarium::decorations {
inline constexpr std::size_t kTankDecorationLimit = 25;
enum class Category { Rocks, Corals, Plants, Other };
Category decorationCategory(const std::string& asset_id);
// Tank-centred X/Z, substrate-relative Y, in eighth-cell (2 world unit) steps.
// Height is independent of lateral movement. Scale steps are quarters, yaw 15°.
struct Decoration {
    std::string id, asset_id;
    int x_steps=0, z_steps=0, height_steps=0, scale_steps=4, yaw_steps=0;
    bool operator==(const Decoration& other) const;
};
struct TankDecorations {
    std::string tank_id;
    std::vector<Decoration> objects;
};
struct Asset {
    std::string id, name;
    std::filesystem::path path;
    AquariumPokemonMetrics bounds;
    float base_scale=1;
    bool measured=false;
};
class Catalog {
public:
    void scan(const std::filesystem::path& project_root);
    const std::vector<Asset>& entries() const { return assets_; }
    std::vector<std::size_t> indices(Category category) const;
    const Asset* resolve(const std::string& id);
    const std::string& error() const { return error_; }
private:
    std::vector<Asset> assets_;
    std::string error_;
};
bool validAssetId(const std::string&);
std::vector<TankDecorations> parseDecorations(const JsonValue&);
JsonValue serializeDecorations(const std::vector<TankDecorations>&);
std::vector<std::string> validateDecorations(const std::vector<TankDecorations>&);
} // namespace pr::gameplay::world3d::aquarium::decorations
