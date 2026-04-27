#include "ui/transfer_system/TransferSystemFocusGraph.hpp"

namespace pr::transfer_system {

void applyTransferSystemFocusEdges(std::vector<FocusNode>& nodes) {
    auto byId = [&](FocusNodeId id) -> FocusNode* {
        for (auto& node : nodes) {
            if (node.id == id) {
                return &node;
            }
        }
        return nullptr;
    };

    auto connect = [&](FocusNodeId from, int dir, FocusNodeId to) {
        FocusNode* source = byId(from);
        FocusNode* target = byId(to);
        if (!source || !target) {
            return;
        }
        source->neighbors[static_cast<std::size_t>(dir)] = to;
    };

    constexpr int kCols = 6;
    constexpr FocusNodeId kRes0 = 1000;
    constexpr FocusNodeId kGame0 = 2000;
    constexpr FocusNodeId kRPrev = 1101;
    constexpr FocusNodeId kRName = 1102;
    constexpr FocusNodeId kRNext = 1103;
    constexpr FocusNodeId kRBoxSpace = 1110;
    constexpr FocusNodeId kRIcon = 1111;
    constexpr FocusNodeId kRScroll = 1112;
    constexpr FocusNodeId kGPrev = 2101;
    constexpr FocusNodeId kGName = 2102;
    constexpr FocusNodeId kGNext = 2103;
    constexpr FocusNodeId kGBoxSpace = 2110;
    constexpr FocusNodeId kGIcon = 2111;
    constexpr FocusNodeId kExit = 5000;
    constexpr FocusNodeId kCarousel = 3000;
    constexpr FocusNodeId kPill = 4000;

    auto resortSlot = [&](int index) -> FocusNodeId { return kRes0 + index; };
    auto gameSlot = [&](int index) -> FocusNodeId { return kGame0 + index; };

    const bool hasGameGrid = byId(gameSlot(0)) != nullptr;
    const bool hasResortGrid = byId(resortSlot(0)) != nullptr;

    if (hasResortGrid) {
        for (int i = 0; i < 30; ++i) {
            const int row = i / kCols;
            const int col = i % kCols;
            const FocusNodeId id = resortSlot(i);

            if (col > 0) {
                connect(id, kFocusNeighborLeft, resortSlot(i - 1));
            } else if (hasGameGrid) {
                connect(id, kFocusNeighborLeft, gameSlot(row * kCols + (kCols - 1)));
            } else {
                connect(id, kFocusNeighborLeft, resortSlot(row * kCols + (kCols - 1)));
            }

            if (col < kCols - 1) {
                connect(id, kFocusNeighborRight, resortSlot(i + 1));
            } else if (hasGameGrid) {
                connect(id, kFocusNeighborRight, gameSlot(row * kCols));
            } else {
                connect(id, kFocusNeighborRight, resortSlot(row * kCols));
            }

            if (row > 0) {
                connect(id, kFocusNeighborUp, resortSlot(i - kCols));
            } else if (col == 0) {
                connect(id, kFocusNeighborUp, kRPrev);
            } else if (col == kCols - 1) {
                connect(id, kFocusNeighborUp, kRNext);
            } else {
                connect(id, kFocusNeighborUp, kRName);
            }

            if (row < 4) {
                connect(id, kFocusNeighborDown, resortSlot(i + kCols));
            } else if (col == 0) {
                connect(id, kFocusNeighborDown, kRIcon);
            } else if (col == kCols - 1) {
                connect(id, kFocusNeighborDown, kRBoxSpace);
            } else {
                connect(id, kFocusNeighborDown, kRScroll);
            }
        }

        connect(kRPrev, kFocusNeighborRight, kRName);
        connect(kRName, kFocusNeighborLeft, kRPrev);
        connect(kRName, kFocusNeighborRight, kRNext);
        connect(kRNext, kFocusNeighborLeft, kRName);

        if (hasGameGrid) {
            connect(kRPrev, kFocusNeighborLeft, kGNext);
            connect(kRNext, kFocusNeighborRight, kGPrev);
        } else {
            connect(kRPrev, kFocusNeighborLeft, kRNext);
            connect(kRNext, kFocusNeighborRight, kRPrev);
        }

        if (byId(kExit)) {
            connect(kRPrev, kFocusNeighborUp, kExit);
            connect(kExit, kFocusNeighborDown, kRPrev);
            connect(kExit, kFocusNeighborUp, kRIcon);
            connect(kExit, kFocusNeighborRight, kCarousel);
            connect(kCarousel, kFocusNeighborLeft, kExit);
        } else {
            connect(kRPrev, kFocusNeighborUp, kCarousel);
        }
        connect(kRName, kFocusNeighborUp, kCarousel);
        connect(kRNext, kFocusNeighborUp, kCarousel);

        connect(kRPrev, kFocusNeighborDown, resortSlot(0));
        connect(kRName, kFocusNeighborDown, resortSlot(2));
        connect(kRNext, kFocusNeighborDown, resortSlot(kCols - 1));

        connect(kRIcon, kFocusNeighborRight, kRScroll);
        connect(kRScroll, kFocusNeighborLeft, kRIcon);
        connect(kRScroll, kFocusNeighborRight, kRBoxSpace);
        connect(kRBoxSpace, kFocusNeighborLeft, kRScroll);
        if (hasGameGrid) {
            connect(kRIcon, kFocusNeighborLeft, kGIcon);
            connect(kRBoxSpace, kFocusNeighborRight, kGBoxSpace);
        } else {
            connect(kRIcon, kFocusNeighborLeft, kRBoxSpace);
            connect(kRBoxSpace, kFocusNeighborRight, kRIcon);
        }

        connect(kRIcon, kFocusNeighborUp, resortSlot(24));
        connect(kRScroll, kFocusNeighborUp, resortSlot(26));
        connect(kRBoxSpace, kFocusNeighborUp, resortSlot(29));
    }

    if (hasGameGrid) {
        for (int i = 0; i < 30; ++i) {
            const int row = i / kCols;
            const int col = i % kCols;
            const FocusNodeId id = gameSlot(i);

            if (col > 0) {
                connect(id, kFocusNeighborLeft, gameSlot(i - 1));
            } else if (hasResortGrid) {
                connect(id, kFocusNeighborLeft, resortSlot(row * kCols + (kCols - 1)));
            } else {
                connect(id, kFocusNeighborLeft, gameSlot(row * kCols + (kCols - 1)));
            }

            if (col < kCols - 1) {
                connect(id, kFocusNeighborRight, gameSlot(i + 1));
            } else if (hasResortGrid) {
                connect(id, kFocusNeighborRight, resortSlot(row * kCols));
            } else {
                connect(id, kFocusNeighborRight, gameSlot(row * kCols));
            }

            if (row > 0) {
                connect(id, kFocusNeighborUp, gameSlot(i - kCols));
            } else if (col == 0) {
                connect(id, kFocusNeighborUp, kGPrev);
            } else if (col == kCols - 1) {
                connect(id, kFocusNeighborUp, kGNext);
            } else {
                connect(id, kFocusNeighborUp, kGName);
            }

            if (row < 4) {
                connect(id, kFocusNeighborDown, gameSlot(i + kCols));
            } else if (col <= 2) {
                connect(id, kFocusNeighborDown, kGBoxSpace);
            } else {
                connect(id, kFocusNeighborDown, kGIcon);
            }
        }

        connect(kGPrev, kFocusNeighborRight, kGName);
        connect(kGName, kFocusNeighborLeft, kGPrev);
        connect(kGName, kFocusNeighborRight, kGNext);
        connect(kGNext, kFocusNeighborLeft, kGName);

        if (hasResortGrid) {
            connect(kGPrev, kFocusNeighborLeft, kRNext);
            connect(kGNext, kFocusNeighborRight, kRPrev);
        } else {
            connect(kGPrev, kFocusNeighborLeft, kGNext);
            connect(kGNext, kFocusNeighborRight, kGPrev);
        }

        connect(kGPrev, kFocusNeighborUp, kPill);
        connect(kGName, kFocusNeighborUp, kPill);
        connect(kGNext, kFocusNeighborUp, kPill);

        connect(kGPrev, kFocusNeighborDown, gameSlot(0));
        connect(kGName, kFocusNeighborDown, gameSlot(2));
        connect(kGNext, kFocusNeighborDown, gameSlot(kCols - 1));

        if (hasResortGrid) {
            // Cross-panel footer connections.
            connect(kGBoxSpace, kFocusNeighborLeft, kRBoxSpace);
            connect(kRBoxSpace, kFocusNeighborRight, kGBoxSpace);
            connect(kRIcon, kFocusNeighborLeft, kGIcon);
            connect(kGIcon, kFocusNeighborRight, kRIcon);
            // Footer ring:
            // Resort icon -> resort scroll/down -> resort boxspace -> game boxspace -> game icon -> (wrap) resort icon.
            connect(kGBoxSpace, kFocusNeighborLeft, kRBoxSpace);
            connect(kGBoxSpace, kFocusNeighborRight, kGIcon);
            connect(kGIcon, kFocusNeighborLeft, kGBoxSpace);
            connect(kGIcon, kFocusNeighborRight, kRIcon);
        } else {
            connect(kGBoxSpace, kFocusNeighborRight, kGIcon);
            connect(kGIcon, kFocusNeighborLeft, kGBoxSpace);
            connect(kGBoxSpace, kFocusNeighborLeft, kGIcon);
            connect(kGIcon, kFocusNeighborRight, kGBoxSpace);
        }

        connect(kGBoxSpace, kFocusNeighborUp, gameSlot(24));
        connect(kGIcon, kFocusNeighborUp, gameSlot(29));
    }

    if (byId(kCarousel) && byId(kRName)) {
        connect(kCarousel, kFocusNeighborDown, kRName);
    }
    if (byId(kPill) && byId(kGName)) {
        connect(kPill, kFocusNeighborDown, kGName);
    }
    if (byId(kCarousel) && byId(kRBoxSpace)) {
        connect(kCarousel, kFocusNeighborUp, kRBoxSpace);
    }
    if (byId(kPill) && byId(kGBoxSpace)) {
        connect(kPill, kFocusNeighborUp, kGBoxSpace);
    }
    if (byId(kCarousel)) {
        if (byId(kRIcon)) {
            connect(kRIcon, kFocusNeighborDown, kCarousel);
        }
        if (byId(kRScroll)) {
            connect(kRScroll, kFocusNeighborDown, kCarousel);
        }
        if (byId(kRBoxSpace)) {
            connect(kRBoxSpace, kFocusNeighborDown, kCarousel);
        }
    }
    if (byId(kPill)) {
        if (byId(kGIcon)) {
            connect(kGIcon, kFocusNeighborDown, kPill);
        }
        if (byId(kGBoxSpace)) {
            connect(kGBoxSpace, kFocusNeighborDown, kPill);
        }
    }
}

} // namespace pr::transfer_system
