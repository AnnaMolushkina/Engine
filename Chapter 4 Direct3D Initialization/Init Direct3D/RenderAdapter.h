#pragma once

#include <DirectXColors.h>
#include <DirectXMath.h>

enum class PrimitiveType { Triangle, Square };

// Адаптер рендеринга (RenderAdapter)
// Интерфейс адаптера рендеринга для абстрагирования от графического API
class RenderAdapter {
public:
    virtual ~RenderAdapter() {}

    // Инициализация графического API
    virtual bool Initialize() = 0;

    virtual void BeginFrame(int windowIndex = 0) = 0;
    virtual void EndFrame(int windowIndex = 0) = 0;

    // Отрисовка базового примитива
    virtual void DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position = {0,0,0}, float rotation = 0.0f, float scale = 1.0f) = 0;
};
