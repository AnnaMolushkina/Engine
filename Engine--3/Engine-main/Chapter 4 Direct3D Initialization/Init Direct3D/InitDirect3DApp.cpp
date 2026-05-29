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

using Microsoft::WRL::ComPtr;
using namespace std;
using namespace DirectX;

// Предварительное объявление оконной процедуры
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Главная точка входа
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd)
{
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif
    Logger::Init();
    Logger::Info("Application Startup - Game Engine");

    try
    {
        Application theApp(hInstance);
        if (!theApp.Initialize())
        {
            Logger::Error("Failed to initialize the application.");
            return 0;
        }

        Logger::Info("Application successfully initialized. Starting the main Game Loop.");
        return theApp.Run();
    }
    catch (DxException& e)
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
    // Дожидаемся завершения всех команд перед разрушением
    FlushCommandQueue();

    Logger::Info("Application shutdown complete");
}


void Application::SaveScene(const std::string& filename) {
    // Сохраняем текущую позицию Y ромба
    Transform* t = m_world.GetTransform(m_circle);
    float circleY = 0.0f;
    if (t) {
        circleY = t->position.y;
    }

    if (SceneSerializer::SaveScene(m_world, filename, m_rotationAngle, m_jumpPhase, circleY)) {
        Logger::Info("Scene saved to " + filename);
    }
    else {
        Logger::Error("Failed to save scene to " + filename);
    }
}

void Application::LoadScene(const std::string& filename) {
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

    if (SceneSerializer::LoadScene(m_world, filename, newTriangle, newCircle, newSquare,
        loadedRotationAngle, loadedJumpPhase, loadedCircleY)) {
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
                // Устанавливаем позицию Y из сохранённого значения
                t->position.y = loadedCircleY;
                // НО исходная точка для прыжков = 0
                m_originalY = 0.0f;  // ← всегда 0
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
    }
}


void Application::InitScene()
{
    // Очищаем старые сущности
    auto oldEntities = m_world.GetRenderableEntities();
    for (Entity e : oldEntities) {
        m_world.DestroyEntity(e);
    }

    //// ========== ТРЕУГОЛЬНИК ==========
    //m_rotatingTriangle = m_world.CreateEntity();
    //Transform& tTri = m_world.AddTransform(m_rotatingTriangle);
    //tTri.position = glm::vec3(0.0f, 0.0f, 0.0f);  // ближе к центру
    //tTri.scale = glm::vec3(0.8f, 0.8f, 1.0f);      // чуть меньше
    //MeshRenderer& mrTri = m_world.AddMeshRenderer(m_rotatingTriangle);
    //mrTri.color = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    //mrTri.type = PrimitiveType::Triangle;
    //m_world.AddTag(m_rotatingTriangle, "MainWindow");

    D3D12RenderAdapter* d3d = dynamic_cast<D3D12RenderAdapter*>(mRenderAdapter.get());
    // Загружаем шейдер один раз
    auto defaultShader = d3d->CreateShaderProgram("Shaders/VertexShader.hlsl", "Shaders/PixelShader.hlsl");
    // Stepler (используем уже загруженную в Initialize)
    {
        auto mesh = ResourceManager::Get().LoadMesh("assets/models/stepler.obj");

        Entity e = m_world.CreateEntity();
        Transform& t = m_world.AddTransform(e);
        t.position = glm::vec3(-1.5f, 0.0f, 0.0f);

        MeshRenderer& mr = m_world.AddMeshRenderer(e);
        mr.mesh = mesh;
        mr.texture = std::shared_ptr<TextureData>(d3d ? d3d->GetMainTexture() : nullptr, [](TextureData*) {});
        mr.shader = defaultShader;
        mr.useLoadedMesh = true;

        m_world.AddTag(e, "MainWindow");
    }

    // Cube + heart.jpg
    {
        auto mesh = ResourceManager::Get().LoadMesh("assets/models/cube.obj");
        auto tex = ResourceManager::Get().LoadTextureData("assets/textures/heart.jpg");

        if (tex && d3d) {
            d3d->UploadTextureToGPU(*tex);   // новый метод
        }

        Entity e = m_world.CreateEntity();
        Transform& t = m_world.AddTransform(e);
        t.position = glm::vec3(1.5f, 0.0f, 0.0f);
        t.scale = glm::vec3(1.3f);

        MeshRenderer& mr = m_world.AddMeshRenderer(e);
        mr.mesh = mesh;
        mr.texture = tex;
        mr.shader = defaultShader;
        mr.useLoadedMesh = true;

        m_world.AddTag(e, "MainWindow");
    }

    Logger::Info("InitScene completed");

    //// ========== ВЫТЯНУТЫЙ РОМБ ==========
    //m_circle = m_world.CreateEntity();
    //Transform& tRhombus = m_world.AddTransform(m_circle);
    //tRhombus.position = glm::vec3(0.4f, 0.0f, 0.0f);   // ближе к центру
    //tRhombus.scale = glm::vec3(0.8f, 0.4f, 1.0f);
    //tRhombus.rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    //m_originalY = 0.0f;

    //MeshRenderer& mrRhombus = m_world.AddMeshRenderer(m_circle);
    //mrRhombus.color = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    //mrRhombus.type = PrimitiveType::Square;
    //m_world.AddTag(m_circle, "MainWindow");


    // ========== КВАДРАТ (второе окно) ==========
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
    
    // ========== ДОБАВЛЯЕМ КАМЕРУ ==========
    m_cameraEntity = m_world.CreateEntity();
    m_camera = &m_world.AddCamera(m_cameraEntity);
    m_camera->type = CameraType::Perspective;
    m_camera->position = glm::vec3(1.0f, 1.0f, 5.0f);
    m_camera->target = glm::vec3(0.0f, 0.0f, 0.0f);
    m_camera->zoom = 5.0f;

    Logger::Info("Camera created and stored as pointer");

}

