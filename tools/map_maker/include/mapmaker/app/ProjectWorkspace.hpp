#pragma once

#include "mapmaker/commands/CommandStack.hpp"
#include "mapmaker/document/OwmapDocument.hpp"
#include "mapmaker/project/MapProjectDocument.hpp"
#include "mapmaker/validation/ProjectValidator.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace pr::mapmaker {

struct OpenMapSource {
    std::string key;
    std::filesystem::path path;
    OwmapDocument document;
    CommandStack commands;
};

class ProjectWorkspace {
public:
    static ProjectWorkspace open(
        const std::filesystem::path& pokemon_resort_root,
        const std::filesystem::path& project_path);

    const MapProjectDocument& project() const { return project_; }
    const std::filesystem::path& projectPath() const { return project_path_; }
    const std::string& activeMapId() const { return active_map_id_; }
    const MapProjectEntry* activeMapEntry() const;
    const OpenMapSource* sourceForMap(const std::string& map_id) const;
    OpenMapSource* activeSource();
    const OpenMapSource* activeSource() const;
    bool activateMap(const std::string& map_id);

    std::filesystem::path mapPath(const MapProjectEntry& entry) const;
    std::vector<ValidationDiagnostic> validate() const;
    bool dirty() const;
    void saveActive();
    void saveAll();

    std::vector<OpenMapSource*> sources();
    std::vector<const OpenMapSource*> sources() const;

private:
    std::filesystem::path pokemon_resort_root_;
    std::filesystem::path project_path_;
    MapProjectDocument project_;
    std::string active_map_id_;
    std::unordered_map<std::string, OpenMapSource> sources_;
};

} // namespace pr::mapmaker
