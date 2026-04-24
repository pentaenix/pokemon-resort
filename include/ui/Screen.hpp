#pragma once

#include "ui/ScreenInput.hpp"

#include <SDL.h>

namespace pr {

class Screen : public ScreenInput {
public:
    ~Screen() override = default;

    virtual void update(double dt) = 0;
    virtual void render(SDL_Renderer* renderer) = 0;
};

} // namespace pr
