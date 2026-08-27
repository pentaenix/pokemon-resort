#pragma once

namespace pr::mapmaker {

// Small input-ownership state machine shared by the native UI and headless
// tests. Play owns movement until Escape or an explicit click outside it.
class PlayInputCapture {
public:
    void setPlayVisible(bool visible);
    void handlePointer(bool viewport_clicked, bool outside_clicked);
    void handleEscape(bool pressed);

    bool captured() const { return captured_; }

private:
    bool play_visible_ = false;
    bool captured_ = false;
};

} // namespace pr::mapmaker
