// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// layer_check: runs the OpenXR layer the way the game will, before it goes anywhere near the game.
// It loads a real OpenXR loader (SteamVR's, or the one given), turns the freshly built layer on for
// itself only (environment variables, no registry), and acts like Le Mans Ultimate: the same application
// name and extensions, a D3D11 session, one swapchain per eye in the game's format and usage, and five
// seconds of frames, the left eye cleared to a colour that encodes the frame's number, the right one to
// grey. Meanwhile it reads what the layer publishes, as VR Mirror does, on a device of its own
// (openxr_check_copies.h): every frame's record must hold what was submitted, nearly every frame's picture
// must be copied, and every copy read back must show its own frame; the layer's clock reading must match the
// runtime's conversion. The layer only wakes up in a program called "Le Mans Ultimate.exe", which is why
// build.bat gives this program that name. It is not the game and touches no game file. Then read the
// layer's log it names at the end, the newest %LOCALAPPDATA%\simoptix\openxr_frame_source_<time>.log: it
// should hold the runtime, the session, both swapchains, the frame layout, the frame source's lines and no
// errors. If the headset isn't ready (asleep, Pimax Play
// closed), the session never starts its frames; the check says so and still makes and destroys everything
// else.
//
// Usage: "Le Mans Ultimate.exe" [--loader <path to openxr_loader.dll>] [--frames <count>] [--installed]
// (--frames 1100 runs past the layer's first 10 s frame-rate and frame source lines. --installed checks
// the installed copy instead, found through the registry as the game finds it: install.bat first.)
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "openxr_check_copies.h"
#include "openxr_d3d11.h"
#include "shared/local_files.h"

namespace {

    constexpr int64_t kGameFormat = 29;  // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, what LMU asks for
    constexpr int kDefaultFrames = 450;  // five seconds at 90 Hz (the layer makes its textures in the first)

    PFN_xrGetInstanceProcAddr g_gipa = nullptr;
    int g_failures = 0;

    template <typename Fn>
    Fn Get(XrInstance instance, const char* name) {
        PFN_xrVoidFunction f = nullptr;
        if (XR_FAILED(g_gipa(instance, name, &f)) || !f) {
            printf("  %s: not found\n", name);
            g_failures++;
        }

        return reinterpret_cast<Fn>(f);
    }

    bool Check(XrResult r, const char* what) {
        printf("  %-34s %s (%d)\n", what, XR_SUCCEEDED(r) ? "ok" : "FAILED", static_cast<int>(r));
        if (XR_FAILED(r)) {
            g_failures++;
        }
        return XR_SUCCEEDED(r);
    }

    std::wstring ExeDir() {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring p = path;
        return p.substr(0, p.find_last_of(L'\\'));
    }

