#pragma once
#include "World.h"
#include "Transform.h"
#include "Camera.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class CameraSystem {
public:
    CameraSystem() {
        m_cameraPos = glm::vec3(0.0f, 2.0f, 6.0f);
        m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        m_cameraYaw = 0.0f;
        m_cameraPitch = 0.0f;
        m_cameraDistance = 6.0f;
    }

    ~CameraSystem() = default;

    // Обновление камеры на основе ввода
    void Update(float deltaTime,
        bool moveForward, bool moveBackward,   // стрелки вверх/вниз
        bool moveLeft, bool moveRight,         // стрелки влево/вправо
        bool moveUp, bool moveDown,            // Q / E
        float mouseDeltaX, float mouseDeltaY,  // для вращения (ПКМ)
        float scrollDelta) {                   // для приближения/отдаления (ЛКМ / колесико)

        float speed = 3.0f * deltaTime;
        float rotateSpeed = 0.005f;

        // === 1. ДВИЖЕНИЕ КАМЕРЫ (СТРЕЛКИ) ===
        // Получаем направления камеры
        glm::vec3 forward = glm::normalize(m_cameraTarget - m_cameraPos);
        glm::vec3 right = glm::normalize(glm::cross(forward, m_cameraUp));
        glm::vec3 up = m_cameraUp;

        // Движение вперед/назад
        if (moveForward) {
            m_cameraPos += forward * speed;
            m_cameraTarget += forward * speed;
        }
        if (moveBackward) {
            m_cameraPos -= forward * speed;
            m_cameraTarget -= forward * speed;
        }

        // Движение влево/вправо
        if (moveLeft) {
            m_cameraPos -= right * speed;
            m_cameraTarget -= right * speed;
        }
        if (moveRight) {
            m_cameraPos += right * speed;
            m_cameraTarget += right * speed;
        }

        // Движение вверх/вниз
        if (moveUp) {
            m_cameraPos += up * speed;
            m_cameraTarget += up * speed;
        }
        if (moveDown) {
            m_cameraPos -= up * speed;
            m_cameraTarget -= up * speed;
        }

        // === 2. ВРАЩЕНИЕ КАМЕРЫ (ПКМ) ===
        if (mouseDeltaX != 0.0f || mouseDeltaY != 0.0f) {
            // Вектор от камеры к цели
            glm::vec3 dir = m_cameraPos - m_cameraTarget;
            float distance = glm::length(dir);

            // Переводим в сферические координаты
            float yaw = atan2(dir.x, dir.z);
            float pitch = asin(dir.y / distance);

            // Изменяем углы
            yaw += mouseDeltaX * rotateSpeed;
            pitch -= mouseDeltaY * rotateSpeed;

            // Ограничиваем pitch, чтобы камера не переворачивалась
            pitch = glm::clamp(pitch, -1.4f, 1.4f);

            // Вычисляем новую позицию
            dir.x = distance * sin(yaw) * cos(pitch);
            dir.y = distance * sin(pitch);
            dir.z = distance * cos(yaw) * cos(pitch);

            m_cameraPos = m_cameraTarget + dir;
        }

        // === 3. ПРИБЛИЖЕНИЕ/ОТДАЛЕНИЕ (ЛКМ / колесико) ===
        if (scrollDelta != 0.0f) {
            glm::vec3 dir = m_cameraPos - m_cameraTarget;
            float distance = glm::length(dir);

            distance -= scrollDelta * 0.5f;
            distance = glm::clamp(distance, 1.0f, 20.0f);

            dir = glm::normalize(dir) * distance;
            m_cameraPos = m_cameraTarget + dir;
        }

        // Синхронизируем с компонентом Camera в World
        if (m_cameraEntity != 0) {
            Camera* camera = m_world->GetCamera(m_cameraEntity);
            if (camera) {
                camera->position = m_cameraPos;
                camera->target = m_cameraTarget;
            }
        }
    }

    // Обновление с World (для совместимости)
    void Update(World& world, float deltaTime,
        bool moveForward, bool moveBackward,
        bool moveLeft, bool moveRight,
        bool moveUp, bool moveDown,
        float mouseDeltaX, float mouseDeltaY,
        float scrollDelta) {

        m_world = &world;

        // Находим камеру в World (если еще не найдена)
        // GetRenderableEntities() требует MeshRenderer — у камеры его нет,
        // поэтому используем GetCameraEntities().
        if (m_cameraEntity == 0) {
            for (Entity e : world.GetCameraEntities()) {
                if (world.GetCamera(e) != nullptr) {
                    m_cameraEntity = e;
                    break;
                }
            }
        }

        // Синхронизируем начальное состояние из компонента
        if (m_cameraEntity != 0) {
            Camera* camera = world.GetCamera(m_cameraEntity);
            if (camera) {
                m_cameraPos = camera->position;
                m_cameraTarget = camera->target;
                m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

                // Вычисляем расстояние для zoom
                m_cameraDistance = glm::length(m_cameraPos - m_cameraTarget);
            }
        }

        Update(deltaTime, moveForward, moveBackward, moveLeft, moveRight,
            moveUp, moveDown, mouseDeltaX, mouseDeltaY, scrollDelta);
    }

    // Сбросить кэш: после уничтожения/пересоздания камеры в World
    // CameraSystem должен заново её найти.
    void Reset() {
        m_cameraEntity = 0;
        m_world = nullptr;
        m_cameraPos = glm::vec3(0.0f, 2.0f, 6.0f);
        m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
        m_cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);
        m_cameraYaw = 0.0f;
        m_cameraPitch = 0.0f;
        m_cameraDistance = 6.0f;
    }

    // Геттеры
    glm::vec3 GetCameraPosition() const { return m_cameraPos; }
    glm::mat4 GetViewMatrix() const {
        return glm::lookAt(m_cameraPos, m_cameraTarget, m_cameraUp);
    }
    glm::mat4 GetProjectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
    }

private:
    World* m_world = nullptr;
    Entity m_cameraEntity = 0;

    glm::vec3 m_cameraPos;
    glm::vec3 m_cameraTarget;
    glm::vec3 m_cameraUp;
    float m_cameraYaw;
    float m_cameraPitch;
    float m_cameraDistance;
};