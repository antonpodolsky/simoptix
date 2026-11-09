// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "fake_openxr_layer.h"

#include <windows.h>

#include <cstdint>

#include "vr_mirror/frame_sources/openxr/openxr_layer_frames.h"

namespace {

    // The fake's clock, handed to the layer as the runtime's time conversion.
    bool (*g_clock)(int64_t counter, int64_t* time) = nullptr;

    XrResult XRAPI_CALL CounterToTime(XrInstance, const LARGE_INTEGER* counter, XrTime* time) {
        int64_t t = 0;

        if (!g_clock || !counter || !time || !g_clock(counter->QuadPart, &t)) {
            return XR_ERROR_RUNTIME_FAILURE;
        }

        *time = t;
        return XR_SUCCESS;
    }

    // Handles the layer only compares: any value but 0 does.
    const XrInstance kInstance = reinterpret_cast<XrInstance>(uintptr_t{0x1});
    const XrSwapchain kSwapchain = reinterpret_cast<XrSwapchain>(uintptr_t{0x5c});

    int64_t Counter() {
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return c.QuadPart;
    }

    XrPosef PoseOf(const openxr_share::Pose& p) {
        XrPosef pose = {};
        pose.orientation = {p.qx, p.qy, p.qz, p.qw};
        pose.position = {p.px, p.py, p.pz};
        return pose;
    }

    XrFovf FovOf(const openxr_share::Fov& f) {
        return {f.left, f.right, f.up, f.down};
    }

}

bool FakeOpenXrLayer::Start(
    ID3D11Device* device,
    int width,
    int height,
    int64_t displayPeriodNs,
    bool (*clock)(int64_t counter, int64_t* time)
) {
    Stop();
    width_ = width, height_ = height;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;

    for (int i = 0; i < kImages; i++) {
        if (FAILED(device->CreateTexture2D(&desc, nullptr, &images_[i]))) {
            return false;
        }

        if (FAILED(device->CreateRenderTargetView(images_[i].Get(), nullptr, &targets_[i]))) {
            return false;
        }
    }

    g_clock = clock;
    XrD3d11Binding binding = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, nullptr, device};
    XrSessionCreateInfo session = {XR_TYPE_SESSION_CREATE_INFO};
    session.next = &binding;
    FramesSessionCreated(kInstance, session, CounterToTime);

    XrSwapchainCreateInfo swapchain = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchain.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    swapchain.width = width, swapchain.height = height;
    swapchain.sampleCount = swapchain.faceCount = swapchain.arraySize = swapchain.mipCount = 1;
    FramesSwapchainCreated(kSwapchain, swapchain);

    XrD3d11SwapchainImage images[kImages];

    for (int i = 0; i < kImages; i++) {
        images[i] = {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR, nullptr, images_[i].Get()};
    }

    FramesSwapchainImages(kSwapchain, kImages, reinterpret_cast<const XrSwapchainImageBaseHeader*>(images));
    FramesDisplayPeriod(displayPeriodNs);
    nextImage_ = 0;
    started_ = true;
    return true;
}

void FakeOpenXrLayer::Stop() {
    if (!started_) {
        return;
    }

    FramesSwapchainDestroyed(kSwapchain);
    FramesSessionDestroyed();
    started_ = false;
}

ID3D11RenderTargetView* FakeOpenXrLayer::Acquire() {
    const int image = nextImage_;
    nextImage_ = (nextImage_ + 1) % kImages;
    FramesImageAcquired(kSwapchain, static_cast<uint32_t>(image));
    return targets_[image].Get();
}

void FakeOpenXrLayer::Release() {
    FramesImageReleased(kSwapchain);
}

void FakeOpenXrLayer::EndFrame(const FrameEnd& frame, const std::function<void()>& inRuntime) {
    XrCompositionLayerProjectionView views[2] = {};

    for (int eye = 0; eye < 2; eye++) {
        views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
        views[eye].pose = PoseOf(frame.eye[eye]);
        views[eye].fov = FovOf(frame.fov[eye]);
        views[eye].subImage.swapchain = kSwapchain;
        views[eye].subImage.imageRect = {{0, 0}, {width_, height_}};
    }

    XrCompositionLayerProjection projection = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    projection.viewCount = 2;
    projection.views = views;
    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projection),
    };

    XrFrameEndInfo info = {XR_TYPE_FRAME_END_INFO};
    info.displayTime = frame.displayTime;
    info.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    info.layerCount = 1;
    info.layers = layers;

    const FrameImage image = FramesEnding(info);
    inRuntime();
    FramesEnded(info, image, Counter());
}

OpenXrPublisherStats FakeOpenXrLayer::TakeStats() {
    OpenXrPublisherStats stats;
    FramesTakeStats(&stats);
    return stats;
}
