// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// Captures the tool's window with Windows Graphics Capture (as OBS does) and reads, from patches
// of each captured image, the direction codes the fake game draws: where the view points, how
// level it is, and whether an edge shows black. Knows nothing about scenarios or grading.
#pragma once

#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_2.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <atomic>
#include <mutex>

#include "common.h"
#include "patches.h"

namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgd = winrt::Windows::Graphics::DirectX;

inline double QpcNow() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();

    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(freq.QuadPart);
}

struct ViewSample {
    double t = 0;
    double received = 0;   // QPC time the capture callback got the frame
    bool decoded = false;  // the patches were read
    bool valid = false;    // the view's direction could be read (center and quarter patches)
    Vec3 forward, right;   // world directions at the output center and toward its right edge
    // The angle between the quarter patches' directions (degrees): the view's width, which a rotation
    // leaves as it is. They sit a quarter of the window either side of the centre, at outW / 2 of tangent.
    double quarterSpanDeg = 0;
    int noCode = 0;  // bit per Patch: shows no direction code (black, or not the fake's code)
};

class WindowCapture {
public:
    bool Start(HWND hwnd) {
        hwnd_ = hwnd;
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (FAILED(D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                flags,
                nullptr,
                0,
                D3D11_SDK_VERSION,
                device_.put(),
                nullptr,
                context_.put()
            ))) {
            return false;
        }

        auto dxgi = device_.as<IDXGIDevice>();
        winrt::com_ptr<::IInspectable> inspectable;
        if (FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()))) {
            return false;
        }
        winrtDevice_ = inspectable.as<wgd::Direct3D11::IDirect3DDevice>();

        auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
        wgc::GraphicsCaptureItem item{nullptr};
        if (FAILED(interop->CreateForWindow(
                hwnd,
                winrt::guid_of<wgc::GraphicsCaptureItem>(),
                winrt::put_abi(item)
            ))) {
            return false;
        }

        size_ = item.Size();

        // Patch positions in the captured image (which includes the title bar, so offset by the
        // client area's position), in Patch order. The edge and corner patches start a pixel
        // inside the edge and are judged by their outermost line (Decode), so a black border
        // shows once it is about 2 px wide whatever the window size (4 px in, then 8% of a
        // patch's pixels, let borders of over a degree pass at small windows).
        RECT client, frame;
        GetClientRect(hwnd, &client);
        DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame));

        POINT origin = {0, 0};
        ClientToScreen(hwnd, &origin);

        int ox = origin.x - frame.left, oy = origin.y - frame.top;
        int cx = ox + client.right / 2, cy = oy + client.bottom / 2;

        const int inset = 1 + kHalf;  // patch centers this far inside the edge
        const int l = ox + inset, r = ox + client.right - inset, t = oy + inset,
                  b = oy + client.bottom - inset;

        POINT centers[kPatches] = {
            {cx, cy},
            {cx - client.right / 4, cy},
            {cx + client.right / 4, cy},
            {l, cy},
            {r, cy},
            {cx, t},
            {cx, b},
            {l, t},
            {r, t},
            {l, b},
            {r, b}
        };

        for (int i = 0; i < kPatches; i++) {
            patch_[i] = {centers[i].x - kHalf, centers[i].y - kHalf};
        }

        D3D11_TEXTURE2D_DESC sd = {};
        sd.Width = kPatches * 2 * kHalf;
        sd.Height = 2 * kHalf;
        sd.MipLevels = sd.ArraySize = 1;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.SampleDesc.Count = 1;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& s : staging_) {
            device_->CreateTexture2D(&sd, nullptr, s.tex.put());
        }

        // A deep pool and non-blocking readback (below), so capture never waits on this code.
        pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrtDevice_,
            wgd::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            6,
            size_
        );

        frameArrived_ = pool_.FrameArrived(winrt::auto_revoke, [this](auto& pool, auto&) {
            inCallback_++;
            OnFrame(pool);
            inCallback_--;
        });

        session_ = pool_.CreateCaptureSession(item);
        session_.IsCursorCaptureEnabled(false);
        session_.StartCapture();
        return true;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;  // a frame callback may still be running; it checks this first
        }

        frameArrived_.revoke();
        while (inCallback_ > 0) {
            Sleep(1);  // never destroy this object under a running callback
        }
        if (session_) {
            session_.Close();
        }
        if (pool_) {
            pool_.Close();
        }
        session_ = nullptr;
        pool_ = nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        Drain(true);
    }

    ~WindowCapture() { Stop(); }

    std::vector<ViewSample> Samples() {
        std::lock_guard<std::mutex> lock(mutex_);
        return samples_;
    }

