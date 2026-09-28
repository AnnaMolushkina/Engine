#pragma once

#include <windows.h>
#include <array>

enum class MouseButton { Left = 0, Right = 1, Middle = 2 };

// Состояние клавиатуры и мыши на текущий кадр (отдельный класс ввода - доп. задание ПЗ1).
// Заполняется из оконных сообщений, поэтому, в отличие от GetAsyncKeyState, реагирует
// только когда окно движка в фокусе. WasKeyPressed срабатывает ровно один раз на нажатие:
// больше не нужен Sleep(200) "от залипания", который замораживал весь игровой цикл.
class Input {
public:
    // Единый экземпляр, как ConfigManager::Get() и ResourceManager::Get().
    // Заполняет Application (оконные сообщения), читают состояния и системы.
    static Input& Get() {
        static Input instance;
        return instance;
    }

    void OnKeyDown(WPARAM key, LPARAM lParam) {
        if (key >= kKeyCount) return;
        const bool autoRepeat = (lParam & (1 << 30)) != 0; // клавиша уже была нажата
        if (!autoRepeat) mPressed[key] = true;
        mKeys[key] = true;
    }

    void OnKeyUp(WPARAM key) {
        if (key < kKeyCount) mKeys[key] = false;
    }

    // btnState - флаги MK_* из сообщений мыши
    void OnMouseButtons(WPARAM btnState) {
        mMouse[(int)MouseButton::Left]   = (btnState & MK_LBUTTON) != 0;
        mMouse[(int)MouseButton::Right]  = (btnState & MK_RBUTTON) != 0;
        mMouse[(int)MouseButton::Middle] = (btnState & MK_MBUTTON) != 0;
    }

    void OnMouseMove(int x, int y) {
        if (mHasMousePos) {
            mMouseDeltaX += x - mMouseX;
            mMouseDeltaY += y - mMouseY;
        }
        mMouseX = x;
        mMouseY = y;
        mHasMousePos = true;
    }

    void OnMouseWheel(int delta) { mWheel += (float)delta / WHEEL_DELTA; }

    // Потеря фокуса: иначе "залипают" клавиши, отпущенные в другом окне
    void Reset() {
        mKeys.fill(false);
        mPressed.fill(false);
        mMouse.fill(false);
        mMouseDeltaX = mMouseDeltaY = 0;
        mWheel = 0.0f;
        mHasMousePos = false;
    }

    // Вызывается в конце кадра, после всех потребителей ввода
    void EndFrame() {
        mPressed.fill(false);
        mMouseDeltaX = mMouseDeltaY = 0;
        mWheel = 0.0f;
    }

    bool IsKeyDown(int key) const { return key >= 0 && key < (int)kKeyCount && mKeys[key]; }
    bool WasKeyPressed(int key) const { return key >= 0 && key < (int)kKeyCount && mPressed[key]; }
    bool IsMouseDown(MouseButton button) const { return mMouse[(int)button]; }

    int MouseX() const { return mMouseX; }
    int MouseY() const { return mMouseY; }
    int MouseDeltaX() const { return mMouseDeltaX; }
    int MouseDeltaY() const { return mMouseDeltaY; }
    float WheelDelta() const { return mWheel; }

private:
    static constexpr size_t kKeyCount = 256;
    std::array<bool, kKeyCount> mKeys{};
    std::array<bool, kKeyCount> mPressed{};
    std::array<bool, 3> mMouse{};
    int mMouseX = 0, mMouseY = 0;
    int mMouseDeltaX = 0, mMouseDeltaY = 0;
    float mWheel = 0.0f;
    bool mHasMousePos = false;
};
