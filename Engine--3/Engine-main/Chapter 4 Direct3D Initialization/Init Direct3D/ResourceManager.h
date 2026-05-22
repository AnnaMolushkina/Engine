#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include "Logger.h"
#include "Resource.h"
#include "Texture.h"
#include "MeshData.h"
#include "TextureData.h"

class ResourceManager {
public:
    static ResourceManager& Get() {
        static ResourceManager instance;
        return instance;
    }

    /*std::shared_ptr<Texture> LoadTexture(const std::string& path) {
        auto it = m_textures.find(path);
        if (it != m_textures.end()) {
            return it->second;
        }
        auto texture = std::make_shared<Texture>();
        if (texture->Load(path)) {
            m_textures[path] = texture;
            return texture;
        }
        return nullptr;
    }*/

    // ѕроста€ верси€ дл€ MeshData (без полного шаблона пока)
    std::shared_ptr<MeshData> LoadMesh(const std::string& path) {
        auto it = m_meshes.find(path);
        if (it != m_meshes.end()) {
            Logger::Info("[ResourceManager] Mesh cache hit: " + path);
            return it->second;
        }

        auto mesh = MeshData::LoadFromFile(path);
        if (mesh && mesh->loaded) {
            m_meshes[path] = mesh;
            Logger::Info("[ResourceManager] Mesh loaded (CPU): " + path);
            return mesh;
        }

        Logger::Error("[ResourceManager] Failed to load mesh: " + path);
        return nullptr;
    }

    std::shared_ptr<TextureData> LoadTextureData(const std::string& path) {
        auto it = m_texturesData.find(path);
        if (it != m_texturesData.end()) {
            Logger::Info("[ResourceManager] TextureData cache hit: " + path);
            return it->second;
        }

        auto tex = TextureData::LoadFromFile(path);
        if (tex && tex->loaded) {
            m_texturesData[path] = tex;
            Logger::Info("[ResourceManager] TextureData loaded: " + path);
            return tex;
        }
        return nullptr;
    }

    void Clear() {
        m_textures.clear();
        m_meshes.clear();
    }

private:
    ResourceManager() = default;
    std::unordered_map<std::string, std::shared_ptr<OLDTexture>> m_textures;
    std::unordered_map<std::string, std::shared_ptr<MeshData>> m_meshes;
    std::unordered_map<std::string, std::shared_ptr<TextureData>> m_texturesData;
};