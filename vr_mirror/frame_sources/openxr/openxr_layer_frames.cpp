// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_layer_frames.h"

#include <atomic>

#include "shared/log.h"

namespace {

    constexpr int kMaxChains = 8;  // swapchains tracked; LMU makes two (OpenXR Toolkit adds one)
    constexpr int kMaxImages = 8;  // images per swapchain; runtimes give three

    // One of the game's swapchains. `handle` 0: a free entry. Written when the game makes it or asks for its
    // images, read at the end of each frame; acquiring and releasing are the game's threads, one at a time
    // per swapchain (OpenXR's rule for the game).
    struct Chain {
        std::atomic<uint64_t> handle{0};
        uint32_t format = 0;  // DXGI_FORMAT
        std::atomic<uint32_t> imageCount{0};
        ID3D11Texture2D* images[kMaxImages] = {};
        UINT mipLevels[kMaxImages] = {};
        uint32_t acquired[kMaxImages] = {};  // acquired and not yet released, oldest first
        uint32_t acquiredHead = 0;
        std::atomic<uint32_t> acquiredCount{0};  // read by the thread that ends the frame, too
        std::atomic<int32_t> released{-1};       // the image of the last release (the one a frame uses)
        std::atomic<uint32_t> releaseThread{0};  // the thread of the last release
        std::atomic<uint32_t> releases{0};       // releases so far
    };

    Chain g_chains[kMaxChains];
    OpenXrPublisher g_publisher;
    XrInstance g_instance = XR_NULL_HANDLE;
    PFN_XrCounterToTime g_counterToTime = nullptr;

    Chain* Find(XrSwapchain swapchain) {
        const uint64_t h = reinterpret_cast<uint64_t>(swapchain);
        for (Chain& c : g_chains) {
            if (c.handle.load(std::memory_order_acquire) == h) {
                return &c;
            }
        }

        return nullptr;
    }

    bool Clock(void*, int64_t counter, int64_t* time) {
        LARGE_INTEGER c;
        c.QuadPart = counter;
        XrTime t = 0;
        if (!g_counterToTime || XR_FAILED(g_counterToTime(g_instance, &c, &t))) {
            return false;
        }
        *time = t;
        return true;
    }

    openxr_share::Pose PoseOf(const XrPosef& p) {
        return {
            p.orientation.x,
            p.orientation.y,
            p.orientation.z,
            p.orientation.w,
            p.position.x,
            p.position.y,
            p.position.z
        };
    }

    openxr_share::Fov FovOf(const XrFovf& f) {
        return {f.angleLeft, f.angleRight, f.angleUp, f.angleDown};
    }

    // The frame's first projection layer, if it has a view; nullptr: nothing shown this frame.
    const XrCompositionLayerProjection* ProjectionOf(const XrFrameEndInfo& info) {
        for (uint32_t i = 0; i < info.layerCount && info.layers; i++) {
            const XrCompositionLayerBaseHeader* l = info.layers[i];

            if (l && l->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION) {
                const auto* projection = reinterpret_cast<const XrCompositionLayerProjection*>(l);
                return projection->viewCount >= 1 && projection->views ? projection : nullptr;
            }
        }

        return nullptr;
    }

}

void FramesSessionCreated(
    XrInstance instance,
    const XrSessionCreateInfo& info,
    PFN_XrCounterToTime counterToTime
) {
    ID3D11Device* device = nullptr;
    for (auto* s = static_cast<const XrBaseInStructure*>(info.next); s; s = s->next) {
        if (s->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            device = reinterpret_cast<const XrD3d11Binding*>(s)->device;
        }
    }

    if (!device) {
        CoreLog("frame source: not a D3D11 session: no frames for VR Mirror");
        return;
    }

    if (!counterToTime) {
        CoreLog("frame source: the instance has no %s: no frames for VR Mirror", kXrTimeConversionExtension);
        return;
    }

    g_instance = instance;
    g_counterToTime = counterToTime;
    g_publisher.Start(device, Clock, nullptr);
}

void FramesSessionDestroyed() {
    g_publisher.Stop();
}

void FramesSwapchainCreated(XrSwapchain swapchain, const XrSwapchainCreateInfo& info) {
    for (Chain& c : g_chains) {
        if (c.handle.load() != 0) {
            continue;
        }
        c.format = static_cast<uint32_t>(info.format);
        c.imageCount = 0;
        c.acquiredHead = 0;
        c.acquiredCount = 0;
        c.released = -1;
        c.releaseThread = 0;
        c.releases = 0;
        c.handle.store(reinterpret_cast<uint64_t>(swapchain), std::memory_order_release);
        return;
    }

    CoreLog(
        "frame source: more than %d swapchains: swapchain %llx not tracked",
        kMaxChains,
        static_cast<unsigned long long>(reinterpret_cast<uint64_t>(swapchain))
    );
}

