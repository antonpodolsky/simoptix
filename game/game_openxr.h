// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// The demo game's OpenXR side: the loader, the instance, a D3D11 session on the headset's graphics card,
// one swapchain per eye in sRGB, as LMU makes them, and the frame loop that draws both eyes with the
// grid. Each eye's swapchain is the size the headset recommends times that eye's scale. It knows nothing
// of what the grid draws (game_grid.h) or where the chart sits (game_view.h).
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <vector>

#include "game_settings.h"
#include "vr_mirror/frame_sources/openxr/openxr_d3d11.h"

class GameGrid;

class GameOpenXr {
public:
    GameOpenXr() = default;
    GameOpenXr(const GameOpenXr&) = delete;
    GameOpenXr& operator=(const GameOpenXr&) = delete;
    ~GameOpenXr();

    // Loads the loader and makes the instance, the device, the session, its spaces and the swapchains.
    bool Open(const GameSettings& settings, std::string* error);

    ID3D11Device* Device() const { return device_.Get(); }

    // Handles OpenXR's events: begins and ends the session as the runtime asks.
    void PollEvents();

    // Asks the runtime to end the session. ShouldExit() turns true once it has.
    void RequestExit();

    bool IsRunning() const { return running_; }
    bool ShouldExit() const { return exit_; }

    // Waits for the runtime's next frame, then draws both eyes with `grid`, the chart centred at
    // `chartYaw` (radians, game_view.h), and submits them.
    void RunFrame(const GameGrid& grid, float chartYaw);

    // The head's yaw at the last frame (game_view.h), to centre the chart on.
    float HeadYaw() const { return headYaw_; }

private:
    struct Calls {
        PFN_xrGetInstanceProcAddr getProcAddr = nullptr;
        PFN_xrDestroyInstance destroyInstance = nullptr;
        PFN_xrGetSystem getSystem = nullptr;
        PFN_xrEnumerateViewConfigurationViews enumerateViews = nullptr;
        PFN_XrGetD3d11Requirements getD3d11Requirements = nullptr;
        PFN_xrCreateSession createSession = nullptr;
        PFN_xrDestroySession destroySession = nullptr;
        PFN_xrBeginSession beginSession = nullptr;
        PFN_xrEndSession endSession = nullptr;
        PFN_xrRequestExitSession requestExitSession = nullptr;
        PFN_xrPollEvent pollEvent = nullptr;
        PFN_xrEnumerateSwapchainFormats enumerateFormats = nullptr;
        PFN_xrCreateSwapchain createSwapchain = nullptr;
        PFN_xrDestroySwapchain destroySwapchain = nullptr;
        PFN_xrEnumerateSwapchainImages enumerateImages = nullptr;
        PFN_xrAcquireSwapchainImage acquireImage = nullptr;
        PFN_xrWaitSwapchainImage waitImage = nullptr;
        PFN_xrReleaseSwapchainImage releaseImage = nullptr;
        PFN_xrCreateReferenceSpace createSpace = nullptr;
        PFN_xrDestroySpace destroySpace = nullptr;
        PFN_xrLocateSpace locateSpace = nullptr;
        PFN_xrWaitFrame waitFrame = nullptr;
        PFN_xrBeginFrame beginFrame = nullptr;
        PFN_xrEndFrame endFrame = nullptr;
        PFN_xrLocateViews locateViews = nullptr;
    };

    struct Eye {
        XrSwapchain swapchain = XR_NULL_HANDLE;
        uint32_t width = 0, height = 0;
        std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> targets;  // one per swapchain image
    };

    bool LoadLoader(const std::wstring& path, std::string* error);
    bool CreateInstance(std::string* error);
    bool LoadCalls(std::string* error);
    bool CreateDevice(std::string* error);
    bool CreateSession(std::string* error);
    bool CreateSwapchains(const GameSettings& settings, std::string* error);
    bool ChooseFormat(int64_t* format, std::string* error);
    bool CreateSwapchain(int i, double scale, int64_t format, std::string* error);
    void LocateHead(XrTime time);
    void DrawEye(const GameGrid& grid, int eye, const XrView& view, float chartYaw);

    // The loader stays loaded until the process ends: the runtime's threads may still run in it.
    HMODULE loader_ = nullptr;
    Calls calls_;
    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrViewConfigurationView views_[2] = {
        {XR_TYPE_VIEW_CONFIGURATION_VIEW},
        {XR_TYPE_VIEW_CONFIGURATION_VIEW}
    };
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;  // the room: the head's place when the session began, level
    XrSpace viewSpace_ = XR_NULL_HANDLE;   // the head
    Eye eyes_[2];
    bool running_ = false, exit_ = false, endFrameFailing_ = false;
    float headYaw_ = 0;
};
