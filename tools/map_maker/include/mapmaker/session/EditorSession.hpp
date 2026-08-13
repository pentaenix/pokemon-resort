#pragma once

#include "mapmaker/commands/CommandStack.hpp"
#include "mapmaker/project/MapProjectDocument.hpp"
#include "mapmaker/selection/SelectionModel.hpp"

#include <optional>
#include <string>

namespace pr::mapmaker {

class EditorSession {
public:
    explicit EditorSession(MapProjectDocument project);

    const MapProjectDocument& project() const { return project_; }
    CommandStack& commands() { return commands_; }
    const CommandStack& commands() const { return commands_; }
    SelectionModel& selection() { return selection_; }
    const SelectionModel& selection() const { return selection_; }

    bool activateMap(std::string map_id);
    const std::string& activeMapId() const { return active_map_id_; }
    const MapProjectEntry* activeMap() const;
    std::optional<MapSourceGroup> activeSource() const;

private:
    MapProjectDocument project_;
    std::string active_map_id_;
    CommandStack commands_;
    SelectionModel selection_;
};

} // namespace pr::mapmaker