bool Application::Initialize()
{
    ConfigManager::Get().Load("config.json");

    if (!D3DApp::Initialize())
        return false;

    CreateSecondaryWindow();
    CreateSecondarySwapChain();

    mRenderAdapter = std::make_unique<D3D12RenderAdapter>(this);
    if (!mRenderAdapter->Initialize())
    {
        return false;
    }

    // ========== СОЗДАЁМ RENDER SYSTEM И СЦЕНУ ==========
    m_renderSystem = std::make_unique<RenderSystem>(mRenderAdapter.get());
    InitScene();

    // ========== СОЗДАЁМ CAMERA SYSTEM ==========
    //m_cameraSystem = std::make_unique<CameraSystem>();
    // ===========================================

    mStateManager.ChangeState(std::make_shared<MainMenuState>());

    return true;
}

void Application::CreateSecondaryWindow()
{
    WNDCLASSW wc = { 0 };
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
    if (mFrameCount <= 500 && (mFrameCount % 100 == 0 || mFrameCount == 1))
    {
        Logger::Info("Frame " + to_string(mFrameCount) + " | DeltaTime: " + to_string(gt.DeltaTime()) + "s");
    }

    // ========== ПЕРЕКЛЮЧЕНИЕ ВТОРОГО ОКНА (W) ==========
    if (GetAsyncKeyState('W') & 0x8000) {
        Sleep(200);
        mShowSecondaryWnd = !mShowSecondaryWnd;
        ShowWindow(mhSecondaryWnd, mShowSecondaryWnd ? SW_SHOW : SW_HIDE);
        Logger::Info(mShowSecondaryWnd ? "Secondary Window: SHOWN" : "Secondary Window: HIDDEN");
    }

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

    // ========== ПОЛУЧАЕМ ДЕЛЬТЫ МЫШИ ==========
    static int lastMouseX = 0;
    static int lastMouseY = 0;
    POINT mousePos;
    GetCursorPos(&mousePos);
    ScreenToClient(mhMainWnd, &mousePos);

    int deltaX = mousePos.x - lastMouseX;
    int deltaY = mousePos.y - lastMouseY;

    // Для вращения камеры (ПКМ)
    float mouseDeltaX = 0.0f;
    float mouseDeltaY = 0.0f;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) {
        mouseDeltaX = static_cast<float>(deltaX);
        mouseDeltaY = static_cast<float>(deltaY);
    }

    lastMouseX = mousePos.x;
    lastMouseY = mousePos.y;
    // ==========================================

    // ========== УПРАВЛЕНИЕ КАМЕРОЙ (СТРЕЛКИ + Q/E) ==========

    if (m_cameraSystem) {
        m_cameraSystem->Update(gt.DeltaTime(),
            (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0,   // вращение влево
            (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0,  // вращение вправо
            (GetAsyncKeyState(VK_UP) & 0x8000) != 0,     // приближение (вверх)
            (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0,   // отдаление (вниз)
            (GetAsyncKeyState('Q') & 0x8000) != 0,       // Q - камера вверх
            (GetAsyncKeyState('E') & 0x8000) != 0,       // E - камера вниз
            mouseDeltaX, mouseDeltaY, 0.0f);
    }
    // ========================================================
    // Управление камерой через указатель
    if (m_camera) {
        float speed = 3.0f * gt.DeltaTime();

        if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
            m_camera->position.x -= speed;
            m_camera->target.x -= speed;
            //Logger::Info("Camera X: " + std::to_string(m_camera->position.x));
        }
        if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
            m_camera->position.x += speed;
            m_camera->target.x += speed;
        }
        if (GetAsyncKeyState(VK_UP) & 0x8000) {
            m_camera->position.z -= speed;
            m_camera->target.z -= speed;
        }
        if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
            m_camera->position.z += speed;
            m_camera->target.z += speed;
        }
    }
