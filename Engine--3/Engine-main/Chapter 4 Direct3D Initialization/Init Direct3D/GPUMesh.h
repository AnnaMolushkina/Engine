#pragma once
#include <wrl.h>
#include <d3d12.h>

using Microsoft::WRL::ComPtr;

struct GPUMesh {
    ComPtr<ID3D12Resource> VertexBuffer = nullptr;
    ComPtr<ID3D12Resource> IndexBuffer = nullptr;
    D3D12_VERTEX_BUFFER_VIEW VBV{};
    D3D12_INDEX_BUFFER_VIEW IBV{};
    UINT IndexCount = 0;

    // Промежуточные upload-буферы: должны жить, пока GPU не выполнит копирование.
    // Раньше для всех мешей использовался один буфер-член адаптера, и загрузка второго меша
    // освобождала буфер первого до того, как видеокарта его прочитала.
    ComPtr<ID3D12Resource> VertexUploader = nullptr;
    ComPtr<ID3D12Resource> IndexUploader = nullptr;
};