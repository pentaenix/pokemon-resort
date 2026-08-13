#include "mapmaker/selection/SelectionModel.hpp"

#include <algorithm>
#include <utility>

namespace pr::mapmaker {

bool SelectionModel::select(SelectionItem item, SelectionMode mode) {
    const auto existing = std::find(items_.begin(), items_.end(), item);

    if (mode == SelectionMode::Replace) {
        if (items_.size() == 1U && existing == items_.begin()) return false;
        items_.assign(1, std::move(item));
        ++revision_;
        return true;
    }

    if (mode == SelectionMode::Toggle && existing != items_.end()) {
        items_.erase(existing);
        ++revision_;
        return true;
    }

    if (existing == items_.end()) {
        items_.push_back(std::move(item));
        ++revision_;
        return true;
    }

    // Re-selecting an item makes it primary without changing the selection set.
    if (existing != std::prev(items_.end())) {
        SelectionItem selected = std::move(*existing);
        items_.erase(existing);
        items_.push_back(std::move(selected));
        ++revision_;
        return true;
    }
    return false;
}

bool SelectionModel::clear() {
    if (items_.empty()) return false;
    items_.clear();
    ++revision_;
    return true;
}

bool SelectionModel::erase(const SelectionItem& item) {
    const auto found = std::find(items_.begin(), items_.end(), item);
    if (found == items_.end()) return false;
    items_.erase(found);
    ++revision_;
    return true;
}

bool SelectionModel::retainMap(const std::string& map_id) {
    const auto first_removed = std::remove_if(
        items_.begin(), items_.end(), [&](const SelectionItem& item) {
            return item.map_id != map_id;
        });
    if (first_removed == items_.end()) return false;
    items_.erase(first_removed, items_.end());
    ++revision_;
    return true;
}

bool SelectionModel::contains(const SelectionItem& item) const {
    return std::find(items_.begin(), items_.end(), item) != items_.end();
}

std::optional<SelectionItem> SelectionModel::primary() const {
    return items_.empty() ? std::nullopt : std::optional<SelectionItem>(items_.back());
}

} // namespace pr::mapmaker
