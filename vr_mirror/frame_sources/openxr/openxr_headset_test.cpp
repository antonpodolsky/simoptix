// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
#include "openxr_headset_test.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "common/rot.h"
#include "openxr/openxr_loader_negotiation.h"
#include "openxr_d3d11.h"
#include "shared/text_raster.h"

using Microsoft::WRL::ComPtr;

namespace {

    constexpr const wchar_t* kText = L"Collecting headset info";
    constexpr ULONGLONG kTimeoutMs = 5000;  // the session's start and its frames: then the test gives up
    constexpr ULONGLONG kShowMs = 1000;     // the text, once the views are read: long enough to read it
    constexpr ULONGLONG kStopWaitMs = 1000;
    constexpr float kTextSpan = 0.4f;  // the text's width in each eye (tangent units: about 22 degrees)

    struct FreeModule {
        void operator()(HMODULE module) const { FreeLibrary(module); }
    };

    using UniqueModule = std::unique_ptr<std::remove_pointer_t<HMODULE>, FreeModule>;

    // The active runtime's manifest, as the registry names it; empty if none.
    std::wstring ActiveManifest() {
        wchar_t path[MAX_PATH] = {};
        DWORD bytes = sizeof(path);
        const LSTATUS status = RegGetValueW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Khronos\\OpenXR\\1",
            L"ActiveRuntime",
            RRF_RT_REG_SZ,
            nullptr,
            path,
            &bytes
        );
        return status == ERROR_SUCCESS ? path : L"";
    }

    // The manifest's runtime.library_path, made absolute against the manifest's folder; empty if it has
    // none. The manifest is small JSON whose only "library_path" is the runtime's: read as text.
    std::filesystem::path RuntimeLibrary(const std::filesystem::path& manifest) {
        std::ifstream file(manifest, std::ios::binary);
        std::stringstream text;
        text << file.rdbuf();
        const std::string json = text.str();

        const size_t key = json.find("\"library_path\"");
        const size_t colon = json.find(':', key == std::string::npos ? json.size() : key);
        const size_t open = json.find('"', colon == std::string::npos ? json.size() : colon);

        if (open == std::string::npos) {
            return {};
        }

        std::string value;

        for (size_t i = open + 1; i < json.size() && json[i] != '"'; i++) {
            value += json[i] == '\\' && i + 1 < json.size() ? json[++i] : json[i];  // JSON's "\\" is one "\"
        }

        const std::filesystem::path library = std::filesystem::u8path(value);
        return library.is_absolute() ? library : manifest.parent_path() / library;
    }

    // The runtime's xrGetInstanceProcAddr, as the loader gets it.
    PFN_xrGetInstanceProcAddr Negotiate(HMODULE runtime) {
        const auto negotiate = reinterpret_cast<PFN_xrNegotiateLoaderRuntimeInterface>(
            GetProcAddress(runtime, "xrNegotiateLoaderRuntimeInterface")
        );

        if (!negotiate) {
            return nullptr;
        }

        XrNegotiateLoaderInfo loader = {};
        loader.structType = XR_LOADER_INTERFACE_STRUCT_LOADER_INFO;
        loader.structVersion = XR_LOADER_INFO_STRUCT_VERSION;
        loader.structSize = sizeof(loader);
        loader.minInterfaceVersion = 1;
        loader.maxInterfaceVersion = XR_CURRENT_LOADER_RUNTIME_VERSION;
        loader.minApiVersion = XR_MAKE_VERSION(1, 0, 0);
        // Every 1.x: the instance asks for 1.0 itself (CreateInstance). Meta's runtime, an OpenXR 1.1 one,
        // refuses a range that ends at the headers' version, 1.0.34 (XR_ERROR_API_VERSION_UNSUPPORTED,
        // measured with a Quest 3 on Link); Pimax's takes either.
        loader.maxApiVersion = XR_MAKE_VERSION(1, 0x3ff, 0xfff);

        XrNegotiateRuntimeRequest request = {};
        request.structType = XR_LOADER_INTERFACE_STRUCT_RUNTIME_REQUEST;
        request.structVersion = XR_RUNTIME_INFO_STRUCT_VERSION;
        request.structSize = sizeof(request);

        return XR_SUCCEEDED(negotiate(&loader, &request)) ? request.getInstanceProcAddr : nullptr;
    }

    // The calls the test makes, from the runtime's instance.
    struct Calls {
        PFN_xrDestroyInstance destroyInstance = nullptr;
        PFN_xrGetSystem getSystem = nullptr;
        PFN_xrEnumerateViewConfigurationViews enumViews = nullptr;
        PFN_XrGetD3d11Requirements getRequirements = nullptr;
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

        // False if the runtime lacks any of them.
        bool Load(PFN_xrGetInstanceProcAddr gipa, XrInstance instance) {
            bool all = true;
            const auto get = [&](auto* f, const char* name) {
                PFN_xrVoidFunction p = nullptr;
                all = all && XR_SUCCEEDED(gipa(instance, name, &p)) && p;
                *f = reinterpret_cast<std::remove_pointer_t<decltype(f)>>(p);
            };

            get(&destroyInstance, "xrDestroyInstance");
            get(&getSystem, "xrGetSystem");
            get(&enumViews, "xrEnumerateViewConfigurationViews");
            get(&getRequirements, "xrGetD3D11GraphicsRequirementsKHR");
            get(&createSession, "xrCreateSession");
            get(&destroySession, "xrDestroySession");
            get(&beginSession, "xrBeginSession");
            get(&endSession, "xrEndSession");
            get(&requestExit, "xrRequestExitSession");
            get(&pollEvent, "xrPollEvent");
            get(&enumFormats, "xrEnumerateSwapchainFormats");
            get(&createSwapchain, "xrCreateSwapchain");
            get(&destroySwapchain, "xrDestroySwapchain");
            get(&enumImages, "xrEnumerateSwapchainImages");
            get(&acquire, "xrAcquireSwapchainImage");
            get(&waitImage, "xrWaitSwapchainImage");
            get(&release, "xrReleaseSwapchainImage");
            get(&createSpace, "xrCreateReferenceSpace");
            get(&destroySpace, "xrDestroySpace");
            get(&waitFrame, "xrWaitFrame");
            get(&beginFrame, "xrBeginFrame");
            get(&endFrame, "xrEndFrame");
            get(&locateViews, "xrLocateViews");
            return all;
        }
    };

    // An image the test shows: an 8-bit RGBA swapchain written once (a static image).
    struct Picture {
        XrSwapchain chain = XR_NULL_HANDLE;
        int32_t width = 0, height = 0;
    };

    // One run of the test: what it made, let go of in the reverse order.
    class Test {
    public:
        ~Test() {
            End();

            for (const Picture& p : {eyes_[0], eyes_[1], black_}) {
                if (p.chain) {
                    xr_.destroySwapchain(p.chain);
                }
            }

            if (space_) {
                xr_.destroySpace(space_);
            }

            if (session_) {
                xr_.destroySession(session_);
            }

            if (instance_) {
                xr_.destroyInstance(instance_);
            }
        }

        bool Open();
        bool Run(HeadsetReport* out);

    private:
        bool CreateInstance(PFN_xrGetInstanceProcAddr gipa);
        bool CreateDevice();
        bool CreateSession();
        bool ChooseFormat();
        bool MakePicture(int32_t width, int32_t height, const std::vector<uint8_t>& rgba, Picture* out);
        bool MakeEyePicture(const XrFovf& fov, Picture* out);
        bool WaitForState(XrSessionState want, ULONGLONG deadline);
        bool ShowFrame(XrView views[2], bool* located, double* period);
        void End();

        UniqueModule runtime_;
        Calls xr_;
        XrInstance instance_ = XR_NULL_HANDLE;
        XrSystemId system_ = XR_NULL_SYSTEM_ID;
        XrViewConfigurationView eyeImage_ = {XR_TYPE_VIEW_CONFIGURATION_VIEW};
        ComPtr<ID3D11Device> device_;
        ComPtr<ID3D11DeviceContext> context_;
        XrSession session_ = XR_NULL_HANDLE;
        XrSpace space_ = XR_NULL_HANDLE;  // the view space: the head, the eyes' midpoint
        int64_t format_ = 0;
        TextRaster text_;
        Picture black_;         // shown until the eyes' views are read
        Picture eyes_[2];       // then each eye's: the text straight ahead of it, on black
        bool running_ = false;  // begun and not ended
    };

    bool Test::CreateInstance(PFN_xrGetInstanceProcAddr gipa) {
        PFN_xrVoidFunction create = nullptr;

        if (XR_FAILED(gipa(XR_NULL_HANDLE, "xrCreateInstance", &create)) || !create) {
            return false;
        }

        const char* extensions[] = {"XR_KHR_D3D11_enable"};
        XrInstanceCreateInfo info = {XR_TYPE_INSTANCE_CREATE_INFO};
        strcpy_s(info.applicationInfo.applicationName, "SimOptix VR Mirror");
        info.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
        info.enabledExtensionCount = 1;
        info.enabledExtensionNames = extensions;

        if (XR_FAILED(reinterpret_cast<PFN_xrCreateInstance>(create)(&info, &instance_))) {
            instance_ = XR_NULL_HANDLE;
            return false;
        }

        return xr_.Load(gipa, instance_);
    }

    // A device on the adapter the runtime asks for.
    bool Test::CreateDevice() {
        XrD3d11Requirements requirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};

        if (XR_FAILED(xr_.getRequirements(instance_, system_, &requirements))) {
            return false;
        }

        ComPtr<IDXGIFactory1> factory;
        ComPtr<IDXGIAdapter1> adapter;

        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
            return false;
        }

        for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 desc = {};
            adapter->GetDesc1(&desc);

            if (memcmp(&desc.AdapterLuid, &requirements.adapterLuid, sizeof(LUID)) == 0) {
                break;
            }

            adapter.Reset();
        }

        return adapter
               && SUCCEEDED(D3D11CreateDevice(
                   adapter.Get(),
                   D3D_DRIVER_TYPE_UNKNOWN,
                   nullptr,
                   0,
                   nullptr,
                   0,
                   D3D11_SDK_VERSION,
                   &device_,
                   nullptr,
                   &context_
               ));
    }

    bool Test::CreateSession() {
        XrD3d11Binding binding = {XR_TYPE_GRAPHICS_BINDING_D3D11_KHR, nullptr, device_.Get()};
        XrSessionCreateInfo info = {XR_TYPE_SESSION_CREATE_INFO};
        info.next = &binding;
        info.systemId = system_;

        if (XR_FAILED(xr_.createSession(instance_, &info, &session_))) {
            session_ = XR_NULL_HANDLE;
            return false;
        }

        XrReferenceSpaceCreateInfo view = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
        view.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
        view.poseInReferenceSpace.orientation.w = 1;
        return XR_SUCCEEDED(xr_.createSpace(session_, &view, &space_));
    }

    // An 8-bit sRGB format the runtime offers.
    bool Test::ChooseFormat() {
        uint32_t count = 0;
        xr_.enumFormats(session_, 0, &count, nullptr);
        std::vector<int64_t> formats(count);
        xr_.enumFormats(session_, count, &count, formats.data());

        for (int64_t f : formats) {
            if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
                format_ = f;
                return true;
            }
        }

        return false;
    }

    // Loads the runtime, opens an instance on the headset and a session on a device of the test's own, and
    // makes the black the first frames show.
    bool Test::Open() {
        const std::wstring manifest = ActiveManifest();

        if (manifest.empty()) {
            return false;
        }

        // Its own folder first for the libraries it loads, as the loader loads it.
        runtime_.reset(
            LoadLibraryExW(RuntimeLibrary(manifest).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH)
        );
        const PFN_xrGetInstanceProcAddr gipa = runtime_ ? Negotiate(runtime_.get()) : nullptr;

        if (!gipa || !CreateInstance(gipa)) {
            return false;
        }

        XrSystemGetInfo info = {XR_TYPE_SYSTEM_GET_INFO};
        info.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrViewConfigurationView eyes[2] = {
            {XR_TYPE_VIEW_CONFIGURATION_VIEW},
            {XR_TYPE_VIEW_CONFIGURATION_VIEW}
        };
        uint32_t count = 0;

        if (XR_FAILED(xr_.getSystem(instance_, &info, &system_))
            || XR_FAILED(
                xr_.enumViews(instance_, system_, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &count, eyes)
            )
            || count < 2) {
            return false;
        }

        eyeImage_ = eyes[0];
        constexpr int32_t kBlack = 16;  // scaled up to fill each eye
        TextStyle style;
        style.weight = 600;
        style.size = 48;

        return CreateDevice() && CreateSession() && ChooseFormat()
               && MakePicture(kBlack, kBlack, std::vector<uint8_t>(kBlack * kBlack * 4, 0), &black_)
               && RasterText(kText, style, 0, 0, 0, &text_);
    }

    // A swapchain of one image, `rgba` (width x height, 4 bytes a pixel) written into it.
    bool Test::MakePicture(int32_t width, int32_t height, const std::vector<uint8_t>& rgba, Picture* out) {
        XrSwapchainCreateInfo info = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.createFlags = XR_SWAPCHAIN_CREATE_STATIC_IMAGE_BIT;
        info.usageFlags = XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format_;
        info.sampleCount = 1;
        info.width = static_cast<uint32_t>(width);
        info.height = static_cast<uint32_t>(height);
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;

        if (XR_FAILED(xr_.createSwapchain(session_, &info, &out->chain))) {
            out->chain = XR_NULL_HANDLE;
            return false;
        }

        out->width = width;
        out->height = height;
        uint32_t count = 0;
        xr_.enumImages(out->chain, 0, &count, nullptr);
        std::vector<XrD3d11SwapchainImage> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        const auto base = reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data());
        uint32_t index = 0;
        XrSwapchainImageWaitInfo wait = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wait.timeout = XR_INFINITE_DURATION;

        if (count == 0 || XR_FAILED(xr_.enumImages(out->chain, count, &count, base))
            || XR_FAILED(xr_.acquire(out->chain, nullptr, &index)) || index >= count
            || XR_FAILED(xr_.waitImage(out->chain, &wait))) {
            return false;
        }

        // White on black, opaque: the byte order (RGBA or BGRA) doesn't matter. Flushed: the compositor
        // reads the image on a device of its own.
        context_->UpdateSubresource(images[index].texture, 0, nullptr, rgba.data(), width * 4, 0);
        context_->Flush();
        return XR_SUCCEEDED(xr_.release(out->chain, nullptr));
    }

    // An eye's image for its field of view `fov`: black, with the text centred where the eye looks straight
    // ahead (the image's middle is off it: the views are asymmetric), so both eyes see it in one place.
    // Pimax refuses a quad layer for the text (XR_ERROR_RUNTIME_FAILURE): it is in the eyes' own images.
    bool Test::MakeEyePicture(const XrFovf& fov, Picture* out) {
        const float left = std::tan(-fov.angleLeft), right = std::tan(fov.angleRight);
        const float up = std::tan(fov.angleUp), down = std::tan(-fov.angleDown);

        if (left + right <= 0 || up + down <= 0) {
            return false;
        }

        const float perTan = static_cast<float>(text_.w) / kTextSpan;  // pixels per tangent unit
        const int32_t width = static_cast<int32_t>(std::ceil(perTan * (left + right)));
        const int32_t height = static_cast<int32_t>(std::ceil(perTan * (up + down)));
        const int32_t x0 = static_cast<int32_t>(perTan * left) - text_.w / 2;
        const int32_t y0 = static_cast<int32_t>(perTan * up) - text_.h / 2;
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 0);

        for (int32_t y = 0; y < height; y++) {
            for (int32_t x = 0; x < width; x++) {
                const int32_t tx = x - x0, ty = y - y0;
                const bool text = tx >= 0 && tx < text_.w && ty >= 0 && ty < text_.h;
                uint8_t* p = &rgba[(static_cast<size_t>(y) * width + x) * 4];
                p[0] = p[1] = p[2] = text ? text_.coverage[static_cast<size_t>(ty) * text_.w + tx] : 0;
                p[3] = 255;
            }
        }

        return MakePicture(width, height, rgba, out);
    }

    // Polls the session's state changes until it is `want`; false if it ends instead, or at `deadline`
    // (GetTickCount64).
    bool Test::WaitForState(XrSessionState want, ULONGLONG deadline) {
        while (GetTickCount64() < deadline) {
            XrEventDataBuffer event = {XR_TYPE_EVENT_DATA_BUFFER};

            while (xr_.pollEvent(instance_, &event) == XR_SUCCESS) {
                if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
                    const XrSessionState
                        state = reinterpret_cast<XrEventDataSessionStateChanged*>(&event)->state;

                    if (state == want) {
                        return true;
                    }

                    if (state == XR_SESSION_STATE_LOSS_PENDING || state == XR_SESSION_STATE_EXITING) {
                        return false;
                    }
                }

                event = {XR_TYPE_EVENT_DATA_BUFFER};
            }

            Sleep(10);
        }

        return false;
    }

    // One frame: the eyes' views located in the view space (`located`: they are), the eyes' images shown.
    // `period`: the runtime's display period (s). False if the runtime refused the frame.
    bool Test::ShowFrame(XrView views[2], bool* located, double* period) {
        XrFrameState frame = {XR_TYPE_FRAME_STATE};

        if (XR_FAILED(xr_.waitFrame(session_, nullptr, &frame))
            || XR_FAILED(xr_.beginFrame(session_, nullptr))) {
            return false;
        }

        *period = static_cast<double>(frame.predictedDisplayPeriod) * 1e-9;
        XrViewLocateInfo locate = {XR_TYPE_VIEW_LOCATE_INFO};
        locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locate.displayTime = frame.predictedDisplayTime;
        locate.space = space_;
        XrViewState state = {XR_TYPE_VIEW_STATE};
        uint32_t count = 0;
        *located = XR_SUCCEEDED(xr_.locateViews(session_, &locate, &state, 2, &count, views)) && count == 2
                   && (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT);

        XrCompositionLayerProjectionView eyes[2] = {
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
            {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}
        };

        // Not located (no tracking): a neutral pose and some field of view, which xrEndFrame accepts.
        const bool text = eyes_[0].chain && eyes_[1].chain;

        for (int e = 0; e < 2; e++) {
            const Picture& image = text ? eyes_[e] : black_;
            eyes[e].pose = *located ? views[e].pose : XrPosef{{0, 0, 0, 1}, {0, 0, 0}};
            eyes[e].fov = *located ? views[e].fov : XrFovf{-0.8f, 0.8f, 0.8f, -0.8f};
            eyes[e].subImage.swapchain = image.chain;
            eyes[e].subImage.imageRect.extent = {image.width, image.height};
        }

        XrCompositionLayerProjection layer = {XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = space_;
        layer.viewCount = 2;
        layer.views = eyes;
        const auto header = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);

        XrFrameEndInfo end = {XR_TYPE_FRAME_END_INFO};
        end.displayTime = frame.predictedDisplayTime;
        end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        end.layerCount = frame.shouldRender ? 1 : 0;
        end.layers = &header;
        return XR_SUCCEEDED(xr_.endFrame(session_, &end));
    }

    void Test::End() {
        if (!running_) {
            return;
        }

        xr_.requestExit(session_);
        WaitForState(XR_SESSION_STATE_STOPPING, GetTickCount64() + kStopWaitMs);
        xr_.endSession(session_);
        running_ = false;
    }

    // Shows the frames and reads the headset from them: the text for kShowMs once the views are read, all
    // within kTimeoutMs. A refused frame or the time running out ends it.
    bool Test::Run(HeadsetReport* out) {
        const ULONGLONG deadline = GetTickCount64() + kTimeoutMs;
        XrSessionBeginInfo begin = {XR_TYPE_SESSION_BEGIN_INFO};
        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

        if (!WaitForState(XR_SESSION_STATE_READY, deadline)
            || XR_FAILED(xr_.beginSession(session_, &begin))) {
            return false;
        }

        running_ = true;
        XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}}, seen[2] = {};
        double period = 0;
        ULONGLONG readAt = 0;  // when the views were first read; 0: not yet

        while (GetTickCount64() < deadline && (readAt == 0 || GetTickCount64() < readAt + kShowMs)) {
            bool located = false;

            if (!ShowFrame(views, &located, &period)) {
                break;
            }

            if (located && readAt == 0) {
                seen[0] = views[0], seen[1] = views[1];
                readAt = GetTickCount64();
                // Without the text the test still reads the headset: both eyes stay black (ShowFrame).
                MakeEyePicture(seen[0].fov, &eyes_[0]);
                MakeEyePicture(seen[1].fov, &eyes_[1]);
            }
        }

        End();

        if (readAt == 0 || period <= 0) {
            return false;
        }

        // As the frame source reads the game's eye (openxr_source.cpp): OpenXR's head is halfway between
        // the eyes, and the left eye's turn from it is its cant.
        const auto quat = [](const XrQuaternionf& q) { return Normalize(Quat{q.x, q.y, q.z, q.w}); };
        const Quat left = quat(seen[0].pose.orientation), right = quat(seen[1].pose.orientation);
        const Quat head = Normalize(Slerp(left, right, 0.5f));
        const XrFovf& fov = seen[0].fov;

        out->imageWidth = eyeImage_.recommendedImageRectWidth;
        out->imageHeight = eyeImage_.recommendedImageRectHeight;
        out->hasEye = true;
        out->eye.leftTan = std::tan(-fov.angleLeft), out->eye.rightTan = std::tan(fov.angleRight);
        out->eye.upTan = std::tan(fov.angleUp), out->eye.downTan = std::tan(-fov.angleDown);
        out->eye.hmdToEye = Normalize(Mul(Conj(head), left));
        out->eye.encodedTwice = false;
        out->period = period;
        return true;
    }

}

bool RunHeadsetTest(HeadsetReport* out) {
    Test test;
    return test.Open() && test.Run(out);
}