// КЛАВИША '1' - вращение треугольника
    static bool wasOnePressed = false;
    bool onePressed = (GetAsyncKeyState('1') & 0x8000);

    if (onePressed && !wasOnePressed) {
        m_isRotating = !m_isRotating;
        if (!m_isRotating) {
            // Сбрасываем угол при остановке (опционально)
            // m_rotationAngle = 0.0f;
        }
        Logger::Info(m_isRotating ? "Triangle Rotation STARTED" : "Triangle Rotation STOPPED");
    }
    wasOnePressed = onePressed;

    if (m_showECS && m_isRotating)
    {
        m_rotationAngle += gt.DeltaTime() * 1.5f;
        Transform* t = m_world.GetTransform(m_rotatingTriangle);
        if (t) {
            t->rotation = glm::angleAxis(m_rotationAngle, glm::vec3(0.0f, 0.0f, 1.0f));
        }
    }
  

    // КЛАВИША '2' - прыжки ромба
    static bool wasThreePressed = false;
    bool threePressed = (GetAsyncKeyState('2') & 0x8000);

    if (threePressed && !wasThreePressed) {
        m_isJumping = !m_isJumping;
        if (!m_isJumping) {
            // Сбрасываем фазу прыжка при остановке
            m_jumpPhase = 0.0f;
        }
        Logger::Info(m_isJumping ? "Rhombus Jumping STARTED" : "Rhombus Jumping STOPPED");
    }
    wasThreePressed = threePressed;

    if (m_isJumping)
    {
        m_jumpPhase += gt.DeltaTime() * m_jumpSpeed;
        float newY = m_originalY + sin(m_jumpPhase) * 1.0f;

        Transform* t = m_world.GetTransform(m_circle);
        if (t) {
            t->position.y = newY;
        }
    }

    // ========== ГОРЯЧИЕ КЛАВИШИ ДЛЯ СЕРИАЛИЗАЦИИ ==========
    static bool wasF5Pressed = false;
    bool f5Pressed = (GetAsyncKeyState(VK_F5) & 0x8000);
    if (f5Pressed && !wasF5Pressed) {
        SaveScene("scene.json");
    }
    wasF5Pressed = f5Pressed;

    static bool wasF6Pressed = false;
    bool f6Pressed = (GetAsyncKeyState(VK_F6) & 0x8000);
    if (f6Pressed && !wasF6Pressed) {
        LoadScene("scene.json");
    }
    wasF6Pressed = f6Pressed;

}


