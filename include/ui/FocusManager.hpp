#pragma once

#include <SDL.h>
#include <array>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

namespace pr {

using FocusNodeId = int;

/// Neighbor slot order: Left (−x), Right (+x), Up (−y), Down (+y) — matches `onNavigate2d` signs.
inline constexpr int kFocusNeighborLeft = 0;
inline constexpr int kFocusNeighborRight = 1;
inline constexpr int kFocusNeighborUp = 2;
inline constexpr int kFocusNeighborDown = 3;

struct FocusNode {
    FocusNodeId id = 0;
    /// Returns bounds in logical coordinates; std::nullopt = not currently focusable/visible.
    std::function<std::optional<SDL_Rect>()> bounds;
    /// Called when "accept"/activate is pressed on this node.
    std::function<void()> activate;
    /// Optional override: return true if handled (e.g. carousel cycles on left/right).
    std::function<bool(int dx, int dy)> navigate_override;
    /// Explicit focus targets per direction. When set for the requested direction, that edge is
    /// followed instead of spatial search. When unset and explicit-only mode is on, that direction is a no-op.
    std::array<std::optional<FocusNodeId>, 4> neighbors{};
};

/// Focus navigation: optional toroidal spatial search as a fallback, plus per-node overrides and
/// explicit directional edges (recommended for predictable multi-panel UIs).
class FocusManager {
public:
    void setNodes(std::vector<FocusNode> nodes);
    void setCurrent(FocusNodeId id);
    FocusNodeId current() const { return current_id_; }

    /// When true, `navigate` only follows `FocusNode::neighbors` (and overrides). No spatial fallback.
    void setExplicitNavigationOnly(bool enabled) { explicit_only_ = enabled; }

    std::optional<SDL_Rect> currentBounds() const;

    void navigate(int dx, int dy, int screen_w, int screen_h);
    void activate();

private:
    const FocusNode* findNode(FocusNodeId id) const;
    FocusNode* findNode(FocusNodeId id);

    std::vector<FocusNode> nodes_;
    std::unordered_map<FocusNodeId, std::size_t> index_by_id_;
    FocusNodeId current_id_ = 0;
    bool explicit_only_ = false;
};

} // namespace pr

