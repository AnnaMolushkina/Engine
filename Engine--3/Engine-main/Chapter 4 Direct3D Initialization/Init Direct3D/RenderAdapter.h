#pragma once

#include <DirectXColors.h>
#include <DirectXMath.h>
#include <glm/glm.hpp>
#include "MeshRenderer.h"
#include "GPUMesh.h"
#include <cstddef>
#include <memory>

// Forward declaration
struct TextureData;

// Готовая к отрисовке запись. Всё считается заранее (RenderSystem, в задачах job system),
// главному потоку остаётся только записать команды.
struct DrawItem {
    glm::mat4 worldT;                 // транспонированная мировая матрица (раскладка константного буфера)
    const GPUMesh* mesh = nullptr;    // nullptr - базовый примитив (primitive)
    TextureData* texture = nullptr;   // nullptr - белая текстура
    PrimitiveType primitive = PrimitiveType::Cube;
};

class RenderAdapter {
public:
    virtual ~RenderAdapter() {}

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0; // освобождение GPU-ресурсов
    virtual void SetVSync(bool enabled) = 0;

    virtual void BeginFrame(int windowIndex = 0) = 0;
    virtual void EndFrame(int windowIndex = 0) = 0;
    virtual void DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position = { 0,0,0 },
        float rotation = 0.0f, float scale = 1.0f) = 0;

    // Новые методы для ECS
    virtual void SetModelMatrix(const glm::mat4& matrix) = 0;
    virtual void SetColor(const glm::vec4& color) = 0;
    virtual void SetViewProjection(const glm::mat4& view, const glm::mat4& proj) = 0;
    virtual void DrawPrimitiveECS(PrimitiveType type) = 0;

    // отрисовка загруженного меша
    virtual void DrawMesh(const GPUMesh& gpuMesh) = 0;

    // метод для установки текстуры (пока без shared_ptr, используем указатель)
    virtual void SetTexture(TextureData* texture) = 0;

    // Пакетная отрисовка подготовленных записей (камера - из SetViewProjection)
    virtual void DrawItems(const DrawItem* items, size_t count) = 0;
};
