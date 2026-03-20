#include "RenderAdapter.h"
#include "../../Common/d3dApp.h"
#include <DirectXColors.h>
#include "Logger.h"
#include "GameStateManager.h"
#include "RenderAdapter.h"
#include "D3D12RenderAdapter.h"
#include "MainMenuState.h"
#include "PauseState.h"
#include "PlayState.h"
#include "ConfigManager.h"
#include <vector>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

// Предварительное объявление оконной процедуры
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Класс Application (Управляет жизненным циклом)
class Application : public D3DApp
{
    friend class D3D12RenderAdapter;

public:
    Application(HINSTANCE hInstance);
    ~Application();

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void CreateSecondaryWindow();
    void CreateSecondarySwapChain();

    GameStateManager mStateManager;
    std::unique_ptr<RenderAdapter> mRenderAdapter;

    HWND mhSecondaryWnd = nullptr;
    ComPtr<IDXGISwapChain> mSecondarySwapChain = nullptr;
    ComPtr<ID3D12Resource> mSecondarySwapChainBuffer[SwapChainBufferCount];
    ComPtr<ID3D12DescriptorHeap> mSecondaryRtvHeap = nullptr;
    int mFrameCount = 0; // Для ограничения потока логов
    bool mShowSecondaryWnd = false;
};

// Главная точка входа
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
#endif
    // Шаг 4: Инициализируем систему логирования
    Logger::Init();
    Logger::Info("Application Startup - Game Engine");

    try
    {
        Application theApp(hInstance);
        if(!theApp.Initialize())
        {
            Logger::Error("Failed to initialize the application.");
            return 0;
        }

        Logger::Info("Application successfully initialized. Starting the main Game Loop.");
        // Здесь запускается игровой цикл (внутри D3DApp::Run)
        return theApp.Run();
    }
    catch(DxException& e)
    {
        Logger::Error(e.ToString());
        return 0;
    }
}

Application::Application(HINSTANCE hInstance)
: D3DApp(hInstance) 
{
}

Application::~Application()
{
}

bool Application::Initialize()
{
    // Загружаем конфиг
    ConfigManager::Get().Load("config.json");

    if(!D3DApp::Initialize())
        return false;

    // Создаем второе окно
    CreateSecondaryWindow();
    CreateSecondarySwapChain();

    mRenderAdapter = std::make_unique<D3D12RenderAdapter>(this);
    if (!mRenderAdapter->Initialize())
    {
        return false;
    }

    mStateManager.ChangeState(std::make_shared<MainMenuState>());

    return true;
}

void Application::CreateSecondaryWindow()
{
    WNDCLASSW wc = {0};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = mhAppInst;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = L"SecondaryWnd";

    RegisterClassW(&wc);

    mhSecondaryWnd = CreateWindowW(L"SecondaryWnd", L"Secondary Window (Square)",
        WS_OVERLAPPEDWINDOW, 100, 100, 400, 400, 0, 0, mhAppInst, 0);

    ShowWindow(mhSecondaryWnd, SW_HIDE);
    UpdateWindow(mhSecondaryWnd);
}

void Application::CreateSecondarySwapChain()
{
    // Инициализируем вторичный RTV хэп
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mSecondaryRtvHeap.GetAddressOf())));

    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = 400;
    sd.BufferDesc.Height = 400;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = mBackBufferFormat;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SwapChainBufferCount;
    sd.OutputWindow = mhSecondaryWnd;
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(mdxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, mSecondarySwapChain.GetAddressOf()));

    // Создаем RTV для вторичного окна
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mSecondaryRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++)
    {
        ThrowIfFailed(mSecondarySwapChain->GetBuffer(i, IID_PPV_ARGS(&mSecondarySwapChainBuffer[i])));
        md3dDevice->CreateRenderTargetView(mSecondarySwapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, mRtvDescriptorSize);
    }
}

void Application::OnResize()
{
	D3DApp::OnResize();
}

void Application::Update(const GameTimer& gt)
{
    mFrameCount++;
    if(mFrameCount <= 500 && (mFrameCount % 100 == 0 || mFrameCount == 1))
    {
        Logger::Info("Frame " + to_string(mFrameCount) + " | DeltaTime: " + to_string(gt.DeltaTime()) + "s");
    }

    // Переключение второго окна на клавишу 'W'
    if (GetAsyncKeyState('W') & 0x8000) {
        Sleep(200); 
        mShowSecondaryWnd = !mShowSecondaryWnd;
        ShowWindow(mhSecondaryWnd, mShowSecondaryWnd ? SW_SHOW : SW_HIDE);
        Logger::Info(mShowSecondaryWnd ? "Secondary Window: SHOWN" : "Secondary Window: HIDDEN");
    }

    mStateManager.Update(gt);
    mStateManager.ProcessKeyboardInput(gt);
}