void Application::Draw(const GameTimer& gt)
{
    /*if (!m_cameraSystem) {
        Logger::Error("CameraSystem is null!");
        return;
    }*/

    /*glm::mat4 view = m_cameraSystem->GetViewMatrix();
    glm::mat4 proj = m_cameraSystem->GetProjectionMatrix((float)mClientWidth / mClientHeight);
    glm::vec3 cameraPos = m_cameraSystem->GetCameraPosition();*/
   
    // Получаем камеру из World
    Camera* camera = m_world.GetCamera(m_cameraEntity);
    if (!camera) {
        Logger::Error("No camera found!");
        return;
    }

    glm::mat4 view = glm::lookAt(m_camera->position, m_camera->target, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj = glm::perspective(glm::radians(45.0f), (float)mClientWidth / mClientHeight, 0.1f, 100.0f);
    glm::vec3 cameraPos = m_camera->position;

    mRenderAdapter->BeginFrame(0);

    if (m_showECS && m_renderSystem) {
        auto currentState = mStateManager.GetCurrentState();
        if (currentState && dynamic_cast<PlayState*>(currentState.get())) {

            m_world.UpdateSpatialGrid();
            std::vector<Entity> visibleEntities = m_world.GetVisibleEntities(cameraPos, 30.0f);

            D3D12RenderAdapter* d3d = dynamic_cast<D3D12RenderAdapter*>(mRenderAdapter.get());

            static int dbgCounter = 0;
            dbgCounter++;

            for (Entity e : visibleEntities) {
                Tag* tag = m_world.GetTag(e);
                if (tag && tag->name == "MainWindow") {
                    Transform* t = m_world.GetTransform(e);
                    MeshRenderer* mr = m_world.GetMeshRenderer(e);
                    if (t && mr) {

                        glm::mat4 model = t->GetLocalMatrix();
                        mRenderAdapter->SetModelMatrix(model);
                        mRenderAdapter->SetColor(mr->color);
                        mRenderAdapter->SetViewProjection(view, proj);

                        // ========== УСТАНОВКА ТЕКСТУРЫ ==========
                        if (mr->texture) {
                            mRenderAdapter->SetTexture(mr->texture.get());
                        }
                        else {
                            mRenderAdapter->SetTexture(nullptr);
                        }

                        if (mr->useLoadedMesh && mr->mesh) {
                            // Если GPU-меш еще не загружен - загружаем
                            if (mr->mesh->gpuMesh.IndexCount == 0 && d3d) {
                                auto gpu = d3d->UploadMesh(*mr->mesh, md3dDevice.Get(), mCommandList.Get());
                                if (gpu.IndexCount > 0) {
                                    mr->mesh->gpuMesh = gpu;
                                    Logger::Info("GPU Mesh uploaded for entity: " + std::to_string(e));
                                }
                            }

                            // Рисуем загруженный меш
                            if (mr->mesh->gpuMesh.IndexCount > 0) {
                                mRenderAdapter->DrawMesh(mr->mesh->gpuMesh);
                            }
                            else {
                                // Fallback на примитив, если меш не загрузился
                                mRenderAdapter->DrawPrimitiveECS(mr->type);
                            }
                        }
                        else {
                            // Рисуем примитив (треугольник, квадрат и т.д.)
                            mRenderAdapter->DrawPrimitiveECS(mr->type);
                        }
                    }
                }
            }
        }
    }

    //if (m_showECS && m_renderSystem) {
    //    auto currentState = mStateManager.GetCurrentState();
    //    if (currentState && dynamic_cast<PlayState*>(currentState.get())) {

    //        m_world.UpdateSpatialGrid();

    //        // Основная отрисовка теперь здесь:
    //        m_renderSystem->UpdateWithLoadedMeshes(m_world, view, proj);
    //    }
    //}

    mRenderAdapter->EndFrame(0);

    if (mShowSecondaryWnd) {
        mRenderAdapter->BeginFrame(1);
        mRenderAdapter->DrawPrimitive(PrimitiveType::Square, { 0.0f, 0.0f, 0.0f }, 0.0f, 1.5f);
        mRenderAdapter->EndFrame(1);
    }

    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
}


void Application::OnMouseDown(WPARAM btnState, int x, int y)
{
    m_lastMouseX = x;
    m_lastMouseY = y;
    mStateManager.OnMouseDown(btnState, x, y);
}

void Application::OnMouseUp(WPARAM btnState, int x, int y)
{
    mStateManager.OnMouseUp(btnState, x, y);
}

void Application::OnMouseMove(WPARAM btnState, int x, int y)
{
    int deltaX = x - m_lastMouseX;
    int deltaY = y - m_lastMouseY;

    // Для вращения камеры (ПКМ)
    if (btnState & MK_RBUTTON) {
        m_mouseDeltaX = static_cast<float>(deltaX);
        m_mouseDeltaY = static_cast<float>(deltaY);
    }

    m_lastMouseX = x;
    m_lastMouseY = y;

    // НЕ вызываем mStateManager.OnMouseMove, чтобы не конфликтовать
    // mStateManager.OnMouseMove(btnState, x, y);
}


bool D3D12RenderAdapter::Initialize()
{
    Logger::Info("Initializing D3D12RenderAdapter...");

    ThrowIfFailed(mApp->mCommandList->Reset(mApp->mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildGeometry();
    BuildPSO();

    // Создаём дескрипторный хип для текстур
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 64;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(mApp->md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mTextureSrvHeap)));
    mTextureSrvDescriptorSize = mApp->md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Загружаем текстуру из файла
    auto texData = TextureData::LoadFromFile("assets/textures/texture_stepler.jpg");
    if (texData) {
        m_mainTextureData = *texData;
        if (CreateTexture(m_mainTextureData, mApp->md3dDevice.Get(), mApp->mCommandList.Get())) {
            CreateTextureSRV(m_mainTextureData.textureResource, 0);
            m_currentTexture = &m_mainTextureData;
            m_mainTextureData.srvIndex = 0;
            Logger::Info("Main texture loaded and SRV created");
        }
        else {
            Logger::Error("Failed to create GPU texture");
        }
    }
    else {
        Logger::Error("Failed to load texture from file");
    }

    // Закрываем command list (команды BuildGeometry + копирование текстуры)
    ThrowIfFailed(mApp->mCommandList->Close());
    ID3D12CommandList* cmdLists[] = { mApp->mCommandList.Get() };
    mApp->mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);
    mApp->FlushCommandQueue();

    // Сбрасываем command list для следующего кадра (он будет открыт)
    ThrowIfFailed(mApp->mCommandList->Reset(mApp->mDirectCmdListAlloc.Get(), nullptr));
    // И сразу закрываем, чтобы первый кадр мог сбросить allocator без ошибки
    ThrowIfFailed(mApp->mCommandList->Close());

    Logger::Info("D3D12RenderAdapter initialized successfully");
    return true;
}

