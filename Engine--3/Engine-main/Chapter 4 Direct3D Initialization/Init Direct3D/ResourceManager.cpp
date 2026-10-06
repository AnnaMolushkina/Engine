#include "ResourceManager.h"
#include "MeshData.h"
#include "TextureData.h"
#include "Logger.h"
#include "Profiler.h"
#include "Jobs/JobSystem.h"

// Singleton
ResourceManager& ResourceManager::Get() {
    static ResourceManager instance;
    return instance;
}

void ResourceManager::SetUploaders(MeshUploader meshUploader, TextureUploader textureUploader) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_meshUploader = std::move(meshUploader);
    m_textureUploader = std::move(textureUploader);
}

// Async: RequestMesh / RequestTexture
std::shared_ptr<MeshData> ResourceManager::RequestMesh(const std::string& path) {
    std::shared_ptr<MeshData> mesh;
    bool needLoad = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_meshes.find(path);
        if (it != m_meshes.end()) return it->second; // уже запрошен (грузитс€ или готов)

        mesh = std::make_shared<MeshData>();
        mesh->filePath = path;
        mesh->path = path;
        m_meshes[path] = mesh;
        needLoad = true;
    }
    if (needLoad) ScheduleMeshLoad(path, mesh);
    return mesh;
}

std::shared_ptr<TextureData> ResourceManager::RequestTexture(const std::string& path) {
    std::shared_ptr<TextureData> texture;
    bool needLoad = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_textures.find(path);
        if (it != m_textures.end()) return it->second;

        texture = std::make_shared<TextureData>();
        texture->filePath = path;
        texture->path = path;
        m_textures[path] = texture;
        needLoad = true;
    }
    if (needLoad) ScheduleTextureLoad(path, texture);
    return texture;
}

// ѕланирование фоновой загрузки
void ResourceManager::ScheduleMeshLoad(const std::string& path, std::shared_ptr<MeshData> mesh) {
    JobSystem* jobs = JobSystem::Instance();

    // Fallback: job system ещЄ не подн€ли (или уже гасим). √рузим синхронно на вызывающем потоке.
    if (!jobs || !jobs->IsInitialized() || jobs->IsShuttingDown()) {
        if (MeshData::LoadInto(*mesh, path)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_meshUploader) m_meshUploader(*mesh);
        }
        return;
    }

    m_pendingLoads.fetch_add(1, std::memory_order_relaxed);
    JobSystem* jobsPtr = jobs;

    jobs->Submit([this, jobsPtr, path, mesh]() {
        if (jobsPtr->IsShuttingDown()) {
            m_pendingLoads.fetch_sub(1, std::memory_order_relaxed);
            return;
        }
        ZoneScopedN("ResourceLoad::Mesh (background)");

        const bool ok = MeshData::LoadInto(*mesh, path);

        // ‘инализаци€ Ч на главном потоке (GPU-аплоад, D3D12 device)
        jobsPtr->RunOnMainThread([this, jobsPtr, ok, mesh]() {
            ZoneScopedN("ResourceLoad::Mesh (finalize)");
            if (jobsPtr->IsShuttingDown()) {
                m_pendingLoads.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
            if (ok) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_meshUploader && !m_meshUploader(*mesh)) {
                    Logger::Warning("ResourceManager: GPU upload failed for mesh " + mesh->filePath);
                }
            }
            else {
                Logger::Error("ResourceManager: CPU load failed for mesh " + mesh->filePath);
            }
            m_pendingLoads.fetch_sub(1, std::memory_order_relaxed);
            });
        }, JobPriority::Low, "LoadMesh");
}

void ResourceManager::ScheduleTextureLoad(const std::string& path, std::shared_ptr<TextureData> texture) {
    JobSystem* jobs = JobSystem::Instance();

    if (!jobs || !jobs->IsInitialized() || jobs->IsShuttingDown()) {
        if (TextureData::LoadInto(*texture, path)) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_textureUploader) m_textureUploader(*texture);
        }
        return;
    }

    m_pendingLoads.fetch_add(1, std::memory_order_relaxed);
    JobSystem* jobsPtr = jobs;

    jobs->Submit([this, jobsPtr, path, texture]() {
        if (jobsPtr->IsShuttingDown()) {
            m_pendingLoads.fetch_sub(1, std::memory_order_relaxed);
            return;
        }
        ZoneScopedN("ResourceLoad::Texture (background)");

        const bool ok = TextureData::LoadInto(*texture, path);

        jobsPtr->RunOnMainThread([this, jobsPtr, ok, texture]() {
            ZoneScopedN("ResourceLoad::Texture (finalize)");
            if (jobsPtr->IsShuttingDown()) {
                m_pendingLoads.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
            if (ok) {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_textureUploader && !m_textureUploader(*texture)) {
                    Logger::Warning("ResourceManager: GPU upload failed for texture " + texture->filePath);
                }
            }
            else {
                Logger::Error("ResourceManager: CPU load failed for texture " + texture->filePath);
            }
            m_pendingLoads.fetch_sub(1, std::memory_order_relaxed);
            });
        }, JobPriority::Low, "LoadTexture");
}

// Sync fallback (не используетс€ основным путЄм, оставлен дл€ тестов)

std::shared_ptr<MeshData> ResourceManager::LoadMesh(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_meshes.find(path);
        if (it != m_meshes.end()) return it->second;
    }
    auto mesh = MeshData::LoadFromFile(path);
    if (!mesh) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_meshUploader) m_meshUploader(*mesh);
    m_meshes[path] = mesh;
    return mesh;
}

std::shared_ptr<TextureData> ResourceManager::LoadTextureData(const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_textures.find(path);
        if (it != m_textures.end()) return it->second;
    }
    auto tex = TextureData::LoadFromFile(path);
    if (!tex) return nullptr;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_textureUploader) m_textureUploader(*tex);
    m_textures[path] = tex;
    return tex;
}

void ResourceManager::Clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_meshes.clear();
    m_textures.clear();
}