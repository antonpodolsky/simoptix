// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_openxr.h"

#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "game_grid.h"
#include "game_view.h"

using Microsoft::WRL::ComPtr;

namespace {

    constexpr const char* kApplicationName = "simoptix_game";
    constexpr int64_t kSrgbFormats[] = {
        29,  // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, what LMU asks for
        91,  // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
    };

    std::string Failed(const char* call, XrResult r) {
        char text[160];
        snprintf(text, sizeof(text), "%s failed (%d)", call, static_cast<int>(r));
        return text;
    }

    uint32_t ScaledSize(uint32_t recommended, double scale, uint32_t maximum) {
        const double size = std::round(recommended * scale);
        return static_cast<uint32_t>(std::clamp(size, 16.0, static_cast<double>(maximum)));
    }

}

GameOpenXr::~GameOpenXr() {
    for (Eye& eye : eyes_) {
        if (eye.swapchain != XR_NULL_HANDLE) {
            calls_.destroySwapchain(eye.swapchain);
        }
    }

    if (viewSpace_ != XR_NULL_HANDLE) {
        calls_.destroySpace(viewSpace_);
    }

    if (localSpace_ != XR_NULL_HANDLE) {
        calls_.destroySpace(localSpace_);
    }

    if (session_ != XR_NULL_HANDLE) {
        calls_.destroySession(session_);
    }

    // A failed Open may have made the instance without finding its other calls.
    if (instance_ != XR_NULL_HANDLE && calls_.destroyInstance) {
        calls_.destroyInstance(instance_);
    }
}

bool GameOpenXr::Open(const GameSettings& settings, std::string* error) {
    if (!LoadLoader(settings.loader, error) || !CreateInstance(error) || !LoadCalls(error)) {
        return false;
    }

    XrSystemGetInfo systemInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    const XrResult r = calls_.getSystem(instance_, &systemInfo, &system_);

    if (XR_FAILED(r)) {
        *error = Failed("xrGetSystem", r) + ": no headset (is it on, and its software running?)";
        return false;
    }

    uint32_t viewCount = 0;

    const XrResult viewsResult = calls_.enumerateViews(
        instance_,
        system_,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
        2,
        &viewCount,
        views_
    );

    if (XR_FAILED(viewsResult) || viewCount != 2) {
        *error = Failed("xrEnumerateViewConfigurationViews", viewsResult) + ": not two views";
        return false;
    }

    return CreateDevice(error) && CreateSession(error) && CreateSwapchains(settings, error);
}

bool GameOpenXr::LoadLoader(const std::wstring& path, std::string* error) {
    std::vector<std::wstring> candidates;

    if (!path.empty()) {
        candidates.push_back(path);
    }

    candidates.push_back(
        L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll"
    );

    candidates.push_back(
        L"C:\\Program Files\\Pimax\\PVRHome\\Engine\\Binaries\\ThirdParty\\OpenXR\\win64\\openxr_loader.dll"
    );

    for (const std::wstring& candidate : candidates) {
        loader_ = LoadLibraryExW(candidate.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);

        if (loader_) {
            printf("OpenXR loader: %ls\n", candidate.c_str());
            break;
        }
    }

    if (!loader_) {
        *error = "no OpenXR loader found: pass --loader <path to openxr_loader.dll>";
        return false;
    }

    calls_.getProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
        GetProcAddress(loader_, "xrGetInstanceProcAddr")
    );

    if (!calls_.getProcAddr) {
        *error = "the OpenXR loader has no xrGetInstanceProcAddr";
        return false;
    }

    return true;
}

