#pragma once
#include "Component.h"
#include <glm/glm.hpp>

enum class CameraType {
    Perspective,
    Orthographic
};

struct Camera : public Component {
    CameraType type = CameraType::Perspective;
    glm::vec3 position = glm::vec3(0.0f, 2.0f, 8.0f);
    glm::vec3 target = glm::vec3(0.0f, 0.0f, 0.0f);
    float zoom = 8.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
};