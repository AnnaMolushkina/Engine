#include "World.h"
#include "Application.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "Tag.h"
#include "RenderSystem.h"
#include "RenderAdapter.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include "../../Common/d3dApp.h"
#include <DirectXColors.h>
#include "Logger.h"
#include "GameStateManager.h"
#include "D3D12RenderAdapter.h"
#include "MainMenuState.h"
#include "PauseState.h"
#include "PlayState.h"
#include "ConfigManager.h"
#include <vector>
#include "CameraSystem.h"
#include <glm/glm.hpp>
#include <DirectXMath.h>
#include "SceneSerializer.h"
#include "ResourceManager.h"
#include "MeshData.h"
#include "TextureData.h"
#include "Input.h"
#include "Profiler.h"
#include "CrashHandler.h"
#include <shellapi.h>
#include <windowsx.h>
#include <cmath>
#include <exception>
#include <random>

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

namespace {

constexpr uint32_t kMainThreadJobsPerFrame = 8; // лимит пампа главного потока на кадр
constexpr wchar_t kSecondaryWindowClass[] = L"SecondaryWnd";

std::vector<std::string> GetCommandLineArgs() {
    std::vector<std::string> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return args;
    for (int i = 1; i < argc; ++i)
        args.push_back(Logger::ToUtf8(argv[i]));
    LocalFree(argv);
    return args;
}

// config.json, шейдеры, ассеты и engine.log - рядом с exe, откуда бы его ни запустили
void SetWorkingDirectoryToExe() {
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) return;
    std::wstring dir(path, length);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        SetCurrentDirectoryW(dir.substr(0, slash).c_str());
}

const char* StateName(const std::shared_ptr<GameState>& state) {
    if (!state) return "None";
    if (dynamic_cast<PlayState*>(state.get())) return "Play";
    if (dynamic_cast<PauseState*>(state.get())) return "Pause";
    if (dynamic_cast<MainMenuState*>(state.get())) return "MainMenu (Enter - start)";
    return "?";
}

} // namespace

// Главная точка входа
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(_MSC_VER) && (defined(DEBUG) || defined(_DEBUG))
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    SetWorkingDirectoryToExe();
    Logger::Init();
    CrashHandler::Install();
    Logger::Info("Application Startup - Game Engine");

    ConfigManager::Get().Load("config.json");
    ConfigManager::Get().ApplyCommandLine(GetCommandLineArgs());

    int exitCode = 0;
    try
    {
        Application theApp(hInstance);
        if (!theApp.Initialize())
        {
            Logger::Error("Failed to initialize the application.");
            exitCode = 1;
        }
        else
        {
            Logger::Info("Application successfully initialized. Starting the main Game Loop.");
            exitCode = theApp.Run();
        }
    }
    catch (DxException& e)
    {
        Logger::Error(e.ToString());
        MessageBoxW(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        exitCode = 1;
    }
    catch (const std::exception& e)
    {
        Logger::Error(std::string("Unhandled exception: ") + e.what());
        exitCode = 1;
    }

    Logger::Info("Application exited with code " + std::to_string(exitCode));
    Logger::Shutdown();
    return exitCode;
}

Application::Application(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
    mMainWndCaption = L"Engine";
}

Application::~Application()
{
    Shutdown();
    Logger::Info("Application shutdown complete");
}

void Application::Shutdown()
{
    if (mShutDown) return;
    mShutDown = true;
    Logger::Info("Application shutdown...");

    // Порядок важен: сначала фоновые задачи (они могут ссылаться на мир и ресурсы),
    // потом состояния и мир, потом GPU-ресурсы
    mJobs.Shutdown();
    mStateManager.Shutdown();
    m_stress = StressScene{};
    if (md3dDevice) FlushCommandQueue();
    ResourceManager::Get().Clear();
    if (mRenderAdapter) {
        mRenderAdapter->Shutdown();
        mRenderAdapter.reset();
    }
    if (mhSecondaryWnd) {
        SetWindowLongPtrW(mhSecondaryWnd, GWLP_USERDATA, 0);
        DestroyWindow(mhSecondaryWnd);
        mhSecondaryWnd = nullptr;
    }
}

