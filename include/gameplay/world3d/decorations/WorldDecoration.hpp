#pragma once

#include "gameplay/world3d/Overworld3DConfig.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pr::gameplay::world3d::decorations {

inline constexpr const char* kCategories[] = {
    "structures", "furniture", "nature", "equipment"};

struct Asset {
    std::string id;
    std::string category;
    std::filesystem::path glb_path;
    float scale = 1.0f;
};

class Catalog {
public:
    bool load(const std::filesystem::path& project_root, std::string& error);
    const std::vector<Asset>& entries() const { return entries_; }
    std::vector<std::size_t> indices(const std::string& category) const;
    const Asset* resolve(const std::string& id) const;

private:
    std::vector<Asset> entries_;
};

struct Placement {
    std::string id;
    std::string asset_id;
    std::string map_id;
    int cell_x = 0;
    int cell_y = 0;
    int yaw_quarter_turns = 0;
};

struct Document {
    std::uint64_t revision = 0;
    std::vector<Placement> placements;
};

std::string serializeDocument(const Document& document);
std::optional<Document> parseDocument(const std::string& text, std::string& error);

class Store {
public:
    explicit Store(std::filesystem::path path) : path_(std::move(path)) {}
    bool exists() const;
    std::optional<Document> load(std::string& error) const;
    bool save(const Document& document, std::string& error) const;

private:
    std::filesystem::path path_;
};

class Editor {
public:
    void open(
        std::string map_id,
        const SceneConfig& scene,
        std::vector<Placement> placements,
        const Catalog& catalog,
        int cursor_x,
        int cursor_y);
    void close();
    bool active() const { return active_; }
    bool dirty() const { return dirty_; }
    int cursorX() const { return cursor_x_; }
    int cursorY() const { return cursor_y_; }
    int assetIndex() const { return asset_index_; }
    int yawQuarterTurns() const { return yaw_quarter_turns_; }
    const std::string& mapId() const { return map_id_; }
    const std::vector<Placement>& placements() const { return placements_; }
    std::optional<Placement> preview() const;
    const std::string& error() const { return error_; }

    void moveCursor(int dx, int dy);
    void selectAsset(int catalog_index);
    void rotate(int direction = 1);
    bool place();
    bool erase();

private:
    bool cellAvailable(int x, int y) const;

    bool active_ = false;
    bool dirty_ = false;
    std::string map_id_;
    SceneConfig scene_;
    std::vector<Placement> placements_;
    const Catalog* catalog_ = nullptr;
    int cursor_x_ = 0;
    int cursor_y_ = 0;
    int asset_index_ = 0;
    int yaw_quarter_turns_ = 0;
    std::uint64_t next_id_ = 1;
    std::string error_;
};

} // namespace pr::gameplay::world3d::decorations
