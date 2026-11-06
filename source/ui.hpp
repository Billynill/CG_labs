#pragma once

#include "scene.hpp"

// Управление сценой: ввод с клавиатуры/мыши и панель ImGui.
namespace ui {

// Поворот камеры: правая кнопка мыши + движение мыши.
// Перемещение: WASD - вперёд/влево/назад/вправо, Q/E - вниз/вверх, Shift - быстрее.
void handleCameraInput(scene::State& state, float delta_time);

void drawControlPanel(scene::State& state);

} // namespace ui
