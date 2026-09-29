#include "gameplay/world3d/decorations/WorldDecoration.hpp"

#include "core/config/Json.hpp"
#include "gameplay/world3d/terrain/TerrainSurface.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pr::gameplay::world3d::decorations {
namespace fs = std::filesystem;
namespace {

int integer(const JsonValue* value, const char* label, int minimum, int maximum) {
    if (!value || !value->isNumber() || !std::isfinite(value->asNumber()) ||
        std::floor(value->asNumber()) != value->asNumber() ||
        value->asNumber() < minimum || value->asNumber() > maximum) {
        throw std::runtime_error(std::string("Invalid ") + label);
    }
    return static_cast<int>(value->asNumber());
}

std::string string(const JsonValue* value, const char* label) {
    if (!value || !value->isString() || value->asString().empty()) {
        throw std::runtime_error(std::string("Invalid ") + label);
    }
    return value->asString();
}

bool validCategory(const std::string& category) {
    return std::find(std::begin(kCategories), std::end(kCategories), category) !=
        std::end(kCategories);
}

std::optional<Document> readDocument(const fs::path& path, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "World decoration save is unavailable";
        return std::nullopt;
    }
    std::ostringstream text;
    text << input.rdbuf();
    return parseDocument(text.str(), error);
}

} // namespace

bool Catalog::load(const fs::path& project_root, std::string& error) {
    entries_.clear();
    try {
        const auto path = project_root / "config/gameplay/world3d/world_decorations.json";
        const JsonValue root = parseJsonFile(path.string());
        if (string(root.get("schema"), "world decoration schema") !=
                "pokemon-resort-world-decoration-catalog" ||
            integer(root.get("schemaVersion"), "world decoration schema version", 1, 1) != 1) {
            throw std::runtime_error("Unsupported world decoration catalog");
        }
        const JsonValue* assets = root.get("assets");
        if (!assets || !assets->isArray() || assets->asArray().empty()) {
            throw std::runtime_error("World decoration catalog has no assets");
        }
        std::set<std::string> ids;
        for (const JsonValue& item : assets->asArray()) {
            Asset asset;
            asset.id = string(item.get("id"), "world decoration asset id");
            asset.category = string(item.get("category"), "world decoration category");
            if (!validCategory(asset.category) || !ids.insert(asset.id).second) {
                throw std::runtime_error("Invalid or duplicate world decoration asset: " + asset.id);
            }
            const fs::path relative = string(item.get("glb"), "world decoration GLB path");
            asset.glb_path = project_root / relative;
            if (!fs::exists(asset.glb_path)) {
                throw std::runtime_error("World decoration GLB is missing: " + relative.string());
            }
            if (const JsonValue* scale = item.get("scale")) {
                if (!scale->isNumber() || !std::isfinite(scale->asNumber()) ||
                    scale->asNumber() <= 0.0 || scale->asNumber() > 100.0) {
                    throw std::runtime_error("Invalid world decoration scale: " + asset.id);
                }
                asset.scale = static_cast<float>(scale->asNumber());
            }
            entries_.push_back(std::move(asset));
        }
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        entries_.clear();
        return false;
    }
}

std::vector<std::size_t> Catalog::indices(const std::string& category) const {
    std::vector<std::size_t> result;
    for (std::size_t index = 0; index < entries_.size(); ++index) {
        if (entries_[index].category == category) result.push_back(index);
    }
    return result;
}

const Asset* Catalog::resolve(const std::string& id) const {
    const auto found = std::find_if(entries_.begin(), entries_.end(), [&](const Asset& asset) {
        return asset.id == id;
    });
    return found == entries_.end() ? nullptr : &*found;
}

std::string serializeDocument(const Document& document) {
    JsonValue::Array placements;
    for (const Placement& placement : document.placements) {
        placements.emplace_back(JsonValue::Object{
            {"assetId", JsonValue(placement.asset_id)},
            {"cellX", JsonValue(static_cast<double>(placement.cell_x))},
            {"cellY", JsonValue(static_cast<double>(placement.cell_y))},
            {"id", JsonValue(placement.id)},
            {"mapId", JsonValue(placement.map_id)},
            {"yawQuarterTurns", JsonValue(static_cast<double>(placement.yaw_quarter_turns))},
        });
    }
    return serializeJsonValue(JsonValue(JsonValue::Object{
        {"placements", JsonValue(std::move(placements))},
        {"revision", JsonValue(static_cast<double>(document.revision))},
        {"schema", JsonValue(std::string("pokemon-resort-world-decorations"))},
        {"schemaVersion", JsonValue(1.0)},
    }), JsonStyle::Pretty, 2) + "\n";
}

std::optional<Document> parseDocument(const std::string& text, std::string& error) {
    try {
        const JsonValue root = parseJsonText(text);
        if (string(root.get("schema"), "world decoration save schema") !=
                "pokemon-resort-world-decorations" ||
            integer(root.get("schemaVersion"), "world decoration save version", 1, 1) != 1) {
            throw std::runtime_error("Unsupported world decoration save");
        }
        Document result;
        result.revision = static_cast<std::uint64_t>(
            integer(root.get("revision"), "world decoration revision", 0, 1000000000));
        const JsonValue* placements = root.get("placements");
        if (!placements || !placements->isArray() || placements->asArray().size() > 10000) {
            throw std::runtime_error("Invalid world decoration placement list");
        }
        std::set<std::string> ids;
        for (const JsonValue& item : placements->asArray()) {
            Placement placement;
            placement.id = string(item.get("id"), "world decoration placement id");
            placement.asset_id = string(item.get("assetId"), "world decoration placement asset");
            placement.map_id = string(item.get("mapId"), "world decoration placement map");
            placement.cell_x = integer(item.get("cellX"), "world decoration cell X", -8192, 8192);
            placement.cell_y = integer(item.get("cellY"), "world decoration cell Y", -8192, 8192);
            placement.yaw_quarter_turns = integer(
                item.get("yawQuarterTurns"), "world decoration yaw", 0, 3);
            if (!ids.insert(placement.id).second) {
                throw std::runtime_error("Duplicate world decoration placement id");
            }
            result.placements.push_back(std::move(placement));
        }
        error.clear();
        return result;
    } catch (const std::exception& exception) {
        error = exception.what();
        return std::nullopt;
    }
}

