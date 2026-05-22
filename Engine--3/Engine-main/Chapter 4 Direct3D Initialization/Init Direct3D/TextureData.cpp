#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "TextureData.h"
#include "Logger.h"

std::shared_ptr<TextureData> TextureData::LoadFromFile(const std::string& path) {
    auto tex = std::make_shared<TextureData>();
    tex->filePath = path;
    tex->path = path;

    stbi_set_flip_vertically_on_load(false);

    int w, h, ch;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);

    if (!data) {
        Logger::Error("stb_image failed to load texture: " + path);
        return nullptr;
    }

    tex->width = w;
    tex->height = h;
    tex->channels = ch;
    tex->pixels.assign(data, data + static_cast<size_t>(w) * h * 4);

    stbi_image_free(data);

    tex->loaded = true;
    Logger::Info("Texture loaded: " + path + " (" + std::to_string(w) + "x" +
        std::to_string(h) + ")");

    return tex;
}