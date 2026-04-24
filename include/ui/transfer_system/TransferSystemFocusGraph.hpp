#pragma once

#include "ui/FocusManager.hpp"

#include <vector>

namespace pr::transfer_system {

/// Applies deterministic directional edges for the transfer screen focus graph.
/// Keeps navigation topology out of `TransferSystemScreen.cpp` so future controls
/// can be added without editing the screen's rendering/input implementation file.
void applyTransferSystemFocusEdges(std::vector<FocusNode>& nodes);

} // namespace pr::transfer_system
