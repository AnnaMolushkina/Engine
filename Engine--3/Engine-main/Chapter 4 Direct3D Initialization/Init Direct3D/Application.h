#pragma once

#include "../../Common/d3dApp.h"
#include "World.h"
#include "RenderSystem.h"
#include "GameStateManager.h"
#include "Benchmark.h"
#include "Jobs/JobSystem.h"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

class RenderAdapter;
class CameraSystem;
struct MeshData;
struct TextureData;

class Application : public D3DApp
{
    friend class D3D12RenderAdapter;

public:
    Application(HINSTANCE hInstance);
    ~Application();

    void PreloadMeshes();
    void PreloadTextures();
    void LoadResourcesToGPU();
    void SaveScene(const std::string& filename);
    void LoadScene(const std::string& filename);

    virtual bool Initialize() override;
    virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

    // Публичные методы для доступа из D3D12RenderAdapter
    ID3D12Device* GetDevice() const { return md3dDevice.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return mCommandList.Get(); }

private:
    bool m_showECS = false;

    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void CreateSecondaryWindow();
    void CreateSecondarySwapChain();
    void CreateSecondaryDepthBuffer();
    void ResizeSecondaryWindow(UINT width, UINT height);
    void SetSecondaryWindowVisible(bool visible);
    static LRESULT CALLBACK SecondaryWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void InitScene();
    void UpdateCamera(const GameTimer& gt);
    void UpdateCaption();
    void Shutdown();

    // "Замерочная" сцена для job system (клавиша B / --bench)
    void SpawnStressScene(uint32_t count);
    void DespawnStressScene();
    void UpdateStressAnimation(const GameTimer& gt);

    // Job system - первой инициализируется, первой останавливается
    JobSystem mJobs;
    Benchmark mBenchmark;

    GameStateManager mStateManager;
    std::unique_ptr<RenderAdapter> mRenderAdapter;

    // Второе окно: свой swap chain, RTV и буфер глубины
    HWND mhSecondaryWnd = nullptr;
    ComPtr<IDXGISwapChain> mSecondarySwapChain = nullptr;
    ComPtr<ID3D12Resource> mSecondarySwapChainBuffer[SwapChainBufferCount];
    ComPtr<ID3D12DescriptorHeap> mSecondaryRtvHeap = nullptr;
    ComPtr<ID3D12Resource> mSecondaryDepthBuffer = nullptr;
    ComPtr<ID3D12DescriptorHeap> mSecondaryDsvHeap = nullptr;
    UINT mSecondaryWidth = 400;
    UINT mSecondaryHeight = 400;
    int mFrameCount = 0;
    bool mShowSecondaryWnd = false;
    bool m_isPlaying = false;

    // ECS поля
    World m_world;
    std::unique_ptr<RenderSystem> m_renderSystem;
    std::unique_ptr<CameraSystem> m_cameraSystem;
    Entity m_cameraEntity;  // ID сущности камеры

    Entity m_rotatingTriangle;
    Entity m_square;

    bool m_isScaling = false;
    float m_scaleDirection = 1.0f;
    float m_currentScale = 1.0f;

    Entity m_circle;
    bool m_isJumping = false;
    float m_jumpHeight = 0.0f;
    float m_jumpSpeed = 3.0f;
    float m_originalY = 0.0f;

    glm::vec3 m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    float m_rotationAngle = 0.0f;
    float m_jumpPhase = 0.0f;
    bool m_isRotating = false;

    // Стресс-сцена: данные анимации лежат плотными массивами - удобно резать на диапазоны
    struct StressScene {
        std::vector<Entity> entities;
        std::vector<Transform*> transforms;   // указатели на элементы unordered_map стабильны
        std::vector<glm::vec3> basePositions;
        std::vector<glm::vec3> axes;
        std::vector<float> speeds;
        std::vector<float> phases;
        std::shared_ptr<MeshData> mesh;
        std::vector<std::shared_ptr<TextureData>> textures;
    } m_stress;

    // Замер времени кадра
    std::chrono::steady_clock::time_point mLastFrameEnd;
    bool mHasLastFrameEnd = false;
    std::wstring mCaptionInfo;
    bool mShutDown = false;
};