private:
    static constexpr int kHalf = 16;  // patches are 32x32 pixels
    static constexpr int kRing = 16;  // staging copies in flight

    struct Staging {
        winrt::com_ptr<ID3D11Texture2D> tex;
        double t = 0, received = 0;
        bool pending = false, inside = false;
    };

    // Copies the three patches of each new frame into a staging texture and releases the
    // frame right away; the copies are read back later, once the GPU has finished them.
    void OnFrame(const wgc::Direct3D11CaptureFramePool& pool) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        try {
            ProcessFrames(pool);
        } catch (const winrt::hresult_error&) {
            // The pool was closed under us during shutdown; nothing left to capture.
        }
    }

    void ProcessFrames(const wgc::Direct3D11CaptureFramePool& pool) {
        while (auto frame = pool.TryGetNextFrame()) {
            Staging& s = staging_[next_];
            if (s.pending) {
                Drain(true);  // ring full: make room (rare)
            }

            s.t = frame.SystemRelativeTime().count() * 1e-7;  // QPC-based, same clock as the fake
            s.received = QpcNow();

            using DxgiAccess = ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess;
            auto access = frame.Surface().as<DxgiAccess>();
            winrt::com_ptr<ID3D11Texture2D> tex;
            access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), tex.put_void());
            D3D11_TEXTURE2D_DESC desc;
            tex->GetDesc(&desc);

            s.inside = true;
            for (int i = 0; i < kPatches; i++) {
                POINT p = patch_[i];
                if (p.x < 0 || p.y < 0 || UINT(p.x + 2 * kHalf) > desc.Width
                    || UINT(p.y + 2 * kHalf) > desc.Height) {
                    s.inside = false;
                    break;
                }

                D3D11_BOX box = {UINT(p.x), UINT(p.y), 0, UINT(p.x + 2 * kHalf), UINT(p.y + 2 * kHalf), 1};
                context_->CopySubresourceRegion(s.tex.get(), 0, i * 2 * kHalf, 0, 0, tex.get(), 0, &box);
            }

            context_->Flush();
            s.pending = true;
            next_ = (next_ + 1) % kRing;
        }

        Drain(false);
    }

    // Reads back finished copies, oldest first. wait=false stops at the first one still in flight.
    void Drain(bool wait) {
        for (int n = 0; n < kRing; n++) {
            Staging& s = staging_[oldest_];
            if (!s.pending) {
                if (oldest_ == next_) {
                    break;
                }
                oldest_ = (oldest_ + 1) % kRing;
                continue;
            }

            ViewSample sample;
            sample.t = s.t;
            sample.received = s.received;
            if (s.inside) {
                D3D11_MAPPED_SUBRESOURCE m;
                HRESULT hr = context_->Map(
                    s.tex.get(),
                    0,
                    D3D11_MAP_READ,
                    wait ? 0 : D3D11_MAP_FLAG_DO_NOT_WAIT,
                    &m
                );

                if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
                    return;
                }
                if (SUCCEEDED(hr)) {
                    Decode(m, &sample);
                    context_->Unmap(s.tex.get(), 0);
                }
            }

            samples_.push_back(sample);
            s.pending = false;
            oldest_ = (oldest_ + 1) % kRing;
        }
    }

    // A pixel's direction code (0.5 + 0.5 * dir per channel): false if it holds none (length
    // outside 0.2-1.1; black is sqrt 3).
    static bool Code(const D3D11_MAPPED_SUBRESOURCE& m, int patch, int x, int y, double d[3]) {
        const uint8_t* p = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch
                           + (patch * 2 * kHalf + x) * 4;

        d[0] = p[2] / 127.5 - 1, d[1] = p[1] / 127.5 - 1, d[2] = p[0] / 127.5 - 1;  // B, G, R, A
        const double len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (len < 0.2 || len > 1.1) {
            return false;
        }
        for (int k = 0; k < 3; k++) {
            d[k] /= len;
        }
        return true;
    }

    // Pixels without a code on an edge or corner patch's outermost line (the column or row next to
    // the window's edge; both at a corner), where a black border shows first.
    static int OuterNoCode(const D3D11_MAPPED_SUBRESOURCE& m, int i) {
        const int last = 2 * kHalf - 1;
        const bool left = i == kLeft || i == kTopLeft || i == kBottomLeft;
        const bool right = i == kRight || i == kTopRight || i == kBottomRight;
        const bool top = i == kTop || i == kTopLeft || i == kTopRight;
        const bool bottom = i == kBottom || i == kBottomLeft || i == kBottomRight;

        int none = 0;
        double d[3];
        for (int y = 0; y <= last; y++) {
            for (int x = 0; x <= last; x++) {
                if (((left && x == 0) || (right && x == last) || (top && y == 0) || (bottom && y == last))
                    && !Code(m, i, x, y, d)) {
                    none++;
                }
            }
        }

        return none;
    }

    static void Decode(const D3D11_MAPPED_SUBRESOURCE& m, ViewSample* out) {
        Vec3 dir[kPatches];
        bool ok[kPatches];

        // Each pixel's direction on its own, averaged over the patch's pixels that hold a code.
        // Pixels blended with black at a border or scaled by the tool's filtering keep their
        // direction this way.
        for (int i = 0; i < kPatches; i++) {
            double sum[3] = {0, 0, 0}, d[3];
            int codes = 0;

            for (int y = 0; y < 2 * kHalf; y++) {
                for (int x = 0; x < 2 * kHalf; x++) {
                    if (!Code(m, i, x, y, d)) {
                        continue;
                    }
                    for (int k = 0; k < 3; k++) {
                        sum[k] += d[k];
                    }
                    codes++;
                }
            }

            // The view patches need nearly all their pixels. An edge or corner patch shows a black
            // border by its outermost line: 3 pixels there without a code (one alone could be
            // noise), so a border shows once it reaches past the line, about 2 px wide.
            ok[i] = i < kLeft ? codes >= 0.92 * 4 * kHalf * kHalf : OuterNoCode(m, i) < 3;
            dir[i] = Normalized({float(sum[0]), float(sum[1]), float(sum[2])});
            if (!ok[i]) {
                out->noCode |= 1 << i;
            }
        }

        out->decoded = true;
        if (!ok[kCenter] || !ok[kQuarterLeft] || !ok[kQuarterRight]) {
            return;
        }
        out->valid = true;
        out->forward = dir[kCenter];
        const Vec3& a = dir[kQuarterLeft];
        const Vec3& b = dir[kQuarterRight];
        out->right = Normalized({b.x - a.x, b.y - a.y, b.z - a.z});
        out->quarterSpanDeg = AngleDeg(a, b);
    }

    HWND hwnd_ = nullptr;
    winrt::com_ptr<ID3D11Device> device_;
    winrt::com_ptr<ID3D11DeviceContext> context_;
    Staging staging_[kRing];
    int next_ = 0, oldest_ = 0;
    bool stopping_ = false;
    std::atomic<int> inCallback_{0};
    wgc::Direct3D11CaptureFramePool::FrameArrived_revoker frameArrived_;
    POINT patch_[kPatches] = {};
    wgd::Direct3D11::IDirect3DDevice winrtDevice_{nullptr};
    wgc::Direct3D11CaptureFramePool pool_{nullptr};
    wgc::GraphicsCaptureSession session_{nullptr};
    winrt::Windows::Graphics::SizeInt32 size_{};
    std::mutex mutex_;
    std::vector<ViewSample> samples_;
};