void Application::SaveScene(const std::string& filename) {
    // Источник истины во время игры — CameraSystem; перед записью
    // подтягиваем позицию в Camera-компонент World.
    if (m_cameraSystem && m_cameraEntity != 0) {
        if (Camera* cam = m_world.GetCamera(m_cameraEntity)) {
            cam->position = m_cameraSystem->GetCameraPosition();
            // target CameraSystem каждый кадр уже пишет в component в Update()
        }
    }

    const Camera* cam = m_world.GetCamera(m_cameraEntity);
    if (cam) {
        Logger::Info("SaveScene: camera at (" +
            std::to_string(cam->position.x) + ", " +
            std::to_string(cam->position.y) + ", " +
            std::to_string(cam->position.z) + ")");
    }
    else {
        Logger::Warning("SaveScene: camera entity is missing, camera will NOT be saved");
    }

    Transform* t = m_world.GetTransform(m_circle);
    float circleY = 0.0f;
    if (t) circleY = t->position.y;

    if (SceneSerializer::SaveScene(m_world, filename, m_cameraEntity,
        m_rotationAngle, m_jumpPhase, circleY)) {
        Logger::Info("Scene saved to " + filename);
    }
    else {
        Logger::Error("Failed to save scene to " + filename);
    }
}

void Application::LoadScene(const std::string& filename) {
    // Загрузка сцены удаляет все сущности — стресс-сцену убираем заранее
    // (у неё есть указатели на Transform).
    DespawnStressScene();

    // Старая камера: у неё нет MeshRenderer, SceneSerializer её не трогает.
    if (m_cameraEntity != 0) {
        m_world.DestroyEntity(m_cameraEntity);
        m_cameraEntity = 0;
    }
    if (m_cameraSystem)
        m_cameraSystem->Reset();

    // Останавливаем анимации
    m_isRotating = false;
    m_isScaling = false;
    m_isJumping = false;
    m_currentScale = 1.0f;
    m_scaleDirection = 1.0f;

    Entity newTriangle = 0;
    Entity newCircle = 0;
    Entity newSquare = 0;
    float loadedRotationAngle = 0.0f;
    float loadedJumpPhase = 0.0f;
    float loadedCircleY = 0.0f;
    // Дефолты: используются и при ошибке загрузки, и если в JSON нет секции camera
    glm::vec3 loadedCamPos(0.0f, 2.0f, 6.0f);
    glm::vec3 loadedCamTarget(0.0f, 0.0f, 0.0f);
    float loadedCamZoom = 6.0f;

    const bool ok = SceneSerializer::LoadScene(
        m_world, filename,
        newTriangle, newCircle, newSquare,
        loadedRotationAngle, loadedJumpPhase, loadedCircleY,
        loadedCamPos, loadedCamTarget, loadedCamZoom);

    if (ok) {
        Logger::Info("Scene loaded from " + filename);

        if (newTriangle != 0) {
            m_rotatingTriangle = newTriangle;
            m_rotationAngle = loadedRotationAngle;
            Transform* t = m_world.GetTransform(m_rotatingTriangle);
            if (t) {
                t->rotation = glm::angleAxis(m_rotationAngle, glm::vec3(0.0f, 0.0f, 1.0f));
            }
            Logger::Info("Triangle restored with angle: " + std::to_string(m_rotationAngle));
        }
        if (newCircle != 0) {
            m_circle = newCircle;
            m_jumpPhase = loadedJumpPhase;
            Transform* t = m_world.GetTransform(m_circle);
            if (t) {
                t->position.y = loadedCircleY;
                m_originalY = 0.0f;
            }
            Logger::Info("Circle restored with Y: " + std::to_string(loadedCircleY));
        }
        if (newSquare != 0) {
            m_square = newSquare;
            Transform* t = m_world.GetTransform(m_square);
            if (t) m_currentScale = t->scale.x;
            Logger::Info("Square restored");
        }
    }
    else {
        Logger::Error("Failed to load scene from " + filename);
        // loadedCam* остаются дефолтными
    }

    // Ровно одна камера. Позиция — из JSON (если был) или дефолт.
    m_cameraEntity = m_world.CreateEntity();
    Camera& camera = m_world.AddCamera(m_cameraEntity);
    camera.type = CameraType::Perspective;
    camera.position = loadedCamPos;
    camera.target = loadedCamTarget;
    camera.zoom = loadedCamZoom;

    Logger::Info("LoadScene: camera entity " + std::to_string(m_cameraEntity) +
        " at (" + std::to_string(loadedCamPos.x) + ", " +
        std::to_string(loadedCamPos.y) + ", " +
        std::to_string(loadedCamPos.z) + ")");

    // CameraSystem подхватит entity при следующем Update (через GetCameraEntities).
    if (m_cameraSystem)
        m_cameraSystem->Reset();
}


