#pragma once

#include "GameState.h"
#include "GameStateManager.h"
#include "Logger.h"
#include "RenderAdapter.h"
#include "PauseState.h"
#include <memory>
#include <algorithm>

class PlayState : public GameState {
public:
    virtual void OnEnter() override {
        Logger::Info("=== GAMEPLAY ===");
        Logger::Info("LMB: Scale | RMB: Rotate | P: Pause");
        Logger::Info("Press 'W' to toggle Secondary Window");
    }
    
    virtual void OnExit() override {
        Logger::Info("Exiting Gameplay.");
    }

    virtual void Update(const GameTimer& gt, GameStateManager* stateManager) override {
        // Проверка на паузу
        if (GetAsyncKeyState('P') & 0x8000) {
            Sleep(200); // Спасает от многократного срабатывания клавиши за один кадр
            stateManager->PushState(std::make_shared<PauseState>());
        }
    }

    virtual void Draw(const GameTimer& gt, RenderAdapter* renderer) override {
        // Отрисовка с параметрами трансформации
        if (renderer) {
            renderer->DrawPrimitive(PrimitiveType::Triangle, mPosition, mRotation, mScale);
        }
    }

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override {
        // Сохраняем начальную позицию мыши при клике
        mLastMousePos.x = x;
        mLastMousePos.y = y;
    }

    virtual void OnMouseMove(WPARAM btnState, int x, int y) override {
        float dx = (float)(x - mLastMousePos.x);

        // Масштабирование (LMB + движение)
        if ((btnState & MK_LBUTTON) != 0) {
            // Движение вправо увеличивает масштаб, влево - уменьшает
            mScale += dx * 0.005f; 
            mScale = (std::max)(0.1f, mScale);
        }

        // Вращение (RMB + движение)
        if ((btnState & MK_RBUTTON) != 0) {
            // По вашей просьбе: движение вправо вращает против часовой стрелки, влево — по часовой.
            // (dx > 0 -> mRotation увеличивается -> вращение против часовой)
            mRotation += dx * 0.01f;
        }

        mLastMousePos.x = x;
        mLastMousePos.y = y;
    }

    virtual void ProcessKeyboardInput(const GameTimer& gt) override {
        // Обработка перемещения (Стрелки)
        float speed = 1.5f; // Единиц в секунду
        if (GetAsyncKeyState(VK_LEFT) & 0x8000)  mPosition.x -= speed * gt.DeltaTime();
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) mPosition.x += speed * gt.DeltaTime();
        if (GetAsyncKeyState(VK_UP) & 0x8000)    mPosition.y += speed * gt.DeltaTime();
        if (GetAsyncKeyState(VK_DOWN) & 0x8000)  mPosition.y -= speed * gt.DeltaTime();
    }

private:
    DirectX::XMFLOAT3 mPosition = { 0.0f, 0.0f, 0.0f };
    float mRotation = 0.0f;
    float mScale = 1.0f;
    POINT mLastMousePos = { 0, 0 };
};

// Реализация метода Update для MainMenuState, так как он использует PlayState
inline void MainMenuState::Update(const GameTimer& gt, GameStateManager* stateManager) {
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
        Sleep(200); // От залипания
        stateManager->ChangeState(std::make_shared<PlayState>());
    }
}