    HMODULE LoadLoader(int argc, wchar_t** argv) {
        std::vector<std::wstring> candidates;
        for (int i = 1; i + 1 < argc; i++) {
            if (wcscmp(argv[i], L"--loader") == 0) {
                candidates.push_back(argv[i + 1]);
            }
        }

        candidates.push_back(
            L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\"
            L"openxr_loader.dll"
        );

        candidates.push_back(
            L"C:\\Program Files\\Pimax\\PVRHome\\Engine\\Binaries\\ThirdParty\\OpenXR\\win64\\"
            L"openxr_loader.dll"
        );

        for (const std::wstring& c : candidates) {
            HMODULE m = LoadLibraryExW(c.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (m) {
                printf("loader: %ls\n", c.c_str());
                return m;
            }
        }

        printf("no OpenXR loader found; pass --loader <path>\n");
        return nullptr;
    }

    // Waits up to `seconds` for the session to reach `want`; false on timeout or loss.
    bool WaitForState(XrInstance instance, PFN_xrPollEvent poll, XrSessionState want, double seconds) {
        ULONGLONG end = GetTickCount64() + static_cast<ULONGLONG>(seconds * 1000);
        while (GetTickCount64() < end) {
            XrEventDataBuffer e = {XR_TYPE_EVENT_DATA_BUFFER};
            while (poll(instance, &e) == XR_SUCCESS) {
                if (e.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    auto* s = reinterpret_cast<XrEventDataSessionStateChanged*>(&e);
                    printf("  session state %d\n", static_cast<int>(s->state));
                    if (s->state == want) {
                        return true;
                    }
                    if (s->state == XR_SESSION_STATE_LOSS_PENDING || s->state == XR_SESSION_STATE_EXITING) {
                        return false;
                    }
                }

                e = {XR_TYPE_EVENT_DATA_BUFFER};
            }

            Sleep(10);
        }

        return false;
    }

    struct Run {
        PFN_xrDestroyInstance destroyInstance = nullptr;
        PFN_xrGetSystem getSystem = nullptr;
        PFN_xrEnumerateViewConfigurationViews enumViews = nullptr;
        PFN_XrGetD3d11Requirements getReqs = nullptr;
        PFN_xrCreateSession createSession = nullptr;
        PFN_xrDestroySession destroySession = nullptr;
        PFN_xrBeginSession beginSession = nullptr;
        PFN_xrEndSession endSession = nullptr;
        PFN_xrRequestExitSession requestExit = nullptr;
        PFN_xrPollEvent pollEvent = nullptr;
        PFN_xrEnumerateSwapchainFormats enumFormats = nullptr;
        PFN_xrCreateSwapchain createSwapchain = nullptr;
        PFN_xrDestroySwapchain destroySwapchain = nullptr;
        PFN_xrEnumerateSwapchainImages enumImages = nullptr;
        PFN_xrAcquireSwapchainImage acquire = nullptr;
        PFN_xrWaitSwapchainImage waitImage = nullptr;
        PFN_xrReleaseSwapchainImage release = nullptr;
        PFN_xrCreateReferenceSpace createSpace = nullptr;
        PFN_xrDestroySpace destroySpace = nullptr;
        PFN_xrWaitFrame waitFrame = nullptr;
        PFN_xrBeginFrame beginFrame = nullptr;
        PFN_xrEndFrame endFrame = nullptr;
        PFN_xrLocateViews locateViews = nullptr;

        XrInstance instance = XR_NULL_HANDLE;
        XrSystemId system = XR_NULL_SYSTEM_ID;
        XrViewConfigurationView views[2] = {
            {XR_TYPE_VIEW_CONFIGURATION_VIEW},
            {XR_TYPE_VIEW_CONFIGURATION_VIEW}
        };

        uint32_t viewCount = 0;
        XrD3d11Requirements reqs = {XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        XrSession session = XR_NULL_HANDLE;
        int64_t format = 0;
        XrSwapchain chains[2] = {};
        std::vector<ID3D11RenderTargetView*> targets[2];
        XrSpace space = XR_NULL_HANDLE;
    };

    void ChooseLayer(bool installed) {
        if (installed) {
            printf("layer from: the registry (the installed copy)\n");
        } else {
            // The freshly built layer, for this program only. An installed copy is kept out, so this checks
            // the build next to it.
            std::wstring layerDir = ExeDir() + L"\\..";
            SetEnvironmentVariableW(L"XR_API_LAYER_PATH", layerDir.c_str());
            SetEnvironmentVariableW(L"XR_ENABLE_API_LAYERS", L"XR_APILAYER_SIMOPTIX_frame_source");
            SetEnvironmentVariableW(L"DISABLE_XR_APILAYER_SIMOPTIX_frame_source", L"1");
            printf("layer from: %ls\n", layerDir.c_str());
        }
    }

    bool CreateGameInstance(PFN_xrCreateInstance createInstance, XrInstance* instance) {
        const char* extensions[] = {"XR_KHR_D3D11_enable"};
        XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(ci.applicationInfo.applicationName, "LeMansUltimate");
        ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        ci.enabledExtensionCount = 1;
        ci.enabledExtensionNames = extensions;
        return Check(createInstance(&ci, instance), "xrCreateInstance");
    }

    void LoadCalls(Run& run) {
        const XrInstance instance = run.instance;
        run.destroyInstance = Get<PFN_xrDestroyInstance>(instance, "xrDestroyInstance");
        run.getSystem = Get<PFN_xrGetSystem>(instance, "xrGetSystem");
        run.enumViews = Get<PFN_xrEnumerateViewConfigurationViews>(
            instance,
            "xrEnumerateViewConfigurationViews"
        );

        run.getReqs = Get<PFN_XrGetD3d11Requirements>(instance, "xrGetD3D11GraphicsRequirementsKHR");
        run.createSession = Get<PFN_xrCreateSession>(instance, "xrCreateSession");
        run.destroySession = Get<PFN_xrDestroySession>(instance, "xrDestroySession");
        run.beginSession = Get<PFN_xrBeginSession>(instance, "xrBeginSession");
        run.endSession = Get<PFN_xrEndSession>(instance, "xrEndSession");
        run.requestExit = Get<PFN_xrRequestExitSession>(instance, "xrRequestExitSession");
        run.pollEvent = Get<PFN_xrPollEvent>(instance, "xrPollEvent");
        run.enumFormats = Get<PFN_xrEnumerateSwapchainFormats>(instance, "xrEnumerateSwapchainFormats");
        run.createSwapchain = Get<PFN_xrCreateSwapchain>(instance, "xrCreateSwapchain");
        run.destroySwapchain = Get<PFN_xrDestroySwapchain>(instance, "xrDestroySwapchain");
        run.enumImages = Get<PFN_xrEnumerateSwapchainImages>(instance, "xrEnumerateSwapchainImages");
        run.acquire = Get<PFN_xrAcquireSwapchainImage>(instance, "xrAcquireSwapchainImage");
        run.waitImage = Get<PFN_xrWaitSwapchainImage>(instance, "xrWaitSwapchainImage");
        run.release = Get<PFN_xrReleaseSwapchainImage>(instance, "xrReleaseSwapchainImage");
        run.createSpace = Get<PFN_xrCreateReferenceSpace>(instance, "xrCreateReferenceSpace");
        run.destroySpace = Get<PFN_xrDestroySpace>(instance, "xrDestroySpace");
        run.waitFrame = Get<PFN_xrWaitFrame>(instance, "xrWaitFrame");
        run.beginFrame = Get<PFN_xrBeginFrame>(instance, "xrBeginFrame");
        run.endFrame = Get<PFN_xrEndFrame>(instance, "xrEndFrame");
        run.locateViews = Get<PFN_xrLocateViews>(instance, "xrLocateViews");
    }

    bool CreateDevice(Run& run) {
        IDXGIFactory1* factory = nullptr;
        IDXGIAdapter1* adapter = nullptr;
        CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory));
        for (UINT i = 0; factory && factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            adapter->GetDesc1(&d);
            if (memcmp(&d.AdapterLuid, &run.reqs.adapterLuid, sizeof(LUID)) == 0) {
                break;
            }
            adapter->Release();
            adapter = nullptr;
        }

        HRESULT hr = D3D11CreateDevice(
            adapter,
            adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &run.device,
            nullptr,
            &run.context
        );

        if (adapter) {
            adapter->Release();
        }
        if (factory) {
            factory->Release();
        }
        if (FAILED(hr)) {
            printf("D3D11CreateDevice failed (0x%08lx)\n", hr);
            return false;
        }

        return true;
    }