void Application::InitScene()
{
    //сначала убираем стресс-сцену
    DespawnStressScene();
   // Убираем рендерящиеся сущности и старую камеру (она не попадает под GetRenderableEntities:
    // у неё нет MeshRenderer, только Camera-компонент).
    m_world.DestroyEntities(m_world.GetRenderableEntities());
    if (m_cameraEntity != 0) {
        m_world.DestroyEntity(m_cameraEntity);
        m_cameraEntity = 0;
    }

    // Асинхронная загрузка L1: RequestXxx возвращает сразу, загрузка идёт в фоне
    // (на job system, JobPriority::Low), GPU-аплоад — на главном потоке через памп.
    // До готовности RenderSystem рисует placeholder: куб / белую текстуру.
    auto steplerMesh = ResourceManager::Get().RequestMesh("assets/models/stepler.obj");
    auto cubeMesh = ResourceManager::Get().RequestMesh("assets/models/cube.obj");
    auto gojoMesh = ResourceManager::Get().RequestMesh("assets/models/Gojo.obj");
    auto steplerTex = ResourceManager::Get().RequestTexture("assets/textures/texture_stepler.jpg");
    auto heartTex = ResourceManager::Get().RequestTexture("assets/textures/heart.jpg");
    auto gojoTex = ResourceManager::Get().RequestTexture("assets/textures/Gojotext.jpg");

    //D3D12RenderAdapter* d3d = dynamic_cast<D3D12RenderAdapter*>(mRenderAdapter.get());

    //// Загружаем ресурсы 1 раз и сразу отправляем на GPU
    //// (раньше меши грузились на GPU прямо посреди кадра, внутри Draw)
    //auto steplerMesh = ResourceManager::Get().LoadMesh("assets/models/stepler.obj");
    //auto cubeMesh = ResourceManager::Get().LoadMesh("assets/models/cube.obj");
    //if (d3d) {
    //    if (steplerMesh) d3d->UploadMeshToGPU(*steplerMesh);
    //    if (cubeMesh) d3d->UploadMeshToGPU(*cubeMesh);
    //}

    //auto steplerTex = ResourceManager::Get().LoadTextureData("assets/textures/texture_stepler.jpg");
    //if (steplerTex && d3d) {
    //    d3d->UploadTextureToGPU(*steplerTex);
    //}

    //auto heartTex = ResourceManager::Get().LoadTextureData("assets/textures/heart.jpg");
    //if (heartTex && d3d) {
    //    d3d->UploadTextureToGPU(*heartTex);
    //}


    // Степлер 1 (слева, уменьшен)
    {
        Entity e = m_world.CreateEntity();
        Transform& t = m_world.AddTransform(e);
        t.position = glm::vec3(-2.5f, 0.0f, 0.0f);
        t.scale = glm::vec3(0.4f, 0.4f, 0.4f);

        MeshRenderer& mr = m_world.AddMeshRenderer(e);
        mr.mesh = steplerMesh;
        mr.texture = steplerTex;
        mr.useLoadedMesh = true;

        m_world.AddTag(e, "MainWindow");
    }

    // Степлер 2 (в центре, стандартный)
    {
        Entity e = m_world.CreateEntity();
        Transform& t = m_world.AddTransform(e);
        t.position = glm::vec3(0.0f, 0.0f, 0.0f);

        MeshRenderer& mr = m_world.AddMeshRenderer(e);
        mr.mesh = gojoMesh;
        mr.texture = gojoTex;
        mr.useLoadedMesh = true;

        m_world.AddTag(e, "MainWindow");
    }

    // Степлер 3 (справа, повернут)
    {
        Entity e = m_world.CreateEntity();
        Transform& t = m_world.AddTransform(e);
        t.position = glm::vec3(2.5f, 0.0f, 0.0f);
        t.scale = glm::vec3(0.7f, 0.7f, 0.7f);
        t.rotation = glm::angleAxis(glm::radians(60.0f), glm::vec3(0.0f, 1.0f, 0.0f)); // поворот по Y

        MeshRenderer& mr = m_world.AddMeshRenderer(e);
        mr.mesh = steplerMesh;
        mr.texture = steplerTex;
        mr.useLoadedMesh = true;

        m_world.AddTag(e, "MainWindow");
    }

    // Красный куб
    {
        Entity e = m_world.CreateEntity();
        Transform& t = m_world.AddTransform(e);
        t.position = glm::vec3(0.0f, -1.0f, -4.0f);
        t.scale = glm::vec3(1.3f, 1.3f, 1.3f);

        MeshRenderer& mr = m_world.AddMeshRenderer(e);
        mr.mesh = cubeMesh;
        mr.texture = heartTex;
        mr.useLoadedMesh = true;

        m_world.AddTag(e, "MainWindow");
    }

    Logger::Info("InitScene completed");

    // Черный квадрат (второе окно)
    Entity squareEntity = m_world.CreateEntity();
    Transform& tSquare = m_world.AddTransform(squareEntity);
    tSquare.position = glm::vec3(0.0f, 0.0f, 0.0f);
    tSquare.scale = glm::vec3(0.5f, 0.5f, 1.0f);
    MeshRenderer& mrSquare = m_world.AddMeshRenderer(squareEntity);
    mrSquare.color = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
    mrSquare.type = PrimitiveType::Square;
    m_world.AddTag(squareEntity, "SecondaryWindow");
    m_square = squareEntity;

    Logger::Info("Square created with ID: " + std::to_string(m_square));

    m_cameraEntity = m_world.CreateEntity();
    Camera& camera = m_world.AddCamera(m_cameraEntity);
    camera.type = CameraType::Perspective;
    camera.position = glm::vec3(0.0f, 2.0f, 6.0f);
    camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    camera.zoom = 6.0f;

    Logger::Info("Camera created");
}