void D3D12RenderAdapter::BeginFrame(int windowIndex) {
    auto cmdListAlloc = mApp->mDirectCmdListAlloc;
    auto cmdList = mApp->mCommandList;

    if (windowIndex == 0) {
        ThrowIfFailed(cmdListAlloc->Reset());
    }
    ThrowIfFailed(cmdList->Reset(cmdListAlloc.Get(), mPSO.Get()));

    UINT backBufferIndex = 0;
    ComPtr<IDXGISwapChain3> sc3;
    if (windowIndex == 0) {
        if (SUCCEEDED(mApp->mSwapChain.As(&sc3))) {
            backBufferIndex = sc3->GetCurrentBackBufferIndex();
        }
    }
    else {
        if (SUCCEEDED(mApp->mSecondarySwapChain.As(&sc3))) {
            backBufferIndex = sc3->GetCurrentBackBufferIndex();
        }
    }

    ID3D12Resource* currentBuffer = (windowIndex == 0) ? mApp->mSwapChainBuffer[backBufferIndex].Get() : mApp->mSecondarySwapChainBuffer[backBufferIndex].Get();
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = (windowIndex == 0) ?
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mApp->mRtvHeap->GetCPUDescriptorHandleForHeapStart(), backBufferIndex, mApp->mRtvDescriptorSize) :
        CD3DX12_CPU_DESCRIPTOR_HANDLE(mApp->mSecondaryRtvHeap->GetCPUDescriptorHandleForHeapStart(), backBufferIndex, mApp->mRtvDescriptorSize);

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(currentBuffer,
        D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &barrier);

    cmdList->RSSetViewports(1, &mApp->mScreenViewport);
    cmdList->RSSetScissorRects(1, &mApp->mScissorRect);

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
    }
    else {
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
    }
    else {
        ThrowIfFailed(mApp->mSecondarySwapChain->Present(0, 0));
    }

    mApp->FlushCommandQueue();
}