    void ChooseFormat(Run& run) {
        uint32_t formatCount = 0;
        run.enumFormats(run.session, 0, &formatCount, nullptr);
        std::vector<int64_t> formats(formatCount);
        run.enumFormats(run.session, formatCount, &formatCount, formats.data());
        run.format = formats.empty() ? kGameFormat : formats[0];
        for (int64_t f : formats) {
            if (f == kGameFormat) {
                run.format = f;
            }
        }
    }

    void CreateSwapchains(Run& run) {
        for (uint32_t eye = 0; eye < 2 && eye < run.viewCount; eye++) {
            XrSwapchainCreateInfo sc = {XR_TYPE_SWAPCHAIN_CREATE_INFO};

            // LMU's: drawn into and copied into, not sampled (0x11).
            sc.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
            sc.format = run.format;
            sc.sampleCount = 1;
            sc.width = run.views[eye].recommendedImageRectWidth;
            sc.height = run.views[eye].recommendedImageRectHeight;
            sc.faceCount = 1;
            sc.arraySize = 1;
            sc.mipCount = 1;

            if (!Check(run.createSwapchain(run.session, &sc, &run.chains[eye]), "xrCreateSwapchain")) {
                continue;
            }

            uint32_t n = 0;
            run.enumImages(run.chains[eye], 0, &n, nullptr);
            std::vector<XrD3d11SwapchainImage> images(n, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
            run.enumImages(
                run.chains[eye],
                n,
                &n,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())
            );

            for (const XrD3d11SwapchainImage& img : images) {
                D3D11_RENDER_TARGET_VIEW_DESC rd = {};
                rd.Format = static_cast<DXGI_FORMAT>(run.format);
                rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                ID3D11RenderTargetView* rtv = nullptr;
                run.device->CreateRenderTargetView(img.texture, &rd, &rtv);
                run.targets[eye].push_back(rtv);  // may be null: that image is then left as it is
            }
        }
    }

