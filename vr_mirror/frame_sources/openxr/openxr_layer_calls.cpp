// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_layer_calls.h"

#include <windows.h>

#include <atomic>
#include <cstring>

#include "openxr_d3d11.h"
#include "openxr_frame_layout.h"
#include "openxr_layer_frames.h"
#include "shared/log.h"

// No locks: the game's threads must never wait here. What crosses threads is atomic; logging copies the
// line and returns (openxr_layer_log.h).
namespace {

    constexpr uint32_t kMaxFailuresLogged = 5;  // per kind of call, so a broken session can't fill the disk
    constexpr double kStatsPeriodS = 10.0;      // performance-counter seconds between frame-rate lines

    // The next layer's or the runtime's functions. Written once in StartCalls, on the game's thread, before
    // the loader hands any of the layer's own functions to the game; read-only after that.
    struct NextCalls {
        PFN_xrGetInstanceProcAddr getInstanceProcAddr = nullptr;
        PFN_xrDestroyInstance destroyInstance = nullptr;
        PFN_xrGetInstanceProperties getInstanceProperties = nullptr;
        PFN_xrGetSystemProperties getSystemProperties = nullptr;
        PFN_xrCreateSession createSession = nullptr;
        PFN_xrDestroySession destroySession = nullptr;
        PFN_xrBeginSession beginSession = nullptr;
        PFN_xrEndSession endSession = nullptr;
        PFN_xrCreateSwapchain createSwapchain = nullptr;
        PFN_xrDestroySwapchain destroySwapchain = nullptr;
        PFN_xrEnumerateSwapchainImages enumerateSwapchainImages = nullptr;
        PFN_xrAcquireSwapchainImage acquireSwapchainImage = nullptr;
        PFN_xrReleaseSwapchainImage releaseSwapchainImage = nullptr;
        PFN_xrWaitFrame waitFrame = nullptr;
        PFN_xrEndFrame endFrame = nullptr;
        PFN_XrCounterToTime counterToTime = nullptr;  // only with the time conversion extension
    };

    NextCalls g_next;

    // Set by BeginSession (any thread), taken by the frame thread to start its counts afresh.
    std::atomic<bool> g_sessionBegan{false};
    // Frames the game ended in this session: counted by the frame thread, read when the session ends.
    std::atomic<uint64_t> g_sessionFrames{0};
    std::atomic<uint32_t> g_frameFailures{0};

    // The frame thread's own state (the game's thread that calls xrEndFrame).
    OpenXrFrameLayout g_lastLayout;
    bool g_haveLayout = false;
    uint64_t g_statsFrames = 0;
    LARGE_INTEGER g_statsStart = {};
    LARGE_INTEGER g_qpcFrequency = {};

    template <typename Fn>
    void Resolve(XrInstance instance, const char* name, Fn& fn) {
        PFN_xrVoidFunction f = nullptr;
        if (XR_FAILED(g_next.getInstanceProcAddr(instance, name, &f))) {
            f = nullptr;
        }
        fn = reinterpret_cast<Fn>(f);
    }

    void LogFailure(const char* call, XrResult r, std::atomic<uint32_t>& count) {
        if (count.fetch_add(1) < kMaxFailuresLogged) {
            CoreLog("%s failed: %d", call, static_cast<int>(r));
        }
    }

    void LogLayout(const OpenXrFrameLayout& f, uint64_t frame) {
        CoreLog(
            "frame %llu: %u layer(s), blend mode %d",
            static_cast<unsigned long long>(frame),
            f.layerCount,
            static_cast<int>(f.blendMode)
        );

        char line[320];
        for (uint32_t i = 0; i < f.layerCount && i < kMaxLayoutLayers; i++) {
            const LayoutLayer& l = f.layers[i];
            CoreLog("%s", FormatLayer(l, i, line, sizeof(line)));
            for (uint32_t v = 0; v < l.viewCount && v < kMaxLayoutViews; v++) {
                CoreLog("%s", FormatView(l.views[v], v, line, sizeof(line)));
            }
        }
    }

