#pragma once

#include "GameState.h"
#include "GameStateManager.h"
#include "Logger.h"
#include <memory>

class PlayState;

class MainMenuState : public GameState {
public:
    virtual void OnEnter() override {
        Logger::Info("=== MAIN MENU ===");
        Logger::Info("Press ENTER to start the game");
        Logger::Info("Press 'W' to toggle Secondary Window");
    }
    
    virtual void OnExit() override {
        Logger::Info("Exiting Main Menu.");
    }

    virtual void Update(const GameTimer& gt, GameStateManager* stateManager) override;

    virtual void Draw(const GameTimer& gt, RenderAdapter* renderer) override {
        // Отрисовка UI главного меню
    }
};
