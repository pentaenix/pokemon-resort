#pragma once

#include "ui/Screen.hpp"

#include <string>

namespace pr {

enum class LoadingScreenType {
    Pokeball,
    QuickBoatPass,
    ResortTransfer
};

class LoadingScreenBase : public Screen {
public:
    ~LoadingScreenBase() override = default;

    virtual LoadingScreenType loadingScreenType() const = 0;
    virtual void setLoadingMessageKey(const std::string& message_key) { (void)message_key; }
    virtual void setMinimumLoopSeconds(double minimum_loop_seconds) { (void)minimum_loop_seconds; }
    virtual void enter() = 0;
    virtual void enterWithMessageKey(const std::string& message_key, double minimum_loop_seconds = -1.0) {
        setLoadingMessageKey(message_key);
        setMinimumLoopSeconds(minimum_loop_seconds);
        enter();
    }
    virtual void beginLoadingWithMessageKey(const std::string& message_key, double minimum_loop_seconds = -1.0) {
        enterWithMessageKey(message_key, minimum_loop_seconds);
        onAdvancePressed();
    }
    virtual void beginQuickPass(bool wait_for_completion = false) {
        (void)wait_for_completion;
        enter();
    }
    virtual void markLoadingComplete() {}
    virtual bool isLoadingAnimationComplete() const { return false; }
    virtual bool consumeReturnToMenuRequest() { return false; }
};

} // namespace pr