bool Application::Initialize()
{
    ZoneScopedN("Application::Initialize");
    const EngineConfig& config = ConfigManager::Get().Config();

    // Job system - первой: остальным подсистемам она может понадобиться уже при инициализации
    JobSystemConfig jobsConfig;
    jobsConfig.parallelEnabled = config.jobsEnabled;
    jobsConfig.workerThreads = config.workerThreads;
    mJobs.Initialize(jobsConfig);

    if (!D3DApp::Initialize())
        return false;

    CreateSecondaryWindow();
    CreateSecondarySwapChain();

    mRenderAdapter = std::make_unique<D3D12RenderAdapter>(this);
    if (!mRenderAdapter->Initialize())
    {
        return false;
    }
    mRenderAdapter->SetVSync(config.vsync);
    // Мост job system ↔ ResourceManager ↔ D3D12: финализация ресурсов идёт
    // на главном потоке через RunOnMainThread, а не посреди кадра.
    D3D12RenderAdapter* d3dAdapter = static_cast<D3D12RenderAdapter*>(mRenderAdapter.get());
    ResourceManager::Get().SetUploaders(
        [d3dAdapter](MeshData& mesh) { return d3dAdapter->UploadMeshToGPU(mesh); },
        [d3dAdapter](TextureData& tex) { return d3dAdapter->UploadTextureToGPU(tex); }
    );

    // СОЗДАЁМ RENDER SYSTEM И СЦЕНУ
    m_renderSystem = std::make_unique<RenderSystem>(mRenderAdapter.get());
    InitScene();

    // СОЗДАЁМ CAMERA SYSTEM
    m_cameraSystem = std::make_unique<CameraSystem>();

    mBenchmark.Configure(config);
    if (config.benchmark) {
        // Замер: сразу игра с "замерочной" сценой, камера неподвижна
        mStateManager.ChangeState(std::make_shared<PlayState>());
        SpawnStressScene(config.stressEntities);
        mBenchmark.Arm(mJobs.IsParallelEnabled(), mJobs.IsParallelEnabled() ? mJobs.ThreadCount() : 1u,
                       (uint32_t)m_world.GetEntities().size());
    }
    else {
        mStateManager.ChangeState(std::make_shared<MainMenuState>());
        if (config.stressOnStart) SpawnStressScene(config.stressEntities);
    }
    mStateManager.ApplyPending();
    UpdateCaption();

    return true;
}

// ============================================================ окна

void Application::CreateSecondaryWindow()
{
    WNDCLASSW wc = { 0 };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = SecondaryWndProc; // своя процедура: раньше сообщения второго окна шли в D3DApp::MsgProc
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = mhAppInst;
    wc.hIcon = LoadIcon(0, IDI_APPLICATION);
    wc.hCursor = LoadCursor(0, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszMenuName = 0;
    wc.lpszClassName = kSecondaryWindowClass;

    RegisterClassW(&wc);

    RECT rect = { 0, 0, (LONG)mSecondaryWidth, (LONG)mSecondaryHeight }; // клиентская область 400x400
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    mhSecondaryWnd = CreateWindowW(kSecondaryWindowClass, L"Secondary Window (Square)",
        WS_OVERLAPPEDWINDOW, 100, 100, rect.right - rect.left, rect.bottom - rect.top, 0, 0, mhAppInst, this);

    RECT client;
    GetClientRect(mhSecondaryWnd, &client);
    mSecondaryWidth = (UINT)std::max<LONG>(1, client.right - client.left);
    mSecondaryHeight = (UINT)std::max<LONG>(1, client.bottom - client.top);

    ShowWindow(mhSecondaryWnd, SW_HIDE);
    UpdateWindow(mhSecondaryWnd);
}

void Application::CreateSecondarySwapChain()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mSecondaryRtvHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(mSecondaryDsvHeap.GetAddressOf())));

    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = mSecondaryWidth;   // раньше 400x400 при клиентской области меньше 400x400
    sd.BufferDesc.Height = mSecondaryHeight;
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
    sd.Flags = SwapChainFlags(); // должны совпадать с ResizeBuffers

    ThrowIfFailed(mdxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, mSecondarySwapChain.GetAddressOf()));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mSecondaryRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++)
    {
        ThrowIfFailed(mSecondarySwapChain->GetBuffer(i, IID_PPV_ARGS(&mSecondarySwapChainBuffer[i])));
        md3dDevice->CreateRenderTargetView(mSecondarySwapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, mRtvDescriptorSize);
    }
    CreateSecondaryDepthBuffer();
}

