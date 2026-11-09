#pragma once

#include "scene.hpp"

namespace ui {

void handleCameraInput(scene::State& state, float delta_time);

void drawControlPanel(scene::State& state);

} // namespace ui