bool GameOpenXr::CreateInstance(std::string* error) {
    PFN_xrCreateInstance createInstance = nullptr;

    const XrResult found = calls_.getProcAddr(
        XR_NULL_HANDLE,
        "xrCreateInstance",
        reinterpret_cast<PFN_xrVoidFunction*>(&createInstance)
    );

    if (XR_FAILED(found) || !createInstance) {
        *error = Failed("xrGetInstanceProcAddr(xrCreateInstance)", found);
        return false;
    }

    const char* extensions[] = {"XR_KHR_D3D11_enable"};
    XrInstanceCreateInfo info = {XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(info.applicationInfo.applicationName, kApplicationName);
    info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = extensions;
    const XrResult r = createInstance(&info, &instance_);

    if (XR_FAILED(r)) {
        *error = Failed("xrCreateInstance", r) + ": the runtime may not offer D3D11";
        return false;
    }

    return true;
}

bool GameOpenXr::LoadCalls(std::string* error) {
    bool allFound = true;

    const auto get = [&](const char* name, auto* call) {
        const XrResult r = calls_.getProcAddr(instance_, name, reinterpret_cast<PFN_xrVoidFunction*>(call));

        if (XR_FAILED(r) || !*call) {
            *error = std::string(name) + " not found";
            allFound = false;
        }
    };

    get("xrDestroyInstance", &calls_.destroyInstance);
    get("xrGetSystem", &calls_.getSystem);
    get("xrEnumerateViewConfigurationViews", &calls_.enumerateViews);
    get("xrGetD3D11GraphicsRequirementsKHR", &calls_.getD3d11Requirements);
    get("xrCreateSession", &calls_.createSession);
    get("xrDestroySession", &calls_.destroySession);
    get("xrBeginSession", &calls_.beginSession);
    get("xrEndSession", &calls_.endSession);
    get("xrRequestExitSession", &calls_.requestExitSession);
    get("xrPollEvent", &calls_.pollEvent);
    get("xrEnumerateSwapchainFormats", &calls_.enumerateFormats);
    get("xrCreateSwapchain", &calls_.createSwapchain);
    get("xrDestroySwapchain", &calls_.destroySwapchain);
    get("xrEnumerateSwapchainImages", &calls_.enumerateImages);
    get("xrAcquireSwapchainImage", &calls_.acquireImage);
    get("xrWaitSwapchainImage", &calls_.waitImage);
    get("xrReleaseSwapchainImage", &calls_.releaseImage);
    get("xrCreateReferenceSpace", &calls_.createSpace);
    get("xrDestroySpace", &calls_.destroySpace);
    get("xrLocateSpace", &calls_.locateSpace);
    get("xrWaitFrame", &calls_.waitFrame);
    get("xrBeginFrame", &calls_.beginFrame);
    get("xrEndFrame", &calls_.endFrame);
    get("xrLocateViews", &calls_.locateViews);
    return allFound;
}

bool GameOpenXr::CreateDevice(std::string* error) {
    // OpenXR asks for the requirements before the session, and the device on the headset's card.
    XrD3d11Requirements requirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    const XrResult r = calls_.getD3d11Requirements(instance_, system_, &requirements);

    if (XR_FAILED(r)) {
        *error = Failed("xrGetD3D11GraphicsRequirementsKHR", r);
        return false;
    }

    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;

    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory))) {
        ComPtr<IDXGIAdapter1> candidate;

        for (UINT i = 0; factory->EnumAdapters1(i, &candidate) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc = {};
            candidate->GetDesc1(&desc);

            if (memcmp(&desc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID)) == 0) {
                adapter = candidate;
                break;
            }
        }
    }

    const HRESULT hr = D3D11CreateDevice(
        adapter.Get(),
        adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr,
        0,
        D3D11_SDK_VERSION,
        &device_,
        nullptr,
        &context_
    );

    if (FAILED(hr)) {
        char text[96];
        snprintf(text, sizeof(text), "D3D11CreateDevice failed (0x%08lx)", static_cast<unsigned long>(hr));
        *error = text;
        return false;
    }

    return true;
}

bool GameOpenXr::CreateSession(std::string* error) {
    XrD3d11Binding binding = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, nullptr, device_.Get()};
    XrSessionCreateInfo info = {XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = system_;
    XrResult r = calls_.createSession(instance_, &info, &session_);

    if (XR_FAILED(r)) {
        *error = Failed("xrCreateSession", r);
        return false;
    }

    XrReferenceSpaceCreateInfo spaceInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.poseInReferenceSpace = {{0, 0, 0, 1}, {0, 0, 0}};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    r = calls_.createSpace(session_, &spaceInfo, &localSpace_);

    if (XR_SUCCEEDED(r)) {
        spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        r = calls_.createSpace(session_, &spaceInfo, &viewSpace_);
    }

    if (XR_FAILED(r)) {
        *error = Failed("xrCreateReferenceSpace", r);
        return false;
    }

    return true;
}

