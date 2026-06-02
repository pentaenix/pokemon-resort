#pragma once

#include "gameplay/contracts/WorldEventBus.hpp"

#include <deque>

namespace pr::gameplay::world3d::events {

class InMemoryWorldEventBus final : public contracts::WorldEventBus {
public:
    void publish(const contracts::WorldEvent& event) override;
    bool poll(contracts::WorldEvent& out_event) override;

private:
    std::deque<contracts::WorldEvent> queue_;
};

} // namespace pr::gameplay::world3d::events