void Application::Draw(const GameTimer& gt)
{
    // Отрисовка в основное окно (Треугольник)
    mRenderAdapter->BeginFrame(0);
    mStateManager.Draw(gt, mRenderAdapter.get());
    mRenderAdapter->EndFrame(0);

    if (mShowSecondaryWnd) {
        // Отрисовка во второе окно (Квадрат)
        mRenderAdapter->BeginFrame(1);
        mRenderAdapter->DrawPrimitive(PrimitiveType::Square, {0,0,0}, 0.0f, 0.5f);
        mRenderAdapter->EndFrame(1);
    }

    // Синхронное обновление счетчика буферов для следующего кадра
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
}

void Application::OnMouseDown(WPARAM btnState, int x, int y)
{
    mStateManager.OnMouseDown(btnState, x, y);
}

void Application::OnMouseUp(WPARAM btnState, int x, int y)
{
    mStateManager.OnMouseUp(btnState, x, y);
}

void Application::OnMouseMove(WPARAM btnState, int x, int y)
{
    mStateManager.OnMouseMove(btnState, x, y);
}


// Реализация D3D12RenderAdapter
// У класса есть доступ к protected членам D3DApp благодаря friend class D3D12RenderAdapter


D3D12RenderAdapter::D3D12RenderAdapter(Application* app) : mApp(app) {}

D3D12RenderAdapter::~D3D12RenderAdapter() {}

bool D3D12RenderAdapter::Initialize() {
    Logger::Info("Initializing D3D12RenderAdapter...");
    
    // Сброс командного списка для создания буферов
    ThrowIfFailed(mApp->mCommandList->Reset(mApp->mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildGeometry();
    BuildPSO();

    // Завершение команд инициализации
    ThrowIfFailed(mApp->mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mApp->mCommandList.Get() };
    mApp->mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    mApp->FlushCommandQueue();

    return true;
}

void D3D12RenderAdapter::BeginFrame(int windowIndex) {
    auto cmdListAlloc = mApp->mDirectCmdListAlloc;
    auto cmdList = mApp->mCommandList;

    if (windowIndex == 0) {
        ThrowIfFailed(cmdListAlloc->Reset());
    }
    ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSO.Get()));
    // Получаем индекс текущего буфера для конкретного окна
    UINT backBufferIndex = 0;
    ComPtr<IDXGISwapChain3> sc3;
    if (windowIndex == 0) {
        if (SUCCEEDED(mApp->mSwapChain.As(&sc3))) {
            backBufferIndex = sc3->GetCurrentBackBufferIndex();
        }
    } else {
        if (SUCCEEDED(mApp->mSecondarySwapChain.As(&sc3))) {
            backBufferIndex = sc3->GetCurrentBackBufferIndex();
        }
    }
    // Выбор ресурса и дескриптора RTV
    ID3D12Resource* currentBuffer = (windowIndex == 0) ? mApp->mSwapChainBuffer[backBufferIndex].Get() : mApp->mSecondarySwapChainBuffer[backBufferIndex].Get();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = (windowIndex == 0) ? 
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mApp->mRtvHeap->GetCPUDescriptorHandleForHeapStart(), backBufferIndex, mApp->mRtvDescriptorSize) : 
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mApp->mSecondaryRtvHeap->GetCPUDescriptorHandleForHeapStart(), backBufferIndex, mApp->mRtvDescriptorSize);
    // Переход ресурса и очистка
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(currentBuffer,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->RSSetViewports(1, &mApp->mScreenViewport);
    cmdList->RSSetScissorRects(1, &mApp->mScissorRect);

    // Цвет из JSON
    const float* clearColor = ConfigManager::Get().GetBackgroundColor();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mApp->DepthStencilView();
    cmdList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
    cmdList->OMSetRenderTargets(1, &rtvHandle, true, &dsvHandle);
}

void D3D12RenderAdapter::EndFrame(int windowIndex) {
    auto cmdList = mApp->mCommandList;
    
    UINT backBufferIndex = 0;
    ComPtr<IDXGISwapChain3> sc3;
    if (windowIndex == 0) {
        if (SUCCEEDED(mApp->mSwapChain.As(&sc3))) {
            backBufferIndex = sc3->GetCurrentBackBufferIndex();
        }
    } else {
        if (SUCCEEDED(mApp->mSecondarySwapChain.As(&sc3))) {
            backBufferIndex = sc3->GetCurrentBackBufferIndex();
        }
    }

    ID3D12Resource* currentBuffer = (windowIndex == 0) ? mApp->mSwapChainBuffer[backBufferIndex].Get() : mApp->mSecondarySwapChainBuffer[backBufferIndex].Get();

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(currentBuffer,
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    cmdList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(cmdList->Close());

    ID3D12CommandList* cmdsLists[] = { cmdList.Get() };
    mApp->mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    if (windowIndex == 0) {
        ThrowIfFailed(mApp->mSwapChain->Present(0, 0));
    } else {
        ThrowIfFailed(mApp->mSecondarySwapChain->Present(0, 0));
    }
    
    mApp->FlushCommandQueue();
}

void D3D12RenderAdapter::DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position, float rotation, float scale) {
    auto cmdList = mApp->mCommandList;

    XMMATRIX s = XMMatrixScaling(scale, scale, 1.0f);
    XMMATRIX r = XMMatrixRotationZ(rotation);
    XMMATRIX t = XMMatrixTranslation(position.x, position.y, position.z);
    XMMATRIX world = s * r * t;
    XMMATRIX transposedWorld = XMMatrixTranspose(world);

    cmdList->SetGraphicsRootSignature(mRootSignature.Get());
    cmdList->SetGraphicsRoot32BitConstants(0, 16, &transposedWorld, 0);

    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (type == PrimitiveType::Triangle) {
        cmdList->DrawIndexedInstanced(3, 1, 0, 0, 0);
    } else {
        // Квадрат начинается с 3-го индекса в буфере
        cmdList->DrawIndexedInstanced(6, 1, 3, 0, 0);
    }
}