    // On the frame thread, after the runtime took the frame.
    void OnFrameEnded(const XrFrameEndInfo& info) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        if (g_sessionBegan.exchange(false)) {
            g_haveLayout = false;
            g_statsFrames = 0;
            g_statsStart = now;
        }

        const uint64_t frame = g_sessionFrames.fetch_add(1) + 1;
        OpenXrFrameLayout layout = SummarizeFrame(info);
        if (!g_haveLayout || !SameLayout(layout, g_lastLayout)) {
            LogLayout(layout, frame);
            g_lastLayout = layout;
            g_haveLayout = true;
        }

        g_statsFrames++;
        const double elapsed = static_cast<double>(now.QuadPart - g_statsStart.QuadPart)
                               / g_qpcFrequency.QuadPart;

        if (elapsed >= kStatsPeriodS) {
            CoreLog(
                "game frames: %llu in %.1f s, %.2f per second",
                static_cast<unsigned long long>(g_statsFrames),
                elapsed,
                g_statsFrames / elapsed
            );

            OpenXrPublisherStats p;
            if (FramesTakeStats(&p) && p.frames) {
                CoreLog(
                    "frame source: %u frames, %u copied, not copied: %u (VR Mirror reading the texture), %u "
                    "(copy before not finished); copy on the GPU p50 %.3f ms, p99 %.3f ms, max %.3f ms (%u "
                    "timed)",
                    p.frames,
                    p.copied,
                    p.readerBusy,
                    p.copyPending,
                    p.gpuP50,
                    p.gpuP99,
                    p.gpuMax,
                    p.timed
                );
            }

            g_statsFrames = 0;
            g_statsStart = now;
        }
    }

    const char* BindingName(const void* next) {
        for (auto* s = static_cast<const XrBaseInStructure*>(next); s; s = s->next) {
            if (const char* name = XrTypeName(s->type)) {
                return name;
            }
        }

        return "none found";
    }

    // ---- The calls handed to the game. Each passes the call on first, then only reads and logs. ----

    XrResult XRAPI_CALL DestroyInstance(XrInstance instance) {
        try {
            FramesSessionDestroyed();  // a session the game didn't destroy itself
            CoreLog("instance destroyed");
        } catch (...) {
        }

        return g_next.destroyInstance(instance);
    }

    XrResult XRAPI_CALL
    CreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
        XrResult r = g_next.createSession(instance, info, session);
        try {
            if (XR_FAILED(r) || !info) {
                CoreLog("session not created: %d", static_cast<int>(r));
                return r;
            }

            XrSystemProperties props = {XR_TYPE_SYSTEM_PROPERTIES};
            const char* system = "unknown";
            if (g_next.getSystemProperties
                && XR_SUCCEEDED(g_next.getSystemProperties(instance, info->systemId, &props))) {
                system = props.systemName;
            }

            CoreLog(
                "session %llx created: graphics %s, headset %s",
                reinterpret_cast<unsigned long long>(*session),
                BindingName(info->next),
                system
            );

            FramesSessionCreated(instance, *info, g_next.counterToTime);
        } catch (...) {
        }

        return r;
    }

    XrResult XRAPI_CALL DestroySession(XrSession session) {
        try {
            FramesSessionDestroyed();  // before the runtime lets go of the device and the swapchains
        } catch (...) {
        }

        XrResult r = g_next.destroySession(session);
        try {
            CoreLog("session %llx destroyed", reinterpret_cast<unsigned long long>(session));
        } catch (...) {
        }

        return r;
    }

    XrResult XRAPI_CALL BeginSession(XrSession session, const XrSessionBeginInfo* info) {
        XrResult r = g_next.beginSession(session, info);
        try {
            g_sessionBegan = true;
            g_sessionFrames = 0;
            CoreLog(
                "session %llx began: view configuration %d, result %d",
                reinterpret_cast<unsigned long long>(session),
                info ? static_cast<int>(info->primaryViewConfigurationType) : -1,
                static_cast<int>(r)
            );
        } catch (...) {
        }

        return r;
    }

    XrResult XRAPI_CALL EndSession(XrSession session) {
        XrResult r = g_next.endSession(session);
        try {
            CoreLog(
                "session %llx ended after %llu frames",
                reinterpret_cast<unsigned long long>(session),
                static_cast<unsigned long long>(g_sessionFrames.load())
            );
        } catch (...) {
        }

        return r;
    }

    XrResult XRAPI_CALL
    CreateSwapchain(XrSession session, const XrSwapchainCreateInfo* info, XrSwapchain* swapchain) {
        XrResult r = g_next.createSwapchain(session, info, swapchain);
        try {
            if (XR_FAILED(r) || !info) {
                CoreLog("swapchain not created: %d", static_cast<int>(r));
                return r;
            }

            SwapchainInfo s = DescribeSwapchain(reinterpret_cast<uint64_t>(*swapchain), *info);
            char line[320];
            CoreLog("%s", FormatSwapchain(s, line, sizeof(line)));
            FramesSwapchainCreated(*swapchain, *info);
        } catch (...) {
        }

        return r;
    }

    XrResult XRAPI_CALL DestroySwapchain(XrSwapchain swapchain) {
        try {
            FramesSwapchainDestroyed(swapchain);
        } catch (...) {
        }

        XrResult r = g_next.destroySwapchain(swapchain);
        try {
            CoreLog("swapchain %llx destroyed", reinterpret_cast<unsigned long long>(swapchain));
        } catch (...) {
        }

        return r;
    }

    XrResult XRAPI_CALL EnumerateSwapchainImages(
        XrSwapchain swapchain,
        uint32_t capacity,
        uint32_t* count,
        XrSwapchainImageBaseHeader* images
    ) {
        XrResult r = g_next.enumerateSwapchainImages(swapchain, capacity, count, images);
        try {
            if (XR_SUCCEEDED(r) && capacity > 0 && images && count) {
                FramesSwapchainImages(swapchain, *count, images);
            }
        } catch (...) {
        }

        return r;
    }

    XrResult XRAPI_CALL
    AcquireSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageAcquireInfo* info, uint32_t* index) {
        XrResult r = g_next.acquireSwapchainImage(swapchain, info, index);
        if (XR_SUCCEEDED(r) && index) {
            FramesImageAcquired(swapchain, *index);
        }
        return r;
    }

    XrResult XRAPI_CALL
    ReleaseSwapchainImage(XrSwapchain swapchain, const XrSwapchainImageReleaseInfo* info) {
        XrResult r = g_next.releaseSwapchainImage(swapchain, info);
        if (XR_SUCCEEDED(r)) {
            FramesImageReleased(swapchain);
        }
        return r;
    }

    XrResult XRAPI_CALL WaitFrame(XrSession session, const XrFrameWaitInfo* info, XrFrameState* state) {
        XrResult r = g_next.waitFrame(session, info, state);
        if (XR_SUCCEEDED(r) && state) {
            FramesDisplayPeriod(state->predictedDisplayPeriod);
        }
        return r;
    }

    XrResult XRAPI_CALL EndFrame(XrSession session, const XrFrameEndInfo* info) {
        XrResult r = g_next.endFrame(session, info);
        try {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            if (XR_FAILED(r)) {
                LogFailure("xrEndFrame", r, g_frameFailures);
            } else if (info) {
                FramesEnded(*info, now.QuadPart);  // the frame already went on: the copy comes after it
                OnFrameEnded(*info);
            }
        } catch (...) {
        }

        return r;
    }

    struct NamedCall {
        const char* name;
        PFN_xrVoidFunction fn;
    };

    const NamedCall kCalls[] = {
        {"xrDestroyInstance", reinterpret_cast<PFN_xrVoidFunction>(DestroyInstance)},
        {"xrCreateSession", reinterpret_cast<PFN_xrVoidFunction>(CreateSession)},
        {"xrDestroySession", reinterpret_cast<PFN_xrVoidFunction>(DestroySession)},
        {"xrBeginSession", reinterpret_cast<PFN_xrVoidFunction>(BeginSession)},
        {"xrEndSession", reinterpret_cast<PFN_xrVoidFunction>(EndSession)},
        {"xrCreateSwapchain", reinterpret_cast<PFN_xrVoidFunction>(CreateSwapchain)},
        {"xrDestroySwapchain", reinterpret_cast<PFN_xrVoidFunction>(DestroySwapchain)},
        {"xrEnumerateSwapchainImages", reinterpret_cast<PFN_xrVoidFunction>(EnumerateSwapchainImages)},
        {"xrAcquireSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(AcquireSwapchainImage)},
        {"xrReleaseSwapchainImage", reinterpret_cast<PFN_xrVoidFunction>(ReleaseSwapchainImage)},
        {"xrWaitFrame", reinterpret_cast<PFN_xrVoidFunction>(WaitFrame)},
        {"xrEndFrame", reinterpret_cast<PFN_xrVoidFunction>(EndFrame)},
    };

}

