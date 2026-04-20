#include "ui/FocusManager.hpp"

#include <algorithm>
#include <cmath>

namespace pr {

namespace {

int centerX(const SDL_Rect& r) { return r.x + r.w / 2; }
int centerY(const SDL_Rect& r) { return r.y + r.h / 2; }

// Minimal absolute distance on a torus.
int torusAbsDelta(int a, int b, int span) {
    if (span <= 0) {
        return std::abs(a - b);
    }
    const int d = std::abs(a - b) % span;
    return std::min(d, span - d);
}

// Positive travel in a direction on a torus (0..span-1). 0 means same coordinate.
int torusForwardDelta(int from, int to, int span) {
    if (span <= 0) {
        return std::max(0, to - from);
    }
    int d = (to - from) % span;
    if (d < 0) d += span;
    return d;
}

int torusBackwardDelta(int from, int to, int span) {
    if (span <= 0) {
        return std::max(0, from - to);
    }
    int d = (from - to) % span;
    if (d < 0) d += span;
    return d;
}

} // namespace

void FocusManager::setNodes(std::vector<FocusNode> nodes) {
    nodes_ = std::move(nodes);
    index_by_id_.clear();
    index_by_id_.reserve(nodes_.size());
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
        index_by_id_[nodes_[i].id] = i;
    }
    if (!findNode(current_id_) && !nodes_.empty()) {
        current_id_ = nodes_[0].id;
    }
}

void FocusManager::setCurrent(FocusNodeId id) {
    if (findNode(id)) {
        current_id_ = id;
    }
}

const FocusNode* FocusManager::findNode(FocusNodeId id) const {
    auto it = index_by_id_.find(id);
    if (it == index_by_id_.end()) return nullptr;
    return &nodes_[it->second];
}

FocusNode* FocusManager::findNode(FocusNodeId id) {
    auto it = index_by_id_.find(id);
    if (it == index_by_id_.end()) return nullptr;
    return &nodes_[it->second];
}

std::optional<SDL_Rect> FocusManager::currentBounds() const {
    const FocusNode* n = findNode(current_id_);
    if (!n || !n->bounds) return std::nullopt;
    return n->bounds();
}

void FocusManager::activate() {
    FocusNode* n = findNode(current_id_);
    if (n && n->activate) {
        n->activate();
    }
}

void FocusManager::navigate(int dx, int dy, int screen_w, int screen_h) {
    dx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    dy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    if (dx == 0 && dy == 0) return;

    FocusNode* cur = findNode(current_id_);
    if (!cur) return;

    if (cur->navigate_override) {
        if (cur->navigate_override(dx, dy)) {
            return;
        }
    }

    int dir = -1;
    if (dx < 0) {
        dir = kFocusNeighborLeft;
    } else if (dx > 0) {
        dir = kFocusNeighborRight;
    } else if (dy < 0) {
        dir = kFocusNeighborUp;
    } else if (dy > 0) {
        dir = kFocusNeighborDown;
    }
    if (dir >= 0 && dir < 4) {
        const auto& nn = cur->neighbors[static_cast<std::size_t>(dir)];
        if (nn.has_value()) {
            FocusNode* next = findNode(*nn);
            if (next) {
                current_id_ = *nn;
            }
            return;
        }
    }

    if (explicit_only_) {
        return;
    }

    const std::optional<SDL_Rect> cur_bounds = cur->bounds ? cur->bounds() : std::nullopt;
    if (!cur_bounds) return;
    const int cx = centerX(*cur_bounds);
    const int cy = centerY(*cur_bounds);

    struct Candidate {
        FocusNodeId id;
        int score;
    };

    Candidate best{current_id_, 0};
    bool found = false;

    for (const FocusNode& n : nodes_) {
        if (n.id == current_id_ || !n.bounds) continue;
        const std::optional<SDL_Rect> b = n.bounds();
        if (!b) continue;
        const int nx = centerX(*b);
        const int ny = centerY(*b);

        int primary = 0;
        int perp = 0;
        if (dx != 0) {
            primary = dx > 0 ? torusForwardDelta(cx, nx, screen_w) : torusBackwardDelta(cx, nx, screen_w);
            if (primary == 0) continue;
            perp = torusAbsDelta(cy, ny, screen_h);
        } else {
            primary = dy > 0 ? torusForwardDelta(cy, ny, screen_h) : torusBackwardDelta(cy, ny, screen_h);
            if (primary == 0) continue;
            perp = torusAbsDelta(cx, nx, screen_w);
        }

        // Strongly prefer nearer in the intended direction; tie-break by perpendicular proximity.
        const int score = primary * 10000 + perp;
        if (!found || score < best.score) {
            found = true;
            best = Candidate{n.id, score};
        }
    }

    if (found) {
        current_id_ = best.id;
    }
}

} // namespace pr

