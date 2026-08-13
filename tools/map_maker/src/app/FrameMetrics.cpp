#include "mapmaker/app/FrameMetrics.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace pr::mapmaker {

void FrameMetrics::recordFrame(double frame_ms) {
    frame_ms_[next_] = std::max(0.0, frame_ms);
    next_ = (next_ + 1U) % kCapacity;
    count_ = std::min(kCapacity, count_ + 1U);
}

void FrameMetrics::recordInputLatency(double latency_ms) {
    const double sample = std::max(0.0, latency_ms);
    input_latency_ms_ = input_latency_ms_ == 0.0
        ? sample
        : (input_latency_ms_ * 0.85) + (sample * 0.15);
}

double FrameMetrics::fps() const {
    if (count_ == 0) return 0.0;
    double total = 0.0;
    for (std::size_t index = 0; index < count_; ++index) total += frame_ms_[index];
    return total <= 0.0 ? 0.0 : 1000.0 * static_cast<double>(count_) / total;
}

double FrameMetrics::frameMillisecondsP95() const {
    if (count_ == 0) return 0.0;
    std::vector<double> sorted(frame_ms_.begin(), frame_ms_.begin() + static_cast<std::ptrdiff_t>(count_));
    std::sort(sorted.begin(), sorted.end());
    const std::size_t index = std::min(
        sorted.size() - 1U,
        static_cast<std::size_t>(static_cast<double>(sorted.size() - 1U) * 0.95));
    return sorted[index];
}

} // namespace pr::mapmaker
