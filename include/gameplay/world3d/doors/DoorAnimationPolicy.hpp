#pragma once

#include <algorithm>

namespace pr::gameplay::world3d::doors {

// Ambient RTPKS material motion may loop. A script-triggered door action is
// always a one-shot and holds its terminal frame until another action changes it.
inline bool animationLoops(bool trigger_phase, bool authored_loop) {
    return !trigger_phase && authored_loop;
}

inline double animationSample(bool trigger_phase, double raw_sample, int frame_count) {
    raw_sample = std::max(0.0, raw_sample);
    if (!trigger_phase) return raw_sample;
    return std::min(raw_sample, static_cast<double>(std::max(1, frame_count) - 1));
}

// Nitro door motion advances through a texture strip. During an explicit door
// action its edges must clamp, otherwise the strip wraps and looks like an
// endlessly scrolling row of doors instead of one panel retracting.
inline bool clampsTextureEdges(bool trigger_phase) {
    return trigger_phase;
}

inline float animationUvOffset(bool trigger_phase, float authored_translation) {
    return trigger_phase ? authored_translation : -authored_translation;
}

inline bool playerUsesMovementAnimation(bool door_controlled, bool physically_moving) {
    return physically_moving && !door_controlled;
}

} // namespace pr::gameplay::world3d::doors
