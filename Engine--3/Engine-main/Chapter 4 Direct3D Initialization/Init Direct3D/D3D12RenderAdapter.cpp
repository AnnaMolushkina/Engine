#include "D3D12RenderAdapter.h"
#include "MeshData.h"
#include "TextureData.h"
#include "../../Common/d3dUtil.h"
#include "../../Common/d3dx12.h"
#include "Logger.h"
#include "ConfigManager.h"
#include "Profiler.h"
#include <memory>
#include "Application.h"
#include <d3dcompiler.h>
#include <dxgi1_5.h>
#pragma comment(lib, "d3dcompiler.lib")

namespace {

// Байты glm::transpose(m) - раскладка float4x4 в константном буфере для mul(v, M) в HLSL
// (то же самое, что делали XMMatrixTranspose(ToXMMATRIX(m)))
struct FrameConstants {
    glm::mat4 viewT;
    glm::mat4 projT;
};

std::wstring ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), out.data(), size);
    return out;
}

// Смещения примитивов в общем буфере геометрии (см. BuildGeometry)
constexpr UINT kTriangleIndexStart = 0, kTriangleIndexCount = 3;
constexpr UINT kSquareIndexStart = 3, kSquareIndexCount = 6;
constexpr UINT kCubeIndexStart = 9, kCubeIndexCount = 36, kCubeBaseVertex = 7;

} // namespace

D3D12RenderAdapter::D3D12RenderAdapter(Application* app) : mApp(app)
{
    m_modelMatrix = glm::mat4(1.0f);
    m_viewMatrix = glm::mat4(1.0f);
    m_projectionMatrix = glm::mat4(1.0f);
    m_color = glm::vec4(1.0f);
}

D3D12RenderAdapter::~D3D12RenderAdapter()
{
    Shutdown();
}

// ============================================================ инициализация

bool D3D12RenderAdapter::Initialize()
{
    Logger::Info(std::string("Initializing D3D12RenderAdapter... (tearing ") +
                 (mApp->mTearingSupported ? "supported)" : "not supported)"));

#if defined(_MSC_VER) && (defined(DEBUG) || defined(_DEBUG))
    // Сообщения debug layer D3D12 (ошибки и предупреждения валидации) - в engine.log
    if (SUCCEEDED(mApp->md3dDevice.As(&mInfoQueue))) {
        auto callback = [](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR description, void*) {
            if (severity <= D3D12_MESSAGE_SEVERITY_ERROR)
                Logger::Error(std::string("D3D12: ") + description);
            else if (severity == D3D12_MESSAGE_SEVERITY_WARNING)
                Logger::Warning(std::string("D3D12: ") + description);
        };
        if (FAILED(mInfoQueue->RegisterMessageCallback(callback, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &mInfoQueueCookie)))
            mInfoQueue.Reset();
    }
    Logger::Info(mInfoQueue ? "D3D12 debug layer messages are routed to engine.log"
                            : "D3D12 debug layer message callback is not available");
#endif

    ThrowIfFailed(mApp->mCommandList->Reset(mApp->mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    if (!mvsByteCode || !mpsByteCode) {
        // Раньше ошибка загрузки шейдеров только логировалась, и движок падал дальше на nullptr
        Logger::Error("D3D12RenderAdapter: shaders are missing (Shaders/*.hlsl next to Engine.exe?)");
        mApp->mCommandList->Close();
        return false;
    }
    BuildGeometry();
    BuildPSO();

    // Дескрипторный хип для текстур
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 64;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mApp->md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mTextureSrvHeap)));
    mTextureSrvDescriptorSize = mApp->md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Белая текстура в слоте 0: пиксельный шейдер всегда читает t0, и раньше примитивы
    // (квадрат во втором окне) рисовались с непривязанной текстурой
    CreateWhiteTexture();

    // Закрываем command list (команды BuildGeometry + копирование текстуры)
    ThrowIfFailed(mApp->mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mApp->mCommandList.Get() };
    mApp->mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
    mApp->FlushCommandQueue();

    // Upload-буферы примитивов и белой текстуры больше не нужны: GPU всё скопировал
    mPrimitiveGeometry.VertexUploader.Reset();
    mPrimitiveGeometry.IndexUploader.Reset();
    mWhiteTexture.uploadBuffer.Reset();

    mInitialized = true;
    Logger::Info("D3D12RenderAdapter initialized successfully");
    return true;
}