// Свой буфер глубины второго окна (раньше использовался буфер главного окна другого размера)
void Application::CreateSecondaryDepthBuffer()
{
    mSecondaryDepthBuffer.Reset();

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = mSecondaryWidth;
    desc.Height = mSecondaryHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = mDepthStencilFormat;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = mDepthStencilFormat;
    clear.DepthStencil.Depth = 1.0f;
    clear.DepthStencil.Stencil = 0;

    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(md3dDevice->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(mSecondaryDepthBuffer.GetAddressOf())));
    md3dDevice->CreateDepthStencilView(mSecondaryDepthBuffer.Get(), nullptr,
        mSecondaryDsvHeap->GetCPUDescriptorHandleForHeapStart());
}

void Application::ResizeSecondaryWindow(UINT width, UINT height)
{
    if (!mSecondarySwapChain || width == 0 || height == 0) return; // свёрнуто
    if (width == mSecondaryWidth && height == mSecondaryHeight) return;

    FlushCommandQueue();
    for (auto& buffer : mSecondarySwapChainBuffer)
        buffer.Reset();
    ThrowIfFailed(mSecondarySwapChain->ResizeBuffers(SwapChainBufferCount, width, height, mBackBufferFormat, SwapChainFlags()));

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(mSecondaryRtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SwapChainBufferCount; i++)
    {
        ThrowIfFailed(mSecondarySwapChain->GetBuffer(i, IID_PPV_ARGS(&mSecondarySwapChainBuffer[i])));
        md3dDevice->CreateRenderTargetView(mSecondarySwapChainBuffer[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset(1, mRtvDescriptorSize);
    }
    mSecondaryWidth = width;
    mSecondaryHeight = height;
    CreateSecondaryDepthBuffer();
}

void Application::SetSecondaryWindowVisible(bool visible)
{
    if (!mhSecondaryWnd || mShowSecondaryWnd == visible) return;
    mShowSecondaryWnd = visible;
    // Без активации: фокус клавиатуры остаётся у главного окна
    ShowWindow(mhSecondaryWnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    Logger::Info(visible ? "Secondary Window: SHOWN" : "Secondary Window: HIDDEN");
}

LRESULT CALLBACK Application::SecondaryWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* app = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (app) {
        switch (msg) {
        case WM_CLOSE:
            // Крестик прячет окно; раньше WM_DESTROY второго окна завершал всё приложение
            app->SetSecondaryWindowVisible(false);
            return 0;
        case WM_SIZE:
            // Раньше этот WM_SIZE менял размер swap chain главного окна
            if (wParam != SIZE_MINIMIZED)
                app->ResizeSecondaryWindow(LOWORD(lParam), HIWORD(lParam));
            return 0;
        case WM_GETMINMAXINFO:
            reinterpret_cast<MINMAXINFO*>(lParam)->ptMinTrackSize = { 200, 200 };
            return 0;
        case WM_KEYDOWN:
            Input::Get().OnKeyDown(wParam, lParam);
            return 0;
        case WM_KEYUP:
            Input::Get().OnKeyUp(wParam);
            if (wParam == VK_ESCAPE) PostQuitMessage(0);
            return 0;
        default:
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT Application::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    Input& input = Input::Get();
    switch (msg) {
    case WM_KEYDOWN:
        input.OnKeyDown(wParam, lParam);
        return 0;
    case WM_SYSKEYDOWN:
        input.OnKeyDown(wParam, lParam);
        break; // Alt+F4 и т.п. обрабатывает DefWindowProc
    case WM_KEYUP:
    case WM_SYSKEYUP:
        input.OnKeyUp(wParam);
        // В D3DApp F2 включает 4xMSAA, но swap chain с FLIP_DISCARD MSAA не поддерживает - приложение падало
        if (wParam == VK_F2) return 0;
        break; // Esc (выход) обрабатывает D3DApp
    case WM_MOUSEWHEEL:
        // Раньше колесо мыши не обрабатывалось вовсе (m_scrollDelta всегда был 0)
        input.OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
        return 0;
    case WM_KILLFOCUS:
        input.Reset();
        break;
    case WM_ACTIVATE:
        // Переход фокуса во второе окно движка (и режим замера) не ставит игру на паузу
        if (LOWORD(wParam) == WA_INACTIVE &&
            (reinterpret_cast<HWND>(lParam) == mhSecondaryWnd || ConfigManager::Get().Config().benchmark))
            return 0;
        break;
    default:
        break;
    }
    return D3DApp::MsgProc(hwnd, msg, wParam, lParam);
}

void Application::OnResize()
{
    D3DApp::OnResize();
}

// ============================================================ кадр

void Application::UpdateCamera(const GameTimer& gt)
{
    if (!m_cameraSystem || ConfigManager::Get().Config().benchmark) return; // на замере камера неподвижна
    const Input& input = Input::Get();

    // Раньше клавиши читались через GetAsyncKeyState - и когда окно не в фокусе
    const bool moveForward = input.IsKeyDown(VK_UP);
    const bool moveBackward = input.IsKeyDown(VK_DOWN);
    const bool moveLeft = input.IsKeyDown(VK_LEFT);
    const bool moveRight = input.IsKeyDown(VK_RIGHT);
    const bool moveDown = input.IsKeyDown('Q');
    const bool moveUp = input.IsKeyDown('E');

    // ПКМ - вращение
    float mouseDeltaX = 0.0f, mouseDeltaY = 0.0f;
    if (input.IsMouseDown(MouseButton::Right)) {
        mouseDeltaX = (float)input.MouseDeltaX();
        mouseDeltaY = (float)input.MouseDeltaY();
    }

    // ЛКМ - приближение/отдаление (движение мыши вверх → приближение) + колесо
    float scrollDelta = input.WheelDelta();
    if (input.IsMouseDown(MouseButton::Left))
        scrollDelta += -(float)input.MouseDeltaY() * 0.05f;

    m_cameraSystem->Update(m_world, gt.DeltaTime(),
        moveForward, moveBackward,
        moveLeft, moveRight,
        moveUp, moveDown,
        mouseDeltaX, mouseDeltaY,
        scrollDelta);
}

void Application::Update(const GameTimer& gt)
{
    ZoneScopedN("Application::Update");
    const EngineConfig& config = ConfigManager::Get().Config();
    Input& input = Input::Get();

    mJobs.BeginFrame();
    mJobs.PumpMainThread(kMainThreadJobsPerFrame);

    mFrameCount++;
    if (mFrameCount <= 500 && (mFrameCount % 100 == 0 || mFrameCount == 1))
    {
        Logger::Info("Frame " + to_string(mFrameCount) + " | DeltaTime: " + to_string(gt.DeltaTime()) + "s");
    }

    //  ПЕРЕКЛЮЧЕНИЕ ВТОРОГО ОКНА (W)
    if (input.WasKeyPressed('W'))
        SetSecondaryWindowVisible(!mShowSecondaryWnd);

    mStateManager.Update(gt);
    mStateManager.ProcessKeyboardInput(gt);

    // Синхронизируем m_showECS с текущим состоянием
    auto currentState = mStateManager.GetCurrentState();
    bool isPlayState = (currentState && dynamic_cast<PlayState*>(currentState.get()));

    if (isPlayState && !m_showECS) {
        m_showECS = true;
        Logger::Info("Objects SHOWN (auto)");
    }

    if (!isPlayState && m_showECS) {
        m_showECS = false;
        Logger::Info("Objects HIDDEN (auto)");
    }

    if (isPlayState) {
        if (!config.benchmark) { // на замере сцена фиксирована
            if (input.WasKeyPressed('B')) {
                if (m_stress.entities.empty()) SpawnStressScene(config.stressEntities);
                else DespawnStressScene();
            }
            if (input.WasKeyPressed('J')) {
                const bool enabled = !mJobs.IsParallelEnabled();
                mJobs.SetParallelEnabled(enabled);
                Logger::Info(std::string("Job system parallel execution: ") + (enabled ? "ON" : "OFF"));
            }
            if (input.WasKeyPressed('L')) {
                // Демо async-загрузки L1: сбрасываем кэш и заново запрашиваем ресурсы.
                // С --jobs=off Submit исполняется инлайн → кадр замирает.
                // С --jobs=on задача уходит в фон → кадр ровный, в заголовке видно "loading: N".
                Logger::Info("=== Reload resources (async L1 demo) ===");
                ResourceManager::Get().Clear();
                InitScene();
            }
        }
        UpdateStressAnimation(gt);
    }

    UpdateCamera(gt);

    // ========== ГОРЯЧИЕ КЛАВИШИ ДЛЯ СЕРИАЛИЗАЦИИ ==========
    if (input.WasKeyPressed(VK_F5)) SaveScene("scene.json");
    if (input.WasKeyPressed(VK_F6)) LoadScene("scene.json");

    UpdateCaption();
    input.EndFrame();
}

void Application::Draw(const GameTimer& gt)
{
    ZoneScopedN("Application::Draw");
    if (!m_cameraSystem) {
        Logger::Error("CameraSystem is null!");
        return;
    }

    glm::mat4 view = m_cameraSystem->GetViewMatrix();
    glm::mat4 proj = m_cameraSystem->GetProjectionMatrix((float)mClientWidth / mClientHeight);

    mRenderAdapter->BeginFrame(0);

    if (m_showECS && m_renderSystem) {
        // Подготовка отрисовки - параллельно на job system, запись команд - здесь
        m_renderSystem->Render(m_world, mJobs, view, proj, "MainWindow", &mBenchmark);
        if (mFrameCount % 600 == 0) {
            const RenderSystem::Stats& stats = m_renderSystem->LastStats();
            Logger::Info("Render: " + std::to_string(stats.visible) + " visible of " +
                         std::to_string(stats.candidates) + " objects (frustum culling)");
        }
    }

    mRenderAdapter->EndFrame(0);

    if (mShowSecondaryWnd) {
        ZoneScopedN("SecondaryWindow");
        // Своя камера (единичная): раньше квадрат рисовался с видом и проекцией главной камеры
        mRenderAdapter->SetViewProjection(glm::mat4(1.0f), glm::mat4(1.0f));
        mRenderAdapter->BeginFrame(1);
        mRenderAdapter->DrawPrimitive(PrimitiveType::Square, { 0.0f, 0.0f, 0.0f }, gt.TotalTime() * 0.5f, 1.5f);
        mRenderAdapter->EndFrame(1);
    }

    FrameMark;

    // Полное время кадра (от конца прошлого до конца текущего) - для замеров
    const auto now = std::chrono::steady_clock::now();
    if (mHasLastFrameEnd) {
        const double frameMs = std::chrono::duration<double, std::milli>(now - mLastFrameEnd).count();
        TracyPlot("Frame time (ms)", frameMs);
        if (mBenchmark.OnFrameEnd(frameMs)) {
            Logger::Info("Benchmark finished, exiting");
            PostQuitMessage(0);
        }
    }
    mLastFrameEnd = now;
    mHasLastFrameEnd = true;
}

void Application::UpdateCaption()
{
    std::wstring info = L"  [";
    for (const char* c = StateName(mStateManager.GetCurrentState()); *c; ++c) info += (wchar_t)*c;
    info += L"]  jobs: ";
    info += mJobs.IsParallelEnabled()
        ? (L"ON, " + std::to_wstring(mJobs.ThreadCount()) + L" threads")
        : std::wstring(L"OFF");

    // Считаем так же, как лог RenderSystem: "видимые / кандидаты в этом окне".
    // На обычной сцене кандидатов 4 (3 степлера + красный куб, все с тегом MainWindow);
    // на стресс-сцене — 4 + N кубов. Раньше тут было m_world.GetEntities().size(),
    // что включало камеру и объект второго окна и расходилось с логом.
    const uint32_t visible = m_renderSystem ? m_renderSystem->LastStats().visible : 0;
    const uint32_t candidates = m_renderSystem ? m_renderSystem->LastStats().candidates : 0;
    info += L"  objects: " + std::to_wstring(visible) + L" visible / " + std::to_wstring(candidates);

    const int pending = ResourceManager::Get().PendingLoads();
    if (pending > 0)
        info += L"  loading: " + std::to_wstring(pending);

    if (info != mCaptionInfo) {
        mCaptionInfo = info;
        mMainWndCaption = L"Engine" + info;
    }
}

// ============================================================ стресс-сцена

void Application::SpawnStressScene(uint32_t count)
{
    if (!m_stress.entities.empty() || count == 0) return;
    ZoneScopedN("SpawnStressScene");
    // Async: если ресурсов нет в кэше, они загрузятся в фоне.
    // 20 000 кубов получат placeholder-куб до тех пор, пока mesh не окажется на GPU.
    m_stress.mesh = ResourceManager::Get().RequestMesh("assets/models/cube.obj");
    for (const char* path : { "assets/textures/wood.jpg",
                              "assets/textures/heart.jpg",
                              "assets/textures/texture_stepler.jpg" }) {
        if (auto tex = ResourceManager::Get().RequestTexture(path))
            m_stress.textures.push_back(tex);
    }

   /* D3D12RenderAdapter* d3d = dynamic_cast<D3D12RenderAdapter*>(mRenderAdapter.get());

    m_stress.mesh = ResourceManager::Get().LoadMesh("assets/models/cube.obj");
    if (m_stress.mesh && d3d) d3d->UploadMeshToGPU(*m_stress.mesh);
    for (const char* path : { "assets/textures/wood.jpg", "assets/textures/heart.jpg", "assets/textures/texture_stepler.jpg" }) {
        auto tex = ResourceManager::Get().LoadTextureData(path);
        if (!tex) continue;
        if (d3d && tex->srvIndex < 0) d3d->UploadTextureToGPU(*tex);
        m_stress.textures.push_back(tex);
    }*/

    // Сетка кубов в плоскости XZ под основной сценой; фиксированный seed - одинаковая сцена в каждом замере
    const uint32_t side = (uint32_t)std::ceil(std::sqrt((double)count));
    const float spacing = 1.1f;
    const float half = (side - 1) * spacing * 0.5f;
    std::mt19937 rng(ConfigManager::Get().Config().sceneSeed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    m_stress.entities.reserve(count);
    m_stress.transforms.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        Entity e = m_world.CreateEntity();
        Transform& t = m_world.AddTransform(e);
        const glm::vec3 base((i % side) * spacing - half, -2.5f, (i / side) * spacing - half);
        t.position = base;
        t.scale = glm::vec3(0.35f);

        MeshRenderer& mr = m_world.AddMeshRenderer(e);
        mr.mesh = m_stress.mesh;
        mr.texture = m_stress.textures.empty() ? nullptr : m_stress.textures[i % m_stress.textures.size()];
        mr.useLoadedMesh = m_stress.mesh != nullptr;
        m_world.AddTag(e, "MainWindow");

        m_stress.entities.push_back(e);
        m_stress.transforms.push_back(&t);
        m_stress.basePositions.push_back(base);
        m_stress.axes.push_back(glm::normalize(glm::vec3(unit(rng) - 0.5f, unit(rng) + 0.2f, unit(rng) - 0.5f)));
        m_stress.speeds.push_back(0.5f + 2.0f * unit(rng));
        m_stress.phases.push_back(unit(rng) * 6.2831853f);
    }
    Logger::Info("Stress scene spawned: " + std::to_string(count) + " cubes");
}

void Application::DespawnStressScene()
{
    if (m_stress.entities.empty()) return;
    ZoneScopedN("DespawnStressScene");
    m_world.DestroyEntities(m_stress.entities);
    Logger::Info("Stress scene removed: " + std::to_string(m_stress.entities.size()) + " cubes");
    m_stress = StressScene{};
}

void Application::UpdateStressAnimation(const GameTimer& gt)
{
    const uint32_t count = (uint32_t)m_stress.transforms.size();
    if (count == 0) return;

    const auto start = std::chrono::steady_clock::now();
    const float time = gt.TotalTime();
    {
        ZoneScopedN("StressAnimation");
        // Каждый куб пишет только свой Transform - диапазоны независимы
        mJobs.ParallelFor(count, 256, [&](uint32_t begin, uint32_t end) {
            for (uint32_t i = begin; i < end; ++i) {
                const float angle = time * m_stress.speeds[i] + m_stress.phases[i];
                Transform* t = m_stress.transforms[i];
                t->rotation = glm::angleAxis(angle, m_stress.axes[i]);
                t->position = m_stress.basePositions[i] + glm::vec3(0.0f, 0.3f * std::sin(angle * 1.3f), 0.0f);
            }
        }, "StressAnimation");
    }
    mBenchmark.RecordZone("StressAnimation",
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
}

// ============================================================ мышь

void Application::OnMouseDown(WPARAM btnState, int x, int y)
{
    Input::Get().OnMouseButtons(btnState);
    Input::Get().OnMouseMove(x, y);
    SetCapture(mhMainWnd); // перетаскивание продолжается и за пределами окна
    mStateManager.OnMouseDown(btnState, x, y);
}

void Application::OnMouseUp(WPARAM btnState, int x, int y)
{
    Input::Get().OnMouseButtons(btnState);
    Input::Get().OnMouseMove(x, y);
    if ((btnState & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0)
        ReleaseCapture();
    mStateManager.OnMouseUp(btnState, x, y);
}

void Application::OnMouseMove(WPARAM btnState, int x, int y)
{
    Input::Get().OnMouseButtons(btnState);
    Input::Get().OnMouseMove(x, y);
}
