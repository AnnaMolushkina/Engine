#pragma once
#include "Component.h"
#include "Texture.h"
#include <memory>
#include <glm/glm.hpp>

struct Material : public Component {
    glm::vec4 color = glm::vec4(1.0f);
    std::shared_ptr<Texture> texture = nullptr;
    float metallic = 0.0f;
    float roughness = 0.5f;

    bool HasTexture() const {
        return texture != nullptr;
    }
};