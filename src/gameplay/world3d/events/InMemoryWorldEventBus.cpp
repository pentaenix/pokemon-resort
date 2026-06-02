#include "gameplay/world3d/events/InMemoryWorldEventBus.hpp"

namespace pr::gameplay::world3d::events {

void InMemoryWorldEventBus::publish(const contracts::WorldEvent& event) {
    queue_.push_back(event);
}

bool InMemoryWorldEventBus::poll(contracts::WorldEvent& out_event) {
    if (queue_.empty()) {
        return false;
    }

    out_event = queue_.front();
    queue_.pop_front();
    return true;
}

} // namespace pr::gameplay::world3d::events
