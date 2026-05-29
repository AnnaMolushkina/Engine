#pragma once
#include "Resource.h"
#include "../../Common/d3dx12.h"
#include <wrl/client.h>
#include <vector>
#include <cstdint>
#include <string>
#include <memory>

using Microsoft::WRL::ComPtr;

struct TextureData : public Resource {
    int width = 0;
    int height = 0;
    int channels = 0;
    int srvIndex = -1;
    std::vector<uint8_t> pixels;

    std::string filePath;

    // GPU-хендл текстуры
    ComPtr<ID3D12Resource> textureResource = nullptr;

    bool IsValid() const { return width > 0 && height > 0 && !pixels.empty(); }
    size_t GetDataSize() const { return static_cast<size_t>(width) * height * 4; }

    static std::shared_ptr<TextureData> LoadFromFile(const std::string& path);
    ComPtr<ID3D12Resource> uploadBuffer;
};