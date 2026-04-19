#pragma once
#include "Texture.h"
#include <unordered_map>
#include <memory>
#include <string>

class ResourceManager {
public:
    static ResourceManager& Get() {
        static ResourceManager instance;
        return instance;
    }

    std::shared_ptr<Texture> LoadTexture(const std::string& path) {
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
    }

    void Clear() {
        m_textures.clear();
    }

private:
    ResourceManager() = default;
    std::unordered_map<std::string, std::shared_ptr<Texture>> m_textures;
};