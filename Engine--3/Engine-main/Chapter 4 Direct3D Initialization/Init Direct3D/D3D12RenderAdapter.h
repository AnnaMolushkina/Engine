#pragma once
#include "RenderAdapter.h"
#include <DirectXMath.h>
#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "Logger.h"
#include "MeshData.h"
#include "TextureData.h"
#include <unordered_map>
#include "GPUMesh.h"
#include "ShaderProgram.h"
#include <memory>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex {
    XMFLOAT3 Pos;
    XMFLOAT4 Color;
};

class Application;

class D3D12RenderAdapter : public RenderAdapter {
public:

    D3D12RenderAdapter(Application* app);
    virtual ~D3D12RenderAdapter();

    virtual bool Initialize() override;
    virtual void Shutdown() override;
    virtual void SetVSync(bool enabled) override { mVSync = enabled; }
    virtual void BeginFrame(int windowIndex = 0) override;
    virtual void EndFrame(int windowIndex = 0) override;
    virtual void DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position = { 0,0,0 }, float rotation = 0.0f, float scale = 1.0f) override;
    virtual void SetModelMatrix(const glm::mat4& matrix) override;
    virtual void SetColor(const glm::vec4& color) override;
    virtual void SetViewProjection(const glm::mat4& view, const glm::mat4& proj) override;
    virtual void DrawPrimitiveECS(PrimitiveType type) override;

    virtual void DrawMesh(const GPUMesh& gpuMesh) override;
    virtual void SetTexture(TextureData* texture) override;
    virtual void DrawItems(const DrawItem* items, size_t count) override;

    // Загрузка меша на GPU: команды копирования пишутся в cmdList (upload-буферы сохраняются в GPUMesh)
    GPUMesh UploadMesh(const MeshData& meshData,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList);

    // Загрузка меша на GPU "здесь и сейчас" (вне кадра): запись, выполнение, ожидание GPU
    bool UploadMeshToGPU(MeshData& meshData);

    bool CreateTexture(TextureData& textureData, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    bool CreateTexture(TextureData& textureData);  // упрощённая версия
    // Шейдерная программа (VS + PS)
    std::shared_ptr<ShaderProgram> mShaderProgram;

    // Компиляция шейдера из файла
    ComPtr<ID3DBlob> CompileShaderFromFile(const std::string& filePath,
        const std::string& entryPoint,
        const std::string& target);

    // Создание шейдерной программы (VS + PS)
    std::shared_ptr<ShaderProgram> CreateShaderProgram(const std::string& vsPath,
        const std::string& psPath);

    void CreateTextureSRV(ComPtr<ID3D12Resource> textureResource, int index = 0);

    TextureData* GetMainTexture() { return &m_mainTextureData; }

    // Загрузка текстуры на GPU "здесь и сейчас" (вне кадра)
    bool UploadTextureToGPU(TextureData& textureData);

private:
    // Куда рисуется текущий кадр (главное окно или второе)
    struct FrameTarget {
        IDXGISwapChain* swapChain = nullptr;
        ID3D12Resource* buffer = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = {};
        D3D12_VIEWPORT viewport = {};
        D3D12_RECT scissor = {};
    };
    FrameTarget GetFrameTarget(int windowIndex) const;

    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildGeometry();
    void BuildPSO();
    void CreateWhiteTexture();
    void BindTexture(ID3D12GraphicsCommandList* cmdList, TextureData* texture);
    void DrawPrimitiveGeometry(ID3D12GraphicsCommandList* cmdList, PrimitiveType type);

private:
    Application* mApp = nullptr;
    bool mInitialized = false;
    bool mVSync = false;
    int mCurrentWindow = -1;
    FrameTarget mCurrentTarget;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12PipelineState> mPSO = nullptr;
    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    // Геометрия базовых примитивов (треугольник, квадрат, куб) в одном буфере
    GPUMesh mPrimitiveGeometry;

    std::unordered_map<std::string, GPUMesh> m_loadedGPUMeshes;

    glm::mat4 m_modelMatrix = glm::mat4(1.0f);
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    glm::vec4 m_color = glm::vec4(1.0f);

    float m_currentRotation = 0.0f;

    // Дескрипторный хип для SRV текстур
    ComPtr<ID3D12DescriptorHeap> mTextureSrvHeap = nullptr;
    UINT mTextureSrvDescriptorSize = 0;
    ComPtr<ID3D12Resource> mPlaceholderTexture = nullptr;  // текстура-заглушка

    std::unordered_map<std::string, int> m_textureSRVIndices;  // путь → индекс в хипе SRV
    int m_nextSRVIndex = 0;
    TextureData* m_currentTexture = nullptr;  // текущая текстура для отрисовки

    TextureData m_mainTextureData;
    TextureData mWhiteTexture; // 1x1 белая: для примитивов и мешей без текстуры (слот SRV 0)

#if defined(_MSC_VER) && (defined(DEBUG) || defined(_DEBUG))
    Microsoft::WRL::ComPtr<ID3D12InfoQueue1> mInfoQueue; // сообщения debug layer -> engine.log
    DWORD mInfoQueueCookie = 0;
#endif
};