    void LocateViews(const Run& run, const XrFrameState& fs, XrView located[2]) {
        XrViewLocateInfo li = {XR_TYPE_VIEW_LOCATE_INFO};
        li.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        li.displayTime = fs.predictedDisplayTime;
        li.space = run.space;

        XrViewState vs = {XR_TYPE_VIEW_STATE};
        uint32_t n = 0;
        XrResult lr = run.locateViews(run.session, &li, &vs, 2, &n, located);

        // Without tracking (headset on the desk) the poses are all zeros, which xrEndFrame refuses
        // (XR_ERROR_POSE_INVALID): send a neutral pose instead.
        const XrViewStateFlags valid = XR_VIEW_STATE_ORIENTATION_VALID_BIT | XR_VIEW_STATE_POSITION_VALID_BIT;

        if (XR_FAILED(lr) || (vs.viewStateFlags & valid) != valid) {
            for (int k = 0; k < 2; k++) {
                XrView& v = located[k];
                v.pose = {{0, 0, 0, 1}, {0, 0, 0}};
                if (v.fov.angleRight <= v.fov.angleLeft) {
                    v.fov = {-0.8f, 0.8f, 0.8f, -0.8f};
                }
            }
        }
    }

    void DrawEyes(const Run& run, int i, const XrView located[2], XrCompositionLayerProjectionView pv[2]) {
        for (uint32_t eye = 0; eye < 2; eye++) {
            uint32_t index = 0;
            run.acquire(run.chains[eye], nullptr, &index);
            XrSwapchainImageWaitInfo wi = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            wi.timeout = XR_INFINITE_DURATION;
            run.waitImage(run.chains[eye], &wi);
            if (index < run.targets[eye].size() && run.targets[eye][index]) {
                float colour[4] = {0.2f, 0.2f, 0.2f, 1.0f};
                if (eye == 0) {
                    CheckColour(i, colour);
                }
                run.context->ClearRenderTargetView(run.targets[eye][index], colour);
            }

            run.release(run.chains[eye], nullptr);
            pv[eye].pose = located[eye].pose;
            pv[eye].fov = located[eye].fov;
            pv[eye].subImage.swapchain = run.chains[eye];
            pv[eye].subImage.imageRect.extent.width = static_cast<int32_t>(
                run.views[eye].recommendedImageRectWidth
            );

            pv[eye].subImage.imageRect.extent.height = static_cast<int32_t>(
                run.views[eye].recommendedImageRectHeight
            );
        }
    }

    bool RunFrame(const Run& run, int i, OpenXrCheckCopies& copies) {
        XrFrameState fs = {XR_TYPE_FRAME_STATE};
        if (XR_FAILED(run.waitFrame(run.session, nullptr, &fs))) {
            return false;
        }
        if (XR_FAILED(run.beginFrame(run.session, nullptr))) {
            return false;
        }

        XrView located[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
        LocateViews(run, fs, located);

        XrCompositionLayerProjectionView pv[2] = {
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}
        };

        DrawEyes(run, i, located, pv);

        XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = run.space;
        layer.viewCount = 2;
        layer.views = pv;
        const XrCompositionLayerBaseHeader* layers[] = {
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
        };

        XrFrameEndInfo ei = {XR_TYPE_FRAME_END_INFO};
        ei.displayTime = fs.predictedDisplayTime;
        ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        ei.layerCount = fs.shouldRender ? 1 : 0;
        ei.layers = layers;
        if (fs.shouldRender) {
            const XrPosef poses[2] = {pv[0].pose, pv[1].pose};
            const XrFovf fovs[2] = {pv[0].fov, pv[1].fov};
            copies.Submitted(i, fs.predictedDisplayTime, poses, fovs);
        }

        if (XR_FAILED(run.endFrame(run.session, &ei))) {
            g_failures++;
            return false;
        }

        return true;
    }

    void RunFrames(const Run& run, int frameCount, OpenXrCheckCopies& copies) {
        int framesEnded = 0;
        if (!WaitForState(run.instance, run.pollEvent, XR_SESSION_STATE_READY, 5.0)) {
            printf(
                "  the session didn't become ready in 5 s (headset asleep, or Pimax Play closed?): no "
                "frames\n"
            );

            return;
        }

        XrSessionBeginInfo bi = {XR_TYPE_SESSION_BEGIN_INFO};
        bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

        if (!Check(run.beginSession(run.session, &bi), "xrBeginSession")) {
            return;
        }

        for (int i = 0; i < frameCount; i++) {
            if (!RunFrame(run, i, copies)) {
                break;
            }
            framesEnded++;
            copies.Poll();
        }

        printf("  frames ended: %d of %d\n", framesEnded, frameCount);
        for (int i = 0; i < 20; i++) {  // the last copies' markers are only checked at later frames
            Sleep(10);
            copies.Poll();
        }

        g_failures += copies.Report();
        run.requestExit(run.session);
        WaitForState(run.instance, run.pollEvent, XR_SESSION_STATE_STOPPING, 3.0);
        Check(run.endSession(run.session), "xrEndSession");
    }

