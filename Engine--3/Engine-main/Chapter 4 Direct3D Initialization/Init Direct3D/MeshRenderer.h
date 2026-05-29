#pragma once
#include "Component.h"
#include <glm/glm.hpp>
#include <memory>

// Forward declaration
struct TextureData;
struct MeshData;
struct ShaderProgram;

enum class PrimitiveType {
    Triangle,
    Square,
    Quad,
    Cube
};

struct MeshRenderer : public Component {
    PrimitiveType type = PrimitiveType::Cube;
    glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    
    // указатель на загруженный меш
    std::shared_ptr<MeshData> mesh = nullptr;

    // Флаг: использовать загруженный меш или примитив
    bool useLoadedMesh = false;

    //текстура для этого объекта
    std::shared_ptr<TextureData> texture = nullptr;

    std::shared_ptr<ShaderProgram> shader = nullptr;
};