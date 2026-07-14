#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/Overworld3DTestScreen.hpp"
#include "ui/AttendTestScreen.hpp"
#include "ui/TitleScreen.hpp"

namespace pr {

void AppScreenCoordinator::updateOverworld3D(double dt) {
    overworld3d_test_.update(dt);
    if (overworld3d_test_.consumeOpenAttendRequested()) {
        overworld3d_test_.shutdownBgfx();
        attend_return_target_ = AttendReturnTarget::Overworld3D;
        attend_test_.beginFromOverworld();
        active_screen_ = ActiveScreen::TestAttend;
        return;
    }
    if (overworld3d_test_.consumeReturnToTitleRequested()) {
        overworld3d_test_.shutdownBgfx();
        overworld3d_test_.resetForNextLaunch();
        title_screen_.returnToMainMenuFromResort();
        active_screen_ = ActiveScreen::Title;
    }
}

} // namespace pr
