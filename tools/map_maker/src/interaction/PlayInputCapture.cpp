#include "mapmaker/interaction/PlayInputCapture.hpp"

namespace pr::mapmaker {

void PlayInputCapture::setPlayVisible(bool visible) {
    if (visible && !play_visible_) captured_ = true;
    if (!visible) captured_ = false;
    play_visible_ = visible;
}

void PlayInputCapture::handlePointer(bool viewport_clicked, bool outside_clicked) {
    if (!play_visible_) return;
    if (viewport_clicked) captured_ = true;
    else if (outside_clicked) captured_ = false;
}

void PlayInputCapture::handleEscape(bool pressed) {
    if (play_visible_ && pressed) captured_ = false;
}

} // namespace pr::mapmaker
