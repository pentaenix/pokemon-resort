#pragma once

namespace pr {

class ScreenInput {
public:
    virtual ~ScreenInput() = default;

    virtual bool canNavigate() const { return false; }
    virtual void onNavigate(int delta) { (void)delta; }

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
