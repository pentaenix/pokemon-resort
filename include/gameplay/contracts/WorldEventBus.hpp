#pragma once

#include <string>

namespace pr::gameplay::contracts {

struct WorldEvent {
    std::string topic;
    std::string payload;
};

class WorldEventBus {
public:
    virtual ~WorldEventBus() = default;

    virtual void publish(const WorldEvent& event) = 0;
    virtual bool poll(WorldEvent& out_event) = 0;
};

} // namespace pr::gameplay::contracts
