#pragma once

#include <windows.h>
#include "GameState.h"
#include <memory>
#include <stack>

class GameStateManager {
public:
    GameStateManager() = default;
    ~GameStateManager() {
        while (!mStateStack.empty()) {
            mStateStack.top()->OnExit();
            mStateStack.pop();
        }
    }

    void PushState(std::shared_ptr<GameState> state) {
        if (!mStateStack.empty()) {
            // Optional: pause current state
        }
        mStateStack.push(state);
        mStateStack.top()->OnEnter();
    }

    void PopState() {
        if (!mStateStack.empty()) {
            mStateStack.top()->OnExit();
            mStateStack.pop();
        }
        if (!mStateStack.empty()) {
            // Optional: resume new top state
        }
    }

    void ChangeState(std::shared_ptr<GameState> state) {
        if (!mStateStack.empty()) {
            mStateStack.top()->OnExit();
            mStateStack.pop();
        }
        mStateStack.push(state);
        mStateStack.top()->OnEnter();
    }

    // Очистить весь стек состояний
    void ClearStates() {
        while (!mStateStack.empty()) {
            mStateStack.top()->OnExit();
            mStateStack.pop();
        }
    }

    void Update(const GameTimer& gt) {
        if (!mStateStack.empty()) {
            mStateStack.top()->Update(gt, this);
        }
    }

    void Draw(const GameTimer& gt, class RenderAdapter* renderer) {
        if (!mStateStack.empty()) {
            mStateStack.top()->Draw(gt, renderer);
        }
    }

    void OnMouseDown(WPARAM btnState, int x, int y) {
        if (!mStateStack.empty()) mStateStack.top()->OnMouseDown(btnState, x, y);
    }

    void OnMouseUp(WPARAM btnState, int x, int y) {
        if (!mStateStack.empty()) mStateStack.top()->OnMouseUp(btnState, x, y);
    }

    void OnMouseMove(WPARAM btnState, int x, int y) {
        if (!mStateStack.empty()) mStateStack.top()->OnMouseMove(btnState, x, y);
    }

    void ProcessKeyboardInput(const GameTimer& gt) {
        if (!mStateStack.empty()) mStateStack.top()->ProcessKeyboardInput(gt);
    }

private:
    std::stack<std::shared_ptr<GameState>> mStateStack;
};