bool Store::exists() const {
    return fs::exists(path_) || fs::exists(path_.string() + ".bak");
}

std::optional<Document> Store::load(std::string& error) const {
    if (auto primary = readDocument(path_, error)) return primary;
    std::string backup_error;
    if (auto backup = readDocument(path_.string() + ".bak", backup_error)) {
        error = "Recovered world decoration backup";
        return backup;
    }
    return std::nullopt;
}

bool Store::save(const Document& document, std::string& error) const {
    const fs::path temporary = path_.string() + ".tmp";
    const fs::path backup = path_.string() + ".bak";
    try {
        fs::create_directories(path_.parent_path());
        const std::string text = serializeDocument(document);
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            output << text;
            output.flush();
            if (!output) throw std::runtime_error("World decoration save write failed");
        }
        std::string verify_error;
        const auto verified = readDocument(temporary, verify_error);
        if (!verified || serializeDocument(*verified) != text) {
            throw std::runtime_error("World decoration save read-back failed");
        }
        if (fs::exists(path_)) {
            fs::copy_file(path_, backup, fs::copy_options::overwrite_existing);
        } else if (!fs::exists(backup)) {
            fs::copy_file(temporary, backup, fs::copy_options::overwrite_existing);
        }
        fs::rename(temporary, path_);
        error.clear();
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }
}

void Editor::open(std::string map_id, const SceneConfig& scene,
    std::vector<Placement> placements, const Catalog& catalog, int cursor_x, int cursor_y) {
    active_ = true;
    dirty_ = false;
    map_id_ = std::move(map_id);
    scene_ = scene;
    placements_ = std::move(placements);
    catalog_ = &catalog;
    cursor_x_ = std::clamp(cursor_x, 0, std::max(0, scene_.grid.width - 1));
    cursor_y_ = std::clamp(cursor_y, 0, std::max(0, scene_.grid.height - 1));
    asset_index_ = 0;
    yaw_quarter_turns_ = 0;
    next_id_ = 1;
    std::set<std::string> ids;
    for (const Placement& placement : placements_) ids.insert(placement.id);
    while (ids.count("world-object-" + std::to_string(next_id_))) ++next_id_;
    error_.clear();
}

void Editor::close() {
    active_ = false;
    placements_.clear();
    catalog_ = nullptr;
    error_.clear();
}

void Editor::moveCursor(int dx, int dy) {
    if (!active_) return;
    cursor_x_ = std::clamp(cursor_x_ + dx, 0, std::max(0, scene_.grid.width - 1));
    cursor_y_ = std::clamp(cursor_y_ + dy, 0, std::max(0, scene_.grid.height - 1));
    error_.clear();
}

void Editor::selectAsset(int catalog_index) {
    if (!catalog_ || catalog_->entries().empty()) return;
    asset_index_ = std::clamp(catalog_index, 0, static_cast<int>(catalog_->entries().size()) - 1);
    error_.clear();
}

void Editor::rotate(int direction) {
    yaw_quarter_turns_ = (yaw_quarter_turns_ + direction) % 4;
    if (yaw_quarter_turns_ < 0) yaw_quarter_turns_ += 4;
}

bool Editor::cellAvailable(int x, int y) const {
    if (x < 0 || y < 0 || x >= scene_.grid.width || y >= scene_.grid.height) return false;
    if (terrain::isActualWaterTile(scene_, x, y)) return false;
    if (y < static_cast<int>(scene_.terrain.collision.size()) &&
        x < static_cast<int>(scene_.terrain.collision[y].size()) &&
        scene_.terrain.collision[y][x] != 0) return false;
    return std::none_of(placements_.begin(), placements_.end(), [&](const Placement& placement) {
        return placement.cell_x == x && placement.cell_y == y;
    });
}

std::optional<Placement> Editor::preview() const {
    if (!active_ || !catalog_ || catalog_->entries().empty()) return std::nullopt;
    const Asset& asset = catalog_->entries()[static_cast<std::size_t>(asset_index_)];
    return Placement{"world-preview", asset.id, map_id_, cursor_x_, cursor_y_, yaw_quarter_turns_};
}

bool Editor::place() {
    if (!active_ || !catalog_ || catalog_->entries().empty()) return false;
    if (!cellAvailable(cursor_x_, cursor_y_)) {
        error_ = "That cell cannot hold a decoration";
        return false;
    }
    Placement placement = *preview();
    placement.id = "world-object-" + std::to_string(next_id_++);
    placements_.push_back(std::move(placement));
    dirty_ = true;
    error_.clear();
    return true;
}

bool Editor::erase() {
    const auto found = std::find_if(placements_.begin(), placements_.end(), [&](const Placement& placement) {
        return placement.cell_x == cursor_x_ && placement.cell_y == cursor_y_;
    });
    if (found == placements_.end()) {
        error_ = "There is no decoration on this cell";
        return false;
    }
    placements_.erase(found);
    dirty_ = true;
    error_.clear();
    return true;
}

} // namespace pr::gameplay::world3d::decorations
