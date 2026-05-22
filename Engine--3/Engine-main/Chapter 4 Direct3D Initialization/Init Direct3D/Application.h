#pragma once

#include "../../Common/d3dApp.h"
#include "World.h"
#include "RenderSystem.h"
#include "GameStateManager.h"
#include <memory>

class RenderAdapter;
class CameraSystem;

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
    virtual void OnMouseWheel(float delta) {}

    // Публичные методы для доступа из D3D12RenderAdapter
    ID3D12Device* GetDevice() const { return md3dDevice.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return mCommandList.Get(); }



private:
    bool m_showECS = false;
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    float m_scrollDelta = 0.0f;
    int m_lastMouseX = 0;
    int m_lastMouseY = 0;

    std::unique_ptr<CameraSystem> m_cameraSystem;

    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;
    void CreateSecondaryWindow();
    void CreateSecondarySwapChain();
    void InitScene();

    GameStateManager mStateManager;
    std::unique_ptr<RenderAdapter> mRenderAdapter;

    HWND mhSecondaryWnd = nullptr;
    ComPtr<IDXGISwapChain> mSecondarySwapChain = nullptr;
    ComPtr<ID3D12Resource> mSecondarySwapChainBuffer[SwapChainBufferCount];
    ComPtr<ID3D12DescriptorHeap> mSecondaryRtvHeap = nullptr;
    int mFrameCount = 0;
    bool mShowSecondaryWnd = false;
    bool m_isPlaying = false;

    // ECS поля
    World m_world;
    std::unique_ptr<RenderSystem> m_renderSystem;
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

    glm::vec3 m_cameraPos = glm::vec3(0.0f, 0.0f, -5.0f);
    glm::vec3 m_cameraTarget = glm::vec3(0.0f, 0.0f, 0.0f);
    float m_cameraZoom = 5.0f;
    float m_cameraAngleX = 0.0f;
    float m_rotationAngle = 0.0f;
    float m_jumpPhase = 0.0f;
    bool m_isRotating = false;
    Camera* m_camera = nullptr;  // указатель на камеру
    Entity m_cameraEntity;  // хранение ID сущности камеры
};