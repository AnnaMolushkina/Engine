#pragma once
#include "RenderAdapter.h"
#include <DirectXMath.h>
#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
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
    virtual void BeginFrame(int windowIndex = 0) override;
    virtual void EndFrame(int windowIndex = 0) override;
    virtual void DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position = { 0,0,0 }, float rotation = 0.0f, float scale = 1.0f) override;
    virtual void SetModelMatrix(const glm::mat4& matrix) override;
    virtual void SetColor(const glm::vec4& color) override;
    virtual void SetViewProjection(const glm::mat4& view, const glm::mat4& proj) override;
    virtual void DrawPrimitiveECS(PrimitiveType type) override;

    virtual void DrawMesh(const GPUMesh& gpuMesh) override;
    virtual void SetTexture(TextureData* texture) override;
    virtual void SetShader(std::shared_ptr<ShaderProgram> shader) override;

    // Загрузка меша на GPU
    GPUMesh UploadMesh(const MeshData& meshData,
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmdList);

    bool CreateTexture(TextureData& textureData, ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
    bool CreateTexture(TextureData& textureData);  // упрощённая версия
    //поле для хранения шейдерной программы
    std::shared_ptr<ShaderProgram> mShaderProgram;

    // Компиляция одного шейдера из файла
    ComPtr<ID3DBlob> CompileShaderFromFile(const std::string& filePath,
        const std::string& entryPoint,
        const std::string& target);

    // Создание шейдерной программы (VS + PS)
    std::shared_ptr<ShaderProgram> CreateShaderProgram(const std::string& vsPath,
        const std::string& psPath);

    void CreateTextureSRV(ComPtr<ID3D12Resource> textureResource, int index = 0);

    TextureData* GetMainTexture() { return &m_mainTextureData; }

    bool UploadTextureToGPU(TextureData& textureData);

    std::shared_ptr<ShaderProgram> m_currentShader = nullptr;

private:
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildGeometry();
    void BuildPSO();

private:
    Application* mApp = nullptr;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12PipelineState> mPSO = nullptr;
    ComPtr<ID3DBlob> mvsByteCode = nullptr;
    ComPtr<ID3DBlob> mpsByteCode = nullptr;
    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    ComPtr<ID3D12Resource> mVertexBuffer = nullptr;
    ComPtr<ID3D12Resource> mVertexBufferUploader = nullptr;
    D3D12_VERTEX_BUFFER_VIEW mVBV;

    ComPtr<ID3D12Resource> mIndexBuffer = nullptr;
    ComPtr<ID3D12Resource> mIndexBufferUploader = nullptr;
    D3D12_INDEX_BUFFER_VIEW mIBV;

    std::unordered_map<std::string, GPUMesh> m_loadedGPUMeshes;

    glm::mat4 m_modelMatrix = glm::mat4(1.0f);
    glm::mat4 m_viewMatrix = glm::mat4(1.0f);
    glm::mat4 m_projectionMatrix = glm::mat4(1.0f);
    glm::vec4 m_color = glm::vec4(1.0f);

    float m_currentRotation = 0.0f;

    // Дескрипторный хип для SRV текстур
    ComPtr<ID3D12DescriptorHeap> mTextureSrvHeap = nullptr;
    UINT mTextureSrvDescriptorSize = 0;
    ComPtr<ID3D12Resource> mPlaceholderTexture = nullptr;  // Текстура-заглушка

    std::unordered_map<std::string, int> m_textureSRVIndices;  // Путь к текстуре индекс SRV
    int m_nextSRVIndex = 0;
    TextureData* m_currentTexture = nullptr;  // Текущая текстура для отрисовки

    TextureData m_mainTextureData;
};