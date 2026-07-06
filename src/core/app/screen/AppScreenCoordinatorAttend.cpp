#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/AttendTestScreen.hpp"
#include "ui/TitleScreen.hpp"

namespace pr {

void AppScreenCoordinator::updateTestAttend(double dt) {
    attend_test_.update(dt);
    if (!attend_test_.consumeReturnRequested()) {
        return;
    }

    switch (attend_return_target_) {
        case AttendReturnTarget::TitleResortMenu:
            attend_test_.shutdownBgfx();
            title_screen_.returnToResortMenuFromAttend();
            active_screen_ = ActiveScreen::Title;
            break;
        case AttendReturnTarget::Overworld3D:
            attend_test_.shutdownBgfx();
            active_screen_ = ActiveScreen::Overworld3DTest;
            break;
    }
}

} // namespace pr