void StartCalls(XrInstance instance, PFN_xrGetInstanceProcAddr next, const XrInstanceCreateInfo& info) {
    QueryPerformanceFrequency(&g_qpcFrequency);
    g_next.getInstanceProcAddr = next;

    Resolve(instance, "xrDestroyInstance", g_next.destroyInstance);
    Resolve(instance, "xrGetInstanceProperties", g_next.getInstanceProperties);
    Resolve(instance, "xrGetSystemProperties", g_next.getSystemProperties);
    Resolve(instance, "xrCreateSession", g_next.createSession);
    Resolve(instance, "xrDestroySession", g_next.destroySession);
    Resolve(instance, "xrBeginSession", g_next.beginSession);
    Resolve(instance, "xrEndSession", g_next.endSession);
    Resolve(instance, "xrCreateSwapchain", g_next.createSwapchain);
    Resolve(instance, "xrDestroySwapchain", g_next.destroySwapchain);
    Resolve(instance, "xrEnumerateSwapchainImages", g_next.enumerateSwapchainImages);
    Resolve(instance, "xrAcquireSwapchainImage", g_next.acquireSwapchainImage);
    Resolve(instance, "xrReleaseSwapchainImage", g_next.releaseSwapchainImage);
    Resolve(instance, "xrWaitFrame", g_next.waitFrame);
    Resolve(instance, "xrEndFrame", g_next.endFrame);

    for (uint32_t i = 0; i < info.enabledExtensionCount; i++) {
        if (strcmp(info.enabledExtensionNames[i], kXrTimeConversionExtension) == 0) {
            Resolve(instance, "xrConvertWin32PerformanceCounterToTimeKHR", g_next.counterToTime);
        }
    }

    XrInstanceProperties props = {XR_TYPE_INSTANCE_PROPERTIES};
    if (g_next.getInstanceProperties && XR_SUCCEEDED(g_next.getInstanceProperties(instance, &props))) {
        CoreLog(
            "runtime: %s %u.%u.%u",
            props.runtimeName,
            XR_VERSION_MAJOR(props.runtimeVersion),
            XR_VERSION_MINOR(props.runtimeVersion),
            XR_VERSION_PATCH(props.runtimeVersion)
        );
    }

    XrVersion api = info.applicationInfo.apiVersion;

    // As it reaches this layer: layers before it in the chain (OpenXR Toolkit) may have added extensions.
    CoreLog(
        "instance for OpenXR %u.%u.%u, with %u extension(s):",
        XR_VERSION_MAJOR(api),
        XR_VERSION_MINOR(api),
        XR_VERSION_PATCH(api),
        info.enabledExtensionCount
    );

    for (uint32_t i = 0; i < info.enabledExtensionCount; i++) {
        CoreLog("  %s", info.enabledExtensionNames[i]);
    }
}

PFN_xrVoidFunction LayerCall(const char* name) {
    if (!name) {
        return nullptr;
    }
    for (const NamedCall& c : kCalls) {
        if (strcmp(c.name, name) == 0) {
            return c.fn;
        }
    }

    return nullptr;
}
