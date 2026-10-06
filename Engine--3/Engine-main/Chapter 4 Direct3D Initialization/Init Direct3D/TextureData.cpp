#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "TextureData.h"
#include "Logger.h"

std::shared_ptr<TextureData> TextureData::LoadFromFile(const std::string& path) {
    auto tex = std::make_shared<TextureData>();
    if (!LoadInto(*tex, path)) return nullptr;
    return tex;
}

bool TextureData::LoadInto(TextureData& tex, const std::string& path) {
    tex.filePath = path;
    tex.path = path;
    tex.pixels.clear();
    tex.srvIndex = -1;
    tex.loaded = false;

    stbi_set_flip_vertically_on_load(false);
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data) {
        Logger::Error("stb_image failed to load texture: " + path);
        return false;
    }
    tex.width = w;
    tex.height = h;
    tex.channels = ch;
    tex.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
    stbi_image_free(data);
    tex.loaded = true;
    Logger::Info("Texture loaded (CPU): " + path + " (" + std::to_string(w) + "x" + std::to_string(h) + ")");
    return true;
}