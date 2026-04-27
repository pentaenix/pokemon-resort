#pragma once

#include <SDL.h>

namespace pr::transfer_system::move {

struct HoldWithinRect {
    bool active = false;
    bool triggered = false;
    double elapsed_seconds = 0.0;
    double hold_seconds = 0.0;
    SDL_Point start_pointer{0, 0};
    SDL_Rect rect{0, 0, 0, 0};
    int cancel_move_threshold_px = 6;

    void start(SDL_Point pointer, const SDL_Rect& bounds, double seconds) {
        active = true;
        triggered = false;
        elapsed_seconds = 0.0;
        hold_seconds = seconds;
        start_pointer = pointer;
        rect = bounds;
    }

    void cancel() {
        active = false;
        triggered = false;
        elapsed_seconds = 0.0;
        hold_seconds = 0.0;
        rect = SDL_Rect{0, 0, 0, 0};
    }

    bool contains(SDL_Point p) const {
        return p.x >= rect.x && p.x < rect.x + rect.w && p.y >= rect.y && p.y < rect.y + rect.h;
    }

    bool update(double dt, SDL_Point pointer) {
        if (!active || triggered) {
            return false;
        }
        if (hold_seconds <= 1e-6) {
            cancel();
            return false;
        }
        if (!contains(pointer)) {
            cancel();
            return false;
        }
        const int dx = pointer.x - start_pointer.x;
        const int dy = pointer.y - start_pointer.y;
        const int thresh = cancel_move_threshold_px;
        if (dx * dx + dy * dy >= thresh * thresh) {
            cancel();
            return false;
        }
        elapsed_seconds += dt;
        if (elapsed_seconds >= hold_seconds) {
            triggered = true;
            return true;
        }
        return false;
    }
};

struct DragThreshold {
    bool active = false;
    SDL_Point start{0, 0};
    int start_threshold_px = 7;

    void startDrag(SDL_Point p) {
        active = true;
        start = p;
    }

    void cancel() { active = false; }

    bool movedFar(SDL_Point p) const {
        if (!active) return false;
        const int dx = p.x - start.x;
        const int dy = p.y - start.y;
        const int t = start_threshold_px;
        return dx * dx + dy * dy >= t * t;
    }
};

} // namespace pr::transfer_system::move

