#pragma once

#include <windows.h>
#include "../../Common/GameTimer.h"

// Система управления состояниями игры (Паттерн "Состояния")
class GameStateManager;
class RenderAdapter;

class GameState {
public:
    virtual ~GameState() = default;

    // Инициализация при входе в состояние
    virtual void OnEnter() {}
    
    // Очистка при выходе из состояния
    virtual void OnExit() {}

    // Обновление логики состояния
    virtual void Update(const GameTimer& gt, GameStateManager* stateManager) = 0;

    // Отрисовка состояния
    virtual void Draw(const GameTimer& gt, RenderAdapter* renderer = nullptr) = 0;

    // Обработка ввода мыши и клавиатуры
    virtual void OnMouseDown(WPARAM btnState, int x, int y) {}
    virtual void OnMouseUp(WPARAM btnState, int x, int y) {}
    virtual void OnMouseMove(WPARAM btnState, int x, int y) {}
    virtual void ProcessKeyboardInput(const GameTimer& gt) {}
};
