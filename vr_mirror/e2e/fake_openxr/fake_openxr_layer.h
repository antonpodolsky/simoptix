// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// FakeOpenXrLayer: the OpenXR layer's own frame code (vr_mirror\frame_sources\openxr\openxr_layer_frames.*)
// driven as the game's OpenXR calls drive it in LMU: one D3D11 session with the runtime's time
// conversion, one swapchain of three images for the left eye (LMU's size and format), and per frame the
// image acquired, drawn into, released, and the frame ended with its display time and both eyes. The
// layer picks the image a frame shows, copies it and publishes the frame, exactly as in the game.
//
// The runtime's side is faked: images are handed out in turn, and what happens while xrEndFrame is in
// the runtime is the caller's (a runtime that holds the call, a game thread that goes on meanwhile).
// Knows nothing about the fake game's timing, motion or image.
#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>

#include "vr_mirror/frame_sources/openxr/openxr_publisher.h"
#include "vr_mirror/frame_sources/openxr/openxr_share.h"

class FakeOpenXrLayer {
public:
    static constexpr int kImages = 3;  // the runtime's swapchain images (Pimax's runtime gives three)

    // What the game passes to xrEndFrame for its one projection layer.
    struct FrameEnd {
        int64_t displayTime = 0;  // XrTime (ns)
        openxr_share::Pose eye[2];
        openxr_share::Fov fov[2];
    };

    ~FakeOpenXrLayer() { Stop(); }

    // The game's session begins: the layer starts publishing. `clock` converts a performance-counter
    // value to XrTime, as the runtime's xrConvertWin32PerformanceCounterToTimeKHR does.
    bool Start(
        ID3D11Device* device,
        int width,
        int height,
        int64_t displayPeriodNs,
        bool (*clock)(int64_t counter, int64_t* time)
    );

    // The session ends: the layer stops and lets go of the shared memory.
    void Stop();

    // xrAcquireSwapchainImage: the next image in turn, as a render target to draw into.
    ID3D11RenderTargetView* Acquire();
    // xrReleaseSwapchainImage: the oldest image acquired goes back to the runtime.
    void Release();
    // xrEndFrame; `inRuntime` runs while the call is in the runtime, before it returns to the layer.
    void EndFrame(const FrameEnd& frame, const std::function<void()>& inRuntime);

    // The layer's numbers since the last call.
    OpenXrPublisherStats TakeStats();

private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> images_[kImages];
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> targets_[kImages];
    int width_ = 0, height_ = 0;
    int nextImage_ = 0;  // the image the next acquire hands out
    bool started_ = false;
};
