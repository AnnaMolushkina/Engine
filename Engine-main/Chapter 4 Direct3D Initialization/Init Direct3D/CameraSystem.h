#pragma once
#include "World.h"
#include "Transform.h"
#include "Camera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class CameraSystem {
public:
    CameraSystem() {
        m_cameraPos = glm::vec3(0.0f, 2.0f, 8.0f);
        m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        m_cameraRadius = 8.0f;      // расстояние от центра
        m_cameraAngleX = 0.0f;      // угол вокруг Y (влево-вправо)
        m_cameraHeight = 2.0f;      // высота камеры
    }

    ~CameraSystem() = default;

    glm::vec3 GetCameraPosition() const {
        return m_cameraPos;
    }

    // Обновление камеры на основе ввода
    void Update(float deltaTime,
        bool rotateLeft, bool rotateRight,   // стрелки влево/вправо - вращение
        bool zoomIn, bool zoomOut,           // стрелки вверх/вниз - приближение/отдаление
        bool moveUp, bool moveDown,          // Q/E - перемещение вверх/вниз
        float mouseDeltaX, float mouseDeltaY, float scrollDelta) {

        // ========== ВРАЩЕНИЕ (СТРЕЛКИ ВЛЕВО/ВПРАВО) ==========
        float rotateSpeed = 2.0f * deltaTime;
        if (rotateLeft) m_cameraAngleX += rotateSpeed;
        if (rotateRight) m_cameraAngleX -= rotateSpeed;

        // ========== ПРИБЛИЖЕНИЕ/ОТДАЛЕНИЕ (СТРЕЛКИ ВВЕРХ/ВНИЗ) ==========
        float zoomSpeed = 5.0f * deltaTime;
        if (zoomIn) m_cameraRadius -= zoomSpeed;     // стрелка вверх - приближение
        if (zoomOut) m_cameraRadius += zoomSpeed;    // стрелка вниз - отдаление

        // ========== ПЕРЕМЕЩЕНИЕ ВВЕРХ/ВНИЗ (Q/E) ==========
        float heightSpeed = 3.0f * deltaTime;
        if (moveUp) m_cameraHeight += heightSpeed;   // Q - вверх
        if (moveDown) m_cameraHeight -= heightSpeed; // E - вниз

        // ========== ВРАЩЕНИЕ МЫШЬЮ (ПКМ) ==========
        if (mouseDeltaX != 0.0f) {
            float mouseRotateSpeed = 0.005f;
            m_cameraAngleX += mouseDeltaX * mouseRotateSpeed;
        }

        // ========== МАСШТАБИРОВАНИЕ МЫШЬЮ (ЛКМ) ==========
        if (scrollDelta != 0.0f) {
            m_cameraRadius -= scrollDelta * 0.5f;
        }

        // Ограничиваем радиус
        if (m_cameraRadius < 3.0f) m_cameraRadius = 3.0f;
        if (m_cameraRadius > 20.0f) m_cameraRadius = 20.0f;

        // Ограничиваем высоту
        if (m_cameraHeight < 0.5f) m_cameraHeight = 0.5f;
        if (m_cameraHeight > 8.0f) m_cameraHeight = 8.0f;

        // Вычисляем позицию камеры в сферических координатах
        float x = sin(m_cameraAngleX) * m_cameraRadius;
        float z = cos(m_cameraAngleX) * m_cameraRadius;
        float y = m_cameraHeight;

        m_cameraPos = glm::vec3(x, y, z);
        m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    }

    // Обновление с World (для совместимости)
    void Update(World& world, float deltaTime,
        bool forward, bool backward, bool left, bool right,
        bool down, bool up,
        float mouseDeltaX, float mouseDeltaY, float scrollDelta) {
        // forward = стрелка вверх (приближение)
        // backward = стрелка вниз (отдаление)
        // left = стрелка влево (вращение влево)
        // right = стрелка вправо (вращение вправо)
        // up = Q (вверх)
        // down = E (вниз)
        Update(deltaTime, left, right, forward, backward, up, down, mouseDeltaX, mouseDeltaY, scrollDelta);

        // Синхронизируем с компонентом Camera в World
        Entity cameraEntity = GetCameraEntity(world);
        if (cameraEntity != 0) {
            Camera* camera = world.GetCamera(cameraEntity);
            if (camera) {
                camera->position = m_cameraPos;
                camera->target = m_cameraTarget;
            }
        }
    }

    // Получить матрицу вида (View)
    glm::mat4 GetViewMatrix() const {
        return glm::lookAt(m_cameraPos, m_cameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    glm::mat4 GetViewMatrix(const World& world) const {
        return GetViewMatrix();
    }

    // Получить матрицу проекции (Projection)
    glm::mat4 GetProjectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    }

    glm::mat4 GetProjectionMatrix(const World& world, float aspectRatio) const {
        return GetProjectionMatrix(aspectRatio);
    }

    // ========== ДЛЯ СОХРАНЕНИЯ/ЗАГРУЗКИ ==========
    void GetCameraState(glm::vec3& pos, glm::vec3& target, float& radius, float& angleX, float& height) const {
        pos = m_cameraPos;
        target = m_cameraTarget;
        radius = m_cameraRadius;
        angleX = m_cameraAngleX;
        height = m_cameraHeight;
    }

    void SetCameraState(const glm::vec3& pos, const glm::vec3& target, float radius, float angleX, float height) {
        m_cameraPos = pos;
        m_cameraTarget = target;
        m_cameraRadius = radius;
        m_cameraAngleX = angleX;
        m_cameraHeight = height;
    }

    Entity GetCameraEntity(const World& world) const {
        auto entities = const_cast<World&>(world).GetRenderableEntities();
        for (Entity e : entities) {
            if (const_cast<World&>(world).GetCamera(e) != nullptr) {
                return e;
            }
        }
        return 0;
    }

private:
    glm::vec3 m_cameraPos;
    glm::vec3 m_cameraTarget;
    float m_cameraRadius;      // расстояние от центра
    float m_cameraAngleX;      // угол вокруг Y (влево-вправо)
    float m_cameraHeight;      // высота камеры
};