void FramesSwapchainDestroyed(XrSwapchain swapchain) {
    if (Chain* c = Find(swapchain)) {
        c->imageCount = 0;
        c->released = -1;
        c->handle.store(0);
    }
}

void FramesSwapchainImages(XrSwapchain swapchain, uint32_t count, const XrSwapchainImageBaseHeader* images) {
    Chain* c = Find(swapchain);
    if (!c || !images || images->type != XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR) {
        return;
    }
    const auto* d3d = reinterpret_cast<const XrD3d11SwapchainImage*>(images);
    const uint32_t n = count < kMaxImages ? count : kMaxImages;
    for (uint32_t i = 0; i < n; i++) {
        c->images[i] = d3d[i].texture;
        D3D11_TEXTURE2D_DESC d = {};
        if (d3d[i].texture) {
            d3d[i].texture->GetDesc(&d);
        }
        c->mipLevels[i] = d.MipLevels ? d.MipLevels : 1;
    }

    c->imageCount.store(n, std::memory_order_release);
}

void FramesImageAcquired(XrSwapchain swapchain, uint32_t index) {
    Chain* c = Find(swapchain);
    if (!c || c->acquiredCount == kMaxImages) {
        return;
    }
    c->acquired[(c->acquiredHead + c->acquiredCount) % kMaxImages] = index;
    c->acquiredCount++;
}

void FramesImageReleased(XrSwapchain swapchain) {
    Chain* c = Find(swapchain);
    if (!c || c->acquiredCount == 0) {
        return;
    }
    c->releaseThread.store(GetCurrentThreadId());
    c->released.store(static_cast<int32_t>(c->acquired[c->acquiredHead]));  // the oldest acquired goes
    c->acquiredHead = (c->acquiredHead + 1) % kMaxImages;
    c->acquiredCount--;
    c->releases++;
}

void FramesDisplayPeriod(XrDuration period) {
    g_publisher.SetDisplayPeriod(period);
}

FrameImage FramesEnding(const XrFrameEndInfo& info) {
    const XrCompositionLayerProjection* projection = ProjectionOf(info);

    if (!g_publisher.Started() || !projection) {
        return {};
    }

    FrameImage image;
    image.swapchain = projection->views[0].subImage.swapchain;

    if (const Chain* c = Find(image.swapchain)) {
        image.releases = c->releases.load();
        image.releaseThread = c->releaseThread.load();
        image.index = c->released.load();
        image.held = c->acquiredCount.load();
    }

    return image;
}

void FramesEnded(const XrFrameEndInfo& info, const FrameImage& image, int64_t endFrameCounter) {
    const XrCompositionLayerProjection* projection = ProjectionOf(info);

    if (!g_publisher.Started() || !projection) {
        return;
    }

    const XrCompositionLayerProjectionView& left = projection->views[0];
    const XrCompositionLayerProjectionView& right = projection->views[projection->viewCount > 1 ? 1 : 0];
    OpenXrPublishedFrame f;
    f.displayTime = info.displayTime;
    f.endFrameCounter = endFrameCounter;
    f.eye[0] = PoseOf(left.pose), f.eye[1] = PoseOf(right.pose);
    f.fov[0] = FovOf(left.fov), f.fov[1] = FovOf(right.fov);
    f.gameImage = image.index;
    f.imagesHeld = image.held;
    f.endThread = GetCurrentThreadId();
    f.releaseThread = image.releaseThread;

    ID3D11Texture2D* src = nullptr;
    UINT subresource = 0;
    uint32_t format = 0;
    const Chain* c = image.swapchain == left.subImage.swapchain ? Find(image.swapchain) : nullptr;
    const int32_t i = image.index;

    if (c) {
        f.releasedDuringEnd = c->releases.load() - image.releases;
    }

    if (c && i >= 0 && static_cast<uint32_t>(i) < c->imageCount.load(std::memory_order_acquire)) {
        src = c->images[i];
        subresource = D3D11CalcSubresource(0, left.subImage.imageArrayIndex, c->mipLevels[i]);
        format = c->format;
    }

    const XrRect2Di& r = left.subImage.imageRect;
    D3D11_BOX box = {};
    box.left = UINT(r.offset.x), box.top = UINT(r.offset.y);
    box.right = UINT(r.offset.x + r.extent.width), box.bottom = UINT(r.offset.y + r.extent.height);
    box.front = 0, box.back = 1;
    g_publisher.Publish(f, src, subresource, box, format);
}

bool FramesTakeStats(OpenXrPublisherStats* stats) {
    if (!g_publisher.Started()) {
        return false;
    }
    *stats = g_publisher.TakeStats();
    return true;
}
