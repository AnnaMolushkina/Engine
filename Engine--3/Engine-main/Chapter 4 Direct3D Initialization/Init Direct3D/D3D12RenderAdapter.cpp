#include "D3D12RenderAdapter.h"
#include "MeshData.h"
#include "TextureData.h"
#include "../../Common/d3dUtil.h"
#include "../../Common/d3dx12.h"
#include "Logger.h"
#include <memory>
#include "Application.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

D3D12RenderAdapter::D3D12RenderAdapter(Application* app) : mApp(app)
{
    m_modelMatrix = glm::mat4(1.0f);
    m_viewMatrix = glm::mat4(1.0f);
    m_projectionMatrix = glm::mat4(1.0f);
    m_color = glm::vec4(1.0f);
}

D3D12RenderAdapter::~D3D12RenderAdapter() = default;

GPUMesh D3D12RenderAdapter::UploadMesh(const MeshData& meshData,
    ID3D12Device* device,
    ID3D12GraphicsCommandList* cmdList)
{
    if (!meshData.IsValid() || meshData.subMeshes.empty()) {
        Logger::Error("Cannot upload invalid mesh: " + meshData.filePath);
        return {};
    }

    const SubMesh& subMesh = meshData.subMeshes[0];

    GPUMesh gpuMesh;
    gpuMesh.IndexCount = static_cast<UINT>(subMesh.indices.size());

    const UINT vbByteSize = static_cast<UINT>(subMesh.vertices.size() * sizeof(Vertex3D));
    gpuMesh.VertexBuffer = d3dUtil::CreateDefaultBuffer(
        device, cmdList,
        subMesh.vertices.data(), vbByteSize,
        mVertexBufferUploader);

    const UINT ibByteSize = static_cast<UINT>(subMesh.indices.size() * sizeof(uint32_t));
    gpuMesh.IndexBuffer = d3dUtil::CreateDefaultBuffer(
        device, cmdList,
        subMesh.indices.data(), ibByteSize,
        mIndexBufferUploader);

    gpuMesh.VBV.BufferLocation = gpuMesh.VertexBuffer->GetGPUVirtualAddress();
    gpuMesh.VBV.StrideInBytes = sizeof(Vertex3D);
    gpuMesh.VBV.SizeInBytes = vbByteSize;

    gpuMesh.IBV.BufferLocation = gpuMesh.IndexBuffer->GetGPUVirtualAddress();
    gpuMesh.IBV.Format = DXGI_FORMAT_R32_UINT;
    gpuMesh.IBV.SizeInBytes = ibByteSize;

    Logger::Info("GPU Upload successful: " + meshData.filePath);
    return gpuMesh;
}


bool D3D12RenderAdapter::CreateTexture(TextureData& textureData, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
    if (!textureData.IsValid() || textureData.pixels.empty()) {
        Logger::Error("CreateTexture: invalid texture data");
        return false;
    }

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = textureData.width;
    textureDesc.Height = textureData.height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ComPtr<ID3D12Resource> texture;
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&texture));
    if (FAILED(hr)) {
        Logger::Error("Failed to create texture resource");
        return false;
    }

    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(texture.Get(), 0, 1);
    D3D12_HEAP_PROPERTIES uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    D3D12_RESOURCE_DESC uploadBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);
    ComPtr<ID3D12Resource> uploadBuffer;
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&uploadBuffer));
    if (FAILED(hr)) {
        Logger::Error("Failed to create upload buffer");
        return false;
    }

    D3D12_SUBRESOURCE_DATA subResource = {};
    subResource.pData = textureData.pixels.data();
    subResource.RowPitch = textureData.width * 4;
    subResource.SlicePitch = subResource.RowPitch * textureData.height;
    UpdateSubresources(cmdList, texture.Get(), uploadBuffer.Get(), 0, 0, 1, &subResource);

    D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        texture.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);

    textureData.textureResource = texture;
    textureData.uploadBuffer = uploadBuffer;   // сохраняем, чтобы не удалился до выполнения команд

    Logger::Info("Texture fully uploaded to GPU: " + textureData.filePath);
    return true;
}
ComPtr<ID3DBlob> D3D12RenderAdapter::CompileShaderFromFile(const std::string& filePath,
    const std::string& entryPoint,
    const std::string& target)
{
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompileFromFile(
        std::wstring(filePath.begin(), filePath.end()).c_str(),
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(),
        target.c_str(),
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION,
        0,
        &shaderBlob,
        &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Error("Shader compilation error: " +
                std::string((char*)errorBlob->GetBufferPointer()));
        }
        return nullptr;
    }

    Logger::Info("Shader compiled: " + filePath);
    return shaderBlob;
}

