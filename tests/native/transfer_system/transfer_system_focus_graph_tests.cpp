#include "ui/transfer_system/TransferSystemFocusGraph.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pr::FocusNode makeNode(pr::FocusNodeId id) {
    pr::FocusNode node;
    node.id = id;
    return node;
}

const std::optional<pr::FocusNodeId>& neighborOf(
    const std::vector<pr::FocusNode>& nodes,
    pr::FocusNodeId id,
    int direction) {
    for (const auto& node : nodes) {
        if (node.id == id) {
            return node.neighbors[static_cast<std::size_t>(direction)];
        }
    }
    static const std::optional<pr::FocusNodeId> kMissing;
    return kMissing;
}

std::vector<pr::FocusNode> makeFullTransferNodes() {
    std::vector<pr::FocusNode> nodes;
    for (int i = 0; i < 30; ++i) {
        nodes.push_back(makeNode(1000 + i));
        nodes.push_back(makeNode(2000 + i));
    }
    for (pr::FocusNodeId id : {1101, 1102, 1103, 1110, 1111, 1112, 2101, 2102, 2103, 2110, 2111, 3000, 4000}) {
        nodes.push_back(makeNode(id));
    }
    return nodes;
}

void testGridWrapsAcrossPanels() {
    auto nodes = makeFullTransferNodes();
    pr::transfer_system::applyTransferSystemFocusEdges(nodes);

    expect(neighborOf(nodes, 1000, pr::kFocusNeighborLeft) == 2005,
           "left from resort slot 0 should wrap to the right edge of the game grid row");
    expect(neighborOf(nodes, 2005, pr::kFocusNeighborRight) == 1000,
           "right from game slot 5 should wrap to the left edge of the resort grid row");
}

void testTopChromeConnectsIntoGridAndBack() {
    auto nodes = makeFullTransferNodes();
    pr::transfer_system::applyTransferSystemFocusEdges(nodes);

    expect(neighborOf(nodes, 2102, pr::kFocusNeighborUp) == 4000,
           "game name plate should navigate up to the pill toggle");
    expect(neighborOf(nodes, 4000, pr::kFocusNeighborDown) == 2102,
           "pill toggle should navigate down back into the game name plate");
    expect(neighborOf(nodes, 3000, pr::kFocusNeighborDown) == 1102,
           "carousel should navigate down into the resort name plate");
}

void testFooterRowsUseCrossWindowWrap() {
    auto nodes = makeFullTransferNodes();
    pr::transfer_system::applyTransferSystemFocusEdges(nodes);

    expect(neighborOf(nodes, 2110, pr::kFocusNeighborRight) == 2111,
           "game Box Space footer should navigate right to the game icon footer");
    expect(neighborOf(nodes, 2111, pr::kFocusNeighborLeft) == 2110,
           "game icon footer should navigate left to the game Box Space footer");
    expect(neighborOf(nodes, 1110, pr::kFocusNeighborRight) == 2110,
           "resort Box Space footer should navigate right to the game Box Space footer");
    expect(neighborOf(nodes, 2111, pr::kFocusNeighborRight) == 1111,
           "game icon footer should wrap right to the resort icon footer");
    expect(neighborOf(nodes, 1111, pr::kFocusNeighborLeft) == 2111,
           "resort icon footer should wrap left to the game icon footer");
    expect(neighborOf(nodes, 2110, pr::kFocusNeighborDown) == 4000,
           "game Box Space footer should navigate down to the pill toggle");
}

void testMissingPanelsProduceLocalFallbacks() {
    std::vector<pr::FocusNode> nodes;
    for (int i = 0; i < 30; ++i) {
        nodes.push_back(makeNode(2000 + i));
    }
    for (pr::FocusNodeId id : {2101, 2102, 2103, 2110, 2111, 3000, 4000}) {
        nodes.push_back(makeNode(id));
    }

    pr::transfer_system::applyTransferSystemFocusEdges(nodes);

    expect(neighborOf(nodes, 2110, pr::kFocusNeighborRight) == 2111,
           "without a resort panel, game footer navigation should stay within the game footer row");
    expect(neighborOf(nodes, 2101, pr::kFocusNeighborLeft) == 2103,
           "without a resort panel, game prev should wrap locally to game next");
}

} // namespace

int main() {
    testGridWrapsAcrossPanels();
    testTopChromeConnectsIntoGridAndBack();
    testFooterRowsUseCrossWindowWrap();
    testMissingPanelsProduceLocalFallbacks();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
