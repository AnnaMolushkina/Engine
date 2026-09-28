#pragma once

#include <windows.h>
#include "GameState.h"
#include <memory>
#include <vector>

// Стек состояний игры. Переходы (Push/Pop/Change/Clear) откладываются и применяются после Update:
// раньше ChangeState/ClearStates удаляли состояние прямо посреди его собственного Update()
// (объект уничтожался, пока выполнялся его метод).
class GameStateManager {
public:
    GameStateManager() = default;
    ~GameStateManager() { Shutdown(); }

    void PushState(std::shared_ptr<GameState> state) { mPending.push_back({ Op::Push, std::move(state) }); }
    void PopState() { mPending.push_back({ Op::Pop, nullptr }); }
    void ChangeState(std::shared_ptr<GameState> state) { mPending.push_back({ Op::Change, std::move(state) }); }

    // Очистить весь стек состояний
    void ClearStates() { mPending.push_back({ Op::Clear, nullptr }); }

    std::shared_ptr<GameState> GetCurrentState() const {
        if (mStack.empty()) return nullptr;
        return mStack.back();
    }

    void Update(const GameTimer& gt) {
        if (!mStack.empty()) {
            std::shared_ptr<GameState> top = mStack.back(); // держим живым до конца Update
            top->Update(gt, this);
        }
        ApplyPending();
    }

    void Draw(const GameTimer& gt, class RenderAdapter* renderer) {
        if (!mStack.empty()) mStack.back()->Draw(gt, renderer);
    }

    void OnMouseDown(WPARAM btnState, int x, int y) {
        if (!mStack.empty()) mStack.back()->OnMouseDown(btnState, x, y);
    }

    void OnMouseUp(WPARAM btnState, int x, int y) {
        if (!mStack.empty()) mStack.back()->OnMouseUp(btnState, x, y);
    }

    void OnMouseMove(WPARAM btnState, int x, int y) {
        if (!mStack.empty()) mStack.back()->OnMouseMove(btnState, x, y);
    }

    void ProcessKeyboardInput(const GameTimer& gt) {
        if (!mStack.empty()) mStack.back()->ProcessKeyboardInput(gt);
        ApplyPending();
    }

    // Применить отложенные переходы (вызывается сам после Update; снаружи - после первого ChangeState)
    void ApplyPending() {
        while (!mPending.empty()) {
            std::vector<PendingOp> ops;
            ops.swap(mPending);
            for (PendingOp& op : ops) {
                switch (op.op) {
                case Op::Push:
                    mStack.push_back(op.state);
                    op.state->OnEnter();
                    break;
                case Op::Pop:
                    if (!mStack.empty()) PopTop();
                    break;
                case Op::Change:
                    if (!mStack.empty()) PopTop();
                    mStack.push_back(op.state);
                    op.state->OnEnter();
                    break;
                case Op::Clear:
                    while (!mStack.empty()) PopTop();
                    break;
                }
            }
        }
    }

    void Shutdown() {
        mPending.clear();
        while (!mStack.empty()) PopTop();
    }

private:
    enum class Op { Push, Pop, Change, Clear };
    struct PendingOp {
        Op op;
        std::shared_ptr<GameState> state;
    };

    void PopTop() {
        std::shared_ptr<GameState> state = mStack.back();
        state->OnExit();
        mStack.pop_back();
    }

    std::vector<std::shared_ptr<GameState>> mStack;
    std::vector<PendingOp> mPending;
};