void D3D12RenderAdapter::DrawPrimitive(PrimitiveType type, DirectX::XMFLOAT3 position, float rotation, float scale) {
    auto cmdList = mApp->mCommandList;

    // Вспомогательная лямбда для конвертации glm::mat4 в XMMATRIX
    auto ToXMMATRIX = [](const glm::mat4& m) -> DirectX::XMMATRIX {
        return DirectX::XMMatrixSet(
            m[0][0], m[0][1], m[0][2], m[0][3],
            m[1][0], m[1][1], m[1][2], m[1][3],
            m[2][0], m[2][1], m[2][2], m[2][3],
            m[3][0], m[3][1], m[3][2], m[3][3]
        );
        };

    DirectX::XMMATRIX s = DirectX::XMMatrixScaling(scale, scale, scale);
    DirectX::XMMATRIX r = DirectX::XMMatrixRotationZ(rotation);
    DirectX::XMMATRIX t = DirectX::XMMatrixTranslation(position.x, position.y, position.z);
    DirectX::XMMATRIX world = s * r * t;

    DirectX::XMMATRIX view = ToXMMATRIX(m_viewMatrix);
    DirectX::XMMATRIX proj = ToXMMATRIX(m_projectionMatrix);

    struct CB {
        DirectX::XMMATRIX world;
        DirectX::XMMATRIX view;
        DirectX::XMMATRIX proj;
    } cb;

    cb.world = DirectX::XMMatrixTranspose(world);
    cb.view = DirectX::XMMatrixTranspose(view);
    cb.proj = DirectX::XMMatrixTranspose(proj);

    cmdList->SetGraphicsRootSignature(mRootSignature.Get());
    cmdList->SetGraphicsRoot32BitConstants(0, 48, &cb, 0);

    cmdList->IASetVertexBuffers(0, 1, &mVBV);
    cmdList->IASetIndexBuffer(&mIBV);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    if (type == PrimitiveType::Triangle) {
        cmdList->DrawIndexedInstanced(3, 1, 0, 0, 0);
    }
    else if (type == PrimitiveType::Square || type == PrimitiveType::Quad) {
        cmdList->DrawIndexedInstanced(6, 1, 3, 0, 0);
    }
    else if (type == PrimitiveType::Cube) {
        cmdList->DrawIndexedInstanced(36, 1, 0, 0, 0);
    }
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
    // Позиция
    DirectX::XMFLOAT3 position(
        m_modelMatrix[3][0],
        m_modelMatrix[3][1],
        m_modelMatrix[3][2]
    );

    // Поворот (угол вокруг Z) из матрицы модели
    // Верхняя левая часть матрицы 2x2: [cos, -sin; sin, cos]
    float rotation = atan2(m_modelMatrix[1][0], m_modelMatrix[0][0]);

    // Масштаб
    float scale = sqrt(m_modelMatrix[0][0] * m_modelMatrix[0][0] + m_modelMatrix[1][0] * m_modelMatrix[1][0]);
    if (scale == 0) scale = 1.0f;

    // Устанавливаем текстуру (пока без текстуры, просто заглушка)
    //SetTexture(nullptr);  // или какую-то конкретную текстуру

    DrawPrimitive(type, position, rotation, scale);
}

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
    struct Vertex {
        XMFLOAT3 Pos;
        XMFLOAT4 Color;
        XMFLOAT2 TexCoord;
    };

    Vertex vertices[] =
    {
        // ТРЕУГОЛЬНИК (0, 1, 2)
        { XMFLOAT3(0.0f,  0.5f, 0.5f), XMFLOAT4(Colors::Red),    XMFLOAT2(0.5f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Green),  XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Blue),   XMFLOAT2(0.0f, 1.0f) },

        // КВАДРАТ (3, 4, 5, 6)
        { XMFLOAT3(-0.5f,  0.5f, 0.5f), XMFLOAT4(Colors::Cyan),    XMFLOAT2(0.0f, 0.0f) },
        { XMFLOAT3(0.5f,  0.5f, 0.5f), XMFLOAT4(Colors::Magenta), XMFLOAT2(1.0f, 0.0f) },
        { XMFLOAT3(0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::Yellow),  XMFLOAT2(1.0f, 1.0f) },
        { XMFLOAT3(-0.5f, -0.5f, 0.5f), XMFLOAT4(Colors::White),   XMFLOAT2(0.0f, 1.0f) }
    };

    std::uint16_t indices[] = {
        0, 1, 2,               // Треугольник
        3, 4, 5, 3, 5, 6       // Квадрат (два треугольника)
    };

    const UINT vbByteSize = sizeof(vertices);
    const UINT ibByteSize = sizeof(indices);

    mVertexBuffer = d3dUtil::CreateDefaultBuffer(mApp->md3dDevice.Get(), mApp->mCommandList.Get(),
        vertices, vbByteSize, mVertexBufferUploader);
    mIndexBuffer = d3dUtil::CreateDefaultBuffer(mApp->md3dDevice.Get(), mApp->mCommandList.Get(),
        indices, ibByteSize, mIndexBufferUploader);

    mVBV.BufferLocation = mVertexBuffer->GetGPUVirtualAddress();
    mVBV.StrideInBytes = sizeof(Vertex);  // теперь 32 байта (12+16+8)
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
    //psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    CD3DX12_RASTERIZER_DESC rasterDesc(D3D12_DEFAULT);
    rasterDesc.CullMode = D3D12_CULL_MODE_NONE;  // Было D3D12_CULL_MODE_BACK
    psoDesc.RasterizerState = rasterDesc;
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
