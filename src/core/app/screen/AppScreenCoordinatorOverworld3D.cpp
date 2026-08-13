#include "core/app/screen/AppScreenCoordinator.hpp"

#include "ui/Overworld3DTestScreen.hpp"
#include "ui/AttendTestScreen.hpp"
#include "ui/TitleScreen.hpp"

namespace pr {

void AppScreenCoordinator::updateOverworld3D(double dt) {
    overworld3d_test_.update(dt);
    if (overworld3d_test_.consumeOpenAttendRequested()) {
        const auto launch_context = overworld3d_test_.consumeAttendLaunchContext();
        // Keep the overworld GPU scene resident. Attend shares the active bgfx
        // device, and returning can resume this exact renderer immediately.
        attend_return_target_ = AttendReturnTarget::Overworld3D;
        if (launch_context) {
            attend_test_.beginFromOverworld(*launch_context);
        } else {
            attend_test_.beginFromOverworld();
        }
        active_screen_ = ActiveScreen::TestAttend;
        return;
    }
    if (overworld3d_test_.consumeReturnToTitleRequested()) {
        overworld3d_test_.shutdownBgfx();
        title_screen_.returnToMainMenuFromResort();
        active_screen_ = ActiveScreen::Title;
    }
}

} // namespace pr
