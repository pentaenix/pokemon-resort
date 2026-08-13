#include "mapmaker/session/EditorSession.hpp"

#include <utility>

namespace pr::mapmaker {

EditorSession::EditorSession(MapProjectDocument project) : project_(std::move(project)) {
    if (project_.findMap(project_.editor().active_map_id)) {
        active_map_id_ = project_.editor().active_map_id;
    } else if (!project_.maps().empty()) {
        active_map_id_ = project_.maps().front().id;
    }
    commands_.markClean();
}

bool EditorSession::activateMap(std::string map_id) {
    if (!project_.findMap(map_id)) return false;
    if (map_id == active_map_id_) return true;
    active_map_id_ = std::move(map_id);
    selection_.retainMap(active_map_id_);
    return true;
}

const MapProjectEntry* EditorSession::activeMap() const {
    return project_.findMap(active_map_id_);
}

std::optional<MapSourceGroup> EditorSession::activeSource() const {
    const std::string key = project_.sourceKeyFor(active_map_id_);
    if (key.empty()) return std::nullopt;
    for (MapSourceGroup group : project_.sourceGroups()) {
        if (group.key == key) return group;
    }
    return std::nullopt;
}

} // namespace pr::mapmaker