void D3D12RenderAdapter::Shutdown()
{
    if (!mInitialized) return;
    Logger::Info("Shutting down D3D12RenderAdapter...");
    mApp->FlushCommandQueue();

#if defined(_MSC_VER) && (defined(DEBUG) || defined(_DEBUG))
    if (mInfoQueue) {
        mInfoQueue->UnregisterMessageCallback(mInfoQueueCookie);
        mInfoQueue.Reset();
    }
#endif

    m_loadedGPUMeshes.clear();
    mPrimitiveGeometry = GPUMesh{};
    mWhiteTexture.textureResource.Reset();
    mTextureSrvHeap.Reset();
    mPSO.Reset();
    mRootSignature.Reset();
    mInitialized = false;
}

void D3D12RenderAdapter::CreateWhiteTexture()
{
    mWhiteTexture.width = 1;
    mWhiteTexture.height = 1;
    mWhiteTexture.channels = 4;
    mWhiteTexture.pixels = { 255, 255, 255, 255 };
    mWhiteTexture.filePath = "__white__";
    mWhiteTexture.path = "__white__";
    mWhiteTexture.loaded = true;
    if (!CreateTexture(mWhiteTexture, mApp->md3dDevice.Get(), mApp->mCommandList.Get()))
        Logger::Error("Failed to create the white placeholder texture");
}

// ============================================================ кадр

D3D12RenderAdapter::FrameTarget D3D12RenderAdapter::GetFrameTarget(int windowIndex) const
{
    FrameTarget target;
    ComPtr<IDXGISwapChain3> sc3;
    UINT index = 0;

    if (windowIndex == 0) {
        if (SUCCEEDED(mApp->mSwapChain.As(&sc3))) index = sc3->GetCurrentBackBufferIndex();
        target.swapChain = mApp->mSwapChain.Get();
        target.buffer = mApp->mSwapChainBuffer[index].Get();
        target.rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mApp->mRtvHeap->GetCPUDescriptorHandleForHeapStart(), index, mApp->mRtvDescriptorSize);
        target.dsv = mApp->DepthStencilView();
        target.viewport = mApp->mScreenViewport;
        target.scissor = mApp->mScissorRect;
    }
    else {
        if (SUCCEEDED(mApp->mSecondarySwapChain.As(&sc3))) index = sc3->GetCurrentBackBufferIndex();
        target.swapChain = mApp->mSecondarySwapChain.Get();
        target.buffer = mApp->mSecondarySwapChainBuffer[index].Get();
        target.rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(mApp->mSecondaryRtvHeap->GetCPUDescriptorHandleForHeapStart(), index, mApp->mRtvDescriptorSize);
        // Раньше второе окно использовало viewport и буфер глубины главного окна
        target.dsv = mApp->mSecondaryDsvHeap->GetCPUDescriptorHandleForHeapStart();
        target.viewport = { 0.0f, 0.0f, (float)mApp->mSecondaryWidth, (float)mApp->mSecondaryHeight, 0.0f, 1.0f };
        target.scissor = { 0, 0, (LONG)mApp->mSecondaryWidth, (LONG)mApp->mSecondaryHeight };
    }
    return target;
}

