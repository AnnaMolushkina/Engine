#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include "Logger.h"
#include "Resource.h"
#include "Texture.h"
#include "MeshData.h"
#include "TextureData.h"
#include <atomic>
#include <functional>
#include <mutex>

struct MeshData;
struct TextureData;

// Единая точка доступа к ресурсам (правило семестра: один движок — одна система фоновой работы).
// Асинхронная загрузка L1 (ТЗ 2.2):
//   * RequestMesh / RequestTexture возвращают shared_ptr немедленно;
//   * фоновая задача в job system делает file I/O + парсинг/декод (Assimp / stb_image);
//   * финализация (GPU-аплоад) — через JobSystem::RunOnMainThread, с лимитом на кадр;
//   * до готовности RenderSystem рисует placeholder (куб / белая текстура);
//   * шатдаун корректный: фоновые задачи проверяют JobSystem::IsShuttingDown().
class ResourceManager {
public:
    using MeshUploader = std::function<bool(MeshData&)>;
    using TextureUploader = std::function<bool(TextureData&)>;

    static ResourceManager& Get();

    // Регистрирует колбэки GPU-аплоада. Вызывается один раз из Application::Initialize
    // после создания D3D12RenderAdapter. Колбэки исполняются на главном потоке.
    void SetUploaders(MeshUploader meshUploader, TextureUploader textureUploader);

    // ---------- Async API (L1) ----------
    // Возврат немедленный. Готовность:
    //   mesh   — mesh->gpuMesh.IndexCount > 0
    //   texture— texture->srvIndex >= 0
    // До готовности RenderSystem рисует placeholder.
    std::shared_ptr<MeshData>    RequestMesh(const std::string& path);
    std::shared_ptr<TextureData> RequestTexture(const std::string& path);

    // ---------- Sync API ----------
    // Оставлены для случаев, когда ресурс нужен прямо сейчас (тесты, fallback).
    // Блокируют вызывающий поток.
    std::shared_ptr<MeshData>    LoadMesh(const std::string& path);
    std::shared_ptr<TextureData> LoadTextureData(const std::string& path);

    // Сколько загрузок ещё в полёте (для HUD/демо).
    int  PendingLoads() const { return m_pendingLoads.load(std::memory_order_relaxed); }

    // Очистить кэш (шатдаун, демо перезагрузки).
    void Clear();

private:
    ResourceManager() = default;
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void ScheduleMeshLoad(const std::string& path, std::shared_ptr<MeshData> mesh);
    void ScheduleTextureLoad(const std::string& path, std::shared_ptr<TextureData> texture);

    std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<MeshData>>    m_meshes;
    std::unordered_map<std::string, std::shared_ptr<TextureData>> m_textures;
    MeshUploader    m_meshUploader;
    TextureUploader m_textureUploader;
    std::atomic<int> m_pendingLoads{ 0 };
};