#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace pr::mapmaker {

enum class SelectionKind {
    TerrainCell,
    Model,
    DoorTrigger,
    Anchor,
    MapEntry,
};

enum class SelectionMode {
    Replace,
    Add,
    Toggle,
};

struct SelectionItem {
    SelectionKind kind = SelectionKind::TerrainCell;
    std::string map_id;
    std::string object_id;
    int tile_x = 0;
    int tile_y = 0;
    int layer = 0;
    // Model IDs are authored labels and are not guaranteed unique. The source
    // array index keeps direct manipulation attached to the instance clicked.
    std::size_t metadata_index = std::numeric_limits<std::size_t>::max();

    friend bool operator==(const SelectionItem&, const SelectionItem&) = default;
};

class SelectionModel {
public:
    bool select(SelectionItem item, SelectionMode mode = SelectionMode::Replace);
    bool clear();
    bool erase(const SelectionItem& item);
    bool retainMap(const std::string& map_id);

    bool empty() const { return items_.empty(); }
    bool contains(const SelectionItem& item) const;
    const std::vector<SelectionItem>& items() const { return items_; }
    std::optional<SelectionItem> primary() const;
    std::uint64_t revision() const { return revision_; }

private:
    std::vector<SelectionItem> items_;
    std::uint64_t revision_ = 0;
};

} // namespace pr::mapmaker
