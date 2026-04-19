#pragma once
#include "Component.h"
#include <glm/glm.hpp>

enum class PrimitiveType {
    Triangle,
    Square,
    Quad,
    Cube
};

struct MeshRenderer : public Component {
    PrimitiveType type = PrimitiveType::Cube;
    glm::vec4 color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // ← ДОБАВИТЬ ЭТУ СТРОКУ
};