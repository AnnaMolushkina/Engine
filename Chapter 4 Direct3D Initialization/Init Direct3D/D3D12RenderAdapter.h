#pragma once

#include "RenderAdapter.h"
#include <DirectXMath.h>
#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "Logger.h"

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
    virtual void DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position = {0,0,0}, float rotation = 0.0f, float scale = 1.0f) override;

private:
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildGeometry();
    void BuildPSO();

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
};