std::shared_ptr<ShaderProgram> D3D12RenderAdapter::CreateShaderProgram(const std::string& vsPath,
    const std::string& psPath)
{
    auto program = std::make_shared<ShaderProgram>();
    program->vsPath = vsPath;
    program->psPath = psPath;


    program->vsBlob = CompileShaderFromFile(vsPath, "main", "vs_5_0");
    program->psBlob = CompileShaderFromFile(psPath, "main", "ps_5_0");

    if (!program->IsValid()) {
        Logger::Error("Failed to create shader program");
        return nullptr;
    }

    Logger::Info("Shader program created successfully");
    return program;
}



void D3D12RenderAdapter::SetTexture(TextureData* texture) {
    m_currentTexture = texture;
    if (texture && texture->textureResource) {
        Logger::Info("SetTexture: texture \"" + texture->filePath + "\" will be used");
    }
    else {
        Logger::Info("SetTexture: no texture");
    }
}

void D3D12RenderAdapter::DrawMesh(const GPUMesh& gpuMesh) {
    if (!mApp || !mApp->mCommandList) {
        return;
    }

    auto cmdList = mApp->mCommandList;

    cmdList->SetGraphicsRootSignature(mRootSignature.Get());

    if (m_currentTexture && m_currentTexture->textureResource) {
        Logger::Info("DrawMesh: texture resource is present");
    }
    else {
        Logger::Error("DrawMesh: no texture resource");
    }
    // Устанавливаем дескрипторный хип для текстур
    ID3D12DescriptorHeap* heaps[] = { mTextureSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    // Если есть текстура, устанавливаем её
    if (m_currentTexture && m_currentTexture->textureResource) {
        CD3DX12_GPU_DESCRIPTOR_HANDLE texHandle(
            mTextureSrvHeap->GetGPUDescriptorHandleForHeapStart(),
            0,  // используем слот 0
            mTextureSrvDescriptorSize
        );
        cmdList->SetGraphicsRootDescriptorTable(1, texHandle);
    }

    // Устанавливаем константы
    auto ToXMMATRIX = [](const glm::mat4& m) -> DirectX::XMMATRIX {
        return DirectX::XMMatrixSet(
            m[0][0], m[0][1], m[0][2], m[0][3],
            m[1][0], m[1][1], m[1][2], m[1][3],
            m[2][0], m[2][1], m[2][2], m[2][3],
            m[3][0], m[3][1], m[3][2], m[3][3]
        );
        };

    struct CB {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX view;
        DirectX::XMMATRIX proj;
    } cb;

    cb.world = DirectX::XMMatrixTranspose(ToXMMATRIX(m_modelMatrix));
    cb.view = DirectX::XMMatrixTranspose(ToXMMATRIX(m_viewMatrix));
    cb.proj = DirectX::XMMatrixTranspose(ToXMMATRIX(m_projectionMatrix));

    cmdList->SetGraphicsRoot32BitConstants(0, 48, &cb, 0);

    cmdList->IASetVertexBuffers(0, 1, &gpuMesh.VBV);
    cmdList->IASetIndexBuffer(&gpuMesh.IBV);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->DrawIndexedInstanced(gpuMesh.IndexCount, 1, 0, 0, 0);
}

void D3D12RenderAdapter::CreateTextureSRV(ComPtr<ID3D12Resource> textureResource, int index) {
    if (!mTextureSrvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
        srvHeapDesc.NumDescriptors = 64;
        srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ThrowIfFailed(mApp->md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mTextureSrvHeap)));
        mTextureSrvDescriptorSize = mApp->md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle(
        mTextureSrvHeap->GetCPUDescriptorHandleForHeapStart(),
        index,
        mTextureSrvDescriptorSize
    );

    mApp->md3dDevice->CreateShaderResourceView(textureResource.Get(), &srvDesc, srvHandle);

    Logger::Info("Created SRV for texture at slot " + std::to_string(index));
}
