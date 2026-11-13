// Copyright (C) 2026 Anton Podolsky
// SPDX-License-Identifier: GPL-2.0-or-later
// LapVideo: the recorded lap's video while it is recorded. Each frame's image is drawn from a stored frame's
// texture (any 8-bit RGBA channel order, its bytes kept as they are) into a texture of Windows' video
// encoder (Media Foundation), which encodes it to HEVC on the graphics card, into an .mp4.
//
// Images are taken before they are written: the lap recorder holds the last few until it knows where the lap
// starts (lap_recorder.h). The encoder's textures are a fixed pool, so a full pool means the encoder has
// fallen behind. Measured on flat test images: 450 fps at 2508x2084, 126 fps at 5084x4168.
//
// Everything on one thread (the lap recorder's), on a device of its own. Knows nothing about laps,
// poses or OBS.
#pragma once

#include <windows.h>
#include <d3d11.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

class LapVideo {
public:
    // Images held before they are written, and in the encoder's queue: the pool's size.
    static constexpr int kPool = 24;

    ~LapVideo();

    // Makes the device on adapter `adapter`, with video support.
    bool Init(LUID adapter, std::string* error);
    ID3D11Device* Device() const { return device_.Get(); }

    // Starts the file `path` for images `width` x `height` (even) at the headset's `period` (s).
    bool Open(const wchar_t* path, int width, int height, double period, std::string* error);
    bool IsOpen() const { return writer_ != nullptr; }

    // An image of the pool with `image`'s top-left `width` x `height` drawn in (`image`: a texture on this
    // device); false if the pool is empty or the draw failed.
    bool Take(ID3D11Texture2D* image, Microsoft::WRL::ComPtr<IMFSample>* out);
    // Writes a taken image at `t` seconds from the start.
    bool Write(IMFSample* sample, double t, std::string* error);
    // Ends the file: false if it couldn't be completed.
    bool Finish(std::string* error);
    // Drops the file unfinished (the caller deletes it).
    void Abort();

private:
    // A view made once per texture: the ring's slots and the pool's textures are few, and the same each
    // frame.
    struct ImageViewOf {
        ID3D11Texture2D* texture = nullptr;  // held by the view
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> view;
    };

    struct TargetViewOf {
        ID3D11Texture2D* texture = nullptr;  // held by the view
        UINT subresource = 0;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> view;
    };

    bool MakeShaders();
    bool Draw(ID3D11Texture2D* image, ID3D11Texture2D* target, UINT subresource);
    ID3D11ShaderResourceView* ImageView(ID3D11Texture2D* image);
    ID3D11RenderTargetView* TargetView(ID3D11Texture2D* target, UINT subresource);
    void ForgetViews();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IMFDXGIDeviceManager> manager_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vs_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> ps_;
    Microsoft::WRL::ComPtr<IMFSinkWriter> writer_;
    Microsoft::WRL::ComPtr<IMFVideoSampleAllocatorEx> pool_;
    std::vector<ImageViewOf> imageViews_;
    std::vector<TargetViewOf> targetViews_;
    bool started_ = false;  // Media Foundation started (MFStartup)
    DWORD stream_ = 0;
    int width_ = 0, height_ = 0;
    LONGLONG duration_ = 0;  // one frame (100 ns units)
};