void D3D12RenderAdapter::BuildRootSignature() {
    // Создаем Root Signature с одним корневым параметром — таблицей констант (16 float для матрицы 4х4)
    CD3DX12_ROOT_PARAMETER slotRootParameter[1];
    slotRootParameter[0].InitAsConstants(16, 0); // 16 констант в register(b0)

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(1, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());
    if (errorBlob) {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }

    ThrowIfFailed(mApp->md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void D3D12RenderAdapter::BuildShadersAndInputLayout() {
    const char* shaderCode = 
        "cbuffer cbPerObject : register(b0) { \n"
        "    float4x4 gWorld; \n"
        "}; \n"
        "struct VertexIn { \n"
        "    float3 PosL  : POSITION; \n"
        "    float4 Color : COLOR; \n"
        "}; \n"
        "struct VertexOut { \n"
        "    float4 PosH  : SV_POSITION; \n"
        "    float4 Color : COLOR; \n"
        "}; \n"
        "VertexOut VS(VertexIn vin) { \n"
        "    VertexOut vout; \n"
        "    vout.PosH = mul(float4(vin.PosL, 1.0f), gWorld); \n"
        "    vout.Color = vin.Color; \n"
        "    return vout; \n"
        "} \n"
        "float4 PS(VertexOut pin) : SV_Target { \n"
        "    return pin.Color; \n"
        "} \n";

    ComPtr<ID3DBlob> errors;
    HRESULT hrVS = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &mvsByteCode, &errors);
    if(errors) Logger::Error((char*)errors->GetBufferPointer());
    
    HRESULT hrPS = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &mpsByteCode, &errors);
    if(errors) Logger::Error((char*)errors->GetBufferPointer());

    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

void D3D12RenderAdapter::BuildGeometry() {
    Vertex vertices[] =
    {
        // ТРЕУГОЛЬНИК (0, 1, 2)
        { XMFLOAT3( 0.0f,  0.5f, 0.5f), XMFLOAT4(Colors::Red) },
        { XMFLOAT3( 0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Green) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Blue) },

        // КВАДРАТ (3, 4, 5, 6)
        { XMFLOAT3(-0.5f,  0.5f, 0.5f), XMFLOAT4(Colors::Cyan) },
        { XMFLOAT3( 0.5f,  0.5f, 0.5f), XMFLOAT4(Colors::Magenta) },
        { XMFLOAT3( 0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Yellow) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::White) }
    };

    std::uint16_t indices[] = { 
        0, 1, 2,               // Треугольник
        3, 4, 5, 3, 5, 6       // Квадрат (два треугольника)
    };

    const UINT vbByteSize = sizeof(vertices);
    const UINT ibByteSize = sizeof(indices);

    mVertexBuffer = d3dUtil::CreateDefaultBuffer(mApp->md3dDevice.Get(), mApp->mCommandList.Get(), vertices, vbByteSize, mVertexBufferUploader);
    mIndexBuffer = d3dUtil::CreateDefaultBuffer(mApp->md3dDevice.Get(), mApp->mCommandList.Get(), indices, ibByteSize, mIndexBufferUploader);

    mVBV.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVBV.StrideInBytes = sizeof(Vertex);
    mVBV.SizeInBytes = vbByteSize;

    mIBV.BufferLocation = mIndexBuffer->GetGPUVirtualAddress();
    mIBV.Format = DXGI_FORMAT_R16_UINT;
    mIBV.SizeInBytes = ibByteSize;
}

void D3D12RenderAdapter::BuildPSO() {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    psoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
    psoDesc.pRootSignature = mRootSignature.Get();
    psoDesc.VS = { reinterpret_cast<BYTE*>(mvsByteCode->GetBufferPointer()), mvsByteCode->GetBufferSize() };
    psoDesc.PS = { reinterpret_cast<BYTE*>(mpsByteCode->GetBufferPointer()), mpsByteCode->GetBufferSize() };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = mApp->mBackBufferFormat;
    psoDesc.SampleDesc.Count = mApp->m4xMsaaState ? 4 : 1;
    psoDesc.SampleDesc.Quality = mApp->m4xMsaaState ? (mApp->m4xMsaaQuality - 1) : 0;
    psoDesc.DSVFormat = mApp->mDepthStencilFormat;

    ThrowIfFailed(mApp->md3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSO)));
}
