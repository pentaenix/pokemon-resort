#pragma once

namespace pr {

class ScreenInput {
public:
    virtual ~ScreenInput() = default;

    virtual bool canNavigate() const { return false; }
    virtual void onNavigate(int delta) { (void)delta; }

    /// Optional 2D navigation (controllers / d-pad / left-right UI graphs).
    virtual bool canNavigate2d() const { return false; }
    virtual void onNavigate2d(int dx, int dy) {
        (void)dx;
        (void)dy;
    }

    virtual bool acceptsAdvanceInput() const { return true; }
    virtual void onAdvancePressed() {}
    virtual void onBackPressed() {}

    virtual void handlePointerMoved(int logical_x, int logical_y) {
        (void)logical_x;
        (void)logical_y;
    }

    virtual bool handlePointerPressed(int logical_x, int logical_y) {
        (void)logical_x;
        (void)logical_y;
        return false;
    }

    virtual bool handlePointerReleased(int logical_x, int logical_y) {
        (void)logical_x;
        (void)logical_y;
        return false;
    }
};

} // namespace pr