void D3D12RenderAdapter::BeginFrame(int windowIndex) {
    ZoneScopedN("Render::BeginFrame");
    assert(mCurrentWindow < 0 && "BeginFrame called twice");
    mCurrentWindow = windowIndex;
    mCurrentTarget = GetFrameTarget(windowIndex);

    auto cmdListAlloc = mApp->mDirectCmdListAlloc;
    auto cmdList = mApp->mCommandList;

    // Прошлый кадр дождались в EndFrame, поэтому аллокатор можно сбрасывать всегда
    ThrowIfFailed(cmdListAlloc->Reset());
    ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSO.Get()));

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mCurrentTarget.buffer,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->RSSetViewports(1, &mCurrentTarget.viewport);
    cmdList->RSSetScissorRects(1, &mCurrentTarget.scissor);

    const EngineConfig& config = ConfigManager::Get().Config();
    const float* clearColor = windowIndex == 0 ? config.backgroundColor : config.secondaryBackgroundColor;
    cmdList->ClearRenderTargetView(mCurrentTarget.rtv, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(mCurrentTarget.dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &mCurrentTarget.rtv, true, &mCurrentTarget.dsv);
}

void D3D12RenderAdapter::EndFrame(int windowIndex) {
    assert(windowIndex == mCurrentWindow);
    (void)windowIndex;
    auto cmdList = mApp->mCommandList;

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(mCurrentTarget.buffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { cmdList.Get() };
    mApp->mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    {
        ZoneScopedN("Render::Present");
        // Без vsync - с tearing: иначе DWM ограничивает кадр частотой монитора и замеры бессмысленны
        const UINT presentFlags = (!mVSync && mApp->mTearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        ThrowIfFailed(mCurrentTarget.swapChain->Present(mVSync ? 1 : 0, presentFlags));
    }
    {
        ZoneScopedN("Render::WaitForGPU");
        mApp->FlushCommandQueue();
    }

    if (mCurrentWindow == 0) {
        // Счётчик D3DApp - по реальному индексу swap chain (раньше увеличивался вручную в Draw)
        ComPtr<IDXGISwapChain3> sc3;
        if (SUCCEEDED(mApp->mSwapChain.As(&sc3)))
            mApp->mCurrBackBuffer = (int)sc3->GetCurrentBackBufferIndex();
    }
    mCurrentWindow = -1;
}

// ============================================================ отрисовка

void D3D12RenderAdapter::BindTexture(ID3D12GraphicsCommandList* cmdList, TextureData* texture)
{
    const int srv = (texture && texture->srvIndex >= 0) ? texture->srvIndex : mWhiteTexture.srvIndex;
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle(mTextureSrvHeap->GetGPUDescriptorHandleForHeapStart(), srv, mTextureSrvDescriptorSize);
    cmdList->SetGraphicsRootDescriptorTable(1, handle);
}

void D3D12RenderAdapter::DrawPrimitiveGeometry(ID3D12GraphicsCommandList* cmdList, PrimitiveType type)
{
    switch (type) {
    case PrimitiveType::Triangle:
        cmdList->DrawIndexedInstanced(kTriangleIndexCount, 1, kTriangleIndexStart, 0, 0);
        break;
    case PrimitiveType::Square:
    case PrimitiveType::Quad:
        cmdList->DrawIndexedInstanced(kSquareIndexCount, 1, kSquareIndexStart, 0, 0);
        break;
    case PrimitiveType::Cube:
        // Раньше куб рисовал 36 индексов из буфера, где их было всего 9
        cmdList->DrawIndexedInstanced(kCubeIndexCount, 1, kCubeIndexStart, kCubeBaseVertex, 0);
        break;
    }
}

void D3D12RenderAdapter::DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position, float rotation, float scale) {
    auto cmdList = mApp->mCommandList;

    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, position.z)) *
                            glm::rotate(glm::mat4(1.0f), rotation, glm::vec3(0.0f, 0.0f, 1.0f)) *
                            glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    struct CB {
        glm::mat4 worldT, viewT, projT;
    } cb{ glm::transpose(world), glm::transpose(m_viewMatrix), glm::transpose(m_projectionMatrix) };

    cmdList->SetGraphicsRootSignature(mRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { mTextureSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    BindTexture(cmdList.Get(), nullptr);
    cmdList->SetGraphicsRoot32BitConstants(0, 48, &cb, 0);

    cmdList->IASetVertexBuffers(0, 1, &mPrimitiveGeometry.VBV);
    cmdList->IASetIndexBuffer(&mPrimitiveGeometry.IBV);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    DrawPrimitiveGeometry(cmdList.Get(), type);
}

void D3D12RenderAdapter::SetModelMatrix(const glm::mat4& matrix) {
    m_modelMatrix = matrix;
}

void D3D12RenderAdapter::SetColor(const glm::vec4& color) {
    m_color = color;
}

void D3D12RenderAdapter::SetViewProjection(const glm::mat4& view, const glm::mat4& proj) {
    m_viewMatrix = view;
    m_projectionMatrix = proj;
}

void D3D12RenderAdapter::DrawPrimitiveECS(PrimitiveType type) {
    // Полная матрица модели (раньше из неё выдирались только сдвиг, поворот вокруг Z и масштаб по X)
    DrawItem item;
    item.worldT = glm::transpose(m_modelMatrix);
    item.primitive = type;
    item.texture = m_currentTexture;
    DrawItems(&item, 1);
}

void D3D12RenderAdapter::SetTexture(TextureData* texture) {
    m_currentTexture = texture;
}

void D3D12RenderAdapter::DrawMesh(const GPUMesh& gpuMesh) {
    if (!mApp || !mApp->mCommandList || gpuMesh.IndexCount == 0) return;
    DrawItem item;
    item.worldT = glm::transpose(m_modelMatrix);
    item.mesh = &gpuMesh;
    item.texture = m_currentTexture; // без текстуры - белая (раньше - предупреждение в лог на каждый кадр)
    DrawItems(&item, 1);
}

void D3D12RenderAdapter::DrawItems(const DrawItem* items, size_t count)
{
    if (count == 0) return;
    ZoneScopedN("Render::DrawItems");
    ID3D12GraphicsCommandList* cmdList = mApp->mCommandList.Get();

    cmdList->SetGraphicsRootSignature(mRootSignature.Get());
    ID3D12DescriptorHeap* heaps[] = { mTextureSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Вид и проекция - один раз на пакет (константы 16..47), мировая матрица - на каждый объект (0..15)
    const FrameConstants frame{ glm::transpose(m_viewMatrix), glm::transpose(m_projectionMatrix) };
    cmdList->SetGraphicsRoot32BitConstants(0, 32, &frame, 16);

    const GPUMesh* boundGeometry = nullptr;
    int boundSrv = -2;
    for (size_t i = 0; i < count; ++i) {
        const DrawItem& item = items[i];

        const int srv = (item.texture && item.texture->srvIndex >= 0) ? item.texture->srvIndex : mWhiteTexture.srvIndex;
        if (srv != boundSrv) {
            BindTexture(cmdList, item.texture);
            boundSrv = srv;
        }
        cmdList->SetGraphicsRoot32BitConstants(0, 16, &item.worldT, 0);

        const GPUMesh* geometry = item.mesh ? item.mesh : &mPrimitiveGeometry;
        if (geometry != boundGeometry) {
            cmdList->IASetVertexBuffers(0, 1, &geometry->VBV);
            cmdList->IASetIndexBuffer(&geometry->IBV);
            boundGeometry = geometry;
        }
        if (item.mesh)
            cmdList->DrawIndexedInstanced(item.mesh->IndexCount, 1, 0, 0, 0);
        else
            DrawPrimitiveGeometry(cmdList, item.primitive);
    }
}

// ============================================================ ресурсы

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

    // Upload-буферы сохраняются в самом GPUMesh (раньше - в одном общем члене адаптера)
    const UINT vbByteSize = static_cast<UINT>(subMesh.vertices.size() * sizeof(Vertex3D));
    gpuMesh.VertexBuffer = d3dUtil::CreateDefaultBuffer(
        device, cmdList,
        subMesh.vertices.data(), vbByteSize,
        gpuMesh.VertexUploader);

    const UINT ibByteSize = static_cast<UINT>(subMesh.indices.size() * sizeof(uint32_t));
    gpuMesh.IndexBuffer = d3dUtil::CreateDefaultBuffer(
        device, cmdList,
        subMesh.indices.data(), ibByteSize,
        gpuMesh.IndexUploader);

    gpuMesh.VBV.BufferLocation = gpuMesh.VertexBuffer->GetGPUVirtualAddress();
    gpuMesh.VBV.StrideInBytes = sizeof(Vertex3D);
    gpuMesh.VBV.SizeInBytes = vbByteSize;

    gpuMesh.IBV.BufferLocation = gpuMesh.IndexBuffer->GetGPUVirtualAddress();
    gpuMesh.IBV.Format = DXGI_FORMAT_R32_UINT;
    gpuMesh.IBV.SizeInBytes = ibByteSize;

    Logger::Info("GPU Upload recorded: " + meshData.filePath);
    return gpuMesh;
}

bool D3D12RenderAdapter::UploadMeshToGPU(MeshData& meshData)
{
    if (meshData.gpuMesh.IndexCount > 0) return true; // уже на GPU
    assert(mCurrentWindow < 0 && "UploadMeshToGPU must be called outside BeginFrame/EndFrame");

    ThrowIfFailed(mApp->mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mApp->mCommandList->Reset(mApp->mDirectCmdListAlloc.Get(), nullptr));
    GPUMesh gpu = UploadMesh(meshData, mApp->md3dDevice.Get(), mApp->mCommandList.Get());
    ThrowIfFailed(mApp->mCommandList->Close());
    if (gpu.IndexCount == 0) return false;

    ID3D12CommandList* cmdLists[] = { mApp->mCommandList.Get() };
    mApp->mCommandQueue->ExecuteCommandLists(1, cmdLists);
    mApp->FlushCommandQueue();

    gpu.VertexUploader.Reset(); // копирование выполнено - промежуточные буферы больше не нужны
    gpu.IndexUploader.Reset();
    meshData.gpuMesh = gpu;
    Logger::Info("Mesh uploaded to GPU: " + meshData.filePath);
    return true;
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

    if (m_nextSRVIndex >= 64) {
        Logger::Error("CreateTexture: SRV heap is full (64 textures)");
        return false;
    }
    // Назначаем индекс SRV
    int index = m_nextSRVIndex++;
    textureData.textureResource = texture;
    textureData.uploadBuffer = uploadBuffer;   // сохраняем, чтобы не удалился до выполнения команд
    textureData.srvIndex = index;
    m_textureSRVIndices[textureData.filePath] = index;

    CreateTextureSRV(textureData.textureResource, index);
    Logger::Info("Texture uploaded to GPU: " + textureData.filePath + " (SRV slot " + std::to_string(index) + ")");
    return true;
}

bool D3D12RenderAdapter::CreateTexture(TextureData& textureData)
{
    if (!textureData.IsValid() || textureData.srvIndex >= 0)
        return false;

    return CreateTexture(textureData, mApp->GetDevice(), mApp->GetCommandList());
}

ComPtr<ID3DBlob> D3D12RenderAdapter::CompileShaderFromFile(const std::string& filePath,
    const std::string& entryPoint,
    const std::string& target)
{
    ComPtr<ID3DBlob> shaderBlob;
    ComPtr<ID3DBlob> errorBlob;

    // Отладочная информация и без оптимизаций - только в Debug (раньше всегда, и в Release тоже)
    UINT flags = 0;
#if defined(DEBUG) || defined(_DEBUG)
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    HRESULT hr = D3DCompileFromFile(
        ToWide(filePath).c_str(), // раньше путь "расширялся" побайтно - ломался на кириллице
        nullptr,
        D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint.c_str(),
        target.c_str(),
        flags,
        0,
        &shaderBlob,
        &errorBlob);

    if (FAILED(hr)) {
        if (errorBlob) {
            Logger::Error("Shader compilation error: " +
                std::string((char*)errorBlob->GetBufferPointer()));
        }
        else {
            Logger::Error("Shader compilation failed: " + filePath + " (file not found?)");
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
}

bool D3D12RenderAdapter::UploadTextureToGPU(TextureData& textureData)
{
    // Уже на GPU — не ошибка (uploader вызывается один раз, но на всякий случай идемпотентно)
    if (textureData.srvIndex >= 0) return true;
    if (!textureData.IsValid())   return false;

    assert(mCurrentWindow < 0 && "UploadTextureToGPU must be called outside BeginFrame/EndFrame");

    ThrowIfFailed(mApp->mDirectCmdListAlloc->Reset());
    ThrowIfFailed(mApp->mCommandList->Reset(mApp->mDirectCmdListAlloc.Get(), nullptr));

    bool success = CreateTexture(textureData, mApp->md3dDevice.Get(), mApp->mCommandList.Get());
    ThrowIfFailed(mApp->mCommandList->Close());

    if (success) {
        ID3D12CommandList* cmdLists[] = { mApp->mCommandList.Get() };
        mApp->mCommandQueue->ExecuteCommandLists(1, cmdLists);
        mApp->FlushCommandQueue();
        textureData.uploadBuffer.Reset();
    }
    return success;
}

// ============================================================ конвейер

void D3D12RenderAdapter::BuildRootSignature()
{
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];

    // 0 — константы (World, View, Proj)
    slotRootParameter[0].InitAsConstants(48, 0);

    // 1 — таблица дескрипторов для текстуры (t0)
    CD3DX12_DESCRIPTOR_RANGE texTable;
    texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    slotRootParameter[1].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

    // Статический сэмплер (s0)
    CD3DX12_STATIC_SAMPLER_DESC samplerDesc(0,
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,
        D3D12_TEXTURE_ADDRESS_MODE_WRAP);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter,
        1, &samplerDesc,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig;
    ComPtr<ID3DBlob> errorBlob;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (FAILED(hr)) {
        if (errorBlob) Logger::Error("Root Signature serialize error: " + std::string((char*)errorBlob->GetBufferPointer()));
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(mApp->md3dDevice->CreateRootSignature(0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}

void D3D12RenderAdapter::BuildShadersAndInputLayout()
{
    Logger::Info("Loading shaders from files...");

    mShaderProgram = CreateShaderProgram(
        "Shaders/VertexShader.hlsl",
        "Shaders/PixelShader.hlsl"
    );

    if (!mShaderProgram || !mShaderProgram->IsValid())
    {
        Logger::Error("Failed to load shaders from files!");
        return;
    }

    mvsByteCode = mShaderProgram->vsBlob;
    mpsByteCode = mShaderProgram->psBlob;

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    Logger::Info("Shaders successfully loaded from files.");
}

void D3D12RenderAdapter::BuildGeometry() {
    struct PrimitiveVertex {
        XMFLOAT3 Pos;
        XMFLOAT4 Color;
        XMFLOAT2 TexCoord;
    };

    PrimitiveVertex vertices[] =
    {
        // ТРЕУГОЛЬНИК (0, 1, 2)
        { XMFLOAT3(0.0f,  0.5f, 0.5f), XMFLOAT4(Colors::Red),    XMFLOAT2(0.5f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Green),  XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Blue),   XMFLOAT2(0.0f, 1.0f) },

        // КВАДРАТ (3, 4, 5, 6)
        { XMFLOAT3(-0.5f,  0.5f, 0.5f), XMFLOAT4(Colors::Cyan),    XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(0.5f,  0.5f, 0.5f), XMFLOAT4(Colors::Magenta), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Yellow),  XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::White),   XMFLOAT2(0.0f, 1.0f) },

        // КУБ (7..14) - раньше тип Cube был, а геометрии для него не было
        { XMFLOAT3(-0.5f, -0.5f, -0.5f), XMFLOAT4(Colors::White),   XMFLOAT2(0.0f, 1.0f) },
        { XMFLOAT3(-0.5f,  0.5f, -0.5f), XMFLOAT4(Colors::Black),   XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3( 0.5f,  0.5f, -0.5f), XMFLOAT4(Colors::Red),     XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3( 0.5f, -0.5f, -0.5f), XMFLOAT4(Colors::Green),   XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f,  0.5f), XMFLOAT4(Colors::Blue),    XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f,  0.5f,  0.5f), XMFLOAT4(Colors::Yellow),  XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3( 0.5f,  0.5f,  0.5f), XMFLOAT4(Colors::Cyan),    XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3( 0.5f, -0.5f,  0.5f), XMFLOAT4(Colors::Magenta), XMFLOAT2(0.0f, 1.0f) },
    };

    std::uint16_t indices[] = {
        0, 1, 2,               // Треугольник
        3, 4, 5, 3, 5, 6,      // Квадрат (два треугольника)
        // Куб (индексы относительно вершины 7)
        0, 1, 2, 0, 2, 3,      // передняя
        4, 6, 5, 4, 7, 6,      // задняя
        4, 5, 1, 4, 1, 0,      // левая
        3, 2, 6, 3, 6, 7,      // правая
        1, 5, 6, 1, 6, 2,      // верхняя
        4, 0, 3, 4, 3, 7       // нижняя
    };

    const UINT vbByteSize = sizeof(vertices);
    const UINT ibByteSize = sizeof(indices);

    mPrimitiveGeometry.VertexBuffer = d3dUtil::CreateDefaultBuffer(mApp->md3dDevice.Get(), mApp->mCommandList.Get(),
        vertices, vbByteSize, mPrimitiveGeometry.VertexUploader);
    mPrimitiveGeometry.IndexBuffer = d3dUtil::CreateDefaultBuffer(mApp->md3dDevice.Get(), mApp->mCommandList.Get(),
        indices, ibByteSize, mPrimitiveGeometry.IndexUploader);

    mPrimitiveGeometry.VBV.BufferLocation = mPrimitiveGeometry.VertexBuffer->GetGPUVirtualAddress();
    mPrimitiveGeometry.VBV.StrideInBytes = sizeof(PrimitiveVertex);  // 32 байта (12+16+8)
    mPrimitiveGeometry.VBV.SizeInBytes = vbByteSize;

    mPrimitiveGeometry.IBV.BufferLocation = mPrimitiveGeometry.IndexBuffer->GetGPUVirtualAddress();
    mPrimitiveGeometry.IBV.Format = DXGI_FORMAT_R16_UINT;
    mPrimitiveGeometry.IBV.SizeInBytes = ibByteSize;
    mPrimitiveGeometry.IndexCount = _countof(indices);
}

void D3D12RenderAdapter::BuildPSO() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()), mvsByteCode->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()), mpsByteCode->GetBufferSize() };
    CD3DX12_RASTERIZER_DESC rasterDesc(D3D12_DEFAULT);
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE;  // Было D3D12_CULL_MODE_BACK
    psoDesc.RasterizerState = rasterDesc;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mApp->mBackBufferFormat;
    psoDesc.SampleDesc.Count = 1; // MSAA в swap chain с FLIP_DISCARD не поддерживается
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = mApp->mDepthStencilFormat;

    ThrowIfFailed(mApp->md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}