    void ReleaseAll(Run& run) {
        if (run.space) {
            run.destroySpace(run.space);
        }
        for (uint32_t eye = 0; eye < 2; eye++) {
            for (ID3D11RenderTargetView* t : run.targets[eye]) {
                if (t) {
                    t->Release();
                }
            }

            if (run.chains[eye]) {
                Check(run.destroySwapchain(run.chains[eye]), "xrDestroySwapchain");
            }
        }

        Check(run.destroySession(run.session), "xrDestroySession");
        Check(run.destroyInstance(run.instance), "xrDestroyInstance");
        run.context->Release();
        run.device->Release();
    }

}

int wmain(int argc, wchar_t** argv) {
    int frameCount = kDefaultFrames;
    bool installed = false;
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"--frames") == 0 && i + 1 < argc) {
            frameCount = _wtoi(argv[i + 1]);
        }
        if (wcscmp(argv[i], L"--installed") == 0) {
            installed = true;
        }
    }

    ChooseLayer(installed);
    SetEnvironmentVariableW(L"XR_LOADER_DEBUG", L"warn");

    HMODULE loader = LoadLoader(argc, argv);
    if (!loader) {
        return 2;
    }
    g_gipa = reinterpret_cast<PFN_xrGetInstanceProcAddr>(GetProcAddress(loader, "xrGetInstanceProcAddr"));
    if (!g_gipa) {
        printf("the loader has no xrGetInstanceProcAddr\n");
        return 2;
    }

    auto createInstance = Get<PFN_xrCreateInstance>(XR_NULL_HANDLE, "xrCreateInstance");
    if (!createInstance) {
        return 2;
    }

    Run run;
    if (!CreateGameInstance(createInstance, &run.instance)) {
        return 1;
    }

    LoadCalls(run);
    if (g_failures) {
        return 1;
    }

    XrSystemGetInfo si = {XR_TYPE_SYSTEM_GET_INFO};
    si.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!Check(run.getSystem(run.instance, &si, &run.system), "xrGetSystem")) {
        run.destroyInstance(run.instance);
        return 1;
    }

    Check(
        run.enumViews(
            run.instance,
            run.system,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            2,
            &run.viewCount,
            run.views
        ),
        "xrEnumerateViewConfigurationViews"
    );

    Check(run.getReqs(run.instance, run.system, &run.reqs), "xrGetD3D11GraphicsRequirementsKHR");
    if (!CreateDevice(run)) {
        run.destroyInstance(run.instance);
        return 1;
    }

    XrD3d11Binding binding = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, nullptr, run.device};
    XrSessionCreateInfo sci = {XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = run.system;
    if (!Check(run.createSession(run.instance, &sci, &run.session), "xrCreateSession")) {
        run.context->Release();
        run.device->Release();
        run.destroyInstance(run.instance);
        return 1;
    }

    ChooseFormat(run);
    CreateSwapchains(run);

    XrReferenceSpaceCreateInfo rs = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rs.poseInReferenceSpace.orientation.w = 1;
    Check(run.createSpace(run.session, &rs, &run.space), "xrCreateReferenceSpace");

    // The reader, on a device of its own, and the runtime's own time conversion (the layer asks for the
    // extension, as for the game).
    OpenXrCheckCopies copies;
    const bool rgba = run.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
                      || run.format == DXGI_FORMAT_R8G8B8A8_UNORM;

    if (!copies.Start(run.reqs.adapterLuid, rgba, run.format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB)) {
        printf("  the reader's device wasn't made\n");
        g_failures++;
    }

    PFN_xrVoidFunction convert = nullptr;
    if (XR_SUCCEEDED(g_gipa(run.instance, "xrConvertWin32PerformanceCounterToTimeKHR", &convert))) {
        copies.SetClock(run.instance, reinterpret_cast<PFN_XrCounterToTime>(convert));
    }

    RunFrames(run, frameCount, copies);
    ReleaseAll(run);
    wchar_t log[MAX_PATH] = L"(none found)";
    NewestLocalFile(LocalFileKind::kLayerLog, log, MAX_PATH);  // keeps "(none found)" if there is none
    printf("%s; the layer's log: %ls\n", g_failures ? "FAILED" : "passed", log);

    return g_failures ? 1 : 0;
}
