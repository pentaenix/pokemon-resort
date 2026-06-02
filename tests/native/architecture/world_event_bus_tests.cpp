#include "gameplay/world3d/events/InMemoryWorldEventBus.hpp"

#include <cassert>

int main() {
    pr::gameplay::world3d::events::InMemoryWorldEventBus bus;
    pr::gameplay::contracts::WorldEvent out{};

    assert(!bus.poll(out));

    bus.publish({.topic = "world.player.spawned", .payload = "{\"id\":\"p1\"}"});
    assert(bus.poll(out));
    assert(out.topic == "world.player.spawned");

    return 0;
}