bool GameOpenXr::CreateSwapchains(const GameSettings& settings, std::string* error) {
    int64_t format = 0;

    if (!ChooseFormat(&format, error)) {
        return false;
    }

    for (int i = 0; i < 2; i++) {
        if (!CreateSwapchain(i, settings.eyeScale[i], format, error)) {
            return false;
        }

        const XrViewConfigurationView& view = views_[i];

        printf(
            "%s eye: %ux%u, %.2f of the recommended %ux%u (%zu images, format %lld)\n",
            i == 0 ? "left" : "right",
            eyes_[i].width,
            eyes_[i].height,
            settings.eyeScale[i],
            view.recommendedImageRectWidth,
            view.recommendedImageRectHeight,
            eyes_[i].targets.size(),
            static_cast<long long>(format)
        );
    }

    return true;
}

bool GameOpenXr::ChooseFormat(int64_t* format, std::string* error) {
    uint32_t formatCount = 0;
    XrResult r = calls_.enumerateFormats(session_, 0, &formatCount, nullptr);
    std::vector<int64_t> formats(formatCount);

    if (XR_SUCCEEDED(r)) {
        r = calls_.enumerateFormats(session_, formatCount, &formatCount, formats.data());
    }

    if (XR_FAILED(r)) {
        *error = Failed("xrEnumerateSwapchainFormats", r);
        return false;
    }

    // The grid writes linear light, which only an sRGB image stores as the headset shows it.
    const auto found = std::find_first_of(
        formats.begin(),
        formats.end(),
        std::begin(kSrgbFormats),
        std::end(kSrgbFormats)
    );

    if (found == formats.end()) {
        *error = "the runtime offers no sRGB swapchain format";
        return false;
    }

    *format = *found;
    return true;
}

bool GameOpenXr::CreateSwapchain(int i, double scale, int64_t format, std::string* error) {
    Eye& eye = eyes_[i];
    const XrViewConfigurationView& view = views_[i];
    eye.width = ScaledSize(view.recommendedImageRectWidth, scale, view.maxImageRectWidth);
    eye.height = ScaledSize(view.recommendedImageRectHeight, scale, view.maxImageRectHeight);

    // LMU's usage: drawn into and copied into (0x11).
    XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = eye.width;
    info.height = eye.height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = calls_.createSwapchain(session_, &info, &eye.swapchain);

    if (XR_FAILED(r)) {
        *error = Failed("xrCreateSwapchain", r);
        return false;
    }

    uint32_t imageCount = 0;
    r = calls_.enumerateImages(eye.swapchain, 0, &imageCount, nullptr);
    std::vector<XrD3d11SwapchainImage> images(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});

    if (XR_SUCCEEDED(r)) {
        r = calls_.enumerateImages(
            eye.swapchain,
            imageCount,
            &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())
        );
    }

    if (XR_FAILED(r)) {
        *error = Failed("xrEnumerateSwapchainImages", r);
        return false;
    }

    for (const XrD3d11SwapchainImage& image : images) {
        D3D11_RENDER_TARGET_VIEW_DESC desc = {};
        desc.Format = static_cast<DXGI_FORMAT>(format);
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> target;

        if (FAILED(device_->CreateRenderTargetView(image.texture, &desc, &target))) {
            *error = "a swapchain image takes no render target";
            return false;
        }

        eye.targets.push_back(target);
    }

    return true;
}

void GameOpenXr::PollEvents() {
    XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};

    while (calls_.pollEvent(instance_, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            exit_ = true;
        }

        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrSessionState state = reinterpret_cast<const XrEventDataSessionStateChanged&>(event).state;

            if (state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo info = {XR_TYPE_SESSION_BEGIN_INFO};
                info.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                const XrResult r = calls_.beginSession(session_, &info);
                running_ = XR_SUCCEEDED(r);

                if (running_) {
                    printf("the session runs: Space centres the grid ahead, Q or Esc quits\n");
                } else {
                    printf("%s\n", Failed("xrBeginSession", r).c_str());
                }
            } else if (state == XR_SESSION_STATE_STOPPING) {
                const XrResult r = calls_.endSession(session_);
                running_ = false;
                printf("the session stopped%s\n", XR_FAILED(r) ? ", and xrEndSession failed" : "");
            } else if (state == XR_SESSION_STATE_EXITING || state == XR_SESSION_STATE_LOSS_PENDING) {
                exit_ = true;
            }
        }

        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

