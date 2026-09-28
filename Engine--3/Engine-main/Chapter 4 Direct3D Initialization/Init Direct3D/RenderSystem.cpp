#include "RenderSystem.h"
#include "Benchmark.h"
#include "MeshData.h"
#include "Profiler.h"
#include "Jobs/JobSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {

// 6 плоскостей пирамиды видимости из матрицы proj * view (метод Gribb/Hartmann).
// Плоскость (n, d): точка внутри, если dot(n, p) + d >= 0.
struct Frustum {
    glm::vec4 planes[6];

    explicit Frustum(const glm::mat4& viewProj) {
        auto row = [&](int i) { return glm::vec4(viewProj[0][i], viewProj[1][i], viewProj[2][i], viewProj[3][i]); };
        const glm::vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        planes[0] = r3 + r0; // левая
        planes[1] = r3 - r0; // правая
        planes[2] = r3 + r1; // нижняя
        planes[3] = r3 - r1; // верхняя
        planes[4] = r3 + r2; // ближняя
        planes[5] = r3 - r2; // дальняя
        for (glm::vec4& p : planes)
            p /= glm::length(glm::vec3(p));
    }

    bool IntersectsSphere(const glm::vec3& center, float radius) const {
        for (const glm::vec4& p : planes)
            if (glm::dot(glm::vec3(p), center) + p.w < -radius) return false;
        return true;
    }
};

constexpr float kPrimitiveRadius = 0.8661f; // полудиагональ куба 1x1x1 - покрывает все примитивы

double MsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

void RenderSystem::Render(const World& world, JobSystem& jobs, const glm::mat4& view, const glm::mat4& projection,
                          const std::string& windowTag, Benchmark* benchmark)
{
    const std::vector<Entity>& entities = world.GetEntities();
    const uint32_t count = (uint32_t)entities.size();
    const uint32_t chunkCount = (count + kChunkSize - 1) / kChunkSize;

    // ---------- Подготовка (параллельно) ----------
    const auto prepareStart = std::chrono::steady_clock::now();
    {
        ZoneScopedN("RenderSystem::Prepare");
        m_scratch.resize(count);
        m_chunkCounts.assign(chunkCount, 0);
        m_chunkCandidates.assign(chunkCount, 0);
        const Frustum frustum(projection * view);

        jobs.ParallelFor(chunkCount, 1, [&](uint32_t chunkBegin, uint32_t chunkEnd) {
            for (uint32_t chunk = chunkBegin; chunk < chunkEnd; ++chunk) {
                const uint32_t begin = chunk * kChunkSize;
                const uint32_t end = std::min(count, begin + kChunkSize);
                uint32_t written = 0, candidates = 0;

                for (uint32_t i = begin; i < end; ++i) {
                    const Entity e = entities[i];
                    // Только чтение хеш-таблиц: пока идёт подготовка, мир никто не меняет
                    const Tag* tag = world.GetTag(e);
                    if (!tag || tag->name != windowTag) continue;
                    const Transform* t = world.GetTransform(e);
                    const MeshRenderer* mr = world.GetMeshRenderer(e);
                    if (!t || !mr) continue;
                    ++candidates;

                    // Загруженный меш - если он уже на GPU; иначе рисуем примитив-заглушку (куб)
                    const GPUMesh* mesh = nullptr;
                    float radius = kPrimitiveRadius;
                    PrimitiveType primitive = mr->type;
                    if (mr->useLoadedMesh && mr->mesh) {
                        if (mr->mesh->gpuMesh.IndexCount > 0) {
                            mesh = &mr->mesh->gpuMesh;
                            radius = mr->mesh->boundingRadius;
                        }
                        else {
                            primitive = PrimitiveType::Cube;
                        }
                    }

                    const float maxScale = std::max(std::abs(t->scale.x), std::max(std::abs(t->scale.y), std::abs(t->scale.z)));
                    if (!frustum.IntersectsSphere(t->position, radius * maxScale)) continue;

                    DrawItem& item = m_scratch[begin + written++];
                    item.worldT = glm::transpose(t->GetLocalMatrix());
                    item.mesh = mesh;
                    item.texture = mr->texture.get();
                    item.primitive = primitive;
                }
                m_chunkCounts[chunk] = written;
                m_chunkCandidates[chunk] = candidates;
            }
        }, "RenderSystem::Prepare");

        // Склейка чанков по порядку (детерминированный порядок отрисовки)
        uint32_t visible = 0, candidates = 0;
        for (uint32_t chunk = 0; chunk < chunkCount; ++chunk) {
            visible += m_chunkCounts[chunk];
            candidates += m_chunkCandidates[chunk];
        }
        m_items.resize(visible);
        uint32_t offset = 0;
        for (uint32_t chunk = 0; chunk < chunkCount; ++chunk) {
            if (m_chunkCounts[chunk] == 0) continue;
            std::memcpy(m_items.data() + offset, m_scratch.data() + chunk * kChunkSize, m_chunkCounts[chunk] * sizeof(DrawItem));
            offset += m_chunkCounts[chunk];
        }
        m_stats.candidates = candidates;
        m_stats.visible = visible;
    }
    const double prepareMs = MsSince(prepareStart);

    // ---------- Запись команд (главный поток) ----------
    const auto recordStart = std::chrono::steady_clock::now();
    {
        ZoneScopedN("RenderSystem::Record");
        m_adapter->SetViewProjection(view, projection);
        m_adapter->DrawItems(m_items.data(), m_items.size());
    }
    const double recordMs = MsSince(recordStart);

    if (benchmark) {
        benchmark->RecordZone("RenderPrepare", prepareMs);
        benchmark->RecordZone("RenderRecord", recordMs);
    }
    TracyPlot("Visible objects", (int64_t)m_stats.visible);
}
