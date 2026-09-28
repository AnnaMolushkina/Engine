#pragma once
#include "World.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "RenderAdapter.h"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

class JobSystem;
class Benchmark;

// Система рендеринга.
// Подготовка кадра (задача №2 ЛР1) идёт параллельно на job system: список сущностей режется
// на чанки, каждый чанк сам читает компоненты из хеш-таблиц World, считает мировую матрицу,
// отбраковывает объект по пирамиде видимости и пишет готовый DrawItem. Затем главный поток
// склеивает чанки (порядок сохраняется) и записывает команды отрисовки.
class RenderSystem {
public:
    RenderSystem(RenderAdapter* adapter) : m_adapter(adapter) {}

    struct Stats {
        uint32_t candidates = 0; // сущностей с нужным тегом
        uint32_t visible = 0;    // прошли отбраковку
    };

    // Основной путь: параллельная подготовка + отрисовка сущностей с тегом windowTag
    void Render(const World& world, JobSystem& jobs, const glm::mat4& view, const glm::mat4& projection,
                const std::string& windowTag, Benchmark* benchmark = nullptr);

    const Stats& LastStats() const { return m_stats; }

    // Простой последовательный путь (как было в ПЗ2): все сущности с Transform и MeshRenderer
    void Update(World& world, const glm::mat4& view, const glm::mat4& projection) {
        auto entities = world.GetRenderableEntities();
        for (Entity e : entities) {
            Transform* t = world.GetTransform(e);
            MeshRenderer* mr = world.GetMeshRenderer(e);
            if (t && mr) {
                m_adapter->SetModelMatrix(t->GetLocalMatrix());
                m_adapter->SetColor(mr->color);
                m_adapter->SetViewProjection(view, projection);
                m_adapter->DrawPrimitiveECS(mr->type);
            }
        }
    }

private:
    static constexpr uint32_t kChunkSize = 256;

    RenderAdapter* m_adapter;
    std::vector<DrawItem> m_scratch;      // по слоту на сущность, чанки пишут компактно в начало своего диапазона
    std::vector<uint32_t> m_chunkCounts;
    std::vector<uint32_t> m_chunkCandidates;
    std::vector<DrawItem> m_items;        // итоговый список на отрисовку
    Stats m_stats;
};