void GameOpenXr::RequestExit() {
    if (!running_ || XR_FAILED(calls_.requestExitSession(session_))) {
        exit_ = true;
    }
}

void GameOpenXr::RunFrame(const GameGrid& grid, float chartYaw) {
    XrFrameState frame = {XR_TYPE_FRAME_STATE};

    if (XR_FAILED(calls_.waitFrame(session_, nullptr, &frame))) {
        return;
    }

    if (XR_FAILED(calls_.beginFrame(session_, nullptr))) {
        return;
    }

    XrViewLocateInfo locateInfo = {XR_TYPE_VIEW_LOCATE_INFO};
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime = frame.predictedDisplayTime;
    locateInfo.space = localSpace_;
    XrViewState viewState = {XR_TYPE_VIEW_STATE};
    XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
    uint32_t viewCount = 0;
    const XrResult located = calls_.locateViews(session_, &locateInfo, &viewState, 2, &viewCount, views);
    const bool oriented = (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    const bool shouldDraw = frame.shouldRender && XR_SUCCEEDED(located) && viewCount == 2 && oriented;
    XrCompositionLayerProjectionView projectionViews[2] = {};

    if (shouldDraw) {
        LocateHead(frame.predictedDisplayTime);

        for (int eye = 0; eye < 2; eye++) {
            DrawEye(grid, eye, views[eye], chartYaw);
            XrCompositionLayerProjectionView& projection = projectionViews[eye];
            projection.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projection.pose = views[eye].pose;
            projection.fov = views[eye].fov;
            projection.subImage.swapchain = eyes_[eye].swapchain;
            projection.subImage.imageRect.extent.width = static_cast<int32_t>(eyes_[eye].width);
            projection.subImage.imageRect.extent.height = static_cast<int32_t>(eyes_[eye].height);
        }
    }

    XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    layer.space = localSpace_;
    layer.viewCount = 2;
    layer.views = projectionViews;

    const XrCompositionLayerBaseHeader* layers[] = {
        reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer)
    };

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frame.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = shouldDraw ? 1 : 0;
    endInfo.layers = layers;
    const XrResult ended = calls_.endFrame(session_, &endInfo);

    // Said once when frames start failing, and once when they recover, not every frame.
    if (XR_FAILED(ended) != endFrameFailing_) {
        endFrameFailing_ = XR_FAILED(ended);
        printf("%s\n", endFrameFailing_ ? Failed("xrEndFrame", ended).c_str() : "frames end again");
    }
}

void GameOpenXr::LocateHead(XrTime time) {
    XrSpaceLocation head = {XR_TYPE_SPACE_LOCATION};

    if (XR_FAILED(calls_.locateSpace(viewSpace_, localSpace_, time, &head))) {
        return;
    }

    if ((head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
        headYaw_ = ::HeadYaw(head.pose.orientation);
    }
}

void GameOpenXr::DrawEye(const GameGrid& grid, int eye, const XrView& view, float chartYaw) {
    Eye& target = eyes_[eye];
    uint32_t index = 0;

    if (XR_FAILED(calls_.acquireImage(target.swapchain, nullptr, &index))) {
        return;
    }

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;

    if (XR_SUCCEEDED(calls_.waitImage(target.swapchain, &waitInfo)) && index < target.targets.size()) {
        const GridView gridView = ViewOfEye(view.pose.orientation, view.fov, chartYaw);
        grid.Draw(context_.Get(), target.targets[index].Get(), target.width, target.height, gridView);
    }

    // A failed release leaves nothing to undo: xrEndFrame then reports the image.
    (void)calls_.releaseImage(target.swapchain, nullptr);
}
