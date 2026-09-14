#include "gameplay/world3d/aquarium/decorations/AquariumDecoration.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>

namespace pr::gameplay::world3d::aquarium::decorations {
bool Decoration::operator==(const Decoration& b) const {
    return id==b.id && asset_id==b.asset_id && x_steps==b.x_steps && z_steps==b.z_steps &&
        height_steps==b.height_steps && scale_steps==b.scale_steps && yaw_steps==b.yaw_steps;
}
bool validAssetId(const std::string& id) {
    return !id.empty() && id.size()<240 && id.find('/')==std::string::npos &&
        id.find('\\')==std::string::npos && id.find("..") == std::string::npos &&
        std::filesystem::path(id).extension()==".glb";
}
namespace {
std::string string(const JsonValue& value,const char* key) {
    const auto* item=value.get(key);
    if (!item || !item->isString()) throw std::runtime_error(std::string("Invalid decoration ")+key);
    return item->asString();
}
int integer(const JsonValue& value,const char* key) {
    const auto* item=value.get(key);
    if (!item || !item->isNumber() || !std::isfinite(item->asNumber()) ||
        std::floor(item->asNumber())!=item->asNumber() || std::abs(item->asNumber())>65536)
        throw std::runtime_error(std::string("Invalid decoration ")+key);
    return static_cast<int>(item->asNumber());
}
}
std::vector<TankDecorations> parseDecorations(const JsonValue& value) {
    if (!value.isArray()) throw std::runtime_error("tankDecorations must be an array");
    std::vector<TankDecorations> result;
    for (const auto& entry:value.asArray()) {
        TankDecorations tank; tank.tank_id=string(entry,"tankId");
        const auto* objects=entry.get("objects");
        if (!objects || !objects->isArray() || objects->asArray().size()>kTankDecorationLimit)
            throw std::runtime_error("Invalid decoration list or more than 25 objects");
        for (const auto& object:objects->asArray()) tank.objects.push_back({
            string(object,"id"),string(object,"assetId"),integer(object,"xSteps"),integer(object,"zSteps"),
            integer(object,"heightSteps"),integer(object,"scaleSteps"),integer(object,"yawSteps")});
        result.push_back(std::move(tank));
    }
    return result;
}
std::vector<std::string> validateDecorations(const std::vector<TankDecorations>& tanks) {
    std::vector<std::string> errors; std::set<std::string> tank_ids,ids;
    for (const auto& tank:tanks) {
        if (tank.tank_id.empty() || !tank_ids.insert(tank.tank_id).second || tank.objects.size()>kTankDecorationLimit)
            errors.push_back("invalid_tank_decorations:"+tank.tank_id);
        for (const auto& o:tank.objects) if(o.id.empty() || !ids.insert(o.id).second || !validAssetId(o.asset_id) ||
            std::abs(o.x_steps)>32768 || std::abs(o.z_steps)>32768 || std::abs(o.height_steps)>1024 ||
            o.scale_steps<1 || o.scale_steps>32 || o.yaw_steps<0 || o.yaw_steps>=24)
                errors.push_back("invalid_decoration:"+o.id);
    }
    return errors;
}
JsonValue serializeDecorations(const std::vector<TankDecorations>& source) {
    auto tanks=source;
    std::sort(tanks.begin(),tanks.end(),[](const auto& a,const auto& b){return a.tank_id<b.tank_id;});
    JsonValue::Array result;
    for (const auto& tank:tanks) {
        JsonValue::Array objects;
        for (const auto& o:tank.objects) objects.emplace_back(JsonValue::Object{
            {"id",JsonValue(o.id)},{"assetId",JsonValue(o.asset_id)},
            {"xSteps",JsonValue(double(o.x_steps))},{"zSteps",JsonValue(double(o.z_steps))},
            {"heightSteps",JsonValue(double(o.height_steps))},{"scaleSteps",JsonValue(double(o.scale_steps))},
            {"yawSteps",JsonValue(double(o.yaw_steps))}});
        result.emplace_back(JsonValue::Object{{"tankId",JsonValue(tank.tank_id)},{"objects",JsonValue(std::move(objects))}});
    }
    return JsonValue(std::move(result));
}
} // namespace pr::gameplay::world3d::aquarium::decorations
