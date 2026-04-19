#pragma once

#include <DirectXColors.h>
#include <DirectXMath.h>
#include <glm/glm.hpp>
#include "MeshRenderer.h"  


class RenderAdapter {
public:
    virtual ~RenderAdapter() {}

    virtual bool Initialize() = 0;
    virtual void BeginFrame(int windowIndex = 0) = 0;
    virtual void EndFrame(int windowIndex = 0) = 0;
    virtual void DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position = { 0,0,0 },
        float rotation = 0.0f, float scale = 1.0f) = 0;

    // Новые методы для ECS
    virtual void SetModelMatrix(const glm::mat4& matrix) = 0;
    virtual void SetColor(const glm::vec4& color) = 0;
    virtual void SetViewProjection(const glm::mat4& view, const glm::mat4& proj) = 0;
    virtual void DrawPrimitiveECS(PrimitiveType type) = 0;
};