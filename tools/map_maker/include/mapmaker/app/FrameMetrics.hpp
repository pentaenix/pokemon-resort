#pragma once

#include <array>
#include <cstddef>

namespace pr::mapmaker {

class FrameMetrics {
public:
    void recordFrame(double frame_ms);
    void recordInputLatency(double latency_ms);

    double fps() const;
    double frameMillisecondsP95() const;
    double inputLatencyMilliseconds() const { return input_latency_ms_; }

private:
    static constexpr std::size_t kCapacity = 180;
    std::array<double, kCapacity> frame_ms_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
    double input_latency_ms_ = 0.0;
};

} // namespace pr::mapmaker
