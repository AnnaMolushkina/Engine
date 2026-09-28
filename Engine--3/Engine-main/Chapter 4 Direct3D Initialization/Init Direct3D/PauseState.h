#pragma once

#include "GameState.h"
#include "GameStateManager.h"
#include "Logger.h"
#include "MainMenuState.h"
#include "Input.h"
#include <memory>

class PauseState : public GameState {
public:
    virtual void OnEnter() override {
        Logger::Info("=== PAUSED ===");
        Logger::Info("Press P to resume.");
        Logger::Info("Press BACKSPACE to return to Main Menu.");
    }
    
    virtual void OnExit() override {
        Logger::Info("Exiting Pause State.");
    }

    virtual void Update(const GameTimer& gt, GameStateManager* stateManager) override {
        // Проверка нажатия P для возврата к игре
        if (Input::Get().WasKeyPressed('P')) {
            stateManager->PopState();
            return;
        }

        // Возврат в главное меню
        if (Input::Get().WasKeyPressed(VK_BACK)) {
            stateManager->ClearStates();
            stateManager->PushState(std::make_shared<MainMenuState>());
        }
    }

    virtual void Draw(const GameTimer& gt, RenderAdapter* renderer) override {
        // Отрисовка экрана паузы
    }
};
