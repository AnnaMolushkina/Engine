#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <memory>

using Microsoft::WRL::ComPtr;

struct ShaderProgram {
    ComPtr<ID3DBlob> vsBlob;        // скомпилированный вершинный шейдер
    ComPtr<ID3DBlob> psBlob;        // скомпилированный пиксельный шейдер

    std::string vsPath;
    std::string psPath;

    bool IsValid() const { return vsBlob && psBlob; }
};