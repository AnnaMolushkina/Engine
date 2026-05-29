#pragma once
#include "World.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "RenderAdapter.h"
#include "MeshData.h"
#include <glm/glm.hpp>
#include <string>  

class RenderSystem {
public:
    RenderSystem(RenderAdapter* adapter) : m_adapter(adapter) {}

    void Update(World& world, const glm::mat4& view, const glm::mat4& projection) {
        auto entities = world.GetRenderableEntities();
        for (Entity e : entities) {
            Transform* t = world.GetTransform(e);
            MeshRenderer* mr = world.GetMeshRenderer(e);
            if (t && mr) {
                glm::mat4 model = t->GetLocalMatrix();
                m_adapter->SetModelMatrix(model);
                m_adapter->SetColor(mr->color);
                m_adapter->SetViewProjection(view, projection);
                m_adapter->DrawPrimitiveECS(mr->type);
            }
        }
    }

    void UpdateForWindow(World& world, const glm::mat4& view, const glm::mat4& projection, const std::string& windowName) {
        std::vector<Entity> entities = world.GetRenderableEntitiesForWindow(windowName);
        for (Entity e : entities) {
            Transform* t = world.GetTransform(e);
            MeshRenderer* mr = world.GetMeshRenderer(e);
            if (t && mr) {
                glm::mat4 model = t->GetLocalMatrix();
                m_adapter->SetModelMatrix(model);
                m_adapter->SetColor(mr->color);
                m_adapter->SetViewProjection(view, projection);
                m_adapter->DrawPrimitiveECS(mr->type);
            }
        }
    }

    // ========== НОВЫЙ МЕТОД С ОТБРАКОВКОЙ НЕВИДИМЫХ ОБЪЕКТОВ ==========
    void UpdateWithCulling(World& world, const glm::mat4& view, const glm::mat4& projection,
        const std::string& windowName, const glm::vec3& cameraPos, float viewDistance = 30.0f) {
        // Обновляем пространственную сетку
        world.UpdateSpatialGrid();

        // Получаем только объекты в радиусе видимости
        std::vector<Entity> visibleEntities = world.GetVisibleEntities(cameraPos, viewDistance);

        // Фильтруем по тегу окна
        int drawnCount = 0;
        for (Entity e : visibleEntities) {
            Tag* tag = world.GetTag(e);
            if (tag && tag->name == windowName) {
                Transform* t = world.GetTransform(e);
                MeshRenderer* mr = world.GetMeshRenderer(e);
                if (t && mr) {
                    glm::mat4 model = t->GetLocalMatrix();
                    m_adapter->SetModelMatrix(model);
                    m_adapter->SetColor(mr->color);
                    m_adapter->SetViewProjection(view, projection);
                    m_adapter->DrawPrimitiveECS(mr->type);
                    drawnCount++;
                }
            }
        }

        // Отладка (каждые 60 кадров)
        static int debugCounter = 0;
        debugCounter++;
        if (debugCounter % 60 == 0) {
            Logger::Info("Visible entities: " + std::to_string(drawnCount) + " / " +
                std::to_string(visibleEntities.size()) + " in range");
        }
    }
    
    void UpdateWithLoadedMeshes(World& world, const glm::mat4& view, const glm::mat4& projection) {
        auto entities = world.GetRenderableEntities();

        for (Entity e : entities) {
            Transform* t = world.GetTransform(e);
            MeshRenderer* mr = world.GetMeshRenderer(e);
            Tag* tag = world.GetTag(e);

            if (!t || !mr) continue;

            // Фильтрация по окну (если нужно)
            if (tag && tag->name != "MainWindow") continue;

            // Устанавливаем трансформацию
            glm::mat4 model = t->GetLocalMatrix();
            m_adapter->SetModelMatrix(model);
            m_adapter->SetViewProjection(view, projection);
            m_adapter->SetColor(mr->color);

            // Текстура
            if (mr->texture) {
                m_adapter->SetTexture(mr->texture.get());
            }
            else {
                m_adapter->SetTexture(nullptr);
            }

            // Шейдер (если используется)
            if (mr->shader) {
                m_adapter->SetShader(mr->shader);
            }

            // Отрисовка
            if (mr->useLoadedMesh && mr->mesh && mr->mesh->gpuMesh.IndexCount > 0) {
                m_adapter->DrawMesh(mr->mesh->gpuMesh);
            }
            else {
                m_adapter->DrawPrimitiveECS(mr->type);
            }
        }
    }
private:
    RenderAdapter* m_adapter;
};